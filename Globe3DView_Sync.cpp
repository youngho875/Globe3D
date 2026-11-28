#include "pch.h"
#include "Globe3DDoc.h"
#include "Globe3DView.h"
#include <algorithm>
#include <cmath>
#include <cpl_json.h>

MapViewSyncState CGlobe3DView::CaptureSyncState() const
{
    MapViewSyncState state;
    state.longitude=std::remainder(lon_,360.0);
    state.latitude=lat_;
    const double pitch=std::clamp((local_?tilt_:0.0)+perspectivePitch_,0.0,65.0)*geo::Pi/180.0;
    const double range=local_?distance_:distance_-geo::RadiusKm;
    state.metresPerPixel=2*std::max(0.001,range)*1000*std::tan(geo::Pi/8)/
        (std::max(1,height_)*std::cos(pitch));
    if(!baseMapConfig_.smallScale.path.empty())state.configuredLayers|=1;
    if(!baseMapConfig_.world.path.empty())state.configuredLayers|=2;
    if(!baseMapConfig_.naturalEarth.path.empty())state.configuredLayers|=4;
    if(baseMapConfig_.smallScale.visible)state.visibleLayers|=1;
    if(baseMapConfig_.world.visible)state.visibleLayers|=2;
    if(textureEnabled_)state.visibleLayers|=4;
    return state;
}
void CGlobe3DView::OnTimer(UINT_PTR id)
{
    if(id==0x4732){AnimateNavigation();return;}
    if(id==0x4731 && rc_ && width_>1 && height_>1)
    {
        if(viewSync_.IsEnabled())
        {
            std::string json,error;
            if(sceneSync_.ReadChanged(json,error) && scene_.SetSnapshot(json))
            {
                terrainVisible_=false;
                CPLJSONDocument document;
                if(document.LoadMemory(json))
                {
                    const auto filename=document.GetRoot().GetString("baseMapXml");
                    static std::string loaded;
                    if(!filename.empty() && filename!=loaded){LoadBaseMapXml(std::filesystem::u8path(filename));loaded=filename;}
                }
            }
        }
        MapViewSyncState incoming;
        if(viewSync_.Tick(CaptureSyncState(),incoming))
        {
            StopNavigation();
            mouseAction_=MouseAction::None;
            mouseButton_=0;
            if(GetCapture()==this)ReleaseCapture();
            lon_=incoming.longitude;
            lat_=std::clamp(incoming.latitude,-89.9,89.9);
            const double pitch=std::clamp((local_?tilt_:0.0)+perspectivePitch_,0.0,65.0)*geo::Pi/180.0;
            const double range=std::clamp(incoming.metresPerPixel*height_*std::cos(pitch)/
                (2000*std::tan(geo::Pi/8)),0.001,200000.0);
            distance_=local_?range:geo::RadiusKm+range;
            for(unsigned i=0;i<3;++i)
                if(incoming.configuredLayers&(1u<<i))
                    SetSyncBaseVisible(i,(incoming.visibleLayers&(1u<<i))!=0);
            viewSync_.Applied(CaptureSyncState());
            UpdateTitle();
            Invalidate(FALSE);
        }
    }
    if(id==0x4731 && scene_.Active())
    {
        const auto state=CaptureSyncState();
        scene_.RequestView(lon_,lat_,state.metresPerPixel,width_,height_);
        Invalidate(FALSE);
    }
    if(id==0x4731 && scene_.ModelsVisible())Invalidate(FALSE);
    CView::OnTimer(id);
}
void CGlobe3DView::SetSyncBaseVisible(unsigned index,bool visible)
{
    if(index>2)return;
    auto& group=index==0?baseMapConfig_.smallScale:index==1?baseMapConfig_.world:baseMapConfig_.naturalEarth;
    if(group.path.empty())return;
    if(index==2)
    {
        if(visible&&!texture_)LoadTexture(group.path,false);
        textureEnabled_=visible&&texture_!=0;
        group.visible=textureEnabled_;
        return;
    }
    if(visible&&!baseMapGroupLoaded_[index])
    {
        try
        {
            if(!wglMakeCurrent(dc_,rc_))return;
            std::vector<std::unique_ptr<ShapeLayer>> loaded;
            for(const auto& path:{group.path,group.boundaryPath,group.cityPath})
            {
                if(path.empty())continue;
                if(!std::filesystem::is_regular_file(path))throw std::runtime_error("Missing base-map shape");
                auto layer=std::make_unique<ShapeLayer>();layer->Load(path.u8string(),group.sourceEpsg);
                loaded.push_back(std::move(layer));
            }
            for(auto& layer:loaded){baseMapLayers_.push_back(std::move(layer));baseMapLayerGroups_.push_back(index);}
            baseMapGroupLoaded_[index]=true;
        }
        catch(const std::exception&){OutputDebugStringW(L"Globe3D: base-map load failed; visibility unchanged.\n");return;}
    }
    group.visible=visible;
}
void CGlobe3DView::ShowSyncBaseMapMenu(CPoint point)
{
    CMenu menu;menu.CreatePopupMenu();
    const auto state=CaptureSyncState();
    const wchar_t* names[]={L"소축척 지도",L"세계지도",L"Natural Earth 영상"};
    for(unsigned i=0;i<3;++i)
        menu.AppendMenu(MF_STRING|((state.visibleLayers&(1u<<i))?MF_CHECKED:0)|
            ((state.configuredLayers&(1u<<i))?0:MF_GRAYED),61001+i,names[i]);
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING|(scene_.ModelsVisible()?MF_CHECKED:0),61005,L"3차원 객체 표시");
    menu.AppendMenu(MF_STRING,61006,L"3차원 객체 XML 불러오기...");
    menu.AppendMenu(MF_STRING,61007,L"3차원 객체 다시 읽기");
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING|MF_GRAYED,61004,viewSync_.IsEnabled()?L"C4IMap 동기화 중":L"동기화 중단됨 (C4IMap에서 시작)");
    const UINT command=menu.TrackPopupMenu(TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,this);
    if(command==61005){scene_.SetModelsVisible(!scene_.ModelsVisible());Invalidate(FALSE);}
    else if(command==61006)
    {
        CFileDialog dialog(TRUE,L"xml",nullptr,OFN_FILEMUSTEXIST|OFN_HIDEREADONLY,L"3D model sources (*.xml)|*.xml||",this);
        if(dialog.DoModal()==IDOK)scene_.SetModelConfiguration(std::filesystem::path(dialog.GetPathName().GetString()).u8string());
    }
    else if(command==61007)scene_.ReloadModels();
    if(command>=61001&&command<=61003)
    {
        const unsigned index=command-61001;
        SetSyncBaseVisible(index,(state.visibleLayers&(1u<<index))==0);
        Invalidate(FALSE);
    }
}
