#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

struct VolumeData
{
    int width = 0;
    int height = 0;
    int depth = 0;
    std::vector<int16_t> huVoxels;
    std::vector<double> spacing = {1.0, 1.0, 1.0};
    std::vector<double> origin = {0.0, 0.0, 0.0};
    std::vector<double> direction;

    bool isValid() const
    {
        return width > 0 && height > 0 && depth > 0
            && huVoxels.size() == static_cast<size_t>(width) * height * depth;
    }

    size_t offset(int x, int y, int z) const
    {
        if (x < 0 || x >= width || y < 0 || y >= height || z < 0 || z >= depth) {
            throw std::out_of_range("VolumeData index out of range");
        }
        return (static_cast<size_t>(z) * height + y) * width + x;
    }

    int16_t value(int x, int y, int z) const
    {
        return huVoxels[offset(x, y, z)];
    }
};
