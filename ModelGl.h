#pragma once
#include <Windows.h>
#include <gl/GL.h>
#include <cstddef>
#include <vector>
#include "ModelAssets/ModelAssets.h"
#include "ModelPlacement.h"

namespace modelview
{
struct GpuMesh
{
    GLuint vertices = 0, indices = 0, list = 0;
    uint32_t count = 0, material = 0;
};
struct GpuModel
{
    std::vector<GpuMesh> meshes;
    std::vector<GLuint> textures;
    std::vector<MA_Material> materials;
    MA_Info info{};
    double longitude = 0, latitude = 0, height = 0, radius = 0;
    bool georeferenced = false;
    uint64_t bytes = 0, used = 0;
};
struct CpuMesh
{
    std::vector<MA_Vertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t material = 0;
};
struct CpuTexture
{
    std::vector<unsigned char> rgba;
    unsigned width = 0, height = 0;
};
struct CpuModel
{
    std::vector<CpuMesh> meshes;
    std::vector<CpuTexture> textures;
    std::vector<MA_Material> materials;
    MA_Info info{};
    double longitude = 0, latitude = 0, height = 0;
    bool georeferenced = false;
};
class ModelGl
{
  public:
    ModelGl();
    ~ModelGl();
    bool Initialize();
    bool Instanced() const;
    void Upload(const CpuModel &source, GpuModel &target);
    void Destroy(GpuModel &model);
    void Release();
    void Begin(const Matrix &modelview, const Matrix &projection);
    size_t Draw(const GpuModel &model, const std::vector<Matrix> &placements, Vector eye);
    void End();

  private:
    struct Api;
    Api *api_;
    GLuint program_ = 0, instances_ = 0;
    GLint previousProgram_ = 0, previousArray_ = 0, previousElements_ = 0;
    GLint matrixUniform_ = -1, colorUniform_ = -1, textureUniform_ = -1, cutoffUniform_ = -1,
          alphaUniform_ = -1;
    bool tried_ = false;
};
} // namespace modelview
