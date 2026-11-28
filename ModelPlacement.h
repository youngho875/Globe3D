#pragma once
#include <array>
namespace modelview
{
struct Vector
{
    double x = 0, y = 0, z = 0;
};
using Matrix = std::array<double, 16>;
Matrix Identity();
Matrix Multiply(const Matrix &a, const Matrix &b);
Vector Transform(const Matrix &matrix, Vector value, bool direction = false);
Vector Add(Vector a, Vector b);
Vector Subtract(Vector a, Vector b);
Vector Scale(Vector a, double scale);
double Dot(Vector a, Vector b);
double Length(Vector a);
Vector Normalize(Vector a);
Vector Earth(double longitude, double latitude, double height);
Vector Ecef(double longitude, double latitude, double height);
bool Geodetic(Vector ecef, double &longitude, double &latitude, double &height);
void Basis(double longitude, double latitude, Vector &east, Vector &north, Vector &up);
Matrix Placement(double longitude, double latitude, double height, double heading, double scale);
} // namespace modelview
