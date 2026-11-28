#pragma once
#include <memory>
#include <string>
class CSceneGround;
class CSceneModels
{
  public:
    CSceneModels();
    ~CSceneModels();
    void Configure(const std::string &xml, bool force = false);
    void SetVisible(bool visible);
    bool Visible() const;
    void Draw(int width, int height, CSceneGround &ground);
    void ReleaseGL();
    double GroundHeight(double longitude, double latitude, CSceneGround &shared);
    std::string Status() const;
    bool FirstModelView(double& longitude, double& latitude, double& rangeMetres) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
