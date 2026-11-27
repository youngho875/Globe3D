#include "pch.h"
#include "SceneSymbols.h"
#pragma pack(push,8)
#include "ICOPS_Framework/CAppContext.h"
#include "ICOPS_Framework/IMapProjection.h"
#include "ICOPS_WarSym/IWarSymFactory.h"
#include "ICOPS_WarSym/IWarSymbol.h"
#include "ICOPS_WarSym/ICOPS_WarSymCommon.h"
#pragma pack(8)
#include <filesystem>
#include <map>
#include <set>
#pragma pack(8)

class CSceneSymbols::Impl
{
public:
    std::unique_ptr<ICOPS::CAppContext> context;
    ICOPS::IWarSymFactoryPtr factory;
    ICOPS::IMapProjectionPtr projection;
    std::map<std::string,ICOPS::IWarSymbolPtr> symbols;
    std::set<std::string> used;
    bool attempted=false;
    bool Ready()
    {
        if(attempted)return factory!=nullptr;
        attempted=true;
        wchar_t exe[32768]={};GetModuleFileNameW(nullptr,exe,32768);
        auto root=std::filesystem::path(exe).parent_path();
        auto path=root/L".."/L".."/L"ICOPS"/L"config"/L"ICOPS_AppContext.xml";
        context=std::make_unique<ICOPS::CAppContext>();
        if(!context->Initialize(_T("ICOPS_Framework"),_T(".dll"),_T("CAppContextDataAgentXml"),path.c_str(),FALSE))
        { OutputDebugStringW(L"Globe3D: ICOPS context initialization failed.\n"); return false; }
        if(!context->GetStockObject(_T("IWarSymFactory"),(void**)&factory)||!factory->Initialize(context.get())){factory=nullptr;return false;}
        if(!context->CreateObject(_T("ICOPS_Framework"),_T(".dll"),_T("CMapProjectionNone"),_T("IMapProjection"),(void**)&projection)){factory=nullptr;return false;}
        ICOPS::SGeoCoord2 center={};ICOPS::SOrthoCoord2 origin={};
        projection->SetCenter(&center);projection->SetViewOrigin(&origin);projection->SetMapScale(1);projection->SetZoomFactor(1);
        return true;
    }
};
CSceneSymbols::CSceneSymbols():impl_(new Impl){}
CSceneSymbols::~CSceneSymbols()=default;
void CSceneSymbols::BeginFrame(){impl_->used.clear();}
void CSceneSymbols::EndFrame()
{
    for(auto it=impl_->symbols.begin();it!=impl_->symbols.end();)
        if(!impl_->used.count(it->first))it=impl_->symbols.erase(it);else ++it;
}
bool CSceneSymbols::Draw(Gdiplus::Graphics& graphics,const std::string& code,
    const std::string& assembly,const std::vector<Gdiplus::PointF>& points,
    float size,int transparency,const SceneSymbolAppearance& appearance)
{
    auto& p=*impl_;if(points.empty()||!p.Ready())return false;
    const auto key=code+"\n"+assembly+"\n"+std::to_string(size)+","+std::to_string(transparency)+","+
        std::to_string(appearance.rotation)+","+std::to_string(appearance.valid)+","+
        std::to_string(appearance.bgWhite)+std::to_string(appearance.colorMode)+std::to_string(appearance.fillFrame)+
        std::to_string(appearance.showFrame)+std::to_string(appearance.showIcon)+std::to_string(appearance.civilian)+","+
        std::to_string(appearance.luminance)+","+std::to_string(appearance.displayType)+","+std::to_string(appearance.state);p.used.insert(key);
    auto& symbol=p.symbols[key];
    if(!symbol)
    {
        if(!p.factory->CreateSymbol(CString(CA2T(code.c_str(),CP_UTF8)),&symbol)||!symbol)return false;
        if(!assembly.empty())
            symbol->SetAssembleStream(CString(CA2T(assembly.c_str(),CP_UTF8)));
    }
    symbol->SetSymbolSize(size);
    symbol->SetTransparency(transparency);
    symbol->SetSymbolRotate(appearance.rotation);
    if (appearance.valid)
    {
        auto mode=symbol->GetDrawMode();
        if (mode)
        {
            mode->isBgWhite=appearance.bgWhite; mode->isColorMode=appearance.colorMode;
            mode->fillFrame=appearance.fillFrame; mode->showFrame=appearance.showFrame;
            mode->showIcon=appearance.showIcon; mode->civ=appearance.civilian;
            mode->luminance=static_cast<decltype(mode->luminance)>(appearance.luminance);
            mode->dispType=static_cast<decltype(mode->dispType)>(appearance.displayType);
            mode->symState=static_cast<decltype(mode->symState)>(appearance.state);
        }
    }
    std::vector<ICOPS::SGeoCoord2> coordinates(points.size());
    p.projection->Inverse(points.data(),coordinates.data(),(INT_PTR)points.size());
    symbol->SetCoordinates(p.projection,coordinates.data(),(INT_PTR)coordinates.size());
    symbol->Draw(&graphics,points.data(),(INT_PTR)points.size(),1.0);
    return true;
}
#pragma pack(pop)
