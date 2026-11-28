#pragma once
#include "Geometry.h"

namespace navigation
{
struct Camera
{
    double longitude = 128, latitude = 36;
    double rangeKm = 13629, heightM = 0;
    double heading = 0, pitch = 0;
    int width = 1, height = 1;
};
struct Frame
{
    geo::Vec3 eye, target, up, right, forward;
};
Frame MakeFrame(const Camera &camera);
bool Pick(const Camera &camera, double x, double y, geo::Vec3 &point);
bool Project(const Camera &camera, geo::Vec3 point, double &x, double &y);
bool KeepAnchor(Camera &camera, geo::Vec3 anchor, double x, double y);
void Pan(Camera &camera, double fromX, double fromY, double toX, double toY);
void Zoom(Camera &camera, double factor, double x, double y);
} // namespace navigation
