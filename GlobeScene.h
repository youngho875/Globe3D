#pragma once
#include <memory>
#include <string>

// Consumes geographic data and style definitions, never a captured C4IMap frame.
class CGlobeScene
{
public:
    CGlobeScene();
    ~CGlobeScene();
    bool SetSnapshot(const std::string& json);
    bool Active() const;
    void RequestView(double longitude, double latitude, double metresPerPixel,
                     int width, int height);
    void DrawMaps();
    void DrawObjects(int width, int height);
    void ReleaseGL();
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
