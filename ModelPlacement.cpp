#include "pch.h"
#include "ModelPlacement.h"
#include <cmath>
namespace modelview
{
constexpr double pi = 3.14159265358979323846;
Matrix Identity()
{
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}
Matrix Multiply(const Matrix &a, const Matrix &b)
{
    Matrix c{};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                c[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
    return c;
}
Vector Transform(const Matrix &m, Vector v, bool direction)
{
    double w = direction ? 0 : 1;
    return {m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * w,
            m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * w,
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * w};
}
Vector Add(Vector a, Vector b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vector Subtract(Vector a, Vector b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vector Scale(Vector a, double n)
{
    return {a.x * n, a.y * n, a.z * n};
}
double Dot(Vector a, Vector b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
double Length(Vector a)
{
    return std::sqrt(Dot(a, a));
}
Vector Normalize(Vector a)
{
    double n = Length(a);
    return n > 1e-12 ? Scale(a, 1 / n) : Vector{0, 0, 1};
}
Vector Earth(double lon, double lat, double h)
{
    lon *= pi / 180;
    lat *= pi / 180;
    const double r = 6371000 + h;
    return {r * std::cos(lat) * std::cos(lon), r * std::cos(lat) * std::sin(lon), r * std::sin(lat)};
}
Vector Ecef(double lon, double lat, double h)
{
    lon *= pi / 180;
    lat *= pi / 180;
    const double e2 = .0066943799901413165, n = 6378137 / std::sqrt(1 - e2 * std::sin(lat) * std::sin(lat));
    return {(n + h) * std::cos(lat) * std::cos(lon), (n + h) * std::cos(lat) * std::sin(lon),
            (n * (1 - e2) + h) * std::sin(lat)};
}
bool Geodetic(Vector p, double &lon, double &lat, double &height)
{
    const double horizontal = std::hypot(p.x, p.y), e2 = .0066943799901413165;
    if (!std::isfinite(horizontal) || !std::isfinite(p.z) || Length(p) < 1)
        return false;
    lon = std::atan2(p.y, p.x);
    lat = std::atan2(p.z, horizontal * (1 - e2));
    for (int i = 0; i < 10; ++i)
    {
        const double n = 6378137 / std::sqrt(1 - e2 * std::sin(lat) * std::sin(lat));
        lat = std::atan2(p.z + e2 * n * std::sin(lat), horizontal);
    }
    const double n = 6378137 / std::sqrt(1 - e2 * std::sin(lat) * std::sin(lat));
    height = horizontal * std::cos(lat) + p.z * std::sin(lat) - n * (1 - e2 * std::sin(lat) * std::sin(lat));
    lon *= 180 / pi;
    lat *= 180 / pi;
    return std::isfinite(height);
}
void Basis(double lon, double lat, Vector &east, Vector &north, Vector &up)
{
    lon *= pi / 180;
    lat *= pi / 180;
    east = {-std::sin(lon), std::cos(lon), 0};
    north = {-std::sin(lat) * std::cos(lon), -std::sin(lat) * std::sin(lon), std::cos(lat)};
    up = {std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)};
}
Matrix Placement(double lon, double lat, double height, double heading, double scale)
{
    Vector east, north, up;
    Basis(lon, lat, east, north, up);
    heading *= pi / 180;
    Vector x = Add(Scale(east, std::cos(heading)), Scale(north, -std::sin(heading)));
    Vector z = Add(Scale(east, -std::sin(heading)), Scale(north, -std::cos(heading)));
    auto center = Earth(lon, lat, height);
    const double unit = scale / 1000;
    return {x.x * unit, x.y * unit, x.z * unit, 0, up.x * unit,     up.y * unit,     up.z * unit,     0,
            z.x * unit, z.y * unit, z.z * unit, 0, center.x / 1000, center.y / 1000, center.z / 1000, 1};
}
} // namespace modelview
