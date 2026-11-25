#pragma once

#include <afxwin.h>
#include <gl/GL.h>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Geometry.h"


class ShapeLayer
{
public:
    struct Primitive
    {
        GLenum mode = GL_LINE_STRIP;
        std::vector<geo::Vec3> vertices;
    };
    //Primitive

    ShapeLayer();
    ~ShapeLayer();

    // UTF-8 파일 경로를 전달합니다.
    // fallbackEPSG: .prj가 없을 때만 사용하는 원본 좌표계
    void Load(const std::string& filename, int fallbackEPSG = 0);
    void Draw() const;
    void Clear();

public:
    std::vector<Primitive> primitives_;

    // 지구 기준면과 겹침 방지  지형 위에 붙이는 기능은 아님
    static constexpr double DisplayHeightM = 200.0;
    static void CheckCoordinate(double lon, double lat);
    static geo::Vec3 ToPosition(double lon, double lat);
    static void AppendLine(const OGRLineString* line, std::vector<Primitive>& output);
    static void AppendGeometry(const OGRGeometry* geometry, std::vector<Primitive>& output);

};
