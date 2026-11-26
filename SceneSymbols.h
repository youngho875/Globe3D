#pragma once
#include <memory>
#include <string>
#include <vector>
#include <gdiplus.h>
class CSceneSymbols
{
public:
    CSceneSymbols();
    ~CSceneSymbols();
    bool Draw(Gdiplus::Graphics& graphics, const std::string& code,
              const std::string& assembly, const std::vector<Gdiplus::PointF>& points);
    void BeginFrame();
    void EndFrame();
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
