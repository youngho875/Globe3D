#pragma once
#include "ModelPlacement.h"
#include <string>
#include <vector>
#include <map>
namespace modelview
{
struct Lod
{
    std::string path;
    double maximumDistance = 1e30;
};
struct Instance
{
    std::string name;
    std::vector<Lod> lod;
    double longitude = 0, latitude = 0, height = 0, heading = 0, scale = 1, radius = 100,
           maximumDistance = 100000;
    bool clamp = true;
    Matrix placement = Identity();
    Vector center;
};
struct Tile
{
    std::string path, key;
    Matrix transform = Identity();
    Vector center;
    double radius = 0, error = 0;
    bool add = false, clamp = false;
    std::vector<size_t> children;
};
struct Catalog
{
    bool enabled = true;
    double screenError = 16, minimumPixels = 1;
    size_t gpuBudget = 512ull * 1024 * 1024, maximumInstances = 50000;
    std::vector<Instance> instances;
    std::map<std::pair<int, int>, std::vector<size_t>> cells;
    std::vector<Tile> tiles;
    std::vector<size_t> roots;
    std::string heightSource, heightDll;
    int heightKind = 0;
};
Catalog LoadCatalog(const std::string &filename);
} // namespace modelview
