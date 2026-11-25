#pragma once
#include <string>
#include <vector>

#include "Geometry.h"

// GDAL 타입은 전방 선언
class OGRGeometry;
class OGRLineString;

class ShapeLayer
{
public:
    void Load(const std::string& filename, int fallbackEPSG = 0);
    void Draw() const;
    void Clear();

private:

    enum class PrimitiveType
    {
        Point,
        Line
    };

    struct Primitive
    {
        PrimitiveType type = PrimitiveType::Line;
        std::vector<geo::Vec3> vertices;
    };

    std::vector<Primitive> primitives_;

    // 지구 기준면 위 표시 높이(m)
    static constexpr double DisplayHeightM = 200.0;
    static void CheckCoordinate(double lon, double lat);
    static geo::Vec3 ToPosition(double lon, double lat);
    static void AppendLine(const OGRLineString* line, std::vector<Primitive>& output);
    static void AppendGeometry(const OGRGeometry* geometry, std::vector<Primitive>& output);
};