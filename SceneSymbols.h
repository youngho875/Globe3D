#pragma once
#pragma pack(push,8)
#include <memory>
#include <string>
#include <vector>
#include <gdiplus.h>
struct SceneSymbolAppearance
{
    bool valid = false;
    bool bgWhite = true;
    bool colorMode = true;
    bool fillFrame = true;
    bool showFrame = true;
    bool showIcon = true;
    bool civilian = false;
    int luminance = 0;
    int displayType = 0;
    int state = 0;
    float rotation = 0;
};

class CSceneSymbols
{
public:
    CSceneSymbols();
    ~CSceneSymbols();
    bool Draw(Gdiplus::Graphics& graphics, const std::string& code,
              const std::string& assembly, const std::vector<Gdiplus::PointF>& points,
              float size, int transparency,
              const SceneSymbolAppearance& appearance = SceneSymbolAppearance());
    void BeginFrame();
    void EndFrame();
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

#pragma pack(pop)
