#include "pch.h"
#include "ModelGl.h"
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cstdint>
#include <cmath>
namespace modelview
{
namespace
{
constexpr GLenum ArrayBuffer = 0x8892, ElementBuffer = 0x8893, StaticDraw = 0x88E4, StreamDraw = 0x88E0;
constexpr GLenum VertexShader = 0x8B31, FragmentShader = 0x8B30, CompileStatus = 0x8B81, LinkStatus = 0x8B82;
PROC Function(const char *name, const char *alternate = nullptr)
{
    auto p = wglGetProcAddress(name);
    if (!p || p == PROC(1) || p == PROC(2) || p == PROC(3) || p == PROC(-1))
        p = alternate ? wglGetProcAddress(alternate) : nullptr;
    return p;
}
} // namespace
struct ModelGl::Api
{
    void(APIENTRY *genBuffers)(GLsizei, GLuint *) = nullptr;
    void(APIENTRY *deleteBuffers)(GLsizei, const GLuint *) = nullptr;
    void(APIENTRY *bindBuffer)(GLenum, GLuint) = nullptr;
    void(APIENTRY *bufferData)(GLenum, ptrdiff_t, const void *, GLenum) = nullptr;
    GLuint(APIENTRY *createShader)(GLenum) = nullptr;
    void(APIENTRY *shaderSource)(GLuint, GLsizei, const char *const *, const GLint *) = nullptr;
    void(APIENTRY *compileShader)(GLuint) = nullptr;
    void(APIENTRY *getShader)(GLuint, GLenum, GLint *) = nullptr;
    void(APIENTRY *deleteShader)(GLuint) = nullptr;
    GLuint(APIENTRY *createProgram)() = nullptr;
    void(APIENTRY *attachShader)(GLuint, GLuint) = nullptr;
    void(APIENTRY *bindAttribute)(GLuint, GLuint, const char *) = nullptr;
    void(APIENTRY *linkProgram)(GLuint) = nullptr;
    void(APIENTRY *getProgram)(GLuint, GLenum, GLint *) = nullptr;
    void(APIENTRY *deleteProgram)(GLuint) = nullptr;
    void(APIENTRY *useProgram)(GLuint) = nullptr;
    GLint(APIENTRY *uniformLocation)(GLuint, const char *) = nullptr;
    void(APIENTRY *uniformMatrix)(GLint, GLsizei, GLboolean, const GLfloat *) = nullptr;
    void(APIENTRY *uniform4)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void(APIENTRY *uniform1f)(GLint, GLfloat) = nullptr;
    void(APIENTRY *uniform1i)(GLint, GLint) = nullptr;
    void(APIENTRY *attributePointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *) = nullptr;
    void(APIENTRY *enableAttribute)(GLuint) = nullptr;
    void(APIENTRY *disableAttribute)(GLuint) = nullptr;
    void(APIENTRY *divisor)(GLuint, GLuint) = nullptr;
    void(APIENTRY *drawInstanced)(GLenum, GLsizei, GLenum, const void *, GLsizei) = nullptr;
};
ModelGl::ModelGl() : api_(new Api)
{
}
ModelGl::~ModelGl()
{
    delete api_;
}
bool ModelGl::Initialize()
{
    if (tried_)
        return program_ != 0;
    tried_ = true;
    auto &a = *api_;
#define LOAD(member, name)                                                                                   \
    a.member = reinterpret_cast<decltype(a.member)>(Function(name));                                         \
    if (!a.member)                                                                                           \
    return false
    LOAD(genBuffers, "glGenBuffers");
    LOAD(deleteBuffers, "glDeleteBuffers");
    LOAD(bindBuffer, "glBindBuffer");
    LOAD(bufferData, "glBufferData");
    LOAD(createShader, "glCreateShader");
    LOAD(shaderSource, "glShaderSource");
    LOAD(compileShader, "glCompileShader");
    LOAD(getShader, "glGetShaderiv");
    LOAD(deleteShader, "glDeleteShader");
    LOAD(createProgram, "glCreateProgram");
    LOAD(attachShader, "glAttachShader");
    LOAD(bindAttribute, "glBindAttribLocation");
    LOAD(linkProgram, "glLinkProgram");
    LOAD(getProgram, "glGetProgramiv");
    LOAD(deleteProgram, "glDeleteProgram");
    LOAD(useProgram, "glUseProgram");
    LOAD(uniformLocation, "glGetUniformLocation");
    LOAD(uniformMatrix, "glUniformMatrix4fv");
    LOAD(uniform4, "glUniform4f");
    LOAD(uniform1f, "glUniform1f");
    LOAD(uniform1i, "glUniform1i");
    LOAD(attributePointer, "glVertexAttribPointer");
    LOAD(enableAttribute, "glEnableVertexAttribArray");
    LOAD(disableAttribute, "glDisableVertexAttribArray");
#undef LOAD
    a.divisor =
        reinterpret_cast<decltype(a.divisor)>(Function("glVertexAttribDivisor", "glVertexAttribDivisorARB"));
    a.drawInstanced = reinterpret_cast<decltype(a.drawInstanced)>(
        Function("glDrawElementsInstanced", "glDrawElementsInstancedARB"));
    if (!a.divisor || !a.drawInstanced)
        return false;
    const char *vertex = R"(#version 120
attribute vec3 position;
attribute vec3 normal;
attribute vec2 uv;
attribute vec4 vertexColor;
attribute mat4 instanceMatrix;
uniform mat4 viewProjection;
varying vec2 texcoord;
varying vec4 tint;
varying float light;
void main(){gl_Position=viewProjection*instanceMatrix*vec4(position,1.0);texcoord=uv;tint=vertexColor;
vec3 n=normalize(mat3(instanceMatrix)*normal);light=0.45+0.55*abs(dot(n,normalize(vec3(0.3,0.4,1.0))));}
)";
    const char *fragment = R"(#version 120
uniform vec4 baseColor;
uniform sampler2D image;
uniform int textured;
uniform int alphaMode;
uniform float alphaCutoff;
varying vec2 texcoord;varying vec4 tint;varying float light;
void main(){vec4 c=baseColor*tint;if(textured!=0)c*=texture2D(image,texcoord);
if(alphaMode==1 && c.a<alphaCutoff)discard;if(alphaMode==0)c.a=1.0;
gl_FragColor=vec4(c.rgb*light,c.a);}
)";
    auto compile = [&](GLenum type, const char *text) {
        GLuint shader = a.createShader(type);
        a.shaderSource(shader, 1, &text, nullptr);
        a.compileShader(shader);
        GLint ok = 0;
        a.getShader(shader, CompileStatus, &ok);
        if (!ok)
        {
            a.deleteShader(shader);
            return GLuint(0);
        }
        return shader;
    };
    GLuint vs = compile(VertexShader, vertex), fs = compile(FragmentShader, fragment);
    if (!vs || !fs)
    {
        if (vs)
            a.deleteShader(vs);
        if (fs)
            a.deleteShader(fs);
        return false;
    }
    program_ = a.createProgram();
    a.attachShader(program_, vs);
    a.attachShader(program_, fs);
    const char *names[] = {"position", "normal", "uv", "vertexColor", "instanceMatrix"};
    for (unsigned i = 0; i < 5; ++i)
        a.bindAttribute(program_, i, names[i]);
    a.linkProgram(program_);
    a.deleteShader(vs);
    a.deleteShader(fs);
    GLint ok = 0;
    a.getProgram(program_, LinkStatus, &ok);
    if (!ok)
    {
        a.deleteProgram(program_);
        program_ = 0;
        return false;
    }
    matrixUniform_ = a.uniformLocation(program_, "viewProjection");
    colorUniform_ = a.uniformLocation(program_, "baseColor");
    textureUniform_ = a.uniformLocation(program_, "textured");
    cutoffUniform_ = a.uniformLocation(program_, "alphaCutoff");
    alphaUniform_ = a.uniformLocation(program_, "alphaMode");
    a.genBuffers(1, &instances_);
    return true;
}
bool ModelGl::Instanced() const
{
    return program_ != 0;
}
void ModelGl::Upload(const CpuModel &source, GpuModel &target)
{
    Initialize();
    target.info = source.info;
    target.materials = source.materials;
    target.longitude = source.longitude;
    target.latitude = source.latitude;
    target.height = source.height;
    target.georeferenced = source.georeferenced;
    target.bytes = source.info.bytes;
    for (int i = 0; i < 3; ++i)
        target.radius += std::pow(source.info.maximum[i] - source.info.minimum[i], 2);
    target.radius = std::sqrt(target.radius) / 2;
    for (const auto &image : source.textures)
    {
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     image.rgba.data());
        target.textures.push_back(texture);
    }
    for (const auto &mesh : source.meshes)
    {
        GpuMesh gpu;
        gpu.material = mesh.material;
        gpu.count = static_cast<uint32_t>(mesh.indices.size());
        if (program_)
        {
            api_->genBuffers(1, &gpu.vertices);
            api_->bindBuffer(ArrayBuffer, gpu.vertices);
            api_->bufferData(ArrayBuffer, mesh.vertices.size() * sizeof(MA_Vertex), mesh.vertices.data(),
                             StaticDraw);
            api_->genBuffers(1, &gpu.indices);
            api_->bindBuffer(ElementBuffer, gpu.indices);
            api_->bufferData(ElementBuffer, mesh.indices.size() * 4, mesh.indices.data(), StaticDraw);
        }
        else
        {
            gpu.list = glGenLists(1);
            glNewList(gpu.list, GL_COMPILE);
            glBegin(GL_TRIANGLES);
            for (auto index : mesh.indices)
            {
                const auto &v = mesh.vertices[index];
                glNormal3fv(v.normal);
                glTexCoord2fv(v.uv);
                const auto &material = source.materials.at(mesh.material);
                glColor4f(v.color[0] * material.baseColor[0], v.color[1] * material.baseColor[1],
                          v.color[2] * material.baseColor[2], v.color[3] * material.baseColor[3]);
                glVertex3fv(v.position);
            }
            glEnd();
            glEndList();
        }
        target.meshes.push_back(gpu);
    }
    if (program_)
    {
        api_->bindBuffer(ArrayBuffer, 0);
        api_->bindBuffer(ElementBuffer, 0);
    }
}
void ModelGl::Destroy(GpuModel &model)
{
    for (auto &mesh : model.meshes)
    {
        if (mesh.vertices)
            api_->deleteBuffers(1, &mesh.vertices);
        if (mesh.indices)
            api_->deleteBuffers(1, &mesh.indices);
        if (mesh.list)
            glDeleteLists(mesh.list, 1);
    }
    for (auto texture : model.textures)
        glDeleteTextures(1, &texture);
    model = GpuModel();
}
void ModelGl::Release()
{
    if (instances_ && api_->deleteBuffers)
        api_->deleteBuffers(1, &instances_);
    if (program_ && api_->deleteProgram)
        api_->deleteProgram(program_);
    instances_ = program_ = 0;
    tried_ = false;
}
void ModelGl::Begin(const Matrix &mv, const Matrix &projection)
{
    Initialize();
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    if (program_)
    {
        glGetIntegerv(0x8B8D, &previousProgram_);
        glGetIntegerv(0x8894, &previousArray_);
        glGetIntegerv(0x8895, &previousElements_);
        api_->useProgram(program_);
        auto rotation = mv;
        rotation[12] = rotation[13] = rotation[14] = 0;
        auto vp = Multiply(projection, rotation);
        float values[16];
        for (int i = 0; i < 16; ++i)
            values[i] = float(vp[i]);
        api_->uniformMatrix(matrixUniform_, 1, GL_FALSE, values);
        api_->uniform1i(api_->uniformLocation(program_, "image"), 0);
    }
    else
    {
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glEnable(GL_COLOR_MATERIAL);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    }
}
size_t ModelGl::Draw(const GpuModel &model, const std::vector<Matrix> &placements, Vector eye)
{
    if (placements.empty())
        return 0;
    auto &a = *api_;
    size_t calls = 0;
    if (program_)
    {
        std::vector<float> matrices;
        matrices.reserve(placements.size() * 16);
        for (auto matrix : placements)
        {
            matrix[12] -= eye.x / 1000;
            matrix[13] -= eye.y / 1000;
            matrix[14] -= eye.z / 1000;
            for (double value : matrix)
                matrices.push_back(float(value));
        }
        a.bindBuffer(ArrayBuffer, instances_);
        a.bufferData(ArrayBuffer, matrices.size() * sizeof(float), matrices.data(), StreamDraw);
        for (unsigned i = 0; i < 4; ++i)
        {
            a.enableAttribute(4 + i);
            a.attributePointer(4 + i, 4, GL_FLOAT, GL_FALSE, 64,
                               reinterpret_cast<const void *>(uintptr_t(i * 16)));
            a.divisor(4 + i, 1);
        }
    }
    for (int transparent = 0; transparent < 2; ++transparent)
        for (const auto &mesh : model.meshes)
        {
            const auto &material = model.materials.at(mesh.material);
            if ((material.alphaMode == 2) != bool(transparent))
                continue;
            if (material.doubleSided)
                glDisable(GL_CULL_FACE);
            else
            {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }
            if (transparent)
            {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
            }
            else
            {
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
            }
            bool textured = material.texture >= 0 && size_t(material.texture) < model.textures.size();
            if (textured)
            {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, model.textures[material.texture]);
            }
            else
                glDisable(GL_TEXTURE_2D);
            if (program_)
            {
                a.uniform4(colorUniform_, material.baseColor[0], material.baseColor[1], material.baseColor[2],
                           material.baseColor[3]);
                a.uniform1i(textureUniform_, textured);
                a.uniform1i(alphaUniform_, material.alphaMode);
                a.uniform1f(cutoffUniform_, material.alphaCutoff);
                a.bindBuffer(ArrayBuffer, mesh.vertices);
                a.bindBuffer(ElementBuffer, mesh.indices);
                const int sizes[] = {3, 3, 2, 4};
                const size_t offsets[] = {offsetof(MA_Vertex, position), offsetof(MA_Vertex, normal),
                                          offsetof(MA_Vertex, uv), offsetof(MA_Vertex, color)};
                for (unsigned i = 0; i < 4; ++i)
                {
                    a.enableAttribute(i);
                    a.attributePointer(i, sizes[i], GL_FLOAT, GL_FALSE, sizeof(MA_Vertex),
                                       reinterpret_cast<const void *>(offsets[i]));
                }
                a.drawInstanced(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr,
                                static_cast<GLsizei>(placements.size()));
                ++calls;
            }
            else
            {
                if (material.alphaMode == 1)
                {
                    glEnable(GL_ALPHA_TEST);
                    glAlphaFunc(GL_GEQUAL, material.alphaCutoff);
                }
                else
                    glDisable(GL_ALPHA_TEST);
                glColor4fv(material.baseColor);
                for (const auto &matrix : placements)
                {
                    glPushMatrix();
                    glMultMatrixd(matrix.data());
                    glCallList(mesh.list);
                    glPopMatrix();
                    ++calls;
                }
            }
        }
    return calls;
}
void ModelGl::End()
{
    if (program_)
    {
        for (unsigned i = 0; i < 8; ++i)
        {
            api_->disableAttribute(i);
            api_->divisor(i, 0);
        }
        api_->bindBuffer(ArrayBuffer, previousArray_);
        api_->bindBuffer(ElementBuffer, previousElements_);
        api_->useProgram(previousProgram_);
    }
    glPopClientAttrib();
    glPopAttrib();
}
} // namespace modelview
