#include "viewer/NiftiVesselSelectionState.h"

#include <vtkDataArray.h>
#include <vtkPointData.h>
#include <vtkType.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

struct RawComponent
{
    std::uint64_t voxelCount = 0;
    std::array<int, 6> indexBounds = {
        std::numeric_limits<int>::max(), -1,
        std::numeric_limits<int>::max(), -1,
        std::numeric_limits<int>::max(), -1
    };
};

std::array<double, 3> componentPhysicalDimensions(
    const RawComponent &component,
    const NiftiVolumeGeometry &geometry)
{
    std::array<double, 6> bounds = {
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };
    for (double x : {component.indexBounds[0] - 0.5,
                     component.indexBounds[1] + 0.5}) {
        for (double y : {component.indexBounds[2] - 0.5,
                         component.indexBounds[3] + 0.5}) {
            for (double z : {component.indexBounds[4] - 0.5,
                             component.indexBounds[5] + 0.5}) {
                const double indexPoint[4] = {x, y, z, 1.0};
                double worldPoint[4] = {};
                geometry.indexToWorldRas->MultiplyPoint(indexPoint, worldPoint);
                for (int axis = 0; axis < 3; ++axis) {
                    bounds[static_cast<size_t>(axis * 2)] =
                        std::min(bounds[static_cast<size_t>(axis * 2)],
                                 worldPoint[axis]);
                    bounds[static_cast<size_t>(axis * 2 + 1)] =
                        std::max(bounds[static_cast<size_t>(axis * 2 + 1)],
                                 worldPoint[axis]);
                }
            }
        }
    }
    return {
        bounds[1] - bounds[0],
        bounds[3] - bounds[2],
        bounds[5] - bounds[4]
    };
}

template<typename T>
bool buildBinaryMaskAndComponents(const MultiStructureVolume &volume,
                                  int labelValue,
                                  vtkSmartPointer<vtkImageData> *binaryMask,
                                  std::uint64_t *foregroundVoxelCount,
                                  int *componentCount,
                                  std::uint64_t *largestComponentVoxelCount,
                                  std::vector<RawComponent> *componentRecords,
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
        RawComponent component;

        for (size_t queueIndex = 0; queueIndex < queue.size(); ++queueIndex) {
            const std::uint64_t offset = queue[queueIndex];
            ++componentSize;
            const std::uint64_t z = offset / sliceSize;
            const std::uint64_t inSlice = offset % sliceSize;
            const std::uint64_t y = inSlice / width;
            const std::uint64_t x = inSlice % width;
            component.indexBounds[0] =
                std::min(component.indexBounds[0], static_cast<int>(x));
            component.indexBounds[1] =
                std::max(component.indexBounds[1], static_cast<int>(x));
            component.indexBounds[2] =
                std::min(component.indexBounds[2], static_cast<int>(y));
            component.indexBounds[3] =
                std::max(component.indexBounds[3], static_cast<int>(y));
            component.indexBounds[4] =
                std::min(component.indexBounds[4], static_cast<int>(z));
            component.indexBounds[5] =
                std::max(component.indexBounds[5], static_cast<int>(z));

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
        component.voxelCount = componentSize;
        componentRecords->push_back(component);
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
                            std::vector<RawComponent> *componentRecords,
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
            foregroundVoxelCount, componentCount, largestComponentVoxelCount,
            componentRecords, errorMessage);
    case VTK_UNSIGNED_CHAR:
        return buildBinaryMaskAndComponents<unsigned char>(volume, labelValue, binaryMask,
            foregroundVoxelCount, componentCount, largestComponentVoxelCount,
            componentRecords, errorMessage);
    case VTK_SHORT:
        return buildBinaryMaskAndComponents<short>(volume, labelValue, binaryMask,
            foregroundVoxelCount, componentCount, largestComponentVoxelCount,
            componentRecords, errorMessage);
    case VTK_UNSIGNED_SHORT:
        return buildBinaryMaskAndComponents<unsigned short>(volume, labelValue, binaryMask,
            foregroundVoxelCount, componentCount, largestComponentVoxelCount,
            componentRecords, errorMessage);
    case VTK_INT:
        return buildBinaryMaskAndComponents<int>(volume, labelValue, binaryMask,
            foregroundVoxelCount, componentCount, largestComponentVoxelCount,
            componentRecords, errorMessage);
    case VTK_UNSIGNED_INT:
        return buildBinaryMaskAndComponents<unsigned int>(volume, labelValue, binaryMask,
            foregroundVoxelCount, componentCount, largestComponentVoxelCount,
            componentRecords, errorMessage);
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
    m_components.clear();
    m_selectedComponent = 0;
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
    std::vector<RawComponent> componentRecords;
    if (!dispatchBuildSelection(*m_volume,
                                labelValue,
                                &binaryMask,
                                &foregroundVoxelCount,
                                &connectedComponentCount,
                                &largestConnectedComponentVoxelCount,
                                &componentRecords,
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
    std::stable_sort(componentRecords.begin(),
                     componentRecords.end(),
                     [](const RawComponent &left, const RawComponent &right) {
                  return left.voxelCount > right.voxelCount;
              });
    const double voxelVolumeMm3 =
        m_volume->segmentationGeometry.spacing[0]
        * m_volume->segmentationGeometry.spacing[1]
        * m_volume->segmentationGeometry.spacing[2];
    const std::uint64_t noiseThreshold =
        static_cast<std::uint64_t>(std::max(
            8.0,
            std::ceil(1.0 / std::max(voxelVolumeMm3, 1e-9))));
    m_components.reserve(componentRecords.size());
    for (size_t index = 0; index < componentRecords.size(); ++index) {
        const RawComponent &component = componentRecords[index];
        NiftiVesselComponentInfo info;
        info.component = static_cast<int>(index + 1);
        info.voxelCount = component.voxelCount;
        info.physicalVolumeMm3 =
            static_cast<double>(component.voxelCount) * voxelVolumeMm3;
        info.percentageOfSelectedLabel =
            100.0 * static_cast<double>(component.voxelCount)
            / static_cast<double>(foregroundVoxelCount);
        info.physicalBoundingBoxDimensionsMm =
            componentPhysicalDimensions(
                component, m_volume->segmentationGeometry);
        info.likelyNoise = component.voxelCount < noiseThreshold;
        m_components.push_back(info);
    }
    if (m_components.size() == 1) {
        m_selectedComponent = 1;
    }
    return true;
}

bool NiftiVesselSelectionState::selectComponent(
    int component,
    QString *errorMessage)
{
    m_selectedComponent = 0;
    if (!m_hasSelection) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Select a vessel label before selecting a component.");
        }
        return false;
    }
    const auto found = std::find_if(
        m_components.cbegin(),
        m_components.cend(),
        [component](const NiftiVesselComponentInfo &info) {
            return info.component == component;
        });
    if (found == m_components.cend()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Component %1 is not present in the selected label.")
                                .arg(component);
        }
        return false;
    }
    m_selectedComponent = component;
    return true;
}

void NiftiVesselSelectionState::clearComponentSelection()
{
    m_selectedComponent = 0;
}

bool NiftiVesselSelectionState::hasSelectedVesselLabel() const
{
    return m_hasSelection;
}

bool NiftiVesselSelectionState::selectionUsableForProcessing() const
{
    return m_hasSelection
        && hasSelectedComponent()
        && m_validation.foregroundVoxelCount > 0
        && m_validation.geometryCompatible
        && m_selectedBinaryMask != nullptr;
}

bool NiftiVesselSelectionState::hasSelectedComponent() const
{
    return m_selectedComponent > 0
        && static_cast<size_t>(m_selectedComponent) <= m_components.size();
}

int NiftiVesselSelectionState::selectedVesselLabel() const
{
    return m_hasSelection ? m_selectedLabel : 0;
}

int NiftiVesselSelectionState::selectedComponent() const
{
    return hasSelectedComponent() ? m_selectedComponent : 0;
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

const std::vector<NiftiVesselComponentInfo> &
NiftiVesselSelectionState::components() const
{
    return m_components;
}

const NiftiVesselComponentInfo *
NiftiVesselSelectionState::selectedComponentInfo() const
{
    if (!hasSelectedComponent()) {
        return nullptr;
    }
    return &m_components[static_cast<size_t>(m_selectedComponent - 1)];
}
