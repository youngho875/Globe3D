#include "pch.h"

#include "ShapeLayer.h"

#include <Windows.h>
#include <gl/GL.h>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>
#include <filesystem>

void ShapeLayer::Load(const std::string& filename, int fallbackEPSG)
{
    static const bool configured = [] {
        wchar_t executable[32768] = {};
        GetModuleFileNameW(nullptr, executable, 32768);
        const auto base = std::filesystem::path(executable).parent_path();
        for (const auto& candidate : {base / L"proj9/share",
             base / L"../../gdal_sdk/bin/proj9/share", base / L"share/proj"})
        {
            if (std::filesystem::is_regular_file(candidate / L"proj.db"))
            {
                const auto path = candidate.lexically_normal().u8string();
                const char* paths[] = {path.c_str(), nullptr};
                OSRSetPROJSearchPaths(paths);
                return true;
            }
        }
        return false;
    }();
    (void)configured;
    GDALAllRegister();

    using DatasetPtr = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;

    DatasetPtr dataset(static_cast<GDALDataset*>(GDALOpenEx(filename.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY,
                nullptr, nullptr, nullptr)), &GDALClose);

    if (!dataset)
        throw std::runtime_error("Shapefile open failed");

    OGRLayer* layer = dataset->GetLayer(0);

    if (!layer)
        throw std::runtime_error("Layer not found");

    OGRSpatialReference source;
    if (const auto* srs = layer->GetSpatialRef())
    {
        source = *srs;
    }
    else
    {
        // .prj가 없으면 호출자가 지정한 원본 EPSG 사용
        if (fallbackEPSG <= 0 || source.importFromEPSG(fallbackEPSG) != OGRERR_NONE)
        {
            throw std::runtime_error("Missing CRS: provide the source EPSG code");
        }
    }

    OGRSpatialReference target;
    if (target.importFromEPSG(4326) != OGRERR_NONE)
    {
        throw std::runtime_error("EPSG:4326 initialization failed");
    }

    source.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    target.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    auto destroyTransform = [](OGRCoordinateTransformation* p)
    {
        if (p)
            OCTDestroyCoordinateTransformation(p);
    };

    std::unique_ptr<
        OGRCoordinateTransformation,
        decltype(destroyTransform)>
        transform(OGRCreateCoordinateTransformation(&source, &target), destroyTransform);

    if (!transform)
    {
        throw std::runtime_error("Coordinate transformation initialization failed");
    }

    // 모든 데이터가 정상적으로 로드된 후 기존 데이터 교체
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
        std::unique_ptr<OGRFeature, decltype(destroyFeature)> feature(rawFeature, destroyFeature);
        OGRGeometry* original = feature->GetGeometryRef();

        if (!original || original->IsEmpty())
            continue;

        std::unique_ptr<OGRGeometry, decltype(destroyGeometry)> geometry(original->clone(), destroyGeometry);

        if (!geometry)
            throw std::runtime_error("Geometry clone failed");

        // 수평 좌표만 사용
        geometry->flattenTo2D();

        if (geometry->transform(transform.get()) != OGRERR_NONE)
        {
            throw std::runtime_error("Feature coordinate transformation failed");
        }
        AppendGeometry(geometry.get(), loaded);
    }
    primitives_.swap(loaded);
}

void ShapeLayer::Draw() const
{
    if (primitives_.empty())
        return;

    glPushAttrib(
        GL_ENABLE_BIT |
        GL_CURRENT_BIT |
        GL_LINE_BIT |
        GL_POINT_BIT |
        GL_DEPTH_BUFFER_BIT);

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glColor3f(1.0f, 0.85f, 0.1f);
    glLineWidth(2.0f);
    glPointSize(6.0f);

    for (const auto& primitive : primitives_)
    {
        const GLenum mode = primitive.type == PrimitiveType::Point ? GL_POINTS : GL_LINE_STRIP;

        glBegin(mode);

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

void ShapeLayer::CheckCoordinate(double lon, double lat)
{
    if (!std::isfinite(lon) || !std::isfinite(lat) || lat < -90.0 || lat > 90.0)
    {
        throw std::runtime_error("Invalid geographic coordinate");
    }
}

geo::Vec3 ShapeLayer::ToPosition(
    double lon,
    double lat)
{
    CheckCoordinate(lon, lat);

    return geo::position(lon, lat, DisplayHeightM);
}

void ShapeLayer::AppendLine(const OGRLineString* line, std::vector<Primitive>& output)
{
    if (!line || line->getNumPoints() < 2)
        return;

    Primitive primitive;
    primitive.type = PrimitiveType::Line;

    primitive.vertices.push_back(ToPosition(line->getX(0), line->getY(0)));

    for (int i = 1; i < line->getNumPoints(); ++i)
    {
        const double lon0 = line->getX(i - 1);
        const double lat0 = line->getY(i - 1);

        const double lon1 = line->getX(i);
        const double lat1 = line->getY(i);

        CheckCoordinate(lon0, lat0);
        CheckCoordinate(lon1, lat1);

        const double dLon = std::remainder(lon1 - lon0, 360.0);

        const double dLat = lat1 - lat0;

        // Windows의 max 매크로와 충돌하지 않도록
        // (std::max)(...) 형태 사용
        const int steps = (std::max)(1, static_cast<int>(std::ceil((std::max)( std::abs(dLon), std::abs(dLat)) / 0.05)));

        for (int k = 1; k <= steps; ++k)
        {
            const double t = static_cast<double>(k) / steps;

            primitive.vertices.push_back(ToPosition(lon0 + dLon * t, lat0 + dLat * t));
        }
    }

    output.push_back(std::move(primitive));
}

void ShapeLayer::AppendGeometry(const OGRGeometry* geometry, std::vector<Primitive>& output)
{
    if (!geometry || geometry->IsEmpty())
        return;

    switch (wkbFlatten(geometry->getGeometryType()))
    {
    case wkbPoint:
    {
        const auto* point = static_cast<const OGRPoint*>(geometry);

        Primitive primitive;
        primitive.type = PrimitiveType::Point;

        primitive.vertices.push_back(ToPosition(point->getX(), point->getY()));

        output.push_back(std::move(primitive));
        break;
    }

    case wkbLineString:
        AppendLine(static_cast<const OGRLineString*>(geometry), output);
        break;

    case wkbPolygon:
    {
        const auto* polygon = static_cast<const OGRPolygon*>(geometry);

        AppendLine(polygon->getExteriorRing(), output);

        for (int i = 0; i < polygon->getNumInteriorRings(); ++i)
        {
            AppendLine(polygon->getInteriorRing(i), output);
        }
        break;
    }

    case wkbMultiPoint:
    case wkbMultiLineString:
    case wkbMultiPolygon:
    case wkbGeometryCollection:
    {
        const auto* collection = static_cast<const OGRGeometryCollection*>(geometry);

        for (int i = 0; i < collection->getNumGeometries(); ++i)
        {
            AppendGeometry(collection->getGeometryRef(i), output);
        }
        break;
    }

    default:
        throw std::runtime_error("Unsupported geometry type");
    }
}
