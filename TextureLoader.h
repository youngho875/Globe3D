#pragma once
#include <filesystem>
#include <vector>
#include <cstdint>

struct EarthImage 
{
    unsigned width = 0;
    unsigned height = 0;
    int level=-1;
    // RGBA, bottom row first for OpenGL; TMS y=0 is the southernmost tile.
    std::vector<uint8_t> rgba;
};

EarthImage LoadNaturalEarthII(const std::filesystem::path& root, int maxTextureSize, int level);
