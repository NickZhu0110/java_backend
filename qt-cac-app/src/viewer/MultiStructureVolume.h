#pragma once

#include <QString>

#include <vtkImageData.h>
#include <vtkMatrix4x4.h>
#include <vtkSmartPointer.h>

#include <array>
#include <cstdint>
#include <vector>

struct NiftiVolumeGeometry
{
    std::array<int, 3> dimensions = {0, 0, 0};
    std::array<double, 3> spacing = {1.0, 1.0, 1.0};
    std::array<double, 3> localOrigin = {0.0, 0.0, 0.0};
    std::array<double, 6> physicalBoundsRasMm = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    vtkSmartPointer<vtkMatrix4x4> qForm;
    vtkSmartPointer<vtkMatrix4x4> sForm;
    vtkSmartPointer<vtkMatrix4x4> dataToWorldRas;
    vtkSmartPointer<vtkMatrix4x4> indexToWorldRas;
    QString scalarTypeName;
    int scalarType = 0;
    int scalarComponents = 1;
    int qFormCode = 0;
    int sFormCode = 0;
    std::uint64_t voxelCount = 0;
    std::uint64_t uncompressedBytes = 0;
};

struct MultiStructureLabelInfo
{
    int value = 0;
    QString displayName;
    std::uint64_t voxelCount = 0;
    std::array<int, 6> indexBounds = {0, -1, 0, -1, 0, -1};
    std::array<double, 6> physicalBoundsRasMm = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::array<double, 3> color = {1.0, 1.0, 1.0};
    double defaultOpacity = 0.6;
};

// Immutable after loading. This native categorical label volume is deliberately
// separate from the editable binary MaskVolume/workingMask workflow.
struct MultiStructureVolume
{
    QString ctPath;
    QString segmentationPath;
    NiftiVolumeGeometry ctGeometry;
    NiftiVolumeGeometry segmentationGeometry;
    vtkSmartPointer<vtkImageData> segmentationImage;
    std::vector<MultiStructureLabelInfo> labels;

    bool isValid() const
    {
        return segmentationImage != nullptr
            && segmentationGeometry.dimensions[0] > 0
            && segmentationGeometry.dimensions[1] > 0
            && segmentationGeometry.dimensions[2] > 0
            && !labels.empty();
    }
};
