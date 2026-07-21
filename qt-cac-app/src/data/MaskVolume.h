#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "data/VolumeData.h"

struct MaskVolume
{
    int width = 0;
    int height = 0;
    int depth = 0;
    std::vector<uint8_t> voxels;
    std::vector<double> spacing = {1.0, 1.0, 1.0};
    std::vector<double> origin = {0.0, 0.0, 0.0};
    std::vector<double> direction;

    bool isValid() const
    {
        return width > 0 && height > 0 && depth > 0
            && voxels.size() == static_cast<size_t>(width) * height * depth;
    }

    bool hasSameDimensionsAs(const VolumeData &volume) const
    {
        return isValid() && volume.isValid()
            && width == volume.width && height == volume.height && depth == volume.depth;
    }

    size_t offset(int x, int y, int z) const
    {
        if (x < 0 || x >= width || y < 0 || y >= height || z < 0 || z >= depth) {
            throw std::out_of_range("MaskVolume index out of range");
        }
        return (static_cast<size_t>(z) * height + y) * width + x;
    }

    uint8_t value(int x, int y, int z) const
    {
        return voxels[offset(x, y, z)];
    }

    void setValue(int x, int y, int z, uint8_t value)
    {
        voxels[offset(x, y, z)] = value ? 1 : 0;
    }
};
