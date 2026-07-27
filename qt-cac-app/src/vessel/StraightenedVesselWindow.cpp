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
#include <QLabel>
#include <QMessageBox>
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
    m_longitudinalModeComboBox->addItem(QStringLiteral("Center slice"));
    m_longitudinalModeComboBox->addItem(QStringLiteral("MIP"));
    controlRow->addWidget(m_longitudinalModeComboBox);
    controlRow->addSpacing(16);
    controlRow->addWidget(new QLabel(QStringLiteral("Window:"), this));
    m_windowWidthSpinBox = new QSpinBox(this);
    m_windowWidthSpinBox->setRange(1, 4000);
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
    auto *longitudinalTitle =
        new QLabel(QStringLiteral("Longitudinal Curved MPR"), this);
    longitudinalTitle->setAlignment(Qt::AlignCenter);
    auto *crossSectionTitle =
        new QLabel(QStringLiteral("Cross-section (X-Y)"), this);
    crossSectionTitle->setAlignment(Qt::AlignCenter);
    imagesLayout->addWidget(longitudinalTitle, 0, 0);
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
    m_ctImage = ct;
    m_maskImage = mask;
    m_result = result;
    m_candidatePaths = candidatePaths;
    m_positionSlider->setRange(0, std::max(0, ctDimensions[2] - 1));
    m_positionSlider->setValue(ctDimensions[2] / 2);
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

void StraightenedVesselWindow::updateImages()
{
    if (!imageIsValid(m_ctImage) || !imageIsValid(m_maskImage)) {
        return;
    }
    const QImage longitudinal = renderLongitudinal();
    const QImage crossSection =
        renderCrossSection(m_positionSlider->value());
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
    int dimensions[3] = {};
    m_ctImage->GetDimensions(dimensions);
    QImage output(
        dimensions[0], dimensions[2], QImage::Format_ARGB32);
    const int centerY = dimensions[1] / 2;
    const bool useMip = m_longitudinalModeComboBox->currentIndex() == 1;
    const bool overlay = m_maskOverlayCheckBox->isChecked();
    for (int z = 0; z < dimensions[2]; ++z) {
        for (int x = 0; x < dimensions[0]; ++x) {
            double ctValue =
                m_ctImage->GetScalarComponentAsDouble(x, centerY, z, 0);
            bool maskValue =
                m_maskImage->GetScalarComponentAsDouble(
                    x, centerY, z, 0) > 0.5;
            if (useMip) {
                for (int y = 0; y < dimensions[1]; ++y) {
                    ctValue = std::max(
                        ctValue,
                        m_ctImage->GetScalarComponentAsDouble(x, y, z, 0));
                    maskValue = maskValue
                        || m_maskImage->GetScalarComponentAsDouble(
                               x, y, z, 0) > 0.5;
                }
            }
            output.setPixel(
                x, dimensions[2] - z - 1,
                displayPixel(ctValue, overlay && maskValue));
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
