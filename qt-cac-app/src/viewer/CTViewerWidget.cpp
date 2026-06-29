#include "viewer/CTViewerWidget.h"

#include <QHBoxLayout>
#include <QDebug>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <stdexcept>

CTViewerWidget::CTViewerWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    createSyntheticStudy();
    updateSliceImages();
    updateSliceLabel();
}

void CTViewerWidget::loadVolumeFromLocalPath(const QString &path)
{
    qInfo() << "Real medical image loading is not implemented yet. "
               "ITK-based DICOM/NRRD/NIfTI/MHD loading will be integrated later."
            << "Requested volume path:" << path;
}

void CTViewerWidget::loadMaskFromLocalPath(const QString &path)
{
    qInfo() << "Real medical image loading is not implemented yet. "
               "ITK-based DICOM/NRRD/NIfTI/MHD loading will be integrated later."
            << "Requested mask path:" << path;
}

void CTViewerWidget::loadJobFilesFromCache(const QString &caseCacheDir)
{
    qInfo() << "Real medical image loading is not implemented yet. "
               "ITK-based DICOM/NRRD/NIfTI/MHD loading will be integrated later."
            << "Requested case cache dir:" << caseCacheDir;
}

bool CTViewerWidget::VolumeData::isValid() const
{
    return width > 0 && height > 0 && depth > 0
        && huVoxels.size() == static_cast<size_t>(width * height * depth);
}

size_t CTViewerWidget::VolumeData::offset(int x, int y, int z) const
{
    if (x < 0 || x >= width || y < 0 || y >= height || z < 0 || z >= depth) {
        throw std::out_of_range("VolumeData index out of range");
    }
    return (static_cast<size_t>(z) * height + y) * width + x;
}

int16_t CTViewerWidget::VolumeData::value(int x, int y, int z) const
{
    return huVoxels[offset(x, y, z)];
}

bool CTViewerWidget::MaskVolume::isValid() const
{
    return width > 0 && height > 0 && depth > 0
        && voxels.size() == static_cast<size_t>(width * height * depth);
}

size_t CTViewerWidget::MaskVolume::offset(int x, int y, int z) const
{
    if (x < 0 || x >= width || y < 0 || y >= height || z < 0 || z >= depth) {
        throw std::out_of_range("MaskVolume index out of range");
    }
    return (static_cast<size_t>(z) * height + y) * width + x;
}

uint8_t CTViewerWidget::MaskVolume::value(int x, int y, int z) const
{
    return voxels[offset(x, y, z)];
}

void CTViewerWidget::MaskVolume::setValue(int x, int y, int z, uint8_t value)
{
    voxels[offset(x, y, z)] = value ? 1 : 0;
}

CTViewerWidget::GraphicsView::GraphicsView(QWidget *parent)
    : QGraphicsView(parent)
{
    setRenderHints(QPainter::SmoothPixmapTransform);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
}

void CTViewerWidget::GraphicsView::wheelEvent(QWheelEvent *event)
{
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    scale(factor, factor);
}

void CTViewerWidget::setSliceIndex(int sliceIndex)
{
    m_sliceIndex = sliceIndex;
    updateSliceImages();
    updateSliceLabel();
}

void CTViewerWidget::createSyntheticStudy()
{
    constexpr int width = 256;
    constexpr int height = 256;
    constexpr int depth = 96;

    m_volume.width = width;
    m_volume.height = height;
    m_volume.depth = depth;
    m_volume.huVoxels.assign(static_cast<size_t>(width * height * depth), -950);

    m_aiMask.width = width;
    m_aiMask.height = height;
    m_aiMask.depth = depth;
    m_aiMask.voxels.assign(static_cast<size_t>(width * height * depth), 0);

    const double cx = width / 2.0;
    const double cy = height / 2.0;
    for (int z = 0; z < depth; ++z) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const double dx = x - cx;
                const double dy = y - cy;
                const double radius = std::sqrt(dx * dx + dy * dy);

                int16_t hu = -900;
                if (radius < 92.0) {
                    hu = static_cast<int16_t>(40 + 25 * std::cos(radius * 0.08));
                }
                if (radius > 58.0 && radius < 63.0 && y < cy + 45) {
                    hu = 180;
                }

                const int plaqueCx = 142 + static_cast<int>(8 * std::sin(z * 0.2));
                const int plaqueCy = 105 + static_cast<int>(5 * std::cos(z * 0.18));
                const int pdx = x - plaqueCx;
                const int pdy = y - plaqueCy;
                if (z > 28 && z < 48 && pdx * pdx + pdy * pdy < 49) {
                    hu = 460;
                    m_aiMask.setValue(x, y, z, 1);
                }

                m_volume.huVoxels[m_volume.offset(x, y, z)] = hu;
            }
        }
    }

    m_sliceSlider->setRange(0, depth - 1);
    m_sliceSlider->setValue(depth / 2);
    m_scene->setSceneRect(0, 0, width, height);
    m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}

void CTViewerWidget::setupUi()
{
    m_scene = new QGraphicsScene(this);
    m_view = new GraphicsView(this);
    m_view->setScene(m_scene);

    m_ctLayer = m_scene->addPixmap(QPixmap());
    m_ctLayer->setZValue(0.0);
    m_maskLayer = m_scene->addPixmap(QPixmap());
    m_maskLayer->setZValue(10.0);

    m_sliceSlider = new QSlider(Qt::Horizontal, this);
    m_sliceLabel = new QLabel(QStringLiteral("- / -"), this);

    auto *sliceLayout = new QHBoxLayout;
    sliceLayout->addWidget(m_sliceSlider, 1);
    sliceLayout->addWidget(m_sliceLabel);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(m_view, 1);
    mainLayout->addLayout(sliceLayout);

    connect(m_sliceSlider, &QSlider::valueChanged,
            this, &CTViewerWidget::setSliceIndex);
}

void CTViewerWidget::updateSliceImages()
{
    if (!m_volume.isValid()) {
        return;
    }

    m_ctLayer->setPixmap(QPixmap::fromImage(renderCtSlice()));
    m_maskLayer->setPixmap(QPixmap::fromImage(renderMaskOverlay()));
}

void CTViewerWidget::updateSliceLabel()
{
    if (!m_volume.isValid()) {
        m_sliceLabel->setText(QStringLiteral("- / -"));
        return;
    }
    m_sliceLabel->setText(QStringLiteral("%1 / %2").arg(m_sliceIndex + 1).arg(m_volume.depth));
}

QImage CTViewerWidget::renderCtSlice() const
{
    QImage image(m_volume.width, m_volume.height, QImage::Format_Grayscale8);
    const double low = m_windowLevel - m_windowWidth / 2.0;
    const double high = m_windowLevel + m_windowWidth / 2.0;

    for (int y = 0; y < m_volume.height; ++y) {
        uchar *line = image.scanLine(y);
        for (int x = 0; x < m_volume.width; ++x) {
            const double hu = static_cast<double>(m_volume.value(x, y, m_sliceIndex));
            const double normalized = std::clamp((hu - low) / (high - low), 0.0, 1.0);
            line[x] = static_cast<uchar>(std::lround(normalized * 255.0));
        }
    }

    return image;
}

QImage CTViewerWidget::renderMaskOverlay() const
{
    QImage image(m_aiMask.width, m_aiMask.height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    const QRgb maskColor = QColor(255, 64, 64, 120).rgba();
    for (int y = 0; y < m_aiMask.height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < m_aiMask.width; ++x) {
            if (m_aiMask.value(x, y, m_sliceIndex) != 0) {
                line[x] = maskColor;
            }
        }
    }

    return image;
}
