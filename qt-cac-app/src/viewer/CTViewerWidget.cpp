#include "viewer/CTViewerWidget.h"

#include "viewer/CaseVolumeLoader.h"
#include "viewer/Mask3DViewerWidget.h"

#include <QButtonGroup>
#include <QBrush>
#include <QCheckBox>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QDebug>
#include <QEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

struct MaskDiagnostics
{
    qsizetype nonzeroVoxelCount = 0;
    qsizetype eligibleVoxelCountHU130 = 0;
    int minHUInsideMask = std::numeric_limits<int>::max();
    int maxHUInsideMask = std::numeric_limits<int>::min();
    quint64 checksum = 1469598103934665603ULL;
};

quint64 updateChecksum(quint64 checksum, uint8_t value)
{
    checksum ^= static_cast<quint64>(value);
    checksum *= 1099511628211ULL;
    return checksum;
}

QString checksumString(quint64 checksum)
{
    return QStringLiteral("0x%1").arg(checksum, 16, 16, QLatin1Char('0'));
}

} // namespace

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
    qInfo() << "Volume artifact cached. Real loading is coordinated through loadJobFilesFromCache()."
            << "Requested volume path:" << path;
}

void CTViewerWidget::loadMaskFromLocalPath(const QString &path)
{
    qInfo() << "Mask artifact cached. Real loading is coordinated through loadJobFilesFromCache()."
            << "Requested mask path:" << path;
}

void CTViewerWidget::loadJobFilesFromCache(const QString &caseCacheDir)
{
    m_caseCacheDir = caseCacheDir;
    qInfo() << "Real case cache detected; attempting CT/mask load:" << caseCacheDir;
    CaseVolumeLoader loader;
    QString processLog;
    QString errorMessage;
    LoadedCaseVolume loaded = loader.loadCaseFromCache(caseCacheDir, &processLog, &errorMessage);
    if (!processLog.trimmed().isEmpty()) {
        qInfo().noquote() << processLog.trimmed();
    }
    for (const QString &warning : loaded.warnings) {
        qWarning().noquote() << warning;
    }
    if (!loaded.volume.isValid()) {
        qWarning() << "Could not load real CT case; synthetic viewer fallback remains active."
                   << errorMessage;
        return;
    }

    qInfo() << "Loading real CT volume into viewer"
            << loaded.volume.width << "x" << loaded.volume.height << "x" << loaded.volume.depth;
    setVolumeAndMask(loaded.volume, loaded.mask, loaded.hasMask);
}

bool CTViewerWidget::hasUnsavedEdits() const
{
    return m_hasUnsavedMaskEdits;
}

bool CTViewerWidget::eventFilter(QObject *watched, QEvent *event)
{
    const ViewportId viewportId = viewportIdForObject(watched);
    if (event->type() == QEvent::MouseButtonDblClick && viewportId != ViewportId::None) {
        finishBrushStroke();
        hideBrushCursor();
        toggleViewportMaximized(viewportId);
        event->accept();
        return true;
    }

    ViewPanel *eventPanel = panelForViewport(watched);
    if (!eventPanel) {
        return QWidget::eventFilter(watched, event);
    }

    if (m_toolMode == ToolMode::ViewPan) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::Enter: {
        eventPanel->view->viewport()->setCursor(Qt::BlankCursor);
        const QPoint viewPos = eventPanel->view->viewport()->mapFromGlobal(QCursor::pos());
        if (eventPanel->view->viewport()->rect().contains(viewPos)) {
            updateBrushCursor(eventPanel->orientation, eventPanel->view->mapToScene(viewPos));
        }
        return false;
    }
    case QEvent::MouseButtonPress: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            const QPointF scenePos = eventPanel->view->mapToScene(mouseEvent->pos());
            updateBrushCursor(eventPanel->orientation, scenePos);
            beginBrushStroke(eventPanel->orientation, scenePos);
            return true;
        }
        break;
    }
    case QEvent::MouseMove: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const QPointF scenePos = eventPanel->view->mapToScene(mouseEvent->pos());
        updateBrushCursor(eventPanel->orientation, scenePos);
        if (m_isBrushDragging && (mouseEvent->buttons() & Qt::LeftButton)) {
            continueBrushStroke(eventPanel->orientation, scenePos);
        }
        return true;
    }
    case QEvent::MouseButtonRelease: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            finishBrushStroke();
            return true;
        }
        break;
    }
    case QEvent::Leave:
        finishBrushStroke();
        hideBrushCursor();
        break;
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

CTViewerWidget::GraphicsView::GraphicsView(CTViewerWidget *owner, ViewOrientation orientation, QWidget *parent)
    : QGraphicsView(parent)
    , m_owner(owner)
    , m_orientation(orientation)
{
    setRenderHints(QPainter::SmoothPixmapTransform);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setDragMode(QGraphicsView::ScrollHandDrag);
}

void CTViewerWidget::GraphicsView::wheelEvent(QWheelEvent *event)
{
    if (m_owner) {
        m_owner->handleViewWheel(m_orientation, event);
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void CTViewerWidget::GraphicsView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_owner) {
        m_owner->handleViewResized(m_orientation);
    }
}

void CTViewerWidget::setSliceIndex(int sliceIndex)
{
    setSliceIndex(ViewOrientation::Axial, sliceIndex);
}

void CTViewerWidget::createSyntheticStudy()
{
    constexpr int width = 256;
    constexpr int height = 256;
    constexpr int depth = 96;

    m_volume.width = width;
    m_volume.height = height;
    m_volume.depth = depth;
    m_volume.direction = {1.0, 0.0, 0.0,
                          0.0, 1.0, 0.0,
                          0.0, 0.0, 1.0};
    m_volume.huVoxels.assign(static_cast<size_t>(width * height * depth), -950);

    m_aiMask.width = width;
    m_aiMask.height = height;
    m_aiMask.depth = depth;
    m_aiMask.spacing = m_volume.spacing;
    m_aiMask.origin = m_volume.origin;
    m_aiMask.direction = m_volume.direction;
    m_aiMask.voxels.assign(static_cast<size_t>(width * height * depth), 0);
    m_workingMask = {};
    m_hasMask = true;
    m_hasWorkingMask = false;
    m_usingSyntheticFallback = true;
    m_undoStack.clear();
    m_redoStack.clear();
    setMaskEditDirty(false);

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

    panel(ViewOrientation::Axial).sliceIndex = depth / 2;
    panel(ViewOrientation::Coronal).sliceIndex = height / 2;
    panel(ViewOrientation::Sagittal).sliceIndex = width / 2;
    updateAllSceneRects();
    for (ViewOrientation orientation : {ViewOrientation::Axial, ViewOrientation::Coronal, ViewOrientation::Sagittal}) {
        ViewPanel &viewPanel = panel(orientation);
        QSignalBlocker blocker(viewPanel.sliceSlider);
        viewPanel.sliceSlider->setRange(0, std::max(0, sliceCount(orientation) - 1));
        viewPanel.sliceSlider->setValue(viewPanel.sliceIndex);
    }
    updateAllFitScales();
    configure3DPositionPlanes();
    refresh3DMaskSurface();
}

void CTViewerWidget::setupUi()
{
    setupViewPanel(panel(ViewOrientation::Axial), ViewOrientation::Axial, QStringLiteral("Axial"));
    setupViewPanel(panel(ViewOrientation::Coronal), ViewOrientation::Coronal, QStringLiteral("Coronal"));
    setupViewPanel(panel(ViewOrientation::Sagittal), ViewOrientation::Sagittal, QStringLiteral("Sagittal"));

    auto *toolGroup = new QButtonGroup(this);
    toolGroup->setExclusive(true);
    m_viewPanButton = new QPushButton(QStringLiteral("Pan"), this);
    m_viewPanButton->setToolTip(QStringLiteral("View / pan mode"));
    m_brushAddButton = new QPushButton(QStringLiteral("Add"), this);
    m_brushAddButton->setToolTip(QStringLiteral("Brush Add"));
    m_brushEraseButton = new QPushButton(QStringLiteral("Erase"), this);
    m_brushEraseButton->setToolTip(QStringLiteral("Brush Erase"));
    for (QPushButton *button : {m_viewPanButton, m_brushAddButton, m_brushEraseButton}) {
        button->setCheckable(true);
        button->setMinimumWidth(58);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        toolGroup->addButton(button);
    }
    m_viewPanButton->setChecked(true);

    m_undoButton = new QPushButton(QStringLiteral("Undo"), this);
    m_redoButton = new QPushButton(QStringLiteral("Redo"), this);
    m_saveMaskButton = new QPushButton(QStringLiteral("Save"), this);
    m_saveMaskButton->setToolTip(QStringLiteral("Save corrected mask"));
    m_fitAllButton = new QPushButton(QStringLiteral("Fit"), this);
    m_fitAllButton->setToolTip(QStringLiteral("Fit all views"));
    m_refresh3DButton = new QPushButton(QStringLiteral("Refresh 3D"), this);
    m_refresh3DButton->setToolTip(QStringLiteral("Rebuild 3D mask surface from current working mask"));
    m_reset3DCameraButton = new QPushButton(QStringLiteral("Reset Camera"), this);
    m_reset3DCameraButton->setToolTip(QStringLiteral("Reset 3D camera"));
    for (QPushButton *button : {m_undoButton, m_redoButton, m_saveMaskButton, m_fitAllButton, m_refresh3DButton, m_reset3DCameraButton}) {
        button->setMinimumWidth(58);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    }

    m_brushRadiusSpinBox = new QSpinBox(this);
    m_brushRadiusSpinBox->setRange(1, 80);
    m_brushRadiusSpinBox->setValue(8);
    m_brushRadiusSpinBox->setSuffix(QStringLiteral(" mm"));
    m_brushRadiusSpinBox->setMinimumWidth(72);
    m_brushRadiusSpinBox->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    m_showAiMaskCheckBox = new QCheckBox(QStringLiteral("AI"), this);
    m_showAiMaskCheckBox->setChecked(true);
    m_showWorkingMaskCheckBox = new QCheckBox(QStringLiteral("Working"), this);
    m_showWorkingMaskCheckBox->setChecked(false);
    m_maskStatusLabel = new QLabel(QStringLiteral("AI mask is read-only. Red=AI, yellow=added/working, cyan=erased"), this);
    m_maskStatusLabel->setWordWrap(true);
    m_maskStatusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_globalZoomSlider = new QSlider(Qt::Horizontal, this);
    m_globalZoomSlider->setRange(25, 1000);
    m_globalZoomSlider->setValue(100);
    m_globalZoomSlider->setMinimumWidth(180);
    m_globalZoomLabel = new QLabel(QStringLiteral("Global 1.00x"), this);
    m_globalZoomLabel->setMinimumWidth(92);

    auto *toolLayout = new QHBoxLayout;
    toolLayout->setContentsMargins(4, 4, 4, 0);
    toolLayout->setSpacing(6);
    toolLayout->addWidget(m_viewPanButton);
    toolLayout->addWidget(m_brushAddButton);
    toolLayout->addWidget(m_brushEraseButton);
    toolLayout->addSpacing(8);
    toolLayout->addWidget(new QLabel(QStringLiteral("Radius"), this));
    toolLayout->addWidget(m_brushRadiusSpinBox);
    toolLayout->addSpacing(8);
    toolLayout->addWidget(m_showAiMaskCheckBox);
    toolLayout->addWidget(m_showWorkingMaskCheckBox);
    toolLayout->addSpacing(8);
    toolLayout->addWidget(m_undoButton);
    toolLayout->addWidget(m_redoButton);
    toolLayout->addWidget(m_saveMaskButton);
    toolLayout->addSpacing(8);
    toolLayout->addWidget(m_refresh3DButton);
    toolLayout->addWidget(m_reset3DCameraButton);
    toolLayout->addStretch(1);

    auto *zoomLayout = new QHBoxLayout;
    zoomLayout->setContentsMargins(4, 0, 4, 0);
    zoomLayout->setSpacing(6);
    zoomLayout->addWidget(new QLabel(QStringLiteral("Global Zoom"), this));
    zoomLayout->addWidget(m_globalZoomSlider, 1);
    zoomLayout->addWidget(m_globalZoomLabel);
    zoomLayout->addWidget(m_fitAllButton);

    auto *statusLayout = new QHBoxLayout;
    statusLayout->setContentsMargins(4, 0, 4, 4);
    statusLayout->addWidget(m_maskStatusLabel, 1);

    m_viewGridLayout = new QGridLayout;
    m_viewGridLayout->setContentsMargins(4, 4, 4, 4);
    m_viewGridLayout->setSpacing(6);
    m_axialPanel = createViewPanelWidget(panel(ViewOrientation::Axial));
    m_threeDPanel = create3DPanelWidget();
    m_coronalPanel = createViewPanelWidget(panel(ViewOrientation::Coronal));
    m_sagittalPanel = createViewPanelWidget(panel(ViewOrientation::Sagittal));
    m_viewGridLayout->addWidget(m_axialPanel, 0, 0);
    m_viewGridLayout->addWidget(m_threeDPanel, 0, 1);
    m_viewGridLayout->addWidget(m_coronalPanel, 1, 0);
    m_viewGridLayout->addWidget(m_sagittalPanel, 1, 1);
    m_viewGridLayout->setColumnStretch(0, 1);
    m_viewGridLayout->setColumnStretch(1, 1);
    m_viewGridLayout->setRowStretch(0, 1);
    m_viewGridLayout->setRowStretch(1, 1);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addLayout(toolLayout);
    mainLayout->addLayout(zoomLayout);
    mainLayout->addLayout(statusLayout);
    mainLayout->addLayout(m_viewGridLayout, 1);

    for (ViewOrientation orientation : {ViewOrientation::Axial, ViewOrientation::Coronal, ViewOrientation::Sagittal}) {
        ViewPanel &viewPanel = panel(orientation);
        connect(viewPanel.sliceSlider, &QSlider::valueChanged, this, [this, orientation](int value) {
            setSliceIndex(orientation, value);
        });
    }
    connect(m_viewPanButton, &QPushButton::clicked, this, [this]() {
        m_toolMode = ToolMode::ViewPan;
        updateToolState();
    });
    connect(m_brushAddButton, &QPushButton::clicked, this, [this]() {
        ensureWorkingMask();
        m_showWorkingMaskCheckBox->setChecked(true);
        m_toolMode = ToolMode::BrushAdd;
        updateToolState();
        updateSliceImages();
    });
    connect(m_brushEraseButton, &QPushButton::clicked, this, [this]() {
        ensureWorkingMask();
        m_showWorkingMaskCheckBox->setChecked(true);
        m_toolMode = ToolMode::BrushErase;
        updateToolState();
        updateSliceImages();
    });
    connect(m_showAiMaskCheckBox, &QCheckBox::toggled, this, [this]() {
        updateSliceImages();
    });
    connect(m_showWorkingMaskCheckBox, &QCheckBox::toggled, this, [this]() {
        updateSliceImages();
    });
    connect(m_undoButton, &QPushButton::clicked, this, &CTViewerWidget::undoLastEdit);
    connect(m_redoButton, &QPushButton::clicked, this, &CTViewerWidget::redoLastEdit);
    connect(m_saveMaskButton, &QPushButton::clicked, this, [this]() {
        saveCorrectedMask();
    });
    connect(m_brushRadiusSpinBox, &QSpinBox::valueChanged, this, [this](int) {
        for (ViewPanel &viewPanel : m_viewPanels) {
            if (viewPanel.brushCursorItem && viewPanel.brushCursorItem->isVisible()) {
                updateBrushCursor(viewPanel.orientation, viewPanel.brushCursorItem->rect().center());
            }
        }
    });
    connect(m_globalZoomSlider, &QSlider::valueChanged, this, [this](int value) {
        setGlobalZoomFactor(static_cast<double>(value) / 100.0);
    });
    connect(m_fitAllButton, &QPushButton::clicked, this, &CTViewerWidget::resetAllViewsToFit);
    connect(m_refresh3DButton, &QPushButton::clicked, this, &CTViewerWidget::refresh3DMaskSurface);
    connect(m_reset3DCameraButton, &QPushButton::clicked, this, [this]() {
        if (m_mask3DViewer) {
            m_mask3DViewer->resetCamera();
        }
    });

    updateToolState();
}

void CTViewerWidget::setupViewPanel(ViewPanel &viewPanel, ViewOrientation orientation, const QString &title)
{
    viewPanel.orientation = orientation;
    viewPanel.title = title;
    viewPanel.scene = new QGraphicsScene(this);
    viewPanel.view = new GraphicsView(this, orientation, this);
    viewPanel.view->setScene(viewPanel.scene);
    viewPanel.view->viewport()->installEventFilter(this);
    viewPanel.view->setMouseTracking(true);
    viewPanel.view->viewport()->setMouseTracking(true);
    viewPanel.view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    viewPanel.ctLayer = viewPanel.scene->addPixmap(QPixmap());
    viewPanel.ctLayer->setZValue(0.0);
    viewPanel.aiMaskLayer = viewPanel.scene->addPixmap(QPixmap());
    viewPanel.aiMaskLayer->setZValue(10.0);
    viewPanel.workingMaskLayer = viewPanel.scene->addPixmap(QPixmap());
    viewPanel.workingMaskLayer->setZValue(20.0);
    viewPanel.brushVoxelPreviewItem = viewPanel.scene->addPath(QPainterPath());
    viewPanel.brushVoxelPreviewItem->setZValue(29.0);
    viewPanel.brushVoxelPreviewItem->setVisible(false);
    viewPanel.brushCursorItem = viewPanel.scene->addEllipse(QRectF(), QPen(QColor(255, 220, 40), 1.5), Qt::NoBrush);
    viewPanel.brushCursorItem->setZValue(30.0);
    viewPanel.brushCursorItem->setVisible(false);

    viewPanel.sliceSlider = new QSlider(Qt::Horizontal, this);
    viewPanel.sliceLabel = new QLabel(QStringLiteral("%1 - / -").arg(title), this);
    viewPanel.sliceLabel->setMinimumWidth(112);
    viewPanel.zoomLabel = new QLabel(QStringLiteral("1.00x"), this);
    viewPanel.zoomLabel->setMinimumWidth(48);
}

QWidget *CTViewerWidget::createViewPanelWidget(ViewPanel &viewPanel)
{
    auto *container = new QFrame(this);
    container->setFrameShape(QFrame::StyledPanel);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *titleLabel = new QLabel(viewPanel.title, container);
    titleLabel->setAlignment(Qt::AlignCenter);
    container->setToolTip(QStringLiteral("Double-click to maximize/restore"));
    titleLabel->setToolTip(container->toolTip());
    viewPanel.view->setToolTip(container->toolTip());
    container->installEventFilter(this);
    titleLabel->installEventFilter(this);

    auto *sliceLayout = new QHBoxLayout;
    sliceLayout->setContentsMargins(0, 0, 0, 0);
    sliceLayout->setSpacing(6);
    sliceLayout->addWidget(viewPanel.sliceSlider, 1);
    sliceLayout->addWidget(viewPanel.sliceLabel);
    sliceLayout->addWidget(viewPanel.zoomLabel);

    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addWidget(titleLabel);
    layout->addWidget(viewPanel.view, 1);
    layout->addLayout(sliceLayout);
    return container;
}

QWidget *CTViewerWidget::create3DPanelWidget()
{
    auto *container = new QFrame(this);
    container->setFrameShape(QFrame::StyledPanel);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *titleLabel = new QLabel(QStringLiteral("3D"), container);
    titleLabel->setAlignment(Qt::AlignCenter);
    container->setToolTip(QStringLiteral("Double-click to maximize/restore"));
    titleLabel->setToolTip(container->toolTip());
    container->installEventFilter(this);
    titleLabel->installEventFilter(this);

    m_mask3DViewer = new Mask3DViewerWidget(container);
    m_mask3DViewer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_mask3DViewer->setToolTip(container->toolTip());

    auto *opacityLabel = new QLabel(QStringLiteral("Opacity"), container);
    m_3DSurfaceOpacitySlider = new QSlider(Qt::Horizontal, container);
    m_3DSurfaceOpacitySlider->setRange(0, 100);
    m_3DSurfaceOpacitySlider->setValue(90);
    m_3DSurfaceOpacitySlider->setMinimumWidth(90);
    m_3DSurfaceOpacitySlider->setToolTip(QStringLiteral("3D mask surface opacity"));
    m_3DSurfaceOpacityLabel = new QLabel(QStringLiteral("90%"), container);
    m_3DSurfaceOpacityLabel->setMinimumWidth(38);
    m_3DSurfaceOpacityLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_showAxial3DPlaneCheckBox = new QCheckBox(QStringLiteral("Axial"), container);
    m_showCoronal3DPlaneCheckBox = new QCheckBox(QStringLiteral("Coronal"), container);
    m_showSagittal3DPlaneCheckBox = new QCheckBox(QStringLiteral("Sagittal"), container);
    m_move3DPlanesCheckBox = new QCheckBox(QStringLiteral("Move Planes"), container);
    m_showAxial3DPlaneCheckBox->setChecked(true);
    m_showCoronal3DPlaneCheckBox->setChecked(false);
    m_showSagittal3DPlaneCheckBox->setChecked(false);
    m_move3DPlanesCheckBox->setChecked(false);
    m_move3DPlanesCheckBox->setToolTip(
        QStringLiteral("Drag visible slice planes to change the linked MPR slice"));

    auto *opacityLayout = new QHBoxLayout;
    opacityLayout->setContentsMargins(4, 0, 4, 0);
    opacityLayout->setSpacing(6);
    opacityLayout->addWidget(opacityLabel);
    opacityLayout->addWidget(m_3DSurfaceOpacitySlider, 1);
    opacityLayout->addWidget(m_3DSurfaceOpacityLabel);
    opacityLayout->addWidget(m_showAxial3DPlaneCheckBox);
    opacityLayout->addWidget(m_showCoronal3DPlaneCheckBox);
    opacityLayout->addWidget(m_showSagittal3DPlaneCheckBox);
    opacityLayout->addWidget(m_move3DPlanesCheckBox);

    connect(m_3DSurfaceOpacitySlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_3DSurfaceOpacityLabel) {
            m_3DSurfaceOpacityLabel->setText(QStringLiteral("%1%").arg(value));
        }
        if (m_mask3DViewer) {
            m_mask3DViewer->setSurfaceOpacity(static_cast<double>(value) / 100.0);
        }
    });
    connect(m_mask3DViewer, &Mask3DViewerWidget::viewportDoubleClicked, this, [this]() {
        toggleViewportMaximized(ViewportId::ThreeD);
    });
    connect(m_mask3DViewer, &Mask3DViewerWidget::axialPlaneSliceRequested,
            this, [this](int index) {
                setSliceIndex(ViewOrientation::Axial, index);
            });
    connect(m_mask3DViewer, &Mask3DViewerWidget::coronalPlaneSliceRequested,
            this, [this](int index) {
                setSliceIndex(ViewOrientation::Coronal, index);
            });
    connect(m_mask3DViewer, &Mask3DViewerWidget::sagittalPlaneSliceRequested,
            this, [this](int index) {
                setSliceIndex(ViewOrientation::Sagittal, index);
            });
    connect(m_showAxial3DPlaneCheckBox, &QCheckBox::toggled,
            m_mask3DViewer, &Mask3DViewerWidget::setAxialPlaneVisible);
    connect(m_showCoronal3DPlaneCheckBox, &QCheckBox::toggled,
            m_mask3DViewer, &Mask3DViewerWidget::setCoronalPlaneVisible);
    connect(m_showSagittal3DPlaneCheckBox, &QCheckBox::toggled,
            m_mask3DViewer, &Mask3DViewerWidget::setSagittalPlaneVisible);
    connect(m_move3DPlanesCheckBox, &QCheckBox::toggled,
            m_mask3DViewer, &Mask3DViewerWidget::setMovePlanesEnabled);

    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addWidget(titleLabel);
    layout->addWidget(m_mask3DViewer, 1);
    layout->addLayout(opacityLayout);
    return container;
}

CTViewerWidget::ViewPanel &CTViewerWidget::panel(ViewOrientation orientation)
{
    return m_viewPanels[static_cast<size_t>(orientation)];
}

const CTViewerWidget::ViewPanel &CTViewerWidget::panel(ViewOrientation orientation) const
{
    return m_viewPanels[static_cast<size_t>(orientation)];
}

CTViewerWidget::ViewPanel *CTViewerWidget::panelForViewport(QObject *viewport)
{
    for (ViewPanel &viewPanel : m_viewPanels) {
        if (viewPanel.view && viewPanel.view->viewport() == viewport) {
            return &viewPanel;
        }
    }
    return nullptr;
}

CTViewerWidget::ViewportId CTViewerWidget::viewportIdForObject(QObject *object) const
{
    for (QObject *current = object; current; current = current->parent()) {
        if (current == m_axialPanel) {
            return ViewportId::Axial;
        }
        if (current == m_threeDPanel) {
            return ViewportId::ThreeD;
        }
        if (current == m_coronalPanel) {
            return ViewportId::Coronal;
        }
        if (current == m_sagittalPanel) {
            return ViewportId::Sagittal;
        }
    }
    return ViewportId::None;
}

QWidget *CTViewerWidget::panelForViewportId(ViewportId id) const
{
    switch (id) {
    case ViewportId::Axial:
        return m_axialPanel;
    case ViewportId::ThreeD:
        return m_threeDPanel;
    case ViewportId::Coronal:
        return m_coronalPanel;
    case ViewportId::Sagittal:
        return m_sagittalPanel;
    case ViewportId::None:
        return nullptr;
    }
    return nullptr;
}

void CTViewerWidget::toggleViewportMaximized(ViewportId id)
{
    if (id == ViewportId::None || !m_viewGridLayout) {
        return;
    }
    if (m_maximizedViewport == id) {
        restoreViewportGrid();
    } else {
        maximizeViewport(id);
    }
}

void CTViewerWidget::maximizeViewport(ViewportId id)
{
    QWidget *targetPanel = panelForViewportId(id);
    if (!m_viewGridLayout || !targetPanel) {
        return;
    }

    const std::array<QWidget *, 4> panels = {
        m_axialPanel, m_threeDPanel, m_coronalPanel, m_sagittalPanel
    };
    for (QWidget *panelWidget : panels) {
        if (!panelWidget) {
            continue;
        }
        m_viewGridLayout->removeWidget(panelWidget);
        panelWidget->setVisible(panelWidget == targetPanel);
    }

    m_viewGridLayout->addWidget(targetPanel, 0, 0, 2, 2);
    targetPanel->show();
    m_maximizedViewport = id;
    m_viewGridLayout->invalidate();
    updateGeometry();
}

void CTViewerWidget::restoreViewportGrid()
{
    if (!m_viewGridLayout) {
        return;
    }

    const std::array<QWidget *, 4> panels = {
        m_axialPanel, m_threeDPanel, m_coronalPanel, m_sagittalPanel
    };
    for (QWidget *panelWidget : panels) {
        if (panelWidget) {
            m_viewGridLayout->removeWidget(panelWidget);
        }
    }

    m_viewGridLayout->addWidget(m_axialPanel, 0, 0);
    m_viewGridLayout->addWidget(m_threeDPanel, 0, 1);
    m_viewGridLayout->addWidget(m_coronalPanel, 1, 0);
    m_viewGridLayout->addWidget(m_sagittalPanel, 1, 1);
    for (QWidget *panelWidget : panels) {
        if (panelWidget) {
            panelWidget->show();
        }
    }

    m_viewGridLayout->setColumnStretch(0, 1);
    m_viewGridLayout->setColumnStretch(1, 1);
    m_viewGridLayout->setRowStretch(0, 1);
    m_viewGridLayout->setRowStretch(1, 1);
    m_maximizedViewport = ViewportId::None;
    m_viewGridLayout->invalidate();
    updateGeometry();
}

QSize CTViewerWidget::sliceImageSize(ViewOrientation orientation) const
{
    return mprGeometry(orientation).imageSize;
}

CTViewerWidget::MprSliceGeometry CTViewerWidget::mprGeometry(ViewOrientation orientation) const
{
    MprSliceGeometry geometry;
    if (!m_volume.isValid()) {
        return geometry;
    }

    const double spacingX = volumeSpacing(0);
    const double spacingY = volumeSpacing(1);
    const double spacingZ = volumeSpacing(2);
    switch (orientation) {
    case ViewOrientation::Axial:
        geometry.sceneSizeMm = QSizeF(m_volume.width * spacingX,
                                      m_volume.height * spacingY);
        break;
    case ViewOrientation::Coronal:
        geometry.sceneSizeMm = QSizeF(m_volume.width * spacingX,
                                      m_volume.depth * spacingZ);
        break;
    case ViewOrientation::Sagittal:
        geometry.sceneSizeMm = QSizeF(m_volume.height * spacingY,
                                      m_volume.depth * spacingZ);
        break;
    }

    geometry.displaySpacingMm = displaySpacingMm(orientation);
    const int imageWidth = std::max(1, static_cast<int>(std::lround(geometry.sceneSizeMm.width() / geometry.displaySpacingMm)));
    const int imageHeight = std::max(1, static_cast<int>(std::lround(geometry.sceneSizeMm.height() / geometry.displaySpacingMm)));
    geometry.imageSize = QSize(imageWidth, imageHeight);
    geometry.itemScaleMmPerPixel = QSizeF(geometry.sceneSizeMm.width() / imageWidth,
                                          geometry.sceneSizeMm.height() / imageHeight);
    return geometry;
}

QSizeF CTViewerWidget::sliceSceneSize(ViewOrientation orientation) const
{
    return mprGeometry(orientation).sceneSizeMm;
}

QSizeF CTViewerWidget::sliceItemScale(ViewOrientation orientation) const
{
    return mprGeometry(orientation).itemScaleMmPerPixel;
}

int CTViewerWidget::sliceCount(ViewOrientation orientation) const
{
    if (!m_volume.isValid()) {
        return 0;
    }

    switch (orientation) {
    case ViewOrientation::Axial:
        return m_volume.depth;
    case ViewOrientation::Coronal:
        return m_volume.height;
    case ViewOrientation::Sagittal:
        return m_volume.width;
    }
    return 0;
}

QString CTViewerWidget::orientationName(ViewOrientation orientation) const
{
    switch (orientation) {
    case ViewOrientation::Axial:
        return QStringLiteral("Axial z");
    case ViewOrientation::Coronal:
        return QStringLiteral("Coronal y");
    case ViewOrientation::Sagittal:
        return QStringLiteral("Sagittal x");
    }
    return QStringLiteral("View");
}

double CTViewerWidget::volumeSpacing(int axis, double fallback) const
{
    if (axis >= 0 && axis < static_cast<int>(m_volume.spacing.size())
        && std::isfinite(m_volume.spacing[static_cast<size_t>(axis)])
        && m_volume.spacing[static_cast<size_t>(axis)] > 0.0) {
        return m_volume.spacing[static_cast<size_t>(axis)];
    }
    return fallback;
}

double CTViewerWidget::displaySpacingMm(ViewOrientation orientation) const
{
    const double spacingX = volumeSpacing(0);
    const double spacingY = volumeSpacing(1);
    const double spacingZ = volumeSpacing(2);
    switch (orientation) {
    case ViewOrientation::Axial:
        return std::min(spacingX, spacingY);
    case ViewOrientation::Coronal:
        return std::min(spacingX, spacingZ);
    case ViewOrientation::Sagittal:
        return std::min(spacingY, spacingZ);
    }
    return 1.0;
}

void CTViewerWidget::setSliceIndex(ViewOrientation orientation, int sliceIndex)
{
    if (!m_volume.isValid()) {
        return;
    }

    ViewPanel &viewPanel = panel(orientation);
    const int maxSlice = std::max(0, sliceCount(orientation) - 1);
    const int clampedSlice = std::clamp(sliceIndex, 0, maxSlice);
    if (viewPanel.sliceIndex == clampedSlice) {
        updateSliceLabel(orientation);
        return;
    }

    viewPanel.sliceIndex = clampedSlice;
    hideBrushCursor();
    if (viewPanel.sliceSlider && viewPanel.sliceSlider->value() != clampedSlice) {
        QSignalBlocker blocker(viewPanel.sliceSlider);
        viewPanel.sliceSlider->setValue(clampedSlice);
    }
    updateSliceImages(orientation);
    updateSliceLabel(orientation);
    update3DPlaneSlice(orientation, clampedSlice);
}

void CTViewerWidget::updateAllSceneRects()
{
    if (!m_volume.isValid()) {
        return;
    }

    for (ViewOrientation orientation : {ViewOrientation::Axial, ViewOrientation::Coronal, ViewOrientation::Sagittal}) {
        ViewPanel &viewPanel = panel(orientation);
        const MprSliceGeometry geometry = mprGeometry(orientation);
        if (viewPanel.scene && geometry.imageSize.isValid() && geometry.sceneSizeMm.isValid()) {
            applyLayerScale(viewPanel);
            viewPanel.scene->setSceneRect(0, 0, geometry.sceneSizeMm.width(), geometry.sceneSizeMm.height());
            qInfo() << orientationName(orientation)
                    << "volume=" << QSize(m_volume.width, m_volume.height) << "depth=" << m_volume.depth
                    << "spacing=(" << volumeSpacing(0) << "," << volumeSpacing(1) << "," << volumeSpacing(2) << ")"
                    << "physical scene size=(" << geometry.sceneSizeMm.width() << "," << geometry.sceneSizeMm.height() << ")"
                    << "output image=" << geometry.imageSize
                    << "displaySpacingMm=" << geometry.displaySpacingMm
                    << "itemScaleMmPerPixel=(" << geometry.itemScaleMmPerPixel.width() << "," << geometry.itemScaleMmPerPixel.height() << ")"
                    << "sceneRect=" << viewPanel.scene->sceneRect();
        }
    }
}

void CTViewerWidget::applyLayerScale(ViewPanel &viewPanel)
{
    const QSizeF itemScale = sliceItemScale(viewPanel.orientation);
    for (QGraphicsPixmapItem *layer : {viewPanel.ctLayer, viewPanel.aiMaskLayer, viewPanel.workingMaskLayer}) {
        if (!layer) {
            continue;
        }
        layer->setTransform(QTransform::fromScale(itemScale.width(), itemScale.height()));
    }
    if (viewPanel.brushCursorItem) {
        viewPanel.brushCursorItem->setTransform(QTransform());
    }
    if (viewPanel.brushVoxelPreviewItem) {
        viewPanel.brushVoxelPreviewItem->setTransform(QTransform());
    }
}

void CTViewerWidget::updateFitScale(ViewOrientation orientation, const QPointF &preserveCenter)
{
    ViewPanel &viewPanel = panel(orientation);
    if (!viewPanel.view || !m_volume.isValid()) {
        return;
    }

    const QSizeF sceneSize = sliceSceneSize(orientation);
    const QSize viewportSize = viewPanel.view->viewport()->size();
    if (!sceneSize.isValid() || viewportSize.width() <= 0 || viewportSize.height() <= 0) {
        return;
    }

    const double xScale = static_cast<double>(viewportSize.width()) / sceneSize.width();
    const double yScale = static_cast<double>(viewportSize.height()) / sceneSize.height();
    viewPanel.fitScale = std::max(0.0001, std::min(xScale, yScale));
    applyViewTransform(orientation, preserveCenter);
    qInfo() << orientationName(orientation)
            << "output image size" << sliceImageSize(orientation)
            << "physical scene size" << sceneSize
            << "displaySpacingMm" << displaySpacingMm(orientation)
            << "fitScale" << viewPanel.fitScale
            << "userZoomFactor" << viewPanel.userZoomFactor;
}

void CTViewerWidget::updateAllFitScales()
{
    for (ViewOrientation orientation : {ViewOrientation::Axial, ViewOrientation::Coronal, ViewOrientation::Sagittal}) {
        updateFitScale(orientation);
    }
}

void CTViewerWidget::applyViewTransform(ViewOrientation orientation, const QPointF &preserveCenter)
{
    ViewPanel &viewPanel = panel(orientation);
    if (!viewPanel.view) {
        return;
    }

    QPointF center = preserveCenter;
    if (center.isNull()) {
        center = viewPanel.scene ? viewPanel.scene->sceneRect().center() : QPointF();
    }

    const double scale = viewPanel.fitScale * viewPanel.userZoomFactor;
    QTransform transform;
    transform.scale(scale, scale);
    viewPanel.view->setTransform(transform);
    viewPanel.view->centerOn(center);
    updateZoomLabel(orientation);
}

void CTViewerWidget::setUserZoomFactor(ViewOrientation orientation, double factor, bool preserveCenter)
{
    ViewPanel &viewPanel = panel(orientation);
    const QPointF center = preserveCenter && viewPanel.view
        ? viewPanel.view->mapToScene(viewPanel.view->viewport()->rect().center())
        : QPointF();
    viewPanel.userZoomFactor = std::clamp(factor, 0.25, 10.0);
    applyViewTransform(orientation, center);
}

void CTViewerWidget::setGlobalZoomFactor(double factor)
{
    const double clampedFactor = std::clamp(factor, 0.25, 10.0);
    qInfo() << "Applying absolute global CT zoom factor" << clampedFactor;
    for (ViewOrientation orientation : {ViewOrientation::Axial, ViewOrientation::Coronal, ViewOrientation::Sagittal}) {
        setUserZoomFactor(orientation, clampedFactor, true);
    }
    updateGlobalZoomLabel();
}

void CTViewerWidget::resetAllViewsToFit()
{
    if (m_globalZoomSlider) {
        QSignalBlocker blocker(m_globalZoomSlider);
        m_globalZoomSlider->setValue(100);
    }
    for (ViewOrientation orientation : {ViewOrientation::Axial, ViewOrientation::Coronal, ViewOrientation::Sagittal}) {
        ViewPanel &viewPanel = panel(orientation);
        viewPanel.userZoomFactor = 1.0;
        updateFitScale(orientation);
    }
    updateGlobalZoomLabel();
}

void CTViewerWidget::updateZoomLabel(ViewOrientation orientation)
{
    ViewPanel &viewPanel = panel(orientation);
    if (viewPanel.zoomLabel) {
        viewPanel.zoomLabel->setText(QStringLiteral("%1x").arg(viewPanel.userZoomFactor, 0, 'f', 2));
    }
}

void CTViewerWidget::updateGlobalZoomLabel()
{
    if (!m_globalZoomLabel || !m_globalZoomSlider) {
        return;
    }
    m_globalZoomLabel->setText(QStringLiteral("Global %1x").arg(static_cast<double>(m_globalZoomSlider->value()) / 100.0, 0, 'f', 2));
}

void CTViewerWidget::configure3DPositionPlanes()
{
    if (!m_mask3DViewer || !m_volume.isValid()) {
        return;
    }
    m_mask3DViewer->setVolumeGeometry(m_volume);
    m_mask3DViewer->setAxialSlice(panel(ViewOrientation::Axial).sliceIndex);
    m_mask3DViewer->setCoronalSlice(panel(ViewOrientation::Coronal).sliceIndex);
    m_mask3DViewer->setSagittalSlice(panel(ViewOrientation::Sagittal).sliceIndex);
}

void CTViewerWidget::update3DPlaneSlice(ViewOrientation orientation, int sliceIndex)
{
    if (!m_mask3DViewer) {
        return;
    }
    switch (orientation) {
    case ViewOrientation::Axial:
        m_mask3DViewer->setAxialSlice(sliceIndex);
        break;
    case ViewOrientation::Coronal:
        m_mask3DViewer->setCoronalSlice(sliceIndex);
        break;
    case ViewOrientation::Sagittal:
        m_mask3DViewer->setSagittalSlice(sliceIndex);
        break;
    }
}

void CTViewerWidget::refresh3DMaskSurface()
{
    if (!m_mask3DViewer) {
        return;
    }
    if (m_hasWorkingMask && m_workingMask.isValid()) {
        m_mask3DViewer->refreshFromMask(m_workingMask);
        return;
    }
    if (m_hasMask && m_aiMask.isValid()) {
        m_mask3DViewer->refreshFromMask(m_aiMask);
        return;
    }
    m_mask3DViewer->clear();
}

void CTViewerWidget::refresh3DMaskIntersections(const EditOperation &operation)
{
    if (!m_mask3DViewer || !m_hasWorkingMask || !m_workingMask.isValid()) {
        return;
    }

    bool updateAxial = false;
    bool updateCoronal = false;
    bool updateSagittal = false;
    const int axialSlice = panel(ViewOrientation::Axial).sliceIndex;
    const int coronalSlice = panel(ViewOrientation::Coronal).sliceIndex;
    const int sagittalSlice = panel(ViewOrientation::Sagittal).sliceIndex;
    for (const PixelChange &change : operation.changes) {
        updateAxial = updateAxial || change.z == axialSlice;
        updateCoronal = updateCoronal || change.y == coronalSlice;
        updateSagittal = updateSagittal || change.x == sagittalSlice;
        if (updateAxial && updateCoronal && updateSagittal) {
            break;
        }
    }

    m_mask3DViewer->updateMaskIntersections(
        m_workingMask, updateAxial, updateCoronal, updateSagittal);
}

void CTViewerWidget::handleViewWheel(ViewOrientation orientation, QWheelEvent *event)
{
    if (!event) {
        return;
    }
    const double step = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    setUserZoomFactor(orientation, panel(orientation).userZoomFactor * step, true);
    event->accept();
}

void CTViewerWidget::handleViewResized(ViewOrientation orientation)
{
    ViewPanel &viewPanel = panel(orientation);
    if (!viewPanel.view || !m_volume.isValid()) {
        return;
    }
    const QPointF center = viewPanel.view->mapToScene(viewPanel.view->viewport()->rect().center());
    updateFitScale(orientation, center);
}

void CTViewerWidget::setVolumeAndMask(const VolumeData &volume, const MaskVolume &mask, bool hasMask)
{
    if (!volume.isValid()) {
        return;
    }

    m_volume = volume;
    m_aiMask = mask;
    // aiMask is original AI output and must never be modified. Edits apply only to workingMask.
    m_hasMask = hasMask && mask.hasSameDimensionsAs(volume);
    if (m_hasMask) {
        m_workingMask = m_aiMask;
        m_hasWorkingMask = true;
        qInfo() << "AI mask loaded.";
        qInfo() << "Working mask initialized from AI mask.";
    } else {
        m_workingMask = {};
        m_hasWorkingMask = false;
    }
    m_usingSyntheticFallback = false;
    m_undoStack.clear();
    m_redoStack.clear();
    setMaskEditDirty(false);
    if (m_showAiMaskCheckBox) {
        m_showAiMaskCheckBox->setChecked(m_hasMask);
    }
    if (m_showWorkingMaskCheckBox) {
        m_showWorkingMaskCheckBox->setChecked(false);
    }
    qInfo() << "AI overlay visible:" << (m_showAiMaskCheckBox && m_showAiMaskCheckBox->isChecked());
    qInfo() << "Working overlay hidden until edit mode.";
    panel(ViewOrientation::Axial).sliceIndex = std::clamp(panel(ViewOrientation::Axial).sliceIndex, 0, m_volume.depth - 1);
    panel(ViewOrientation::Coronal).sliceIndex = m_volume.height / 2;
    panel(ViewOrientation::Sagittal).sliceIndex = m_volume.width / 2;
    updateAllSceneRects();
    for (ViewOrientation orientation : {ViewOrientation::Axial, ViewOrientation::Coronal, ViewOrientation::Sagittal}) {
        ViewPanel &viewPanel = panel(orientation);
        const int count = sliceCount(orientation);
        viewPanel.sliceIndex = std::clamp(viewPanel.sliceIndex, 0, std::max(0, count - 1));
        QSignalBlocker blocker(viewPanel.sliceSlider);
        viewPanel.sliceSlider->setRange(0, std::max(0, count - 1));
        viewPanel.sliceSlider->setValue(viewPanel.sliceIndex);
        qInfo() << orientationName(orientation)
                << "slice index" << viewPanel.sliceIndex
                << "slice count" << count
                << "image size" << sliceImageSize(orientation);
    }
    updateSliceImages();
    updateSliceLabel();
    updateAllFitScales();
    configure3DPositionPlanes();
    refresh3DMaskSurface();

    if (!m_hasMask && hasMask) {
        qWarning() << "Loaded mask is not geometry-compatible with CT; overlay disabled.";
    }
    updateToolState();
    updateMaskStatusLabel();
}

void CTViewerWidget::updateSliceImages()
{
    for (ViewOrientation orientation : {ViewOrientation::Axial, ViewOrientation::Coronal, ViewOrientation::Sagittal}) {
        updateSliceImages(orientation);
    }
}

void CTViewerWidget::updateSliceImages(ViewOrientation orientation)
{
    if (!m_volume.isValid()) {
        return;
    }

    ViewPanel &viewPanel = panel(orientation);
    if (!viewPanel.scene) {
        return;
    }

    viewPanel.ctLayer->setPixmap(QPixmap::fromImage(renderCtSlice(orientation, viewPanel.sliceIndex)));
    updateMaskLayers(orientation);
    updateToolState();
}

void CTViewerWidget::updateMaskLayers(ViewOrientation orientation)
{
    if (!m_volume.isValid()) {
        return;
    }

    ViewPanel &viewPanel = panel(orientation);
    if (!viewPanel.scene) {
        return;
    }

    const bool aiRequested = m_showAiMaskCheckBox && m_showAiMaskCheckBox->isChecked();
    const bool workingRequested = m_showWorkingMaskCheckBox && m_showWorkingMaskCheckBox->isChecked();
    const bool showAiMask = aiRequested
        && m_hasMask && m_aiMask.hasSameDimensionsAs(m_volume);
    const bool showWorkingMask = workingRequested
        && m_hasWorkingMask && m_workingMask.hasSameDimensionsAs(m_volume);

    if (showAiMask && showWorkingMask && m_hasMask && m_hasWorkingMask) {
        viewPanel.aiMaskLayer->setVisible(false);
        viewPanel.aiMaskLayer->setPixmap(QPixmap());
        viewPanel.workingMaskLayer->setVisible(true);
        viewPanel.workingMaskLayer->setPixmap(QPixmap::fromImage(renderMaskDiffOverlay(orientation, viewPanel.sliceIndex)));
    } else if (showAiMask) {
        viewPanel.aiMaskLayer->setVisible(true);
        viewPanel.aiMaskLayer->setPixmap(QPixmap::fromImage(renderMaskOverlay(orientation, viewPanel.sliceIndex, m_aiMask, QColor(255, 64, 64), m_aiMaskOpacity)));
        viewPanel.workingMaskLayer->setVisible(false);
        viewPanel.workingMaskLayer->setPixmap(QPixmap());
    } else if (showWorkingMask) {
        viewPanel.aiMaskLayer->setVisible(false);
        viewPanel.aiMaskLayer->setPixmap(QPixmap());
        viewPanel.workingMaskLayer->setVisible(true);
        viewPanel.workingMaskLayer->setPixmap(QPixmap::fromImage(renderMaskOverlay(orientation, viewPanel.sliceIndex, m_workingMask, QColor(255, 218, 40), m_workingMaskOpacity)));
    } else {
        viewPanel.aiMaskLayer->setVisible(false);
        viewPanel.aiMaskLayer->setPixmap(QPixmap());
        viewPanel.workingMaskLayer->setVisible(false);
        viewPanel.workingMaskLayer->setPixmap(QPixmap());
    }
    viewPanel.scene->update(viewPanel.scene->sceneRect());
    viewPanel.view->viewport()->update();
}

void CTViewerWidget::updateSliceLabel()
{
    for (ViewOrientation orientation : {ViewOrientation::Axial, ViewOrientation::Coronal, ViewOrientation::Sagittal}) {
        updateSliceLabel(orientation);
    }
}

void CTViewerWidget::updateSliceLabel(ViewOrientation orientation)
{
    ViewPanel &viewPanel = panel(orientation);
    if (!m_volume.isValid()) {
        viewPanel.sliceLabel->setText(QStringLiteral("%1 - / -").arg(orientationName(orientation)));
        return;
    }
    viewPanel.sliceLabel->setText(QStringLiteral("%1: %2 / %3")
                                      .arg(orientationName(orientation))
                                      .arg(viewPanel.sliceIndex + 1)
                                      .arg(sliceCount(orientation)));
}

QImage CTViewerWidget::renderCtSlice(ViewOrientation orientation, int sliceIndex) const
{
    const MprSliceGeometry geometry = mprGeometry(orientation);
    const QSize imageSize = geometry.imageSize;
    QImage image(imageSize, QImage::Format_Grayscale8);
    const double low = m_windowLevel - m_windowWidth / 2.0;
    const double high = m_windowLevel + m_windowWidth / 2.0;

    for (int v = 0; v < imageSize.height(); ++v) {
        uchar *line = image.scanLine(v);
        for (int u = 0; u < imageSize.width(); ++u) {
            const QPointF scenePoint((static_cast<double>(u) + 0.5) * geometry.itemScaleMmPerPixel.width(),
                                      (static_cast<double>(v) + 0.5) * geometry.itemScaleMmPerPixel.height());
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            if (!scenePointToVoxelContinuous(orientation, scenePoint, sliceIndex, &x, &y, &z)) {
                line[u] = 0;
                continue;
            }
            const double hu = static_cast<double>(sampleCtLinear(x, y, z));
            const double normalized = std::clamp((hu - low) / (high - low), 0.0, 1.0);
            line[u] = static_cast<uchar>(std::lround(normalized * 255.0));
        }
    }

    return image;
}

void CTViewerWidget::updateToolState()
{
    const bool editToolActive = m_toolMode != ToolMode::ViewPan;
    for (ViewPanel &viewPanel : m_viewPanels) {
        if (viewPanel.view) {
            viewPanel.view->setDragMode(editToolActive ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag);
            if (editToolActive) {
                viewPanel.view->viewport()->setCursor(Qt::BlankCursor);
            } else {
                viewPanel.view->viewport()->unsetCursor();
            }
        }
    }
    if (!editToolActive) {
        hideBrushCursor();
    }

    if (m_undoButton) {
        m_undoButton->setEnabled(!m_undoStack.empty());
    }
    if (m_redoButton) {
        m_redoButton->setEnabled(!m_redoStack.empty());
    }
    if (m_saveMaskButton) {
        m_saveMaskButton->setEnabled(m_hasWorkingMask);
    }
}

void CTViewerWidget::updateBrushCursor(ViewOrientation orientation, const QPointF &scenePos)
{
    ViewPanel &viewPanel = panel(orientation);
    if ((!viewPanel.brushCursorItem && !viewPanel.brushVoxelPreviewItem)
        || m_toolMode == ToolMode::ViewPan || !m_volume.isValid()) {
        return;
    }

    const double radiusMm = m_brushRadiusSpinBox ? static_cast<double>(m_brushRadiusSpinBox->value()) : 1.0;
    QColor outlineColor = m_toolMode == ToolMode::BrushErase
        ? QColor(40, 210, 255)
        : QColor(255, 220, 40);
    QColor fillColor = outlineColor;
    fillColor.setAlpha(60);

    QPen cellPen(outlineColor, 1.0);
    cellPen.setCosmetic(true);
    QPainterPath voxelPath;
    const std::vector<VoxelCoord> affectedVoxels = computeBrushAffectedVoxels(orientation, scenePos, radiusMm);
    for (const VoxelCoord &voxel : affectedVoxels) {
        voxelPath.addRect(voxelSceneRect(orientation, voxel.x, voxel.y, voxel.z));
    }
    if (viewPanel.brushVoxelPreviewItem) {
        viewPanel.brushVoxelPreviewItem->setPen(cellPen);
        viewPanel.brushVoxelPreviewItem->setBrush(QBrush(fillColor));
        viewPanel.brushVoxelPreviewItem->setPath(voxelPath);
        viewPanel.brushVoxelPreviewItem->setVisible(!affectedVoxels.empty());
    }

    if (viewPanel.brushCursorItem) {
        QColor guideColor = outlineColor;
        guideColor.setAlpha(170);
        QPen guidePen(guideColor, 1.0);
        guidePen.setCosmetic(true);
        viewPanel.brushCursorItem->setPen(guidePen);
        viewPanel.brushCursorItem->setBrush(Qt::NoBrush);
        viewPanel.brushCursorItem->setRect(scenePos.x() - radiusMm,
                                           scenePos.y() - radiusMm,
                                           radiusMm * 2.0,
                                           radiusMm * 2.0);
        viewPanel.brushCursorItem->setVisible(true);
    }
    for (ViewPanel &otherPanel : m_viewPanels) {
        if (otherPanel.orientation == orientation) {
            continue;
        }
        if (otherPanel.brushCursorItem) {
            otherPanel.brushCursorItem->setVisible(false);
        }
        if (otherPanel.brushVoxelPreviewItem) {
            otherPanel.brushVoxelPreviewItem->setVisible(false);
            otherPanel.brushVoxelPreviewItem->setPath(QPainterPath());
        }
    }
}

void CTViewerWidget::hideBrushCursor()
{
    for (ViewPanel &viewPanel : m_viewPanels) {
        if (viewPanel.brushCursorItem) {
            viewPanel.brushCursorItem->setVisible(false);
        }
        if (viewPanel.brushVoxelPreviewItem) {
            viewPanel.brushVoxelPreviewItem->setVisible(false);
            viewPanel.brushVoxelPreviewItem->setPath(QPainterPath());
        }
    }
}

void CTViewerWidget::ensureWorkingMask()
{
    if (m_hasWorkingMask || !m_hasMask || !m_aiMask.hasSameDimensionsAs(m_volume)) {
        return;
    }

    m_workingMask = m_aiMask;
    m_hasWorkingMask = true;
    qInfo() << "Created editable working mask from read-only ai_mask_v0.";
    updateSliceImages();
    updateMaskStatusLabel();
}

bool CTViewerWidget::scenePointToVoxel(ViewOrientation orientation, const QPointF &scenePos, int *x, int *y, int *z) const
{
    if (!m_volume.isValid()) {
        return false;
    }

    const double spacingX = volumeSpacing(0);
    const double spacingY = volumeSpacing(1);
    const double spacingZ = volumeSpacing(2);
    const int sliceIndex = panel(orientation).sliceIndex;
    switch (orientation) {
    case ViewOrientation::Axial:
        *x = std::clamp(static_cast<int>(std::floor(scenePos.x() / spacingX)), 0, m_volume.width - 1);
        *y = std::clamp(static_cast<int>(std::floor(scenePos.y() / spacingY)), 0, m_volume.height - 1);
        *z = std::clamp(sliceIndex, 0, m_volume.depth - 1);
        break;
    case ViewOrientation::Coronal:
        *x = std::clamp(static_cast<int>(std::floor(scenePos.x() / spacingX)), 0, m_volume.width - 1);
        *y = std::clamp(sliceIndex, 0, m_volume.height - 1);
        *z = std::clamp(static_cast<int>(std::floor(scenePos.y() / spacingZ)), 0, m_volume.depth - 1);
        break;
    case ViewOrientation::Sagittal:
        *x = std::clamp(sliceIndex, 0, m_volume.width - 1);
        *y = std::clamp(static_cast<int>(std::floor(scenePos.x() / spacingY)), 0, m_volume.height - 1);
        *z = std::clamp(static_cast<int>(std::floor(scenePos.y() / spacingZ)), 0, m_volume.depth - 1);
        break;
    }
    return true;
}

bool CTViewerWidget::scenePointToVoxelContinuous(ViewOrientation orientation, const QPointF &scenePos, int sliceIndex, double *x, double *y, double *z) const
{
    if (!m_volume.isValid()) {
        return false;
    }

    const double spacingX = volumeSpacing(0);
    const double spacingY = volumeSpacing(1);
    const double spacingZ = volumeSpacing(2);
    switch (orientation) {
    case ViewOrientation::Axial:
        *x = scenePos.x() / spacingX - 0.5;
        *y = scenePos.y() / spacingY - 0.5;
        *z = static_cast<double>(std::clamp(sliceIndex, 0, m_volume.depth - 1));
        break;
    case ViewOrientation::Coronal:
        *x = scenePos.x() / spacingX - 0.5;
        *y = static_cast<double>(std::clamp(sliceIndex, 0, m_volume.height - 1));
        *z = scenePos.y() / spacingZ - 0.5;
        break;
    case ViewOrientation::Sagittal:
        *x = static_cast<double>(std::clamp(sliceIndex, 0, m_volume.width - 1));
        *y = scenePos.x() / spacingY - 0.5;
        *z = scenePos.y() / spacingZ - 0.5;
        break;
    }
    return true;
}

bool CTViewerWidget::slicePointToVoxel(ViewOrientation orientation, int u, int v, int sliceIndex, int *x, int *y, int *z) const
{
    if (!m_volume.isValid()) {
        return false;
    }

    switch (orientation) {
    case ViewOrientation::Axial:
        *x = u;
        *y = v;
        *z = sliceIndex;
        break;
    case ViewOrientation::Coronal:
        *x = u;
        *y = sliceIndex;
        *z = v;
        break;
    case ViewOrientation::Sagittal:
        *x = sliceIndex;
        *y = u;
        *z = v;
        break;
    }

    return *x >= 0 && *x < m_volume.width
        && *y >= 0 && *y < m_volume.height
        && *z >= 0 && *z < m_volume.depth;
}

int16_t CTViewerWidget::sampleCtLinear(double x, double y, double z) const
{
    if (!m_volume.isValid()) {
        return 0;
    }

    x = std::clamp(x, 0.0, static_cast<double>(m_volume.width - 1));
    y = std::clamp(y, 0.0, static_cast<double>(m_volume.height - 1));
    z = std::clamp(z, 0.0, static_cast<double>(m_volume.depth - 1));

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int z0 = static_cast<int>(std::floor(z));
    const int x1 = std::min(x0 + 1, m_volume.width - 1);
    const int y1 = std::min(y0 + 1, m_volume.height - 1);
    const int z1 = std::min(z0 + 1, m_volume.depth - 1);
    const double tx = x - x0;
    const double ty = y - y0;
    const double tz = z - z0;

    const auto value = [this](int vx, int vy, int vz) {
        return static_cast<double>(m_volume.value(vx, vy, vz));
    };
    const double c00 = value(x0, y0, z0) * (1.0 - tx) + value(x1, y0, z0) * tx;
    const double c10 = value(x0, y1, z0) * (1.0 - tx) + value(x1, y1, z0) * tx;
    const double c01 = value(x0, y0, z1) * (1.0 - tx) + value(x1, y0, z1) * tx;
    const double c11 = value(x0, y1, z1) * (1.0 - tx) + value(x1, y1, z1) * tx;
    const double c0 = c00 * (1.0 - ty) + c10 * ty;
    const double c1 = c01 * (1.0 - ty) + c11 * ty;
    return static_cast<int16_t>(std::lround(c0 * (1.0 - tz) + c1 * tz));
}

uint8_t CTViewerWidget::sampleMaskNearest(const MaskVolume &mask, double x, double y, double z) const
{
    if (!mask.hasSameDimensionsAs(m_volume)) {
        return 0;
    }

    const int ix = std::clamp(static_cast<int>(std::floor(x + 0.5)), 0, mask.width - 1);
    const int iy = std::clamp(static_cast<int>(std::floor(y + 0.5)), 0, mask.height - 1);
    const int iz = std::clamp(static_cast<int>(std::floor(z + 0.5)), 0, mask.depth - 1);
    return mask.value(ix, iy, iz);
}

void CTViewerWidget::applyBrushAtScenePoint(ViewOrientation orientation, const QPointF &scenePos)
{
    beginBrushStroke(orientation, scenePos);
    finishBrushStroke();
}

void CTViewerWidget::beginBrushStroke(ViewOrientation orientation, const QPointF &scenePos)
{
    ensureWorkingMask();
    if (!m_hasWorkingMask || !m_workingMask.hasSameDimensionsAs(m_volume)) {
        return;
    }

    int centerX = 0;
    int centerY = 0;
    int centerZ = 0;
    if (!scenePointToVoxel(orientation, scenePos, &centerX, &centerY, &centerZ)) {
        return;
    }

    const double radiusMm = m_brushRadiusSpinBox ? static_cast<double>(m_brushRadiusSpinBox->value()) : 1.0;
    resetActiveBrushStroke();
    m_isBrushDragging = true;
    m_hasLastBrushPoint = true;
    m_activeBrushOrientation = orientation;
    m_lastBrushScenePoint = scenePos;
    m_activeBrushStepMm = std::max(0.5, radiusMm * 0.33);
    m_activeBrushOperation.type = m_toolMode == ToolMode::BrushErase ? EditOperationType::BrushErase : EditOperationType::BrushAdd;
    m_activeBrushOperation.orientation = orientation;
    m_activeBrushOperation.sliceIndex = panel(orientation).sliceIndex;
    m_activeBrushOperation.centerX = centerX;
    m_activeBrushOperation.centerY = centerY;
    m_activeBrushOperation.centerZ = centerZ;
    m_activeBrushOperation.radius = static_cast<int>(std::lround(radiusMm));
    m_activeBrushOperation.timestampUtc = QDateTime::currentDateTimeUtc();

    stampBrushAtScenePoint(orientation, scenePos);
    updateMaskLayers(orientation);
    updateBrushCursor(orientation, scenePos);
}

void CTViewerWidget::continueBrushStroke(ViewOrientation orientation, const QPointF &scenePos)
{
    if (!m_isBrushDragging || !m_hasLastBrushPoint) {
        beginBrushStroke(orientation, scenePos);
        return;
    }

    if (orientation != m_activeBrushOrientation) {
        finishBrushStroke();
        return;
    }

    const double radiusMm = m_brushRadiusSpinBox ? static_cast<double>(m_brushRadiusSpinBox->value()) : 1.0;
    const double stepMm = std::max(0.5, radiusMm * 0.33);
    const double distanceMm = std::hypot(scenePos.x() - m_lastBrushScenePoint.x(),
                                         scenePos.y() - m_lastBrushScenePoint.y());
    const int steps = std::max(1, static_cast<int>(std::ceil(distanceMm / stepMm)));
    for (int i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        const QPointF interpolated(m_lastBrushScenePoint.x() * (1.0 - t) + scenePos.x() * t,
                                   m_lastBrushScenePoint.y() * (1.0 - t) + scenePos.y() * t);
        stampBrushAtScenePoint(orientation, interpolated);
    }

    m_activeBrushDistanceMm += distanceMm;
    m_activeBrushStepMm = stepMm;
    m_lastBrushScenePoint = scenePos;
    updateMaskLayers(orientation);
    updateBrushCursor(orientation, scenePos);
}

void CTViewerWidget::finishBrushStroke()
{
    if (!m_isBrushDragging && !m_hasLastBrushPoint) {
        return;
    }

    m_isBrushDragging = false;
    m_hasLastBrushPoint = false;
    if (m_activeBrushOperation.changes.empty()) {
        qInfo() << "Brush stroke no-op"
                << "orientation=" << orientationName(m_activeBrushOrientation)
                << "distance=" << m_activeBrushDistanceMm
                << "radius=" << m_activeBrushOperation.radius
                << "step=" << m_activeBrushStepMm
                << "stamps=" << m_activeBrushStampCount
                << "no-op voxels=" << m_activeBrushNoOpVoxels;
        resetActiveBrushStroke();
        return;
    }

    m_undoStack.push_back(m_activeBrushOperation);
    m_redoStack.clear();
    setMaskEditDirty(true);
    updateSliceImages();
    refresh3DMaskIntersections(m_activeBrushOperation);

    qInfo() << "Brush stroke"
            << "orientation=" << orientationName(m_activeBrushOrientation)
            << "distance=" << m_activeBrushDistanceMm
            << "radius=" << m_activeBrushOperation.radius
            << "step=" << m_activeBrushStepMm
            << "stamps=" << m_activeBrushStampCount
            << "changedVoxels=" << m_activeBrushOperation.changes.size()
            << "changedHU130=" << m_activeBrushChangedVoxelsHU130
            << "changed min HU=" << (m_activeBrushHasChangedHu ? m_activeBrushMinChangedHU : 0)
            << "changed max HU=" << (m_activeBrushHasChangedHu ? m_activeBrushMaxChangedHU : 0)
            << "newly added vs AI=" << m_activeBrushNewlyAddedVsAi
            << "erased AI pixels=" << m_activeBrushErasedAiVoxels
            << "no-op voxels=" << m_activeBrushNoOpVoxels
            << "; ai_mask_v0 is read-only, changed workingMask only.";
    if (m_activeBrushChangedVoxelsHU130 == 0) {
        qInfo() << "Brush stroke changed mask, but no changed voxels are HU>=130; Agatston score may not change.";
    }
    resetActiveBrushStroke();
}

void CTViewerWidget::resetActiveBrushStroke()
{
    m_activeBrushOperation = {};
    m_activeBrushChangeIndexByOffset.clear();
    m_activeBrushDistanceMm = 0.0;
    m_activeBrushStepMm = 0.0;
    m_activeBrushStampCount = 0;
    m_activeBrushChangedVoxelsHU130 = 0;
    m_activeBrushNewlyAddedVsAi = 0;
    m_activeBrushErasedAiVoxels = 0;
    m_activeBrushNoOpVoxels = 0;
    m_activeBrushMinChangedHU = 0;
    m_activeBrushMaxChangedHU = 0;
    m_activeBrushHasChangedHu = false;
}

bool CTViewerWidget::stampBrushAtScenePoint(ViewOrientation orientation, const QPointF &scenePos)
{
    if (!m_hasWorkingMask || !m_workingMask.hasSameDimensionsAs(m_volume)) {
        return false;
    }

    const double radiusMm = m_brushRadiusSpinBox ? static_cast<double>(m_brushRadiusSpinBox->value()) : 1.0;
    const int targetValue = m_toolMode == ToolMode::BrushErase ? 0 : 1;
    const std::vector<VoxelCoord> affectedVoxels = computeBrushAffectedVoxels(orientation, scenePos, radiusMm);
    bool changedAny = false;
    ++m_activeBrushStampCount;

    for (const VoxelCoord &voxel : affectedVoxels) {
        const int x = voxel.x;
        const int y = voxel.y;
        const int z = voxel.z;
        const uint8_t previousValue = m_workingMask.value(x, y, z);
        const uint8_t newValue = static_cast<uint8_t>(targetValue);
        if (previousValue == newValue) {
            ++m_activeBrushNoOpVoxels;
            continue;
        }

        const int hu = m_volume.value(x, y, z);
        if (!m_activeBrushHasChangedHu) {
            m_activeBrushMinChangedHU = hu;
            m_activeBrushMaxChangedHU = hu;
            m_activeBrushHasChangedHu = true;
        } else {
            m_activeBrushMinChangedHU = std::min(m_activeBrushMinChangedHU, hu);
            m_activeBrushMaxChangedHU = std::max(m_activeBrushMaxChangedHU, hu);
        }
        if (hu >= 130) {
            ++m_activeBrushChangedVoxelsHU130;
        }

        if (m_hasMask && m_aiMask.hasSameDimensionsAs(m_volume)) {
            const uint8_t aiValue = m_aiMask.value(x, y, z);
            if (aiValue == 0 && newValue == 1) {
                ++m_activeBrushNewlyAddedVsAi;
            } else if (aiValue == 1 && newValue == 0) {
                ++m_activeBrushErasedAiVoxels;
            }
        }
        m_workingMask.setValue(x, y, z, newValue);
        mergeActiveBrushChange({x, y, z, previousValue, newValue});
        changedAny = true;
    }

    return changedAny;
}

std::vector<CTViewerWidget::VoxelCoord> CTViewerWidget::computeBrushAffectedVoxels(ViewOrientation orientation, const QPointF &scenePos, double radiusMm) const
{
    std::vector<VoxelCoord> voxels;
    if (!m_volume.isValid()) {
        return voxels;
    }

    int centerX = 0;
    int centerY = 0;
    int centerZ = 0;
    if (!scenePointToVoxel(orientation, scenePos, &centerX, &centerY, &centerZ)) {
        return voxels;
    }

    const double spacingX = volumeSpacing(0);
    const double spacingY = volumeSpacing(1);
    const double spacingZ = volumeSpacing(2);
    const double radiusSquaredMm = radiusMm * radiusMm;

    switch (orientation) {
    case ViewOrientation::Axial: {
        const int radiusX = static_cast<int>(std::ceil(radiusMm / spacingX));
        const int radiusY = static_cast<int>(std::ceil(radiusMm / spacingY));
        const int minX = std::max(0, centerX - radiusX);
        const int maxX = std::min(m_volume.width - 1, centerX + radiusX);
        const int minY = std::max(0, centerY - radiusY);
        const int maxY = std::min(m_volume.height - 1, centerY + radiusY);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const double dxMm = (x - centerX) * spacingX;
                const double dyMm = (y - centerY) * spacingY;
                if (dxMm * dxMm + dyMm * dyMm <= radiusSquaredMm) {
                    voxels.push_back({x, y, centerZ});
                }
            }
        }
        break;
    }
    case ViewOrientation::Coronal: {
        const int radiusX = static_cast<int>(std::ceil(radiusMm / spacingX));
        const int radiusZ = static_cast<int>(std::ceil(radiusMm / spacingZ));
        const int minX = std::max(0, centerX - radiusX);
        const int maxX = std::min(m_volume.width - 1, centerX + radiusX);
        const int minZ = std::max(0, centerZ - radiusZ);
        const int maxZ = std::min(m_volume.depth - 1, centerZ + radiusZ);
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const double dxMm = (x - centerX) * spacingX;
                const double dzMm = (z - centerZ) * spacingZ;
                if (dxMm * dxMm + dzMm * dzMm <= radiusSquaredMm) {
                    voxels.push_back({x, centerY, z});
                }
            }
        }
        break;
    }
    case ViewOrientation::Sagittal: {
        const int radiusY = static_cast<int>(std::ceil(radiusMm / spacingY));
        const int radiusZ = static_cast<int>(std::ceil(radiusMm / spacingZ));
        const int minY = std::max(0, centerY - radiusY);
        const int maxY = std::min(m_volume.height - 1, centerY + radiusY);
        const int minZ = std::max(0, centerZ - radiusZ);
        const int maxZ = std::min(m_volume.depth - 1, centerZ + radiusZ);
        for (int z = minZ; z <= maxZ; ++z) {
            for (int y = minY; y <= maxY; ++y) {
                const double dyMm = (y - centerY) * spacingY;
                const double dzMm = (z - centerZ) * spacingZ;
                if (dyMm * dyMm + dzMm * dzMm <= radiusSquaredMm) {
                    voxels.push_back({centerX, y, z});
                }
            }
        }
        break;
    }
    }

    if (voxels.empty()) {
        voxels.push_back({centerX, centerY, centerZ});
    }
    return voxels;
}

QRectF CTViewerWidget::voxelSceneRect(ViewOrientation orientation, int x, int y, int z) const
{
    const double spacingX = volumeSpacing(0);
    const double spacingY = volumeSpacing(1);
    const double spacingZ = volumeSpacing(2);

    switch (orientation) {
    case ViewOrientation::Axial:
        return QRectF(x * spacingX, y * spacingY, spacingX, spacingY);
    case ViewOrientation::Coronal:
        return QRectF(x * spacingX, z * spacingZ, spacingX, spacingZ);
    case ViewOrientation::Sagittal:
        return QRectF(y * spacingY, z * spacingZ, spacingY, spacingZ);
    }

    return QRectF();
}

void CTViewerWidget::mergeActiveBrushChange(const PixelChange &change)
{
    const size_t offset = m_workingMask.offset(change.x, change.y, change.z);
    const auto existing = m_activeBrushChangeIndexByOffset.find(offset);
    if (existing == m_activeBrushChangeIndexByOffset.end()) {
        m_activeBrushChangeIndexByOffset.emplace(offset, m_activeBrushOperation.changes.size());
        m_activeBrushOperation.changes.push_back(change);
        return;
    }

    PixelChange &activeChange = m_activeBrushOperation.changes[existing->second];
    activeChange.newValue = change.newValue;
}

void CTViewerWidget::applyEditOperation(const EditOperation &operation, bool useNewValues)
{
    if (!m_hasWorkingMask || !m_workingMask.isValid()) {
        return;
    }

    for (const PixelChange &change : operation.changes) {
        if (change.x < 0 || change.x >= m_workingMask.width
            || change.y < 0 || change.y >= m_workingMask.height
            || change.z < 0 || change.z >= m_workingMask.depth) {
            continue;
        }
        const uint8_t value = useNewValues ? change.newValue : change.previousValue;
        m_workingMask.setValue(change.x, change.y, change.z, value);
    }
    updateSliceImages();
    refresh3DMaskIntersections(operation);
}

void CTViewerWidget::undoLastEdit()
{
    if (m_undoStack.empty()) {
        return;
    }

    const EditOperation operation = m_undoStack.back();
    m_undoStack.pop_back();
    applyEditOperation(operation, false);
    m_redoStack.push_back(operation);
}

void CTViewerWidget::redoLastEdit()
{
    if (m_redoStack.empty()) {
        return;
    }

    const EditOperation operation = m_redoStack.back();
    m_redoStack.pop_back();
    applyEditOperation(operation, true);
    m_undoStack.push_back(operation);
}

int CTViewerWidget::nextCorrectedMaskVersion() const
{
    const QString outputDir = QDir(m_caseCacheDir).filePath(QStringLiteral("corrected_masks"));
    for (int version = 1; version < 100000; ++version) {
        const QString rawPath = QDir(outputDir).filePath(QStringLiteral("corrected_mask_v%1.raw").arg(version));
        const QString metadataPath = QDir(outputDir).filePath(QStringLiteral("corrected_mask_v%1_metadata.json").arg(version));
        if (!QFileInfo::exists(rawPath) && !QFileInfo::exists(metadataPath)) {
            return version;
        }
    }
    return -1;
}

QJsonObject CTViewerWidget::editOperationToJson(const EditOperation &operation) const
{
    QJsonObject object;
    object.insert(QStringLiteral("type"),
                  operation.type == EditOperationType::BrushErase
                      ? QStringLiteral("BrushErase")
                      : QStringLiteral("BrushAdd"));
    object.insert(QStringLiteral("orientation"), orientationName(operation.orientation));
    object.insert(QStringLiteral("sliceIndex"), operation.sliceIndex);
    object.insert(QStringLiteral("centerX"), operation.centerX);
    object.insert(QStringLiteral("centerY"), operation.centerY);
    object.insert(QStringLiteral("centerZ"), operation.centerZ);
    object.insert(QStringLiteral("radius"), operation.radius);
    object.insert(QStringLiteral("radiusUnit"), QStringLiteral("mm"));
    object.insert(QStringLiteral("changedVoxelCount"), static_cast<int>(operation.changes.size()));
    object.insert(QStringLiteral("timestampUtc"), operation.timestampUtc.toString(Qt::ISODate));

    QJsonArray changes;
    for (const PixelChange &change : operation.changes) {
        QJsonObject changeObject;
        changeObject.insert(QStringLiteral("x"), change.x);
        changeObject.insert(QStringLiteral("y"), change.y);
        changeObject.insert(QStringLiteral("z"), change.z);
        changeObject.insert(QStringLiteral("previousValue"), static_cast<int>(change.previousValue));
        changeObject.insert(QStringLiteral("newValue"), static_cast<int>(change.newValue));
        changes.append(changeObject);
    }
    object.insert(QStringLiteral("changes"), changes);
    return object;
}

bool CTViewerWidget::writeEditOperationsJson(int version) const
{
    const QString editOpsDir = QDir(m_caseCacheDir).filePath(QStringLiteral("edit_ops"));
    QDir().mkpath(editOpsDir);
    const QString editOpsPath = QDir(editOpsDir).filePath(QStringLiteral("edit_ops_v%1.json").arg(version));

    QJsonArray operations;
    for (const EditOperation &operation : m_undoStack) {
        operations.append(editOperationToJson(operation));
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), version);
    root.insert(QStringLiteral("operationCount"), static_cast<int>(m_undoStack.size()));
    root.insert(QStringLiteral("operations"), operations);
    root.insert(QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QFile file(editOpsPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Failed to write edit operations:" << editOpsPath << file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

void CTViewerWidget::setMaskEditDirty(bool dirty)
{
    m_hasUnsavedMaskEdits = dirty;
    updateMaskStatusLabel();
    updateToolState();
}

void CTViewerWidget::updateMaskStatusLabel()
{
    if (!m_maskStatusLabel) {
        return;
    }

    if (m_hasUnsavedMaskEdits) {
        m_maskStatusLabel->setText(QStringLiteral("Unsaved edits. Red=AI retained, yellow=added/working, cyan=erased"));
    } else if (m_hasWorkingMask) {
        m_maskStatusLabel->setText(QStringLiteral("AI mask is read-only; Working hidden until edit mode"));
    } else {
        m_maskStatusLabel->setText(QStringLiteral("AI mask is read-only"));
    }
}

bool CTViewerWidget::saveCorrectedMask()
{
    if (!m_hasWorkingMask || !m_workingMask.isValid()) {
        qWarning() << "No working mask is available to save.";
        return false;
    }
    if (m_caseCacheDir.isEmpty()) {
        qWarning() << "Cannot save corrected mask because case cache dir is unknown.";
        return false;
    }

    const QString outputDir = QDir(m_caseCacheDir).filePath(QStringLiteral("corrected_masks"));
    QDir().mkpath(outputDir);
    const int version = nextCorrectedMaskVersion();
    if (version <= 0) {
        qWarning() << "Could not find an available corrected mask version.";
        return false;
    }

    const QString rawPath = QDir(outputDir).filePath(QStringLiteral("corrected_mask_v%1.raw").arg(version));
    MaskDiagnostics diagnostics;
    if (m_workingMask.isValid()) {
        for (size_t index = 0; index < m_workingMask.voxels.size(); ++index) {
            const uint8_t value = m_workingMask.voxels[index];
            diagnostics.checksum = updateChecksum(diagnostics.checksum, value);
            if (value == 0) {
                continue;
            }

            ++diagnostics.nonzeroVoxelCount;
            if (m_volume.isValid() && m_workingMask.hasSameDimensionsAs(m_volume)) {
                const int x = static_cast<int>(index % static_cast<size_t>(m_workingMask.width));
                const int y = static_cast<int>((index / static_cast<size_t>(m_workingMask.width)) % static_cast<size_t>(m_workingMask.height));
                const int z = static_cast<int>(index / (static_cast<size_t>(m_workingMask.width) * static_cast<size_t>(m_workingMask.height)));
                const int hu = m_volume.value(x, y, z);
                diagnostics.minHUInsideMask = std::min(diagnostics.minHUInsideMask, hu);
                diagnostics.maxHUInsideMask = std::max(diagnostics.maxHUInsideMask, hu);
                if (hu >= 130) {
                    ++diagnostics.eligibleVoxelCountHU130;
                }
            }
        }
    }
    const bool hasMaskHuStats = diagnostics.nonzeroVoxelCount > 0
        && m_volume.isValid()
        && m_workingMask.hasSameDimensionsAs(m_volume);

    QFile rawFile(rawPath);
    if (QFileInfo::exists(rawPath) || !rawFile.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        qWarning() << "Failed to write corrected mask:" << rawPath << rawFile.errorString();
        return false;
    }
    rawFile.write(reinterpret_cast<const char *>(m_workingMask.voxels.data()),
                  static_cast<qint64>(m_workingMask.voxels.size()));
    rawFile.close();

    QJsonObject metadata;
    const QString caseKey = QFileInfo(m_caseCacheDir).fileName();
    metadata.insert(QStringLiteral("version"), version);
    metadata.insert(QStringLiteral("caseKey"), caseKey);
    metadata.insert(QStringLiteral("width"), m_workingMask.width);
    metadata.insert(QStringLiteral("height"), m_workingMask.height);
    metadata.insert(QStringLiteral("depth"), m_workingMask.depth);
    metadata.insert(QStringLiteral("dtype"), QStringLiteral("uint8"));
    metadata.insert(QStringLiteral("axis_order"), QStringLiteral("zyx"));
    metadata.insert(QStringLiteral("source"), QStringLiteral("workingMask"));
    metadata.insert(QStringLiteral("parent_ai_mask"), QStringLiteral("ai_mask_v0.nrrd"));
    metadata.insert(QStringLiteral("raw_path"), rawPath);
    metadata.insert(QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    metadata.insert(QStringLiteral("edit_operation_count"), static_cast<int>(m_undoStack.size()));
    metadata.insert(QStringLiteral("hasUnsavedMaskEditsBeforeSave"), m_hasUnsavedMaskEdits);
    metadata.insert(QStringLiteral("maskNonzeroVoxelCount"), QString::number(diagnostics.nonzeroVoxelCount));
    metadata.insert(QStringLiteral("eligibleVoxelCountHU130"), QString::number(diagnostics.eligibleVoxelCountHU130));
    if (hasMaskHuStats) {
        metadata.insert(QStringLiteral("maxHUInsideMask"), diagnostics.maxHUInsideMask);
        metadata.insert(QStringLiteral("minHUInsideMask"), diagnostics.minHUInsideMask);
    } else {
        metadata.insert(QStringLiteral("maxHUInsideMask"), QJsonValue());
        metadata.insert(QStringLiteral("minHUInsideMask"), QJsonValue());
    }
    metadata.insert(QStringLiteral("workingMaskChecksum"), checksumString(diagnostics.checksum));
    QJsonArray spacing;
    for (double value : m_workingMask.spacing) {
        spacing.append(value);
    }
    metadata.insert(QStringLiteral("spacing"), spacing);

    const QString metadataPath = QDir(outputDir).filePath(QStringLiteral("corrected_mask_v%1_metadata.json").arg(version));
    QFile metadataFile(metadataPath);
    if (QFileInfo::exists(metadataPath) || !metadataFile.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        qWarning() << "Failed to write corrected mask metadata:" << metadataPath << metadataFile.errorString();
        QFile::remove(rawPath);
        return false;
    }
    metadataFile.write(QJsonDocument(metadata).toJson(QJsonDocument::Indented));
    metadataFile.close();

    if (!writeEditOperationsJson(version)) {
        qWarning() << "Corrected mask was saved, but edit operation JSON could not be written.";
    }

    // TODO: Write corrected masks as NRRD via the temporary SimpleITK bridge or
    // future native ITK writer before using this for production interchange.
    setMaskEditDirty(false);
    qInfo() << "Saved corrected working mask version" << version
            << "raw file:" << rawPath
            << "metadata:" << metadataPath
            << "raw bytes:" << QFileInfo(rawPath).size()
            << "mask nonzero voxels:" << diagnostics.nonzeroVoxelCount
            << "eligible voxels HU>=130:" << diagnostics.eligibleVoxelCountHU130
            << "min HU inside mask:" << (hasMaskHuStats ? diagnostics.minHUInsideMask : 0)
            << "max HU inside mask:" << (hasMaskHuStats ? diagnostics.maxHUInsideMask : 0)
            << "workingMask checksum:" << checksumString(diagnostics.checksum)
            << "; ai_mask_v0.nrrd was not overwritten.";
    emit correctedMaskSaved(version, rawPath, metadataPath);
    refresh3DMaskSurface();
    return true;
}

QImage CTViewerWidget::renderMaskOverlay(ViewOrientation orientation, int sliceIndex, const MaskVolume &mask, const QColor &color, double opacity) const
{
    const MprSliceGeometry geometry = mprGeometry(orientation);
    const QSize imageSize = geometry.imageSize;
    if (!mask.hasSameDimensionsAs(m_volume)) {
        QImage empty(imageSize, QImage::Format_ARGB32);
        empty.fill(Qt::transparent);
        return empty;
    }

    QImage image(imageSize, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    QColor overlayColor = color;
    overlayColor.setAlphaF(std::clamp(opacity, 0.0, 1.0));
    const QRgb maskColor = overlayColor.rgba();
    for (int v = 0; v < imageSize.height(); ++v) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(v));
        for (int u = 0; u < imageSize.width(); ++u) {
            const QPointF scenePoint((static_cast<double>(u) + 0.5) * geometry.itemScaleMmPerPixel.width(),
                                      (static_cast<double>(v) + 0.5) * geometry.itemScaleMmPerPixel.height());
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            if (scenePointToVoxelContinuous(orientation, scenePoint, sliceIndex, &x, &y, &z)
                && sampleMaskNearest(mask, x, y, z) != 0) {
                line[u] = maskColor;
            }
        }
    }

    return image;
}

QImage CTViewerWidget::renderMaskDiffOverlay(ViewOrientation orientation, int sliceIndex) const
{
    const MprSliceGeometry geometry = mprGeometry(orientation);
    const QSize imageSize = geometry.imageSize;
    QImage image(imageSize, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    if (!m_hasMask || !m_hasWorkingMask
        || !m_aiMask.hasSameDimensionsAs(m_volume)
        || !m_workingMask.hasSameDimensionsAs(m_volume)) {
        return image;
    }

    QColor retainedAiColor(255, 64, 64);
    retainedAiColor.setAlphaF(m_aiMaskOpacity);
    QColor doctorAddedColor(255, 218, 40);
    doctorAddedColor.setAlphaF(0.70);
    QColor doctorErasedColor(64, 220, 255);
    doctorErasedColor.setAlphaF(0.70);

    const QRgb retainedAi = retainedAiColor.rgba();
    const QRgb doctorAdded = doctorAddedColor.rgba();
    const QRgb doctorErased = doctorErasedColor.rgba();

    for (int v = 0; v < imageSize.height(); ++v) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(v));
        for (int u = 0; u < imageSize.width(); ++u) {
            const QPointF scenePoint((static_cast<double>(u) + 0.5) * geometry.itemScaleMmPerPixel.width(),
                                      (static_cast<double>(v) + 0.5) * geometry.itemScaleMmPerPixel.height());
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            if (!scenePointToVoxelContinuous(orientation, scenePoint, sliceIndex, &x, &y, &z)) {
                continue;
            }
            const bool ai = sampleMaskNearest(m_aiMask, x, y, z) != 0;
            const bool working = sampleMaskNearest(m_workingMask, x, y, z) != 0;
            if (ai && working) {
                line[u] = retainedAi;
            } else if (!ai && working) {
                line[u] = doctorAdded;
            } else if (ai && !working) {
                line[u] = doctorErased;
            }
        }
    }

    return image;
}
