#include "GlobeNavigation.h"

namespace navigation
{
Frame MakeFrame(const Camera &c)
{
    const auto radial = geo::unit(geo::position(c.longitude, c.latitude));
    const double lon = c.longitude * geo::Pi / 180;
    const geo::Vec3 east{-std::sin(lon), std::cos(lon), 0};
    const auto north = geo::unit(geo::cross(radial, east));
    const double h = c.heading * geo::Pi / 180;
    const double p = c.pitch * geo::Pi / 180;
    const auto horizontal = north * std::cos(h) + east * std::sin(h);
    Frame f;
    f.target = geo::position(c.longitude, c.latitude, c.heightM);
    f.eye = f.target + (radial * std::cos(p) - horizontal * std::sin(p)) * c.rangeKm;
    f.up = radial * std::sin(p) + horizontal * std::cos(p);
    f.forward = geo::unit(f.target - f.eye);
    f.right = geo::unit(geo::cross(f.forward, f.up));
    return f;
}
bool Pick(const Camera &c, double x, double y, geo::Vec3 &point)
{
    if (c.width < 1 || c.height < 1)
        return false;
    const auto f = MakeFrame(c);
    const double scale = 2 * std::tan(geo::Pi / 8) / c.height;
    const auto ray =
        geo::unit(f.forward + f.right * ((x - c.width * .5) * scale) + f.up * ((c.height * .5 - y) * scale));
    const double radius = geo::RadiusKm + c.heightM / 1000;
    const double b = geo::dot(f.eye, ray);
    const double discriminant = b * b - geo::dot(f.eye, f.eye) + radius * radius;
    if (discriminant < 0)
        return false;
    const double t = -b - std::sqrt(discriminant);
    if (t <= 0)
        return false;
    point = f.eye + ray * t;
    return true;
}
bool Project(const Camera &c, geo::Vec3 point, double &x, double &y)
{
    const auto f = MakeFrame(c);
    const auto delta = point - f.eye;
    const double depth = geo::dot(delta, f.forward);
    if (depth <= 1e-9)
        return false;
    const double scale = c.height / (2 * std::tan(geo::Pi / 8) * depth);
    x = c.width * .5 + geo::dot(delta, f.right) * scale;
    y = c.height * .5 - geo::dot(delta, f.up) * scale;
    return std::isfinite(x) && std::isfinite(y);
}
bool KeepAnchor(Camera &c, geo::Vec3 anchor, double x, double y)
{
    // Solve in screen space, so heading, tilt, latitude and window size all participate.
    const auto original = c;
    double lastError = 1e100;
    for (int iteration = 0; iteration < 12; ++iteration)
    {
        double px, py, lx, ly, bx, by;
        if (!Project(c, anchor, px, py))
            break;
        const double ex = x - px, ey = y - py, error = std::hypot(ex, ey);
        if (error < .02)
            return true;
        if (error > lastError * 1.5)
            break;
        lastError = error;
        const double epsilon = 1e-5;
        auto l = c, b = c;
        l.longitude += epsilon;
        b.latitude += epsilon;
        if (!Project(l, anchor, lx, ly) || !Project(b, anchor, bx, by))
            break;
        const double a = (lx - px) / epsilon, d = (by - py) / epsilon;
        const double bb = (bx - px) / epsilon, cc = (ly - py) / epsilon;
        const double det = a * d - bb * cc;
        if (std::abs(det) < 1e-10)
            break;
        c.longitude = std::remainder(c.longitude + std::clamp((ex * d - bb * ey) / det, -10., 10.), 360.);
        c.latitude = std::clamp(c.latitude + std::clamp((a * ey - ex * cc) / det, -10., 10.), -89.9, 89.9);
    }
    c = original;
    return false;
}
void Pan(Camera &c, double x0, double y0, double x1, double y1)
{
    geo::Vec3 anchor;
    if (Pick(c, x0, y0, anchor) && KeepAnchor(c, anchor, x1, y1))
        return;
    // Sky / horizon fallback has the same distance-dependent sensitivity.
    const double step = 2 * c.rangeKm * std::tan(geo::Pi / 8) / std::max(1, c.height);
    const double heading = c.heading * geo::Pi / 180;
    const double east = -(x1 - x0) * std::cos(heading) - (y1 - y0) * std::sin(heading);
    const double north = (y1 - y0) * std::cos(heading) - (x1 - x0) * std::sin(heading);
    c.longitude = std::remainder(
        c.longitude + east * step * 180 /
                          (geo::Pi * geo::RadiusKm * std::max(.02, std::cos(c.latitude * geo::Pi / 180))),
        360.);
    c.latitude = std::clamp(c.latitude + north * step * 180 / (geo::Pi * geo::RadiusKm), -89.9, 89.9);
}
void Zoom(Camera &c, double factor, double x, double y)
{
    geo::Vec3 anchor;
    const bool hit = Pick(c, x, y, anchor);
    c.rangeKm = std::clamp(c.rangeKm * factor, .005, 200000.);
    if (hit)
        KeepAnchor(c, anchor, x, y);
}
} // namespace navigation
