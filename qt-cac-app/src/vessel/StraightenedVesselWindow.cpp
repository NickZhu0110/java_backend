#include "vessel/StraightenedVesselWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <vtkNrrdReader.h>
#include <vtkNew.h>
#include <vtkPointData.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

bool imageIsValid(vtkImageData *image)
{
    if (!image || !image->GetPointData()
        || !image->GetPointData()->GetScalars()) {
        return false;
    }
    int dimensions[3] = {};
    image->GetDimensions(dimensions);
    return dimensions[0] > 0 && dimensions[1] > 0
        && dimensions[2] > 0;
}

using Vector3 = std::array<double, 3>;

Vector3 subtract(const Vector3 &left, const Vector3 &right)
{
    return {
        left[0] - right[0],
        left[1] - right[1],
        left[2] - right[2]
    };
}

double dot(const Vector3 &left, const Vector3 &right)
{
    return left[0] * right[0]
        + left[1] * right[1]
        + left[2] * right[2];
}

Vector3 cross(const Vector3 &left, const Vector3 &right)
{
    return {
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0]
    };
}

Vector3 normalized(const Vector3 &value)
{
    const double length = std::sqrt(dot(value, value));
    if (length < 1.0e-12) {
        return {1.0, 0.0, 0.0};
    }
    return {
        value[0] / length,
        value[1] / length,
        value[2] / length
    };
}

} // namespace

StraightenedVesselWindow::StraightenedVesselWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlag(Qt::Window, true);
    setWindowTitle(QStringLiteral("Straightened Vessel"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(1120, 760);

    auto *mainLayout = new QVBoxLayout(this);
    auto *topRow = new QHBoxLayout;
    m_metadataLabel = new QLabel(this);
    m_metadataLabel->setWordWrap(true);
    topRow->addWidget(m_metadataLabel, 1);
    auto *showCenterlineCheckBox =
        new QCheckBox(QStringLiteral("Show 3D centerline"), this);
    showCenterlineCheckBox->setChecked(true);
    auto *anotherPathButton =
        new QPushButton(QStringLiteral("Select Another Path"), this);
    auto *exportButton =
        new QPushButton(QStringLiteral("Export"), this);
    topRow->addWidget(showCenterlineCheckBox);
    topRow->addWidget(anotherPathButton);
    topRow->addWidget(exportButton);
    mainLayout->addLayout(topRow);

    auto *controlRow = new QHBoxLayout;
    controlRow->addWidget(new QLabel(QStringLiteral("Longitudinal:"), this));
    m_longitudinalModeComboBox = new QComboBox(this);
    m_longitudinalModeComboBox->addItem(
        QStringLiteral("Curved CPR \u2014 orientation A"));
    m_longitudinalModeComboBox->addItem(
        QStringLiteral("Curved CPR \u2014 orientation B"));
    m_longitudinalModeComboBox->addItem(
        QStringLiteral("Straightened MPR \u2014 X-Z"));
    m_longitudinalModeComboBox->addItem(
        QStringLiteral("Straightened MPR \u2014 Y-Z"));
    m_longitudinalModeComboBox->addItem(
        QStringLiteral("Thin-slab MIP"));
    controlRow->addWidget(m_longitudinalModeComboBox);
    controlRow->addWidget(new QLabel(QStringLiteral("Slab:"), this));
    m_slabThicknessSpinBox = new QSpinBox(this);
    m_slabThicknessSpinBox->setRange(1, 15);
    m_slabThicknessSpinBox->setSuffix(QStringLiteral(" mm"));
    m_slabThicknessSpinBox->setValue(5);
    m_slabThicknessSpinBox->setEnabled(false);
    controlRow->addWidget(m_slabThicknessSpinBox);
    controlRow->addSpacing(16);
    controlRow->addWidget(new QLabel(QStringLiteral("Window:"), this));
    m_windowWidthSpinBox = new QSpinBox(this);
    m_windowWidthSpinBox->setRange(50, 4000);
    m_windowWidthSpinBox->setValue(700);
    controlRow->addWidget(m_windowWidthSpinBox);
    controlRow->addWidget(new QLabel(QStringLiteral("Level:"), this));
    m_windowLevelSpinBox = new QSpinBox(this);
    m_windowLevelSpinBox->setRange(-1500, 3000);
    m_windowLevelSpinBox->setValue(150);
    controlRow->addWidget(m_windowLevelSpinBox);
    m_maskOverlayCheckBox =
        new QCheckBox(QStringLiteral("Vessel mask overlay"), this);
    m_maskOverlayCheckBox->setChecked(true);
    controlRow->addWidget(m_maskOverlayCheckBox);
    controlRow->addStretch(1);
    mainLayout->addLayout(controlRow);

    auto *imagesLayout = new QGridLayout;
    m_longitudinalTitleLabel =
        new QLabel(QStringLiteral("Curved CPR \u2014 orientation A"), this);
    m_longitudinalTitleLabel->setAlignment(Qt::AlignCenter);
    auto *crossSectionTitle =
        new QLabel(QStringLiteral("Cross-section (X-Y)"), this);
    crossSectionTitle->setAlignment(Qt::AlignCenter);
    imagesLayout->addWidget(m_longitudinalTitleLabel, 0, 0);
    imagesLayout->addWidget(crossSectionTitle, 0, 1);
    m_longitudinalLabel = new QLabel(this);
    m_longitudinalLabel->setAlignment(Qt::AlignCenter);
    m_longitudinalLabel->setMinimumSize(480, 500);
    m_longitudinalLabel->setStyleSheet(
        QStringLiteral("background: black; border: 1px solid #555;"));
    m_crossSectionLabel = new QLabel(this);
    m_crossSectionLabel->setAlignment(Qt::AlignCenter);
    m_crossSectionLabel->setMinimumSize(420, 420);
    m_crossSectionLabel->setStyleSheet(
        QStringLiteral("background: black; border: 1px solid #555;"));
    imagesLayout->addWidget(m_longitudinalLabel, 1, 0);
    imagesLayout->addWidget(m_crossSectionLabel, 1, 1);
    imagesLayout->setColumnStretch(0, 1);
    imagesLayout->setColumnStretch(1, 1);
    mainLayout->addLayout(imagesLayout, 1);

    auto *positionRow = new QHBoxLayout;
    positionRow->addWidget(new QLabel(QStringLiteral("Arc length:"), this));
    m_positionSlider = new QSlider(Qt::Horizontal, this);
    m_positionSlider->setRange(0, 0);
    positionRow->addWidget(m_positionSlider, 1);
    m_positionLabel = new QLabel(QStringLiteral("0.0 mm"), this);
    m_positionLabel->setMinimumWidth(90);
    positionRow->addWidget(m_positionLabel);
    mainLayout->addLayout(positionRow);

    connect(m_positionSlider, &QSlider::valueChanged,
            this, [this]() { updateImages(); });
    connect(m_windowWidthSpinBox, qOverload<int>(&QSpinBox::valueChanged),
            this, [this]() { updateImages(); });
    connect(m_windowLevelSpinBox, qOverload<int>(&QSpinBox::valueChanged),
            this, [this]() { updateImages(); });
    connect(m_maskOverlayCheckBox, &QCheckBox::toggled,
            this, [this]() { updateImages(); });
    connect(m_longitudinalModeComboBox,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        m_slabThicknessSpinBox->setEnabled(index == 4);
        updateImages();
    });
    connect(m_slabThicknessSpinBox, qOverload<int>(&QSpinBox::valueChanged),
            this, [this]() { updateImages(); });
    connect(anotherPathButton, &QPushButton::clicked,
            this, &StraightenedVesselWindow::anotherPathRequested);
    connect(showCenterlineCheckBox, &QCheckBox::toggled,
            this, &StraightenedVesselWindow::centerlineVisibilityRequested);
    connect(exportButton, &QPushButton::clicked,
            this, &StraightenedVesselWindow::exportResult);
}

bool StraightenedVesselWindow::loadResult(
    const QJsonObject &result,
    const QJsonArray &candidatePaths,
    QString *errorMessage)
{
    vtkSmartPointer<vtkImageData> ct;
    vtkSmartPointer<vtkImageData> mask;
    if (!readNrrd(
            result.value(QStringLiteral("straightened_ct_path")).toString(),
            &ct,
            errorMessage)
        || !readNrrd(
            result.value(
                QStringLiteral("straightened_vessel_mask_path")).toString(),
            &mask,
            errorMessage)) {
        return false;
    }
    int ctDimensions[3] = {};
    int maskDimensions[3] = {};
    ct->GetDimensions(ctDimensions);
    mask->GetDimensions(maskDimensions);
    if (!std::equal(std::begin(ctDimensions), std::end(ctDimensions),
                    std::begin(maskDimensions))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Straightened CT and vessel mask dimensions differ.");
        }
        return false;
    }
    if (!readCenterlineFrames(
            result.value(
                QStringLiteral("selected_centerline_path")).toString(),
            errorMessage)) {
        return false;
    }
    if (static_cast<int>(m_centerlinePoints.size()) != ctDimensions[2]) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Centerline sample count (%1) does not match the "
                "straightened volume Z dimension (%2).")
                                .arg(m_centerlinePoints.size())
                                .arg(ctDimensions[2]);
        }
        return false;
    }
    m_ctImage = ct;
    m_maskImage = mask;
    m_result = result;
    m_candidatePaths = candidatePaths;
    m_positionSlider->setRange(0, std::max(0, ctDimensions[2] - 1));
    m_positionSlider->setValue(ctDimensions[2] / 2);
    m_longitudinalModeComboBox->setCurrentIndex(0);
    updateMetadata();
    updateImages();
    return true;
}

bool StraightenedVesselWindow::readNrrd(
    const QString &path,
    vtkSmartPointer<vtkImageData> *image,
    QString *errorMessage)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("NRRD output does not exist: %1").arg(path);
        }
        return false;
    }
    QString readerPath = info.absoluteFilePath();
    QTemporaryDir temporaryDirectory;
    vtkNew<vtkNrrdReader> reader;
    QByteArray encodedPath = QFile::encodeName(readerPath);
    if (!reader->CanReadFile(encodedPath.constData())) {
        if (!temporaryDirectory.isValid()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral(
                    "Could not create a Unicode-safe NRRD read directory.");
            }
            return false;
        }
        readerPath = QDir(temporaryDirectory.path())
                         .absoluteFilePath(QStringLiteral("image.nrrd"));
        if (!QFile::copy(info.absoluteFilePath(), readerPath)) {
            if (errorMessage) {
                *errorMessage =
                    QStringLiteral("Could not stage NRRD for reading.");
            }
            return false;
        }
        encodedPath = QFile::encodeName(readerPath);
    }
    reader->SetFileName(encodedPath.constData());
    reader->Update();
    if (reader->GetErrorCode() != 0
        || !imageIsValid(reader->GetOutput())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "VTK could not read the straightened NRRD: %1")
                                    .arg(path);
        }
        return false;
    }
    *image = vtkSmartPointer<vtkImageData>::New();
    (*image)->DeepCopy(reader->GetOutput());
    return true;
}

bool StraightenedVesselWindow::readCenterlineFrames(
    const QString &path,
    QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Could not read the selected centerline frames: %1")
                                .arg(file.errorString());
        }
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Selected centerline JSON is invalid: %1")
                                .arg(parseError.errorString());
        }
        return false;
    }
    const QJsonObject object = document.object();
    const auto readVectors =
        [errorMessage](
            const QJsonArray &values,
            const QString &name,
            std::vector<std::array<double, 3>> *output) {
        output->clear();
        output->reserve(static_cast<size_t>(values.size()));
        for (const QJsonValue &value : values) {
            const QJsonArray vector = value.toArray();
            if (vector.size() != 3
                || !vector.at(0).isDouble()
                || !vector.at(1).isDouble()
                || !vector.at(2).isDouble()) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral(
                        "Selected centerline has an invalid %1 entry.")
                                            .arg(name);
                }
                return false;
            }
            output->push_back({
                vector.at(0).toDouble(),
                vector.at(1).toDouble(),
                vector.at(2).toDouble()
            });
        }
        return !output->empty();
    };
    std::vector<std::array<double, 3>> points;
    std::vector<std::array<double, 3>> tangents;
    std::vector<std::array<double, 3>> normals;
    if (!readVectors(
            object.value(QStringLiteral("points_lps_mm")).toArray(),
            QStringLiteral("point"), &points)
        || !readVectors(
            object.value(QStringLiteral("frenet_tangents")).toArray(),
            QStringLiteral("tangent"), &tangents)
        || !readVectors(
            object.value(
                QStringLiteral("parallel_transport_normals")).toArray(),
            QStringLiteral("parallel-transport normal"), &normals)
        || points.size() != tangents.size()
        || points.size() != normals.size()) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral(
                "Selected centerline frame arrays have different lengths.");
        }
        return false;
    }
    m_centerlinePoints = std::move(points);
    m_centerlineTangents = std::move(tangents);
    m_centerlineNormals = std::move(normals);
    return true;
}

void StraightenedVesselWindow::updateImages()
{
    if (!imageIsValid(m_ctImage) || !imageIsValid(m_maskImage)) {
        return;
    }
    const QImage longitudinal = renderLongitudinal();
    const QImage crossSection =
        renderCrossSection(m_positionSlider->value());
    if (m_longitudinalTitleLabel) {
        m_longitudinalTitleLabel->setText(
            m_longitudinalModeComboBox->currentText());
    }
    m_longitudinalLabel->setPixmap(
        QPixmap::fromImage(longitudinal).scaled(
            m_longitudinalLabel->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
    m_crossSectionLabel->setPixmap(
        QPixmap::fromImage(crossSection).scaled(
            m_crossSectionLabel->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
    double spacing[3] = {};
    m_ctImage->GetSpacing(spacing);
    m_positionLabel->setText(
        QStringLiteral("%1 mm")
            .arg(m_positionSlider->value() * spacing[2], 0, 'f', 1));
}

void StraightenedVesselWindow::updateMetadata()
{
    m_metadataLabel->setText(
        QStringLiteral(
            "Label %1 | Component %2 | Path %3 | Length %4 mm | "
            "Processing %5 s")
            .arg(m_result.value(
                     QStringLiteral("selected_numeric_label")).toInt())
            .arg(m_result.value(
                     QStringLiteral("selected_component")).toInt())
            .arg(m_result.value(
                     QStringLiteral("selected_path")).toString())
            .arg(m_result.value(
                     QStringLiteral("path_length_mm")).toDouble(), 0, 'f', 1)
            .arg(m_result.value(
                     QStringLiteral("processing_duration_seconds")).toDouble(),
                 0, 'f', 2));
}

QImage StraightenedVesselWindow::renderLongitudinal() const
{
    switch (m_longitudinalModeComboBox->currentIndex()) {
    case 0:
        return renderCurvedCpr(true);
    case 1:
        return renderCurvedCpr(false);
    case 2:
        return renderStraightenedMpr(true, false);
    case 3:
        return renderStraightenedMpr(false, false);
    case 4:
        return renderStraightenedMpr(true, true);
    default:
        return renderCurvedCpr(true);
    }
}

QImage StraightenedVesselWindow::renderCurvedCpr(
    bool orientationA) const
{
    int dimensions[3] = {};
    m_ctImage->GetDimensions(dimensions);
    if (m_centerlinePoints.size()
            != static_cast<size_t>(dimensions[2])
        || m_centerlineTangents.size() != m_centerlinePoints.size()
        || m_centerlineNormals.size() != m_centerlinePoints.size()) {
        return renderStraightenedMpr(orientationA, false);
    }
    double spacing[3] = {};
    m_ctImage->GetSpacing(spacing);
    const int radialCount =
        orientationA ? dimensions[0] : dimensions[1];
    const double radialSpacing =
        orientationA ? spacing[0] : spacing[1];
    const double rasterSpacing =
        std::max(0.05, std::min(radialSpacing, spacing[2]));
    std::vector<std::array<double, 2>> centers(
        m_centerlinePoints.size(), {0.0, 0.0});
    std::vector<double> angles(m_centerlinePoints.size(), 0.0);
    for (size_t index = 1; index < m_centerlinePoints.size(); ++index) {
        const double segmentLength =
            std::sqrt(dot(
                subtract(
                    m_centerlinePoints[index],
                    m_centerlinePoints[index - 1]),
                subtract(
                    m_centerlinePoints[index],
                    m_centerlinePoints[index - 1])));
        centers[index][0] =
            centers[index - 1][0]
            + segmentLength * std::cos(angles[index - 1]);
        centers[index][1] =
            centers[index - 1][1]
            + segmentLength * std::sin(angles[index - 1]);

        const Vector3 previousTangent =
            normalized(m_centerlineTangents[index - 1]);
        const Vector3 currentTangent =
            normalized(m_centerlineTangents[index]);
        Vector3 normal = m_centerlineNormals[index - 1];
        const double tangentProjection = dot(normal, previousTangent);
        for (int axis = 0; axis < 3; ++axis) {
            normal[axis] -=
                tangentProjection * previousTangent[axis];
        }
        normal = normalized(normal);
        const Vector3 second =
            normalized(cross(previousTangent, normal));
        Vector3 planeNormal = orientationA ? second : normal;
        if (!orientationA) {
            for (double &value : planeNormal) {
                value = -value;
            }
        }
        const double signedTurn = std::atan2(
            dot(cross(previousTangent, currentTangent), planeNormal),
            std::clamp(
                dot(previousTangent, currentTangent), -1.0, 1.0));
        angles[index] = angles[index - 1] + signedTurn;
    }

    const double halfWidth =
        0.5 * static_cast<double>(radialCount - 1) * radialSpacing;
    double minimumX = std::numeric_limits<double>::infinity();
    double maximumX = -std::numeric_limits<double>::infinity();
    double minimumY = std::numeric_limits<double>::infinity();
    double maximumY = -std::numeric_limits<double>::infinity();
    for (size_t index = 0; index < centers.size(); ++index) {
        const double normalX = -std::sin(angles[index]);
        const double normalY = std::cos(angles[index]);
        for (double offset : {-halfWidth, halfWidth}) {
            minimumX = std::min(
                minimumX, centers[index][0] + offset * normalX);
            maximumX = std::max(
                maximumX, centers[index][0] + offset * normalX);
            minimumY = std::min(
                minimumY, centers[index][1] + offset * normalY);
            maximumY = std::max(
                maximumY, centers[index][1] + offset * normalY);
        }
    }
    constexpr int padding = 4;
    const int outputWidth = std::max(
        2,
        static_cast<int>(std::ceil(
            (maximumX - minimumX) / rasterSpacing))
            + 1 + 2 * padding);
    const int outputHeight = std::max(
        2,
        static_cast<int>(std::ceil(
            (maximumY - minimumY) / rasterSpacing))
            + 1 + 2 * padding);
    const size_t outputSize =
        static_cast<size_t>(outputWidth)
        * static_cast<size_t>(outputHeight);
    std::vector<double> accumulatedCt(outputSize, 0.0);
    std::vector<double> accumulatedWeight(outputSize, 0.0);
    std::vector<unsigned char> mask(outputSize, 0);
    const int centerX = dimensions[0] / 2;
    const int centerY = dimensions[1] / 2;
    for (int z = 0; z < dimensions[2]; ++z) {
        const double normalX = -std::sin(angles[static_cast<size_t>(z)]);
        const double normalY = std::cos(angles[static_cast<size_t>(z)]);
        for (int radial = 0; radial < radialCount; ++radial) {
            const double offset =
                (static_cast<double>(radial)
                 - 0.5 * static_cast<double>(radialCount - 1))
                * radialSpacing;
            const double physicalX =
                centers[static_cast<size_t>(z)][0] + offset * normalX;
            const double physicalY =
                centers[static_cast<size_t>(z)][1] + offset * normalY;
            const double rasterX =
                (physicalX - minimumX) / rasterSpacing + padding;
            const double rasterY =
                (physicalY - minimumY) / rasterSpacing + padding;
            const int x0 = static_cast<int>(std::floor(rasterX));
            const int y0 = static_cast<int>(std::floor(rasterY));
            const double fractionX = rasterX - x0;
            const double fractionY = rasterY - y0;
            const int sourceX = orientationA ? radial : centerX;
            const int sourceY = orientationA ? centerY : radial;
            const double ctValue =
                m_ctImage->GetScalarComponentAsDouble(
                    sourceX, sourceY, z, 0);
            const bool maskValue =
                m_maskImage->GetScalarComponentAsDouble(
                    sourceX, sourceY, z, 0) > 0.5;
            for (int dy = 0; dy <= 1; ++dy) {
                for (int dx = 0; dx <= 1; ++dx) {
                    const int x = x0 + dx;
                    const int y = y0 + dy;
                    if (x < 0 || y < 0
                        || x >= outputWidth || y >= outputHeight) {
                        continue;
                    }
                    const double weight =
                        (dx == 0 ? 1.0 - fractionX : fractionX)
                        * (dy == 0 ? 1.0 - fractionY : fractionY);
                    const size_t target =
                        static_cast<size_t>(y * outputWidth + x);
                    accumulatedCt[target] += weight * ctValue;
                    accumulatedWeight[target] += weight;
                    if (maskValue && weight > 0.0) {
                        mask[target] = 1;
                    }
                }
            }
        }
    }

    QImage output(
        outputWidth, outputHeight, QImage::Format_ARGB32);
    const bool overlay = m_maskOverlayCheckBox->isChecked();
    for (int y = 0; y < outputHeight; ++y) {
        for (int x = 0; x < outputWidth; ++x) {
            const size_t index =
                static_cast<size_t>(y * outputWidth + x);
            const double ctValue = accumulatedWeight[index] > 1.0e-8
                ? accumulatedCt[index] / accumulatedWeight[index]
                : -1024.0;
            bool outline = false;
            if (overlay && mask[index]) {
                for (const std::array<int, 2> &offset :
                     {std::array<int, 2>{-1, 0},
                      std::array<int, 2>{1, 0},
                      std::array<int, 2>{0, -1},
                      std::array<int, 2>{0, 1}}) {
                    const int neighborX = x + offset[0];
                    const int neighborY = y + offset[1];
                    if (neighborX < 0 || neighborY < 0
                        || neighborX >= outputWidth
                        || neighborY >= outputHeight
                        || !mask[static_cast<size_t>(
                            neighborY * outputWidth + neighborX)]) {
                        outline = true;
                        break;
                    }
                }
            }
            output.setPixel(
                x, outputHeight - y - 1,
                displayPixel(ctValue, outline));
        }
    }
    return output;
}

QImage StraightenedVesselWindow::renderStraightenedMpr(
    bool xzOrientation,
    bool thinSlabMip) const
{
    int dimensions[3] = {};
    m_ctImage->GetDimensions(dimensions);
    double spacing[3] = {};
    m_ctImage->GetSpacing(spacing);
    const int radialCount =
        xzOrientation ? dimensions[0] : dimensions[1];
    const int longitudinalCount = dimensions[2];
    const int centerX = dimensions[0] / 2;
    const int centerY = dimensions[1] / 2;
    const int slabRadius = thinSlabMip
        ? std::max(
            0,
            static_cast<int>(std::lround(
                0.5 * m_slabThicknessSpinBox->value()
                / spacing[1])))
        : 0;
    std::vector<double> ctValues(
        static_cast<size_t>(radialCount * longitudinalCount),
        -1024.0);
    std::vector<unsigned char> maskValues(
        static_cast<size_t>(radialCount * longitudinalCount), 0);
    for (int z = 0; z < longitudinalCount; ++z) {
        for (int radial = 0; radial < radialCount; ++radial) {
            double ctValue = -std::numeric_limits<double>::infinity();
            bool maskValue = false;
            const int firstSlab = thinSlabMip ? -slabRadius : 0;
            const int lastSlab = thinSlabMip ? slabRadius : 0;
            for (int slab = firstSlab; slab <= lastSlab; ++slab) {
                int sourceX = xzOrientation ? radial : centerX;
                int sourceY = xzOrientation ? centerY + slab : radial;
                if (!xzOrientation && thinSlabMip) {
                    sourceX = centerX + slab;
                }
                if (sourceX < 0 || sourceY < 0
                    || sourceX >= dimensions[0]
                    || sourceY >= dimensions[1]) {
                    continue;
                }
                ctValue = std::max(
                    ctValue,
                    m_ctImage->GetScalarComponentAsDouble(
                        sourceX, sourceY, z, 0));
                maskValue = maskValue
                    || m_maskImage->GetScalarComponentAsDouble(
                           sourceX, sourceY, z, 0) > 0.5;
            }
            const size_t target =
                static_cast<size_t>(z * radialCount + radial);
            ctValues[target] = std::isfinite(ctValue)
                ? ctValue : -1024.0;
            maskValues[target] = maskValue ? 1 : 0;
        }
    }
    QImage output(
        radialCount, longitudinalCount, QImage::Format_ARGB32);
    const bool overlay = m_maskOverlayCheckBox->isChecked();
    for (int z = 0; z < longitudinalCount; ++z) {
        for (int radial = 0; radial < radialCount; ++radial) {
            const size_t index =
                static_cast<size_t>(z * radialCount + radial);
            bool outline = false;
            if (overlay && maskValues[index]) {
                for (const std::array<int, 2> &offset :
                     {std::array<int, 2>{-1, 0},
                      std::array<int, 2>{1, 0},
                      std::array<int, 2>{0, -1},
                      std::array<int, 2>{0, 1}}) {
                    const int neighborRadial = radial + offset[0];
                    const int neighborZ = z + offset[1];
                    if (neighborRadial < 0 || neighborZ < 0
                        || neighborRadial >= radialCount
                        || neighborZ >= longitudinalCount
                        || !maskValues[static_cast<size_t>(
                            neighborZ * radialCount + neighborRadial)]) {
                        outline = true;
                        break;
                    }
                }
            }
            output.setPixel(
                radial, longitudinalCount - z - 1,
                displayPixel(ctValues[index], outline));
        }
    }
    return output;
}

QImage StraightenedVesselWindow::renderCrossSection(int slice) const
{
    int dimensions[3] = {};
    m_ctImage->GetDimensions(dimensions);
    slice = std::clamp(slice, 0, dimensions[2] - 1);
    QImage output(
        dimensions[0], dimensions[1], QImage::Format_ARGB32);
    const bool overlay = m_maskOverlayCheckBox->isChecked();
    for (int y = 0; y < dimensions[1]; ++y) {
        for (int x = 0; x < dimensions[0]; ++x) {
            const double ctValue =
                m_ctImage->GetScalarComponentAsDouble(x, y, slice, 0);
            const bool maskValue = overlay
                && m_maskImage->GetScalarComponentAsDouble(
                       x, y, slice, 0) > 0.5;
            output.setPixel(
                x, dimensions[1] - y - 1,
                displayPixel(ctValue, maskValue));
        }
    }
    QPainter painter(&output);
    painter.setPen(QPen(QColor(0, 220, 255), 1));
    const int centerX = dimensions[0] / 2;
    const int centerY = dimensions[1] / 2;
    const int displayY = dimensions[1] - centerY - 1;
    painter.drawLine(centerX - 5, displayY, centerX + 5, displayY);
    painter.drawLine(centerX, displayY - 5, centerX, displayY + 5);
    return output;
}

QRgb StraightenedVesselWindow::displayPixel(
    double ctValue,
    bool maskValue) const
{
    const double width =
        std::max(1, m_windowWidthSpinBox->value());
    const double level = m_windowLevelSpinBox->value();
    const int gray = std::clamp(
        static_cast<int>(std::lround(
            255.0 * (ctValue - (level - width / 2.0)) / width)),
        0, 255);
    if (!maskValue) {
        return qRgb(gray, gray, gray);
    }
    return qRgb(
        std::min(255, static_cast<int>(0.55 * gray + 0.45 * 255)),
        static_cast<int>(0.55 * gray),
        static_cast<int>(0.55 * gray));
}

void StraightenedVesselWindow::exportResult()
{
    const QString selectedDirectory =
        QFileDialog::getExistingDirectory(
            this,
            QStringLiteral("Export Straightened Vessel"));
    if (selectedDirectory.isEmpty()) {
        return;
    }
    const QString exportDirectory =
        QDir(selectedDirectory).absoluteFilePath(
            QStringLiteral("vessel-straightening-export-%1")
                .arg(QDateTime::currentDateTime().toString(
                    QStringLiteral("yyyyMMdd-HHmmss"))));
    if (!QDir().mkpath(exportDirectory)) {
        QMessageBox::warning(
            this, QStringLiteral("Export"),
            QStringLiteral("Could not create export directory."));
        return;
    }
    const QStringList keys = {
        QStringLiteral("straightened_ct_path"),
        QStringLiteral("straightened_vessel_mask_path"),
        QStringLiteral("selected_centerline_path"),
        QStringLiteral("selected_centerline_polydata_path"),
        QStringLiteral("metadata_path")
    };
    QStringList failures;
    for (const QString &key : keys) {
        const QString source = m_result.value(key).toString();
        if (source.isEmpty()) {
            continue;
        }
        const QString destination =
            QDir(exportDirectory).absoluteFilePath(
                QFileInfo(source).fileName());
        if (!QFile::copy(source, destination)) {
            failures.push_back(QFileInfo(source).fileName());
        }
    }
    if (!failures.isEmpty()) {
        QMessageBox::warning(
            this, QStringLiteral("Export"),
            QStringLiteral("Some files could not be exported: %1")
                .arg(failures.join(QStringLiteral(", "))));
        return;
    }
    QMessageBox::information(
        this, QStringLiteral("Export"),
        QStringLiteral("Exported to:\n%1").arg(exportDirectory));
}

void StraightenedVesselWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateImages();
}
