#include "viewer/MultiStructureNiftiLoader.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>

#include <vtkDataArray.h>
#include <vtkImageData.h>
#include <vtkMatrix4x4.h>
#include <vtkNIFTIImageHeader.h>
#include <vtkNIFTIImageReader.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkType.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

namespace {

constexpr double kMatrixTolerance = 1e-5;
constexpr double kMinimumAffineDeterminant = 1e-12;

struct LabelAccumulator
{
    std::uint64_t count = 0;
    int minX = std::numeric_limits<int>::max();
    int maxX = -1;
    int minY = std::numeric_limits<int>::max();
    int maxY = -1;
    int minZ = std::numeric_limits<int>::max();
    int maxZ = -1;
};

struct NeutralLabelStyle
{
    std::array<double, 3> color;
    double opacity;
};

// Numeric-to-anatomical label mapping is intentionally not present here. The
// source files contain no metadata proving which value is which anatomy.
constexpr std::array<NeutralLabelStyle, 6> kNeutralLabelStyles = {{
    {{{0.86, 0.22, 0.24}}, 0.25},
    {{{0.10, 0.72, 0.88}}, 0.60},
    {{{1.00, 0.72, 0.05}}, 0.95},
    {{{0.54, 0.38, 0.88}}, 0.60},
    {{{0.26, 0.78, 0.42}}, 0.60},
    {{{0.95, 0.46, 0.16}}, 0.70},
}};

vtkSmartPointer<vtkMatrix4x4> copyMatrix(vtkMatrix4x4 *source)
{
    if (!source) {
        return nullptr;
    }
    auto copy = vtkSmartPointer<vtkMatrix4x4>::New();
    copy->DeepCopy(source);
    return copy;
}

bool matricesNearlyEqual(vtkMatrix4x4 *left, vtkMatrix4x4 *right, double tolerance)
{
    if (!left || !right) {
        return left == right;
    }
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            if (std::abs(left->GetElement(row, column) - right->GetElement(row, column)) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

vtkSmartPointer<vtkMatrix4x4> makeIndexToWorld(const NiftiVolumeGeometry &geometry)
{
    auto affine = vtkSmartPointer<vtkMatrix4x4>::New();
    affine->Identity();
    for (int row = 0; row < 3; ++row) {
        double translation = geometry.dataToWorldRas->GetElement(row, 3);
        for (int column = 0; column < 3; ++column) {
            const double matrixValue = geometry.dataToWorldRas->GetElement(row, column);
            affine->SetElement(row, column, matrixValue * geometry.spacing[static_cast<size_t>(column)]);
            translation += matrixValue * geometry.localOrigin[static_cast<size_t>(column)];
        }
        affine->SetElement(row, 3, translation);
    }
    return affine;
}

std::array<double, 6> transformedBounds(const std::array<int, 6> &indexBounds,
                                        const NiftiVolumeGeometry &geometry)
{
    std::array<double, 6> bounds = {
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()
    };

    for (double x : {indexBounds[0] - 0.5, indexBounds[1] + 0.5}) {
        for (double y : {indexBounds[2] - 0.5, indexBounds[3] + 0.5}) {
            for (double z : {indexBounds[4] - 0.5, indexBounds[5] + 0.5}) {
                const double indexPoint[4] = {x, y, z, 1.0};
                double worldPoint[4] = {};
                geometry.indexToWorldRas->MultiplyPoint(indexPoint, worldPoint);
                for (int axis = 0; axis < 3; ++axis) {
                    bounds[static_cast<size_t>(axis * 2)] = std::min(bounds[static_cast<size_t>(axis * 2)], worldPoint[axis]);
                    bounds[static_cast<size_t>(axis * 2 + 1)] = std::max(bounds[static_cast<size_t>(axis * 2 + 1)], worldPoint[axis]);
                }
            }
        }
    }
    return bounds;
}

bool readGeometry(vtkNIFTIImageReader *reader,
                  const QString &path,
                  NiftiVolumeGeometry *geometry,
                  QString *errorMessage)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("NIfTI file does not exist: %1").arg(path);
        }
        return false;
    }
    const QByteArray encodedPath = QFile::encodeName(info.absoluteFilePath());
    if (!reader->CanReadFile(encodedPath.constData())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("VTK cannot identify a supported NIfTI image: %1").arg(path);
        }
        return false;
    }

    reader->SetFileName(encodedPath.constData());
    reader->UpdateInformation();
    if (reader->GetErrorCode() != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("VTK failed to read NIfTI metadata for %1 (error code %2).")
                                .arg(path).arg(reader->GetErrorCode());
        }
        return false;
    }

    vtkNIFTIImageHeader *header = reader->GetNIFTIHeader();
    if (!header || header->GetDim(0) != 3) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Expected a three-dimensional NIfTI image: %1").arg(path);
        }
        return false;
    }
    for (int dimension = 4; dimension < 8; ++dimension) {
        if (header->GetDim(dimension) > 1) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Four-dimensional or multi-channel NIfTI is not supported by this categorical preview: %1")
                                    .arg(path);
            }
            return false;
        }
    }

    for (int axis = 0; axis < 3; ++axis) {
        const auto dimension = header->GetDim(axis + 1);
        const double spacing = std::abs(header->GetPixDim(axis + 1));
        if (dimension <= 0 || dimension > std::numeric_limits<int>::max()
            || !std::isfinite(spacing) || spacing <= 0.0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Invalid NIfTI dimensions or spacing in %1").arg(path);
            }
            return false;
        }
        geometry->dimensions[static_cast<size_t>(axis)] = static_cast<int>(dimension);
        geometry->spacing[static_cast<size_t>(axis)] = spacing;
    }

    geometry->qFormCode = header->GetQFormCode();
    geometry->sFormCode = header->GetSFormCode();
    geometry->qForm = copyMatrix(reader->GetQFormMatrix());
    geometry->sForm = copyMatrix(reader->GetSFormMatrix());
    if (geometry->qForm && geometry->sForm
        && !matricesNearlyEqual(geometry->qForm, geometry->sForm, kMatrixTolerance)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("NIfTI qform and sform disagree; refusing to guess the physical transform for %1")
                                .arg(path);
        }
        return false;
    }

    geometry->dataToWorldRas = geometry->sForm ? copyMatrix(geometry->sForm)
                                               : copyMatrix(geometry->qForm);
    if (!geometry->dataToWorldRas) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("NIfTI has no qform or sform physical transform: %1").arg(path);
        }
        return false;
    }
    geometry->indexToWorldRas = makeIndexToWorld(*geometry);
    geometry->scalarType = header->GetDataType();
    geometry->scalarTypeName = QStringLiteral("NIfTI datatype %1").arg(header->GetDataType());
    geometry->scalarComponents = 1;
    geometry->voxelCount = static_cast<std::uint64_t>(geometry->dimensions[0])
        * static_cast<std::uint64_t>(geometry->dimensions[1])
        * static_cast<std::uint64_t>(geometry->dimensions[2]);
    geometry->uncompressedBytes = geometry->voxelCount
        * static_cast<std::uint64_t>(std::max(header->GetBitPix(), 0)) / 8ULL;

    const std::array<int, 6> wholeExtent = {
        0, geometry->dimensions[0] - 1,
        0, geometry->dimensions[1] - 1,
        0, geometry->dimensions[2] - 1
    };
    geometry->physicalBoundsRasMm = transformedBounds(wholeExtent, *geometry);
    return true;
}

double normalizedColumnDot(vtkMatrix4x4 *left, vtkMatrix4x4 *right, int column)
{
    double leftLength = 0.0;
    double rightLength = 0.0;
    double dot = 0.0;
    for (int row = 0; row < 3; ++row) {
        const double l = left->GetElement(row, column);
        const double r = right->GetElement(row, column);
        leftLength += l * l;
        rightLength += r * r;
        dot += l * r;
    }
    if (leftLength <= 0.0 || rightLength <= 0.0) {
        return -1.0;
    }
    return dot / std::sqrt(leftLength * rightLength);
}

bool geometriesSharePhysicalSpace(const NiftiVolumeGeometry &ct,
                                  const NiftiVolumeGeometry &segmentation,
                                  QString *reason)
{
    for (int axis = 0; axis < 3; ++axis) {
        if (normalizedColumnDot(ct.indexToWorldRas, segmentation.indexToWorldRas, axis) < 0.9999) {
            if (reason) {
                *reason = QStringLiteral("CT and segmentation axes have different physical orientations.");
            }
            return false;
        }
    }

    const double tolerance = std::max({ct.spacing[0], ct.spacing[1], ct.spacing[2],
                                       segmentation.spacing[0], segmentation.spacing[1], segmentation.spacing[2]})
        * 1.1 + 1e-3;
    for (int index = 0; index < 6; ++index) {
        if (std::abs(ct.physicalBoundsRasMm[static_cast<size_t>(index)]
                     - segmentation.physicalBoundsRasMm[static_cast<size_t>(index)]) > tolerance) {
            if (reason) {
                *reason = QStringLiteral("CT and segmentation physical bounds differ by more than %1 mm.")
                              .arg(tolerance, 0, 'g', 5);
            }
            return false;
        }
    }
    return true;
}

template<typename T>
bool collectLabelsTyped(
    vtkImageData *image,
    const NiftiVolumeGeometry &geometry,
    std::vector<MultiStructureLabelInfo> *labels,
    QString *errorMessage)
{
    vtkDataArray *scalars = image->GetPointData()->GetScalars();
    const auto *values = static_cast<const T *>(scalars->GetVoidPointer(0));
    const std::uint64_t voxelCount = geometry.voxelCount;
    if (!values || voxelCount == 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Segmentation scalar buffer is empty.");
        }
        return false;
    }

    std::map<int, LabelAccumulator> accumulators;
    std::uint64_t offset = 0;
    for (int z = 0; z < geometry.dimensions[2]; ++z) {
        for (int y = 0; y < geometry.dimensions[1]; ++y) {
            for (int x = 0; x < geometry.dimensions[0]; ++x, ++offset) {
                const auto value = static_cast<std::int64_t>(values[offset]);
                if (value == 0) {
                    continue;
                }
                if (value < std::numeric_limits<int>::min()
                    || value > std::numeric_limits<int>::max()) {
                    if (errorMessage) {
                        *errorMessage = QStringLiteral(
                            "Segmentation label %1 is outside the supported 32-bit integer range.")
                                            .arg(value);
                    }
                    return false;
                }

                LabelAccumulator &accumulator = accumulators[static_cast<int>(value)];
                ++accumulator.count;
                accumulator.minX = std::min(accumulator.minX, x);
                accumulator.maxX = std::max(accumulator.maxX, x);
                accumulator.minY = std::min(accumulator.minY, y);
                accumulator.maxY = std::max(accumulator.maxY, y);
                accumulator.minZ = std::min(accumulator.minZ, z);
                accumulator.maxZ = std::max(accumulator.maxZ, z);
            }
        }
    }

    if (accumulators.empty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Segmentation does not contain any nonzero categorical labels.");
        }
        return false;
    }

    std::vector<MultiStructureLabelInfo> collectedLabels;
    collectedLabels.reserve(accumulators.size());
    size_t styleIndex = 0;
    for (const auto &[labelValue, accumulator] : accumulators) {
        MultiStructureLabelInfo label;
        label.value = labelValue;
        label.displayName = QStringLiteral("Label %1").arg(label.value);
        label.voxelCount = accumulator.count;
        label.indexBounds = {
            accumulator.minX, accumulator.maxX,
            accumulator.minY, accumulator.maxY,
            accumulator.minZ, accumulator.maxZ
        };
        label.physicalBoundsRasMm =
            transformedBounds(label.indexBounds, geometry);
        const NeutralLabelStyle &style =
            kNeutralLabelStyles[styleIndex % kNeutralLabelStyles.size()];
        label.color = style.color;
        label.defaultOpacity = style.opacity;
        collectedLabels.push_back(label);
        ++styleIndex;
    }

    *labels = std::move(collectedLabels);
    return true;
}

bool collectLabels(
    vtkImageData *image,
    const NiftiVolumeGeometry &geometry,
    std::vector<MultiStructureLabelInfo> *labels,
    QString *errorMessage)
{
    if (!image || !labels
        || !image->GetPointData() || !image->GetPointData()->GetScalars()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Cannot scan labels from an empty segmentation.");
        }
        return false;
    }

    switch (image->GetScalarType()) {
    case VTK_SIGNED_CHAR:
        return collectLabelsTyped<signed char>(image, geometry, labels, errorMessage);
    case VTK_UNSIGNED_CHAR:
        return collectLabelsTyped<unsigned char>(image, geometry, labels, errorMessage);
    case VTK_SHORT:
        return collectLabelsTyped<short>(image, geometry, labels, errorMessage);
    case VTK_UNSIGNED_SHORT:
        return collectLabelsTyped<unsigned short>(image, geometry, labels, errorMessage);
    case VTK_INT:
        return collectLabelsTyped<int>(image, geometry, labels, errorMessage);
    case VTK_UNSIGNED_INT:
        return collectLabelsTyped<unsigned int>(image, geometry, labels, errorMessage);
    default:
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Segmentation must be a categorical integer label map; "
                "VTK scalar type %1 is unsupported.")
                                .arg(QString::fromLatin1(
                                    image->GetScalarTypeAsString()));
        }
        return false;
    }
}

bool validateLoadedImage(vtkImageData *image,
                         const NiftiVolumeGeometry &geometry,
                         const QString &description,
                         QString *errorMessage)
{
    if (!image || image->GetNumberOfScalarComponents() != 1
        || !image->GetPointData() || !image->GetPointData()->GetScalars()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1 must contain one scalar component.")
                                .arg(description);
        }
        return false;
    }

    int outputDimensions[3] = {};
    image->GetDimensions(outputDimensions);
    for (int axis = 0; axis < 3; ++axis) {
        if (outputDimensions[axis] != geometry.dimensions[static_cast<size_t>(axis)]) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("%1 dimensions disagree with the NIfTI header.")
                                    .arg(description);
            }
            return false;
        }
    }
    if (static_cast<std::uint64_t>(image->GetPointData()->GetScalars()->GetNumberOfTuples())
        != geometry.voxelCount) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1 scalar tuple count disagrees with its dimensions.")
                                .arg(description);
        }
        return false;
    }
    return true;
}

bool matrixIsFinite(vtkMatrix4x4 *matrix)
{
    if (!matrix) {
        return false;
    }
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            if (!std::isfinite(matrix->GetElement(row, column))) {
                return false;
            }
        }
    }
    return true;
}

bool createIndexSpaceTransforms(const NiftiVolumeGeometry &ctGeometry,
                                const NiftiVolumeGeometry &segmentationGeometry,
                                vtkSmartPointer<vtkMatrix4x4> *ctIndexToSegmentationIndex,
                                vtkSmartPointer<vtkMatrix4x4> *segmentationIndexToCtIndex,
                                vtkSmartPointer<vtkMatrix4x4> *segmentationDataToCtData,
                                QString *errorMessage)
{
    if (!ctIndexToSegmentationIndex || !segmentationIndexToCtIndex
        || !segmentationDataToCtData
        || !matrixIsFinite(ctGeometry.indexToWorldRas)
        || !matrixIsFinite(segmentationGeometry.indexToWorldRas)
        || !matrixIsFinite(ctGeometry.dataToWorldRas)
        || !matrixIsFinite(segmentationGeometry.dataToWorldRas)
        || std::abs(ctGeometry.indexToWorldRas->Determinant()) < kMinimumAffineDeterminant
        || std::abs(segmentationGeometry.indexToWorldRas->Determinant()) < kMinimumAffineDeterminant
        || std::abs(ctGeometry.dataToWorldRas->Determinant()) < kMinimumAffineDeterminant
        || std::abs(segmentationGeometry.dataToWorldRas->Determinant()) < kMinimumAffineDeterminant) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "CT or segmentation index-to-RAS transform is invalid or singular.");
        }
        return false;
    }

    auto worldToCtIndex = vtkSmartPointer<vtkMatrix4x4>::New();
    auto worldToSegmentationIndex = vtkSmartPointer<vtkMatrix4x4>::New();
    vtkMatrix4x4::Invert(ctGeometry.indexToWorldRas, worldToCtIndex);
    vtkMatrix4x4::Invert(segmentationGeometry.indexToWorldRas,
                        worldToSegmentationIndex);

    auto ctToSegmentation = vtkSmartPointer<vtkMatrix4x4>::New();
    auto segmentationToCt = vtkSmartPointer<vtkMatrix4x4>::New();
    vtkMatrix4x4::Multiply4x4(worldToSegmentationIndex,
                             ctGeometry.indexToWorldRas,
                             ctToSegmentation);
    vtkMatrix4x4::Multiply4x4(worldToCtIndex,
                             segmentationGeometry.indexToWorldRas,
                             segmentationToCt);
    auto worldToCtData = vtkSmartPointer<vtkMatrix4x4>::New();
    vtkMatrix4x4::Invert(ctGeometry.dataToWorldRas, worldToCtData);
    auto segmentationDataToCt =
        vtkSmartPointer<vtkMatrix4x4>::New();
    vtkMatrix4x4::Multiply4x4(worldToCtData,
                             segmentationGeometry.dataToWorldRas,
                             segmentationDataToCt);
    if (!matrixIsFinite(ctToSegmentation)
        || !matrixIsFinite(segmentationToCt)
        || !matrixIsFinite(segmentationDataToCt)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Could not derive finite CT/segmentation index-space transforms.");
        }
        return false;
    }

    *ctIndexToSegmentationIndex = std::move(ctToSegmentation);
    *segmentationIndexToCtIndex = std::move(segmentationToCt);
    *segmentationDataToCtData = std::move(segmentationDataToCt);
    return true;
}

QString matrixSummary(vtkMatrix4x4 *matrix)
{
    if (!matrix) {
        return QStringLiteral("none");
    }
    QStringList rows;
    for (int row = 0; row < 4; ++row) {
        QStringList values;
        for (int column = 0; column < 4; ++column) {
            values << QString::number(matrix->GetElement(row, column), 'g', 8);
        }
        rows << QStringLiteral("[%1]").arg(values.join(QStringLiteral(", ")));
    }
    return rows.join(QStringLiteral(" "));
}

} // namespace

bool MultiStructureNiftiLoader::load(const QString &ctPath,
                                     const QString &segmentationPath,
                                     MultiStructureVolume *loadedVolume,
                                     QString *errorMessage) const
{
    if (!loadedVolume) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Multi-structure output object is null.");
        }
        return false;
    }

    MultiStructureVolume candidate;
    candidate.ctPath = QFileInfo(ctPath).absoluteFilePath();
    candidate.segmentationPath = QFileInfo(segmentationPath).absoluteFilePath();

    vtkNew<vtkNIFTIImageReader> ctReader;
    vtkNew<vtkNIFTIImageReader> segmentationReader;
    if (!readGeometry(ctReader, candidate.ctPath, &candidate.ctGeometry, errorMessage)
        || !readGeometry(segmentationReader, candidate.segmentationPath, &candidate.segmentationGeometry, errorMessage)) {
        return false;
    }

    QString alignmentError;
    if (!geometriesSharePhysicalSpace(candidate.ctGeometry, candidate.segmentationGeometry, &alignmentError)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("CT and segmentation alignment could not be proven: %1").arg(alignmentError);
        }
        return false;
    }

    QElapsedTimer ctReadTimer;
    ctReadTimer.start();
    ctReader->Update();
    if (ctReader->GetErrorCode() != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("VTK failed to read CT voxels (error code %1).")
                                .arg(ctReader->GetErrorCode());
        }
        return false;
    }
    const qint64 ctReadMilliseconds = ctReadTimer.elapsed();

    QElapsedTimer segmentationReadTimer;
    segmentationReadTimer.start();
    segmentationReader->Update();
    if (segmentationReader->GetErrorCode() != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("VTK failed to read segmentation voxels (error code %1).")
                                .arg(segmentationReader->GetErrorCode());
        }
        return false;
    }
    const qint64 segmentationReadMilliseconds = segmentationReadTimer.elapsed();

    vtkImageData *ctReaderOutput = ctReader->GetOutput();
    vtkImageData *segmentationReaderOutput = segmentationReader->GetOutput();
    if (!validateLoadedImage(ctReaderOutput,
                             candidate.ctGeometry,
                             QStringLiteral("CT"),
                             errorMessage)
        || !validateLoadedImage(segmentationReaderOutput,
                                candidate.segmentationGeometry,
                                QStringLiteral("Segmentation"),
                                errorMessage)) {
        return false;
    }

    const double storedSlope = ctReader->GetRescaleSlope();
    const double storedIntercept = ctReader->GetRescaleIntercept();
    if (!std::isfinite(storedSlope) || !std::isfinite(storedIntercept)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("CT NIfTI contains non-finite rescale calibration.");
        }
        return false;
    }
    // NIfTI defines scl_slope == 0 as "no rescaling"; expose an effective
    // calibration so consumers can always use stored*slope + intercept.
    candidate.ctRescaleSlope = storedSlope == 0.0 ? 1.0 : storedSlope;
    candidate.ctRescaleIntercept = storedSlope == 0.0 ? 0.0 : storedIntercept;

    candidate.ctGeometry.scalarType = ctReaderOutput->GetScalarType();
    candidate.ctGeometry.scalarTypeName =
        QString::fromLatin1(ctReaderOutput->GetScalarTypeAsString());
    candidate.ctGeometry.scalarComponents =
        ctReaderOutput->GetNumberOfScalarComponents();
    candidate.ctImage = vtkSmartPointer<vtkImageData>::New();
    candidate.ctImage->ShallowCopy(ctReaderOutput);

    candidate.segmentationGeometry.scalarType =
        segmentationReaderOutput->GetScalarType();
    candidate.segmentationGeometry.scalarTypeName =
        QString::fromLatin1(segmentationReaderOutput->GetScalarTypeAsString());
    candidate.segmentationGeometry.scalarComponents =
        segmentationReaderOutput->GetNumberOfScalarComponents();
    candidate.segmentationImage = vtkSmartPointer<vtkImageData>::New();
    candidate.segmentationImage->ShallowCopy(segmentationReaderOutput);

    if (!createIndexSpaceTransforms(candidate.ctGeometry,
                                    candidate.segmentationGeometry,
                                    &candidate.ctIndexToSegmentationIndex,
                                    &candidate.segmentationIndexToCtIndex,
                                    &candidate.segmentationDataToCtData,
                                    errorMessage)) {
        return false;
    }

    QElapsedTimer categoricalScanTimer;
    categoricalScanTimer.start();
    if (!collectLabels(candidate.segmentationImage,
                       candidate.segmentationGeometry,
                       &candidate.labels,
                       errorMessage)) {
        return false;
    }
    const qint64 categoricalScanMilliseconds = categoricalScanTimer.elapsed();

    qInfo() << "Loaded native multi-label NIfTI preview"
            << "CT=" << candidate.ctPath
            << "segmentation=" << candidate.segmentationPath
            << "dimensions=" << candidate.segmentationGeometry.dimensions[0]
            << "x" << candidate.segmentationGeometry.dimensions[1]
            << "x" << candidate.segmentationGeometry.dimensions[2]
            << "spacing=" << candidate.segmentationGeometry.spacing[0]
            << candidate.segmentationGeometry.spacing[1]
            << candidate.segmentationGeometry.spacing[2]
            << "CT read ms=" << ctReadMilliseconds
            << "segmentation read ms=" << segmentationReadMilliseconds
            << "categorical label scan ms="
            << categoricalScanMilliseconds;
    qInfo() << "NIfTI CT scalar type=" << candidate.ctGeometry.scalarTypeName
            << "rescale slope/intercept="
            << candidate.ctRescaleSlope << candidate.ctRescaleIntercept;
    qInfo() << "NIfTI CT index-to-RAS affine:" << matrixSummary(candidate.ctGeometry.indexToWorldRas);
    qInfo() << "NIfTI segmentation index-to-RAS affine:" << matrixSummary(candidate.segmentationGeometry.indexToWorldRas);
    qInfo() << "NIfTI CT-index to segmentation-index transform:"
            << matrixSummary(candidate.ctIndexToSegmentationIndex);
    qInfo() << "NIfTI segmentation-index to CT-index transform:"
            << matrixSummary(candidate.segmentationIndexToCtIndex);
    qInfo() << "NIfTI segmentation-data to CT-data physical transform:"
            << matrixSummary(candidate.segmentationDataToCtData);
    for (const MultiStructureLabelInfo &label : candidate.labels) {
        qInfo() << label.displayName
                << "voxels=" << label.voxelCount
                << "index bounds="
                << label.indexBounds[0] << label.indexBounds[1]
                << label.indexBounds[2] << label.indexBounds[3]
                << label.indexBounds[4] << label.indexBounds[5]
                << "RAS bounds mm="
                << label.physicalBoundsRasMm[0] << label.physicalBoundsRasMm[1]
                << label.physicalBoundsRasMm[2] << label.physicalBoundsRasMm[3]
                << label.physicalBoundsRasMm[4] << label.physicalBoundsRasMm[5];
    }

    *loadedVolume = std::move(candidate);
    return true;
}
