#include "pch.h"
#include "GlobeScene.h"
#include "Geometry.h"
#include "VectorTileStyle.h"
#include "SceneSymbols.h"
#include "SceneGround.h"
#include "../c4imap/OverlayRibbonRender.h"
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#include <gl/GL.h>
#include <gl/GLU.h>
#include <cpl_json.h>
#include <gdal_utils.h>
#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <set>
#include <atomic>
#include <deque>

namespace
{
struct Bounds
{
    double west=0, south=0, east=0, north=0;
};
struct TileJob
{
    std::string key, path, style, layer;
    Bounds bounds;
    double scale=1;
    bool vector=false;
    unsigned generation=0;
};
struct TileImage
{
    TileJob job;
    std::vector<unsigned char> rgba;
};
struct GpuTile
{
    GLuint texture=0;
    Bounds bounds;
    unsigned long long used=0;
    bool mercator=false;
};
using Dataset=std::unique_ptr<GDALDataset, decltype(&GDALClose)>;
using Transform=std::unique_ptr<OGRCoordinateTransformation, decltype(&OCTDestroyCoordinateTransformation)>;

int TileX(double lon,int z)
{
    return std::clamp(int(std::floor((lon+180)/360*(1<<z))),0,(1<<z)-1);
}
int TileY(double lat,int z)
{
    const double p=std::clamp(lat,-85.05112878,85.05112878)*geo::Pi/180;
    return std::clamp(int(std::floor((1-std::asinh(std::tan(p))/geo::Pi)/2*(1<<z))),0,(1<<z)-1);
}
Bounds TileBounds(int z,int x,int y)
{
    const double n=double(1<<z);
    return {x/n*360-180,std::atan(std::sinh(geo::Pi*(1-2*(y+1)/n)))*180/geo::Pi,
        (x+1)/n*360-180,std::atan(std::sinh(geo::Pi*(1-2*y/n)))*180/geo::Pi};
}
void BitmapPixels(Gdiplus::Bitmap& bitmap,std::vector<unsigned char>& pixels)
{
    Gdiplus::BitmapData data;
    Gdiplus::Rect rect(0,0,bitmap.GetWidth(),bitmap.GetHeight());
    if(bitmap.LockBits(&rect,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&data)!=Gdiplus::Ok)return;
    pixels.resize(size_t(rect.Width)*rect.Height*4);
    for(int y=0;y<rect.Height;++y)
    {
        auto src=(unsigned char*)data.Scan0+y*data.Stride;
        auto dst=pixels.data()+size_t(y)*rect.Width*4;
        for(int x=0;x<rect.Width;++x)
        {
            dst[x*4]=src[x*4+2];dst[x*4+1]=src[x*4+1];
            dst[x*4+2]=src[x*4];dst[x*4+3]=src[x*4+3];
        }
    }
    bitmap.UnlockBits(&data);
}
void FallbackGeometry(Gdiplus::Graphics& graphics,OGRGeometry* geometry,const Bounds& b)
{
    auto point=[&](double x,double y){return Gdiplus::PointF(float((x-b.west)*256/(b.east-b.west)),float((b.north-y)*256/(b.north-b.south)));};
    const auto type=wkbFlatten(geometry->getGeometryType());
    Gdiplus::Pen pen(Gdiplus::Color(255,220,205,90),1.2f);
    Gdiplus::SolidBrush fill(Gdiplus::Color(100,150,170,90));
    if(type==wkbPoint)
    {
        auto p=geometry->toPoint();auto q=point(p->getX(),p->getY());
        graphics.FillEllipse(&fill,q.X-2,q.Y-2,4.f,4.f);
    }
    else if(type==wkbLineString || type==wkbPolygon)
    {
        Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
        auto add=[&](OGRLineString* line,bool closed)
        {
            std::vector<Gdiplus::PointF> points;
            for(int i=0;i<line->getNumPoints();++i)points.push_back(point(line->getX(i),line->getY(i)));
            if(points.size()<2)return;
            path.StartFigure();path.AddLines(points.data(),(INT)points.size());
            if(closed)path.CloseFigure();
        };
        if(type==wkbPolygon)
        {
            auto poly=geometry->toPolygon();if(poly->getExteriorRing())add(poly->getExteriorRing(),true);
            for(int i=0;i<poly->getNumInteriorRings();++i)add(poly->getInteriorRing(i),true);
            graphics.FillPath(&fill,&path);
        }
        else add(geometry->toLineString(),false);
        graphics.DrawPath(&pen,&path);
    }
    else if(OGR_GT_IsSubClassOf(type,wkbGeometryCollection))
    {
        auto collection=geometry->toGeometryCollection();
        for(int i=0;i<collection->getNumGeometries();++i)FallbackGeometry(graphics,collection->getGeometryRef(i),b);
    }
}
TileImage LoadTile(const TileJob& job,std::map<std::string,std::unique_ptr<vtstyle::Style>>& styles)
{
    TileImage image;image.job=job;
    if(!job.vector)
    {
        Gdiplus::Bitmap input(vtstyle::Wide(job.path).c_str());
        if(input.GetLastStatus()!=Gdiplus::Ok)return image;
        Gdiplus::Bitmap bitmap(256,256,PixelFormat32bppARGB);
        {Gdiplus::Graphics g(&bitmap);g.Clear(Gdiplus::Color(0,0,0,0));g.DrawImage(&input,0,0,256,256);}
        BitmapPixels(bitmap,image.rgba);return image;
    }
    const char* drivers[]={"MVT",nullptr};
    Dataset dataset(static_cast<GDALDataset*>(GDALOpenEx(("MVT:"+job.path).c_str(),GDAL_OF_VECTOR|GDAL_OF_READONLY,drivers,nullptr,nullptr)),GDALClose);
    if(!dataset)return image;
    vtstyle::Style* style=nullptr;
    if(!job.style.empty())
    {
        auto& slot=styles[job.style];if(!slot)slot=std::make_unique<vtstyle::Style>(job.style);
        if(slot->Valid())style=slot.get();
    }
    Gdiplus::Bitmap bitmap(256,256,PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bitmap);g.Clear(Gdiplus::Color(0,0,0,0));g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        OGRSpatialReference target;target.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);target.importFromEPSG(4326);
        for(int i=0;i<dataset->GetLayerCount();++i)
        {
            auto layer=dataset->GetLayer(i);if(!layer)continue;
            if(!job.layer.empty() && _stricmp(job.layer.c_str(),layer->GetName()) && dataset->GetLayerCount()>1)continue;
            Transform transform(nullptr,OCTDestroyCoordinateTransformation);
            if(auto srs=layer->GetSpatialRef())
            {
                OGRSpatialReference source(*srs);source.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
                if(!source.IsSame(&target))transform.reset(OGRCreateCoordinateTransformation(&source,&target));
                if(!source.IsSame(&target)&&!transform)continue;
            }
            layer->ResetReading();
            while(auto raw=layer->GetNextFeature())
            {
                std::unique_ptr<OGRFeature,decltype(&OGRFeature::DestroyFeature)> feature(raw,OGRFeature::DestroyFeature);
                auto original=feature->GetGeometryRef();if(!original)continue;
                std::unique_ptr<OGRGeometry> geometry(original->clone());
                if(transform && geometry->transform(transform.get())!=OGRERR_NONE)continue;
                // A valid SLD may intentionally filter out this feature.
                if(style)style->Draw(g,feature.get(),geometry.get(),job.layer.empty()?layer->GetName():job.layer,job.scale,
                    job.bounds.west,job.bounds.north,job.bounds.east,job.bounds.south);
                else FallbackGeometry(g,geometry.get(),job.bounds);
            }
        }
    }
    BitmapPixels(bitmap,image.rgba);return image;
}
void Upload(GLuint& texture,int width,int height,const std::vector<unsigned char>& pixels)
{
    if(pixels.empty())return;
    if(!texture)glGenTextures(1,&texture);
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
}
void DrawPatch(const GpuTile& tile)
{
    glBindTexture(GL_TEXTURE_2D,tile.texture);
    const int divisions=std::clamp(int(std::ceil(std::max(tile.bounds.east-tile.bounds.west,tile.bounds.north-tile.bounds.south)*2)),12,720);
    const double arc=std::max(tile.bounds.east-tile.bounds.west,tile.bounds.north-tile.bounds.south)/divisions*geo::Pi/180;
    const double lift=1+2*geo::RadiusKm*1000*(1-std::cos(arc/2));
    for(int row=0;row<divisions;++row)
    {
        glBegin(GL_TRIANGLE_STRIP);
        for(int col=0;col<=divisions;++col)
            for(int edge=0;edge<2;++edge)
            {
                const double u=double(col)/divisions,v=double(row+edge)/divisions;
                double latitude=tile.bounds.north-v*(tile.bounds.north-tile.bounds.south);
                if(tile.mercator)
                {
                    double north=std::asinh(std::tan(tile.bounds.north*geo::Pi/180));
                    double south=std::asinh(std::tan(tile.bounds.south*geo::Pi/180));
                    latitude=std::atan(std::sinh(north+(south-north)*v))*180/geo::Pi;
                }
                auto p=geo::position(tile.bounds.west+u*(tile.bounds.east-tile.bounds.west),latitude,lift);
                glTexCoord2d(u,v);glVertex3d(p.x,p.y,p.z);
            }
        glEnd();
    }
}
}

class CGlobeScene::Impl
{
public:
    CPLJSONObject scene;
    std::string sourceSignature;
    bool active=false;
    std::mutex mutex;
    std::condition_variable ready;
    bool stop=false;
    std::thread worker;
    unsigned generation=0;
    unsigned long long clock=0;
    std::deque<TileJob> jobs;
    std::deque<TileImage> completed;
    std::set<std::string> pending,failed;
    std::map<std::string,GpuTile> gpu;
    std::map<std::string,std::vector<int>> levels;
    std::vector<std::string> wanted;
    std::string lastView;
    GLuint overlayTexture=0;
    CSceneSymbols symbols;
    CSceneGround ground;
    std::map<std::string, GpuTile> groundObjects;

    Impl():worker([this]{Work();}){}
    ~Impl()
    {
        {std::lock_guard<std::mutex> lock(mutex);stop=true;jobs.clear();}
        ready.notify_all();worker.join();
    }
    void Work()
    {
        GDALAllRegister();
        std::map<std::string,std::unique_ptr<vtstyle::Style>> styles;
        for(;;)
        {
            TileJob job;
            {std::unique_lock<std::mutex> lock(mutex);ready.wait(lock,[&]{return stop||!jobs.empty();});
                if(stop)return;job=std::move(jobs.front());jobs.pop_front();}
            TileImage image;
            try{image=LoadTile(job,styles);}
            catch(const std::exception& error){image.job=job;OutputDebugStringA(error.what());}
            {std::lock_guard<std::mutex> lock(mutex);if(job.generation==generation)completed.push_back(std::move(image));}
        }
    }
};
CGlobeScene::CGlobeScene():impl_(new Impl){}
CGlobeScene::~CGlobeScene()=default;
bool CGlobeScene::Active()const{return impl_->active;}
bool CGlobeScene::SetSnapshot(const std::string& json)
{
    CPLJSONDocument document;
    if(!document.LoadMemory(json)||document.GetRoot().GetInteger("version")!=1)return false;
    auto next=document.GetRoot();
    auto signature=next.GetArray("sources").Format(CPLJSONObject::PrettyFormat::Plain);
    if(signature!=impl_->sourceSignature)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ++impl_->generation;impl_->jobs.clear();impl_->completed.clear();impl_->pending.clear();impl_->failed.clear();
        impl_->sourceSignature=signature;impl_->wanted.clear();impl_->lastView.clear();
    }
    impl_->ground.Configure(next.GetString("heightSource"), next.GetInteger("heightKind"), next.GetString("heightDll"));
    impl_->scene=next;impl_->active=true;return true;
}
void CGlobeScene::RequestView(double lon,double lat,double mpp,int width,int height)
{
    auto& p=*impl_;if(!p.active||!std::isfinite(mpp)||mpp<=0)return;
    const auto request=std::to_string(lon)+"|"+std::to_string(lat)+"|"+std::to_string(mpp)+"|"+std::to_string(width)+"|"+std::to_string(height)+"|"+std::to_string(p.scene.GetBool("autoScale"));
    if(request==p.lastView)return;
    p.lastView=request;
    const double scale=mpp*96/0.0254;
    const double coslat=std::max(.05,std::cos(lat*geo::Pi/180));
    double halfLon=std::min(180.,mpp*width/(111319.49*coslat));
    double halfLat=std::min(90.,mpp*height/111319.49);
    int preferred=std::clamp(int(std::floor(std::log2(156543.033928*coslat/mpp))),0,22);
    std::vector<std::string> wanted;
    std::deque<TileJob> queue;
    ++p.clock;
    std::map<std::string,std::pair<double,double>> best;
    if(p.scene.GetBool("autoScale"))
        for(auto source:p.scene.GetArray("sources"))
        {
            if(!source.GetBool("visual"))continue;
            const double min=source.GetDouble("minScale"),max=source.GetDouble("maxScale"),native=source.GetDouble("scale");
            if((min>0&&scale<min)||(max>0&&scale>max)||native<=0)continue;
            auto key=source.GetString("type")+"|"+source.GetString("family")+"|"+source.GetString("endpoint");
            double score=std::abs(std::log(scale/native));
            if(!best.count(key)||score<best[key].first)best[key]={score,native};
        }
    for(auto source:p.scene.GetArray("sources"))
    {
        if(!source.GetBool("visual"))continue;
        const double min=source.GetDouble("minScale"),max=source.GetDouble("maxScale");
        if(p.scene.GetBool("autoScale")&&((min>0&&scale<min)||(max>0&&scale>max)))continue;
        auto group=source.GetString("type")+"|"+source.GetString("family")+"|"+source.GetString("endpoint");
        if(source.GetDouble("scale")>0&&best.count(group)&&source.GetDouble("scale")!=best[group].second)continue;
        auto type=source.GetString("type"),root=source.GetString("path");
        const bool vector=type=="VectorTile";
        if(!vector&&type!="Raster"&&type!="CIB")continue;
        if(root.empty())continue;
        auto found=p.levels.find(root);
        if(found==p.levels.end())
        {
            std::vector<int> available;std::error_code ec;
            for(auto it=std::filesystem::directory_iterator(std::filesystem::u8path(root),ec);!ec&&it!=std::filesystem::directory_iterator();it.increment(ec))
            {
                const auto name=it->path().filename().u8string();
                if(it->is_directory(ec)&&!name.empty()&&name.find_first_not_of("0123456789")==std::string::npos)
                {int z=atoi(name.c_str());if(z<=22)available.push_back(z);}
            }
            std::sort(available.begin(),available.end());found=p.levels.emplace(root,std::move(available)).first;
        }
        if(found->second.empty())continue;
        int z=found->second.front();for(int level:found->second)if(level<=preferred)z=level;
        // Bound the visible working set by choosing coarser existing tiles, not by dropping layers.
        while(z>found->second.front())
        {
            int count=(TileX(std::min(180.,lon+halfLon),z)-TileX(std::max(-180.,lon-halfLon),z)+1)*
                (TileY(lat-halfLat,z)-TileY(lat+halfLat,z)+1);
            if(count<=48)break;
            auto it=std::lower_bound(found->second.begin(),found->second.end(),z);z=*--it;
        }
        const int x0=TileX(std::max(-180.,lon-halfLon),z),x1=TileX(std::min(180.,lon+halfLon),z);
        const int y0=TileY(lat+halfLat,z),y1=TileY(lat-halfLat,z);
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)
        {
            const int fileY=source.GetString("tileScheme")=="TMS"?(1<<z)-1-y:y;
            std::string prefix=root+"/"+std::to_string(z)+"/"+std::to_string(x)+"/"+std::to_string(fileY);
            std::string path;
            for(const char* ext:vector?std::vector<const char*>{".pbf",".mvt"}:std::vector<const char*>{".png",".jpg",".jpeg"})
            {std::error_code ec;if(std::filesystem::is_regular_file(std::filesystem::u8path(prefix+ext),ec)){path=prefix+ext;break;}}
            if(path.empty())continue;
            const std::string key=path+"|"+source.GetString("styleSld")+"|"+source.GetString("localLayerName")+"|"+std::to_string(preferred);
            wanted.push_back(key);
            auto cached=p.gpu.find(key);if(cached!=p.gpu.end()){cached->second.used=p.clock;continue;}
            if(p.pending.count(key)||p.failed.count(key))continue;
            TileJob job;job.key=key;job.path=path;job.vector=vector;job.bounds=TileBounds(z,x,y);job.scale=p.scene.GetBool("autoScale")?scale:0;
            job.style=source.GetString("styleSld");job.layer=source.GetString("layer");
            if(job.layer.empty())job.layer=source.GetString("localLayerName");job.generation=p.generation;
            queue.push_back(std::move(job));p.pending.insert(key);
        }
    }
    p.wanted.swap(wanted);
    {std::lock_guard<std::mutex> lock(p.mutex);for(auto& job:queue)p.jobs.push_back(std::move(job));}
    p.ready.notify_one();
}
void CGlobeScene::DrawMaps()
{
    auto& p=*impl_;if(!p.active)return;
    std::deque<TileImage> completed;
    {std::lock_guard<std::mutex> lock(p.mutex);completed.swap(p.completed);}
    for(auto& image:completed)
    {
        p.pending.erase(image.job.key);
        if(image.rgba.empty()){p.failed.insert(image.job.key);continue;}
        auto& tile=p.gpu[image.job.key];tile.bounds=image.job.bounds;tile.used=p.clock;tile.mercator=!image.job.vector;
        Upload(tile.texture,256,256,image.rgba);
    }
    glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_TEXTURE_BIT|GL_CURRENT_BIT);
    glDisable(GL_LIGHTING);glDisable(GL_CULL_FACE);glEnable(GL_TEXTURE_2D);glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);glDepthFunc(GL_LEQUAL);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    for(const auto& key:p.wanted)
    {auto tile=p.gpu.find(key);if(tile!=p.gpu.end())DrawPatch(tile->second);}
    glPopAttrib();
    while(p.gpu.size()>1024)
    {
        auto oldest=std::min_element(p.gpu.begin(),p.gpu.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
        if(oldest->second.used==p.clock)break;
        glDeleteTextures(1,&oldest->second.texture);p.gpu.erase(oldest);
    }
}
void CGlobeScene::ReleaseGL()
{
    for(auto& tile:impl_->gpu)glDeleteTextures(1,&tile.second.texture);
    for (auto& object : impl_->groundObjects) glDeleteTextures(1, &object.second.texture);
    impl_->groundObjects.clear();
    impl_->gpu.clear();if(impl_->overlayTexture)glDeleteTextures(1,&impl_->overlayTexture);
    impl_->overlayTexture=0;
}

void CGlobeScene::DrawObjects(int width,int height)
{
    auto& p=*impl_;
    if(!p.active||width<1||height<1)return;
    auto objects=p.scene.GetArray("overlays"),military=p.scene.GetArray("military");
    if(objects.Size()==0&&military.Size()==0)
    {
        for(auto& object:p.groundObjects) glDeleteTextures(1,&object.second.texture);
        p.groundObjects.clear(); p.symbols.BeginFrame(); p.symbols.EndFrame();
        return;
    }
    GLdouble model[16],projection[16];GLint viewport[4];
    glGetDoublev(GL_MODELVIEW_MATRIX,model);glGetDoublev(GL_PROJECTION_MATRIX,projection);glGetIntegerv(GL_VIEWPORT,viewport);
    auto project=[&](const CPLJSONObject& coordinate,Gdiplus::PointF& result)
    {
        auto a=coordinate.ToArray();if(a.Size()!=2)return false;
        auto v=geo::position(a[0].ToDouble(),a[1].ToDouble(),p.ground.Height(a[0].ToDouble(),a[1].ToDouble())+30);
        // Reject points on the far side of the sphere using the eye-space normal.
        const double ex=model[0]*v.x+model[4]*v.y+model[8]*v.z+model[12];
        const double ey=model[1]*v.x+model[5]*v.y+model[9]*v.z+model[13];
        const double ez=model[2]*v.x+model[6]*v.y+model[10]*v.z+model[14];
        const double nx=ex-model[12],ny=ey-model[13],nz=ez-model[14];
        if(nx*(-ex)+ny*(-ey)+nz*(-ez)<=0)return false;
        double x,y,z;if(!gluProject(v.x,v.y,v.z,model,projection,viewport,&x,&y,&z)||z<0||z>1)return false;
        result=Gdiplus::PointF(float(x),float(height-y));return std::isfinite(x)&&std::isfinite(y);
    };
    auto visible=[&](int id)
    {
        for(auto layer:p.scene.GetArray("overlayLayers"))if(layer.GetInteger("id")==id)return layer.GetBool("visible");
        return false;
    };
    std::set<std::string> usedObjects;
    Gdiplus::Bitmap bitmap(width,height,PixelFormat32bppARGB);
    {
        Gdiplus::Graphics graphics(&bitmap);graphics.Clear(Gdiplus::Color(0,0,0,0));
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        for(auto object:objects)
        {
            if(!visible(object.GetInteger("layer")))continue;
            const std::string key = object.Format(CPLJSONObject::PrettyFormat::Plain);
            usedObjects.insert(key);
            auto found = p.groundObjects.find(key);
            if (found != p.groundObjects.end()) continue;
            auto start = object["start"].ToArray(), end = object["end"].ToArray();
            if (start.Size()!=2 || end.Size()!=2) continue;
            const double origin = start[0].ToDouble();
            auto longitude = [origin](double lon) { return origin + std::remainder(lon-origin,360.0); };
            Bounds bounds{origin, start[1].ToDouble(), origin, start[1].ToDouble()};
            auto extend = [&](const CPLJSONObject& coordinate) {
                auto a = coordinate.ToArray(); if(a.Size()!=2) return;
                const double lon = longitude(a[0].ToDouble()), lat = a[1].ToDouble();
                bounds.west = std::min(bounds.west,lon); bounds.east = std::max(bounds.east,lon);
                bounds.south = std::min(bounds.south,lat); bounds.north = std::max(bounds.north,lat);
            };
            extend(object["end"]);
            for(auto coordinate : object.GetArray("points")) extend(coordinate);
            const double dx=std::max(.0001,bounds.east-bounds.west),dy=std::max(.0001,bounds.north-bounds.south);
            bounds.west-=dx*.2; bounds.east+=dx*.2;
            bounds.south=std::max(-89.999,bounds.south-dy*.2); bounds.north=std::min(89.999,bounds.north+dy*.2);
            auto mapPoint = [&](const CPLJSONObject& coordinate, Gdiplus::PointF& result) {
                auto a=coordinate.ToArray(); if(a.Size()!=2) return false;
                result=Gdiplus::PointF(float((longitude(a[0].ToDouble())-bounds.west)*1024/(bounds.east-bounds.west)),
                    float((bounds.north-a[1].ToDouble())*1024/(bounds.north-bounds.south)));
                return true;
            };
            Gdiplus::Bitmap surface(1024,1024,PixelFormat32bppARGB);
            auto surfaceGraphics=std::make_unique<Gdiplus::Graphics>(&surface);
            auto& graphics=*surfaceGraphics;
            graphics.Clear(Gdiplus::Color(0,0,0,0));
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::PointF a,b;
            mapPoint(object["start"],a); mapPoint(object["end"],b);
            const int type=object.GetInteger("type");
            auto encoded=object["style"];
            CLocalOverlay::Style style;
            style.lineColor=static_cast<decltype(style.lineColor)>(encoded.GetDouble("lineColor",style.lineColor));
            style.fillColor=static_cast<decltype(style.fillColor)>(encoded.GetDouble("fillColor",style.fillColor));
            style.patternColor=static_cast<decltype(style.patternColor)>(encoded.GetDouble("patternColor",style.patternColor));
            style.width=static_cast<decltype(style.width)>(encoded.GetDouble("width",style.width));
            style.lineAlpha=static_cast<decltype(style.lineAlpha)>(encoded.GetDouble("lineAlpha",style.lineAlpha));
            style.fillAlpha=static_cast<decltype(style.fillAlpha)>(encoded.GetDouble("fillAlpha",style.fillAlpha));
            style.dash=static_cast<decltype(style.dash)>(encoded.GetDouble("dash",style.dash));
            style.fillType=static_cast<decltype(style.fillType)>(encoded.GetDouble("fillType",style.fillType));
            style.hatch=static_cast<decltype(style.hatch)>(encoded.GetDouble("hatch",style.hatch));
            style.rotation=static_cast<decltype(style.rotation)>(encoded.GetDouble("rotation",style.rotation));
            style.flipX=static_cast<decltype(style.flipX)>(encoded.GetDouble("flipX",style.flipX));
            style.flipY=static_cast<decltype(style.flipY)>(encoded.GetDouble("flipY",style.flipY));
            style.startArrow=static_cast<decltype(style.startArrow)>(encoded.GetDouble("startArrow",style.startArrow));
            style.endArrow=static_cast<decltype(style.endArrow)>(encoded.GetDouble("endArrow",style.endArrow));
            style.lineCap=static_cast<decltype(style.lineCap)>(encoded.GetDouble("lineCap",style.lineCap));
            style.lineJoin=static_cast<decltype(style.lineJoin)>(encoded.GetDouble("lineJoin",style.lineJoin));
            style.lineAlignment=static_cast<decltype(style.lineAlignment)>(encoded.GetDouble("lineAlignment",style.lineAlignment));
            style.lineStripe=static_cast<decltype(style.lineStripe)>(encoded.GetDouble("lineStripe",style.lineStripe));
            style.gradientMode=static_cast<decltype(style.gradientMode)>(encoded.GetDouble("gradientMode",style.gradientMode));
            style.gradientPosition=static_cast<decltype(style.gradientPosition)>(encoded.GetDouble("gradientPosition",style.gradientPosition));
            style.gradientColor=static_cast<decltype(style.gradientColor)>(encoded.GetDouble("gradientColor",style.gradientColor));
            style.textColor=static_cast<decltype(style.textColor)>(encoded.GetDouble("textColor",style.textColor));
            style.fontSize=static_cast<decltype(style.fontSize)>(encoded.GetDouble("fontSize",style.fontSize));
            style.fontStyle=static_cast<decltype(style.fontStyle)>(encoded.GetDouble("fontStyle",style.fontStyle));
            style.textAlign=static_cast<decltype(style.textAlign)>(encoded.GetDouble("textAlign",style.textAlign));
            style.textAlignY=static_cast<decltype(style.textAlignY)>(encoded.GetDouble("textAlignY",style.textAlignY));
            style.lineInterval=static_cast<decltype(style.lineInterval)>(encoded.GetDouble("lineInterval",style.lineInterval));
            style.vertical=static_cast<decltype(style.vertical)>(encoded.GetDouble("vertical",style.vertical));
            style.rightToLeft=static_cast<decltype(style.rightToLeft)>(encoded.GetDouble("rightToLeft",style.rightToLeft));
            style.autoTextBox=static_cast<decltype(style.autoTextBox)>(encoded.GetDouble("autoTextBox",style.autoTextBox));
            style.lineEnabled=static_cast<decltype(style.lineEnabled)>(encoded.GetDouble("lineEnabled",style.lineEnabled));
            style.fillEnabled=static_cast<decltype(style.fillEnabled)>(encoded.GetDouble("fillEnabled",style.fillEnabled));
            wcsncpy_s(style.text,vtstyle::Wide(encoded.GetString("text")).c_str(),_TRUNCATE);
            wcsncpy_s(style.font,vtstyle::Wide(encoded.GetString("font")).c_str(),_TRUNCATE);
            wcsncpy_s(style.imagePath,vtstyle::Wide(encoded.GetString("imagePath")).c_str(),_TRUNCATE);
            std::vector<Gdiplus::PointF> points;
            bool good=true;
            for(auto coordinate:object.GetArray("points"))
            {
                Gdiplus::PointF point;
                if(!mapPoint(coordinate,point)){good=false;break;}
                points.push_back(point);
            }
            if(good)
                OverlayRibbonRender::Draw(graphics,type,a.X,a.Y,b.X,b.Y,style,points);
            graphics.Flush(Gdiplus::FlushIntentionSync);
            surfaceGraphics.reset();
            std::vector<unsigned char> pixels;
            BitmapPixels(surface,pixels);
            if (!pixels.empty())
            {
                GpuTile tile; tile.bounds=bounds;
                Upload(tile.texture,1024,1024,pixels);
                p.groundObjects.emplace(key,tile);
            }
        }
        p.symbols.BeginFrame();
        for(auto object:military)
        {
            if(!visible(object.GetInteger("layer")))continue;
            std::vector<Gdiplus::PointF> points;bool good=true;
            for(auto coordinate:object.GetArray("points")){Gdiplus::PointF point;if(!project(coordinate,point)){good=false;break;}points.push_back(point);}
            SceneSymbolAppearance appearance;
            appearance.valid=object.GetBool("hasDrawMode");
            appearance.rotation=float(object.GetDouble("rotation"));
            appearance.bgWhite=object.GetBool("bgWhite",true);
            appearance.colorMode=object.GetBool("colorMode",true);
            appearance.fillFrame=object.GetBool("fillFrame",true);
            appearance.showFrame=object.GetBool("showFrame",true);
            appearance.showIcon=object.GetBool("showIcon",true);
            appearance.civilian=object.GetBool("civilian");
            appearance.luminance=object.GetInteger("luminance");
            appearance.displayType=object.GetInteger("displayType");
            appearance.state=object.GetInteger("symbolState");
            if(good)p.symbols.Draw(graphics,object.GetString("code"),object.GetString("assembly"),points,
                float(object.GetDouble("symbolSize",40)),object.GetInteger("transparency",255),appearance);
        }
        p.symbols.EndFrame();
    }
    // Ground overlays are geographic meshes, not a screen-space screenshot.
    glPushAttrib(GL_ENABLE_BIT|GL_TEXTURE_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_CURRENT_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
    glDisable(GL_LIGHTING); glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    for(auto it=p.groundObjects.begin();it!=p.groundObjects.end();)
    {
        if(!usedObjects.count(it->first))
        { glDeleteTextures(1,&it->second.texture); it=p.groundObjects.erase(it); continue; }
        const auto& tile=it->second;
        glBindTexture(GL_TEXTURE_2D,tile.texture);
        const auto& b=tile.bounds;
        const int steps=std::clamp(int(std::ceil(std::max(b.east-b.west,b.north-b.south)*4)),24,360);
        const double arc=std::max(b.east-b.west,b.north-b.south)/steps*geo::Pi/180;
        const double lift=5+2*geo::RadiusKm*1000*(1-std::cos(arc/2));
        for(int row=0;row<steps;++row)
        {
            glBegin(GL_TRIANGLE_STRIP);
            for(int col=0;col<=steps;++col) for(int edge=0;edge<2;++edge)
            {
                const double u=double(col)/steps,v=double(row+edge)/steps;
                const double lon=b.west+(b.east-b.west)*u,lat=b.north-(b.north-b.south)*v;
                auto vertex=geo::position(lon,lat,p.ground.Height(lon,lat)+lift);
                glTexCoord2d(u,v); glVertex3d(vertex.x,vertex.y,vertex.z);
            }
            glEnd();
        }
        ++it;
    }
    glPopAttrib();
    std::vector<unsigned char> pixels;BitmapPixels(bitmap,pixels);
    glPushAttrib(GL_ENABLE_BIT|GL_TEXTURE_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    // The globe enables back-face culling; the y-down billboard quad has
    // the opposite winding. Keep that globe state out of this screen pass.
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);glDisable(GL_LIGHTING);glEnable(GL_TEXTURE_2D);glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);Upload(p.overlayTexture,width,height,pixels);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glOrtho(0,width,height,0,-1,1);
    glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2i(0,0);glTexCoord2f(1,0);glVertex2i(width,0);
    glTexCoord2f(1,1);glVertex2i(width,height);glTexCoord2f(0,1);glVertex2i(0,height);
    glEnd();glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(GL_MODELVIEW);glPopAttrib();
}
