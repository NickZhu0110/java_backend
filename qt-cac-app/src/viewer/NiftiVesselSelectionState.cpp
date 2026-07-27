#include "viewer/NiftiVesselSelectionState.h"

#include <vtkDataArray.h>
#include <vtkPointData.h>
#include <vtkType.h>

#include <algorithm>
#include <limits>
#include <vector>

namespace {

template<typename T>
bool buildBinaryMaskAndComponents(const MultiStructureVolume &volume,
                                  int labelValue,
                                  vtkSmartPointer<vtkImageData> *binaryMask,
                                  std::uint64_t *foregroundVoxelCount,
                                  int *componentCount,
                                  std::uint64_t *largestComponentVoxelCount,
                                  QString *errorMessage)
{
    vtkDataArray *sourceScalars =
        volume.segmentationImage->GetPointData()->GetScalars();
    const auto *sourceValues = static_cast<const T *>(sourceScalars->GetVoidPointer(0));
    const std::uint64_t totalVoxelCount = volume.segmentationGeometry.voxelCount;
    if (!sourceValues || totalVoxelCount == 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The current segmentation scalar buffer is empty.");
        }
        return false;
    }

    auto mask = vtkSmartPointer<vtkImageData>::New();
    mask->CopyStructure(volume.segmentationImage);
    mask->AllocateScalars(VTK_UNSIGNED_CHAR, 1);
    vtkDataArray *maskScalars = mask->GetPointData()->GetScalars();
    auto *maskValues = maskScalars
        ? static_cast<unsigned char *>(maskScalars->GetVoidPointer(0))
        : nullptr;
    if (!maskValues) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not allocate the selected-label binary mask.");
        }
        return false;
    }

    std::uint64_t foreground = 0;
    for (std::uint64_t offset = 0; offset < totalVoxelCount; ++offset) {
        const bool selected = static_cast<std::int64_t>(sourceValues[offset]) == labelValue;
        maskValues[offset] = selected ? 1 : 0;
        foreground += selected ? 1 : 0;
    }
    if (foreground == 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Selected label %1 has zero voxels.").arg(labelValue);
        }
        return false;
    }

    const std::uint64_t width =
        static_cast<std::uint64_t>(volume.segmentationGeometry.dimensions[0]);
    const std::uint64_t height =
        static_cast<std::uint64_t>(volume.segmentationGeometry.dimensions[1]);
    const std::uint64_t depth =
        static_cast<std::uint64_t>(volume.segmentationGeometry.dimensions[2]);
    const std::uint64_t sliceSize = width * height;
    if (sliceSize == 0 || sliceSize * depth != totalVoxelCount) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Segmentation dimensions do not match its voxel count.");
        }
        return false;
    }

    std::vector<std::uint64_t> queue;
    queue.reserve(static_cast<size_t>(std::min<std::uint64_t>(
        foreground,
        static_cast<std::uint64_t>(std::numeric_limits<size_t>::max()))));
    int components = 0;
    std::uint64_t largestComponent = 0;

    for (std::uint64_t seed = 0; seed < totalVoxelCount; ++seed) {
        if (maskValues[seed] != 1) {
            continue;
        }

        ++components;
        queue.clear();
        queue.push_back(seed);
        maskValues[seed] = 2;
        std::uint64_t componentSize = 0;

        for (size_t queueIndex = 0; queueIndex < queue.size(); ++queueIndex) {
            const std::uint64_t offset = queue[queueIndex];
            ++componentSize;
            const std::uint64_t z = offset / sliceSize;
            const std::uint64_t inSlice = offset % sliceSize;
            const std::uint64_t y = inSlice / width;
            const std::uint64_t x = inSlice % width;

            const auto visit = [&](std::uint64_t neighbor) {
                if (maskValues[neighbor] == 1) {
                    maskValues[neighbor] = 2;
                    queue.push_back(neighbor);
                }
            };
            if (x > 0) visit(offset - 1);
            if (x + 1 < width) visit(offset + 1);
            if (y > 0) visit(offset - width);
            if (y + 1 < height) visit(offset + width);
            if (z > 0) visit(offset - sliceSize);
            if (z + 1 < depth) visit(offset + sliceSize);
        }
        largestComponent = std::max(largestComponent, componentSize);
    }

    for (std::uint64_t offset = 0; offset < totalVoxelCount; ++offset) {
        if (maskValues[offset] == 2) {
            maskValues[offset] = 1;
        }
    }
    maskScalars->Modified();

    *binaryMask = std::move(mask);
    *foregroundVoxelCount = foreground;
    *componentCount = components;
    *largestComponentVoxelCount = largestComponent;
    return true;
}

bool dispatchBuildSelection(const MultiStructureVolume &volume,
                            int labelValue,
                            vtkSmartPointer<vtkImageData> *binaryMask,
                            std::uint64_t *foregroundVoxelCount,
                            int *componentCount,
                            std::uint64_t *largestComponentVoxelCount,
                            QString *errorMessage)
{
    if (!volume.segmentationImage || !volume.segmentationImage->GetPointData()
        || !volume.segmentationImage->GetPointData()->GetScalars()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No categorical segmentation is loaded.");
        }
        return false;
    }

    switch (volume.segmentationImage->GetScalarType()) {
    case VTK_SIGNED_CHAR:
        return buildBinaryMaskAndComponents<signed char>(volume, labelValue, binaryMask,
            foregroundVoxelCount, componentCount, largestComponentVoxelCount, errorMessage);
    case VTK_UNSIGNED_CHAR:
        return buildBinaryMaskAndComponents<unsigned char>(volume, labelValue, binaryMask,
            foregroundVoxelCount, componentCount, largestComponentVoxelCount, errorMessage);
    case VTK_SHORT:
        return buildBinaryMaskAndComponents<short>(volume, labelValue, binaryMask,
            foregroundVoxelCount, componentCount, largestComponentVoxelCount, errorMessage);
    case VTK_UNSIGNED_SHORT:
        return buildBinaryMaskAndComponents<unsigned short>(volume, labelValue, binaryMask,
            foregroundVoxelCount, componentCount, largestComponentVoxelCount, errorMessage);
    case VTK_INT:
        return buildBinaryMaskAndComponents<int>(volume, labelValue, binaryMask,
            foregroundVoxelCount, componentCount, largestComponentVoxelCount, errorMessage);
    case VTK_UNSIGNED_INT:
        return buildBinaryMaskAndComponents<unsigned int>(volume, labelValue, binaryMask,
            foregroundVoxelCount, componentCount, largestComponentVoxelCount, errorMessage);
    default:
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unsupported categorical segmentation scalar type.");
        }
        return false;
    }
}

} // namespace

void NiftiVesselSelectionState::setCurrentVolume(const MultiStructureVolume *volume)
{
    clearSelection();
    m_volume = volume;
}

void NiftiVesselSelectionState::clear()
{
    clearSelection();
    m_volume = nullptr;
}

void NiftiVesselSelectionState::clearSelection()
{
    m_hasSelection = false;
    m_selectedLabel = 0;
    m_selectedBinaryMask = nullptr;
    m_selectedSurface = nullptr;
    m_validation = {};
}

bool NiftiVesselSelectionState::selectLabel(int labelValue,
                                            const vtkPolyData *surface,
                                            QString *errorMessage)
{
    clearSelection();
    if (!m_volume || !m_volume->isValid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("CT and categorical segmentation are not loaded.");
        }
        return false;
    }

    const auto labelIt = std::find_if(
        m_volume->labels.cbegin(),
        m_volume->labels.cend(),
        [labelValue](const MultiStructureLabelInfo &label) {
            return label.value == labelValue;
        });
    if (labelIt == m_volume->labels.cend() || labelIt->voxelCount == 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Label %1 is not present in the current segmentation.").arg(labelValue);
        }
        return false;
    }
    if (!surface) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Label %1 does not have a surface in the current preview.").arg(labelValue);
        }
        return false;
    }

    vtkSmartPointer<vtkImageData> binaryMask;
    std::uint64_t foregroundVoxelCount = 0;
    int connectedComponentCount = 0;
    std::uint64_t largestConnectedComponentVoxelCount = 0;
    if (!dispatchBuildSelection(*m_volume,
                                labelValue,
                                &binaryMask,
                                &foregroundVoxelCount,
                                &connectedComponentCount,
                                &largestConnectedComponentVoxelCount,
                                errorMessage)) {
        return false;
    }
    if (foregroundVoxelCount != labelIt->voxelCount) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Label %1 changed after the current dataset was loaded.").arg(labelValue);
        }
        return false;
    }

    m_validation.labelValue = labelValue;
    m_validation.foregroundVoxelCount = foregroundVoxelCount;
    m_validation.connectedComponentCount = connectedComponentCount;
    m_validation.largestConnectedComponentVoxelCount =
        largestConnectedComponentVoxelCount;
    m_validation.physicalBoundsRasMm = labelIt->physicalBoundsRasMm;
    m_validation.geometryCompatible =
        m_volume->ctImage != nullptr
        && m_volume->segmentationImage != nullptr
        && m_volume->ctIndexToSegmentationIndex != nullptr
        && m_volume->segmentationIndexToCtIndex != nullptr
        && m_volume->segmentationDataToCtData != nullptr;
    if (connectedComponentCount > 1) {
        m_validation.warning = QStringLiteral(
            "Selected label contains %1 connected components. "
            "Vessel straightening may require component or branch selection.")
                                   .arg(connectedComponentCount);
    }
    if (!m_validation.geometryCompatible) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "CT and segmentation geometry cannot be mapped safely.");
        }
        clearSelection();
        return false;
    }

    m_selectedLabel = labelValue;
    m_selectedBinaryMask = std::move(binaryMask);
    m_selectedSurface = surface;
    m_hasSelection = true;
    return true;
}

bool NiftiVesselSelectionState::hasSelectedVesselLabel() const
{
    return m_hasSelection;
}

bool NiftiVesselSelectionState::selectionUsableForProcessing() const
{
    return m_hasSelection
        && m_validation.foregroundVoxelCount > 0
        && m_validation.geometryCompatible
        && m_selectedBinaryMask != nullptr;
}

int NiftiVesselSelectionState::selectedVesselLabel() const
{
    return m_hasSelection ? m_selectedLabel : 0;
}

const MultiStructureVolume *NiftiVesselSelectionState::currentMultiStructureVolume() const
{
    return m_volume;
}

const vtkImageData *NiftiVesselSelectionState::selectedLabelBinaryMask() const
{
    return m_selectedBinaryMask;
}

const vtkPolyData *NiftiVesselSelectionState::selectedLabelSurface() const
{
    return m_selectedSurface;
}

const NiftiVesselLabelValidation *NiftiVesselSelectionState::validation() const
{
    return m_hasSelection ? &m_validation : nullptr;
}
