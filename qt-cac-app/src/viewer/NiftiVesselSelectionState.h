#pragma once

#include "viewer/MultiStructureVolume.h"

#include <QString>

#include <vtkImageData.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

#include <array>
#include <cstdint>

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

    bool hasSelectedVesselLabel() const;
    bool selectionUsableForProcessing() const;
    int selectedVesselLabel() const;
    const MultiStructureVolume *currentMultiStructureVolume() const;
    const vtkImageData *selectedLabelBinaryMask() const;
    const vtkPolyData *selectedLabelSurface() const;
    const NiftiVesselLabelValidation *validation() const;

private:
    const MultiStructureVolume *m_volume = nullptr;
    bool m_hasSelection = false;
    int m_selectedLabel = 0;
    vtkSmartPointer<vtkImageData> m_selectedBinaryMask;
    const vtkPolyData *m_selectedSurface = nullptr;
    NiftiVesselLabelValidation m_validation;
};
