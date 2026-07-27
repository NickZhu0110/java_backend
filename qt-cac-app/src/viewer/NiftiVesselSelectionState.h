#pragma once

#include "viewer/MultiStructureVolume.h"

#include <QString>

#include <vtkImageData.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

#include <array>
#include <cstdint>
#include <vector>

struct NiftiVesselComponentInfo
{
    int component = 0;
    std::uint64_t voxelCount = 0;
    double physicalVolumeMm3 = 0.0;
    double percentageOfSelectedLabel = 0.0;
    std::array<double, 3> physicalBoundingBoxDimensionsMm = {0.0, 0.0, 0.0};
    bool likelyNoise = false;
};

struct NiftiVesselLabelValidation
{
    int labelValue = 0;
    std::uint64_t foregroundVoxelCount = 0;
    std::uint64_t largestConnectedComponentVoxelCount = 0;
    int connectedComponentCount = 0;
    std::array<double, 6> physicalBoundsRasMm = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    bool geometryCompatible = false;
    QString warning;
};

// Dataset-scoped, read-only semantic selection for NIfTI review. The class
// shares immutable VTK storage with the current MultiStructureVolume and only
// allocates one uint8 binary view for the manually selected label.
class NiftiVesselSelectionState
{
public:
    void setCurrentVolume(const MultiStructureVolume *volume);
    void clear();
    void clearSelection();

    bool selectLabel(int labelValue,
                     const vtkPolyData *surface,
                     QString *errorMessage = nullptr);
    bool selectComponent(int component, QString *errorMessage = nullptr);
    void clearComponentSelection();

    bool hasSelectedVesselLabel() const;
    bool hasSelectedComponent() const;
    bool selectionUsableForProcessing() const;
    int selectedVesselLabel() const;
    int selectedComponent() const;
    const MultiStructureVolume *currentMultiStructureVolume() const;
    const vtkImageData *selectedLabelBinaryMask() const;
    const vtkPolyData *selectedLabelSurface() const;
    const NiftiVesselLabelValidation *validation() const;
    const std::vector<NiftiVesselComponentInfo> &components() const;
    const NiftiVesselComponentInfo *selectedComponentInfo() const;

private:
    const MultiStructureVolume *m_volume = nullptr;
    bool m_hasSelection = false;
    int m_selectedLabel = 0;
    vtkSmartPointer<vtkImageData> m_selectedBinaryMask;
    const vtkPolyData *m_selectedSurface = nullptr;
    NiftiVesselLabelValidation m_validation;
    std::vector<NiftiVesselComponentInfo> m_components;
    int m_selectedComponent = 0;
};
