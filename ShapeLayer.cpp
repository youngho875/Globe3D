#include "ShapeLayer.h"


ShapeLayer::ShapeLayer()
{
    DisplayHeightM = 200.0;
}

ShapeLayer::~ShapeLayer()
{

}

void ShapeLayer::Load(const std::string& filename, int fallbackEPSG = 0)
{
    GDALAllRegister();

    using DatasetPtr = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;
    DatasetPtr dataset(static_cast<GDALDataset*>(GDALOpenEx(filename.c_str(),
        GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr)), &GDALClose);

    if (!dataset)
        throw std::runtime_error("Shapefile open failed");

    OGRLayer* layer = dataset->GetLayer(0);
    if (!layer)
        throw std::runtime_error("Layer not found");

    OGRSpatialReference source;
    if (const auto* srs = layer->GetSpatialRef())
        source = *srs;
    else
    {
        if (source.importFromEPSG(4326) != OGRERR_NONE)
            throw std::runtime_error("Missing CRS: provide the source EPSG code");
    }
    //source.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    OGRSpatialReference target;
    if (target.importFromEPSG(4326) != OGRERR_NONE)
        throw std::runtime_error("EPSG:4326 initialization failed");

    source.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    target.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    auto destroyTransform = [](OGRCoordinateTransformation* p)
    {
        if (p)
            OCTDestroyCoordinateTransformation(p);
    };

    std::unique_ptr<OGRCoordinateTransformation, decltype(destroyTransform)>transform(
        OGRCreateCoordinateTransformation(&source, &target), destroyTransform);

    if (!transform)
        throw std::runtime_error("Coordinate transformation initialization failed");

    std::vector<Primitive> loaded;
    auto destroyFeature = [](OGRFeature* p)
    {
        OGRFeature::DestroyFeature(p);
    };

    auto destroyGeometry = [](OGRGeometry* p)
    {
        OGRGeometryFactory::destroyGeometry(p);
    };

    layer->ResetReading();
    while (OGRFeature* rawFeature = layer->GetNextFeature())
    {
        std::unique_ptr<OGRFeature, decltype(destroyFeature)>feature(rawFeature, destroyFeature);
        OGRGeometry* original = feature->GetGeometryRef();
        if (!original || original->IsEmpty())
            continue;

        std::unique_ptr<OGRGeometry, decltype(destroyGeometry)>geometry(original->clone(), destroyGeometry);

        if (!geometry)
            throw std::runtime_error("Geometry clone failed");

        geometry->flattenTo2D();

        if (geometry->transform(transform.get()) != OGRERR_NONE)
            throw std::runtime_error("Feature coordinate transformation failed");

        AppendGeometry(geometry.get(), loaded);
    }
    primitives_.swap(loaded);
}

void ShapeLayer::Draw() const
{
    if (primitives_.empty())
        return;

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT | GL_POINT_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glColor3f(1.0f, 0.85f, 0.1f);
    glLineWidth(2.0f);
    glPointSize(6.0f);

    for (const auto& primitive : primitives_)
    {
        glBegin(primitive.mode);
        for (const auto& p : primitive.vertices)
            glVertex3d(p.x, p.y, p.z);
        glEnd();
    }
    glPopAttrib();
}

void ShapeLayer::Clear()
{
    primitives_.clear();
}

static void ShapeLayer::CheckCoordinate(double lon, double lat)
{
    if (!std::isfinite(lon) || !std::isfinite(lat) || lat < -90.0 || lat > 90.0)
        throw std::runtime_error("Invalid geographic coordinate");
}

static ShapeLayer::Vec3 ToPosition(double lon, double lat)
{
    ShapeLayer::CheckCoordinate(lon, lat);
    return geo::position(lon, lat, ShapeLayer::DisplayHeightM);
}

static void ShapeLayerAppendLine(const OGRLineString* line, std::vector<Primitive>& output)
{
    if (!line || line->getNumPoints() < 2)
        return;

    Primitive primitive;
    primitive.mode = GL_LINE_STRIP;
    primitive.vertices.push_back(ToPosition(line->getX(0), line->getY(0)));
    for (int i = 1; i < line->getNumPoints(); ++i)
    {
        const double lon0 = line->getX(i - 1);
        const double lat0 = line->getY(i - 1);
        const double lon1 = line->getX(i);
        const double lat1 = line->getY(i);

        ShapeLayer::CheckCoordinate(lon0, lat0);
        ShapeLayer::CheckCoordinate(lon1, lat1);

        // 날짜 변경선을 지나는 선은 짧은 방향으로 연결
        const double dLon = std::remainder(lon1 - lon0, 360.0);
        const double dLat = lat1 - lat0;

        // 긴 선이 구체 내부를 관통하지 않도록 분할
        const int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::abs(dLon), std::abs(dLat)) / 0.05)));

        for (int k = 1; k <= steps; ++k)
        {
            const double t = static_cast<double>(k) / steps;
            primitive.vertices.push_back(ToPosition(lon0 + dLon * t, lat0 + dLat * t));
        }
    }
    output.push_back(std::move(primitive));
}

static void AppendGeometry(const OGRGeometry* geometry, std::vector<Primitive>& output)
{
    if (!geometry || geometry->IsEmpty())
        return;

    switch (wkbFlatten(geometry->getGeometryType()))
    {
    case wkbPoint:
    {
        const auto* point = static_cast<const OGRPoint*>(geometry);

        Primitive primitive;
        primitive.mode = GL_POINTS;
        primitive.vertices.push_back(ToPosition(point->getX(), point->getY()));
        output.push_back(std::move(primitive));
        break;
    }
    case wkbLineString:
        ShapeLayer::AppendLine(static_cast<const OGRLineString*>(geometry), output);
        break;
    case wkbPolygon:
    {
        const auto* polygon = static_cast<const OGRPolygon*>(geometry);
        ShapeLayer::AppendLine(polygon->getExteriorRing(), output);
        for (int i = 0; i < polygon->getNumInteriorRings(); ++i)
            ShapeLayer::AppendLine(polygon->getInteriorRing(i), output);
        break;
    }
    case wkbMultiPoint:
    case wkbMultiLineString:
    case wkbMultiPolygon:
    case wkbGeometryCollection:
    {
        const auto* collection = static_cast<const OGRGeometryCollection*>(geometry);
        for (int i = 0; i < collection->getNumGeometries(); ++i)
            AppendGeometry(collection->getGeometryRef(i), output);
        break;
    }
    default:
        throw std::runtime_error("Unsupported geometry type");
    }
}

