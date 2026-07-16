#pragma once

#include <QGraphicsPixmapItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QLabel>
#include <QPointF>
#include <QPushButton>
#include <QSize>
#include <QSizeF>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>
#include <QWidget>

#include <QDateTime>

#include <array>
#include <unordered_map>
#include <vector>
#include "data/MaskVolume.h"
#include "data/VolumeData.h"

class Mask3DViewerWidget;
class QGridLayout;

class CTViewerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CTViewerWidget(QWidget *parent = nullptr);
    void loadVolumeFromLocalPath(const QString &path);
    void loadMaskFromLocalPath(const QString &path);
    void loadJobFilesFromCache(const QString &caseCacheDir);
    bool hasUnsavedEdits() const;
    bool saveCorrectedMask();

signals:
    void correctedMaskSaved(int version, const QString &rawPath, const QString &metadataPath);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void setSliceIndex(int sliceIndex);

private:
    enum class ToolMode {
        ViewPan,
        BrushAdd,
        BrushErase
    };

    enum class EditOperationType {
        BrushAdd,
        BrushErase
    };

    enum class ViewOrientation {
        Axial = 0,
        Coronal = 1,
        Sagittal = 2
    };

    enum class ViewportId {
        None,
        Axial,
        ThreeD,
        Coronal,
        Sagittal
    };

    struct PixelChange {
        int x = 0;
        int y = 0;
        int z = 0;
        uint8_t previousValue = 0;
        uint8_t newValue = 0;
    };

    struct VoxelCoord {
        int x = 0;
        int y = 0;
        int z = 0;
    };

    struct EditOperation {
        EditOperationType type = EditOperationType::BrushAdd;
        ViewOrientation orientation = ViewOrientation::Axial;
        int sliceIndex = 0;
        int centerX = 0;
        int centerY = 0;
        int centerZ = 0;
        int radius = 0;
        QDateTime timestampUtc;
        std::vector<PixelChange> changes;
    };

    struct MprSliceGeometry {
        QSize imageSize;
        QSizeF sceneSizeMm;
        QSizeF itemScaleMmPerPixel;
        double displaySpacingMm = 1.0;
    };

    class GraphicsView : public QGraphicsView
    {
    public:
        GraphicsView(CTViewerWidget *owner, ViewOrientation orientation, QWidget *parent = nullptr);

    protected:
        void wheelEvent(QWheelEvent *event) override;
        void resizeEvent(QResizeEvent *event) override;

    private:
        CTViewerWidget *m_owner = nullptr;
        ViewOrientation m_orientation = ViewOrientation::Axial;
    };

    struct ViewPanel {
        ViewOrientation orientation = ViewOrientation::Axial;
        QString title;
        GraphicsView *view = nullptr;
        QGraphicsScene *scene = nullptr;
        QGraphicsPixmapItem *ctLayer = nullptr;
        QGraphicsPixmapItem *aiMaskLayer = nullptr;
        QGraphicsPixmapItem *workingMaskLayer = nullptr;
        QGraphicsPathItem *brushVoxelPreviewItem = nullptr;
        QGraphicsEllipseItem *brushCursorItem = nullptr;
        QSlider *sliceSlider = nullptr;
        QLabel *sliceLabel = nullptr;
        QLabel *zoomLabel = nullptr;
        int sliceIndex = 0;
        double fitScale = 1.0;
        double userZoomFactor = 1.0;
    };

    void createSyntheticStudy();
    void setupUi();
    void setVolumeAndMask(const VolumeData &volume, const MaskVolume &mask, bool hasMask);
    QWidget *createViewPanelWidget(ViewPanel &panel);
    QWidget *create3DPanelWidget();
    void setupViewPanel(ViewPanel &panel, ViewOrientation orientation, const QString &title);
    ViewPanel &panel(ViewOrientation orientation);
    const ViewPanel &panel(ViewOrientation orientation) const;
    ViewPanel *panelForViewport(QObject *viewport);
    ViewportId viewportIdForObject(QObject *object) const;
    QWidget *panelForViewportId(ViewportId id) const;
    void toggleViewportMaximized(ViewportId id);
    void maximizeViewport(ViewportId id);
    void restoreViewportGrid();
    QSize sliceImageSize(ViewOrientation orientation) const;
    MprSliceGeometry mprGeometry(ViewOrientation orientation) const;
    QSizeF sliceSceneSize(ViewOrientation orientation) const;
    QSizeF sliceItemScale(ViewOrientation orientation) const;
    int sliceCount(ViewOrientation orientation) const;
    QString orientationName(ViewOrientation orientation) const;
    double volumeSpacing(int axis, double fallback = 1.0) const;
    double displaySpacingMm(ViewOrientation orientation) const;
    void setSliceIndex(ViewOrientation orientation, int sliceIndex);
    void updateSliceImages();
    void updateSliceImages(ViewOrientation orientation);
    void updateMaskLayers(ViewOrientation orientation);
    void updateSliceLabel();
    void updateSliceLabel(ViewOrientation orientation);
    void updateAllSceneRects();
    void applyLayerScale(ViewPanel &panel);
    void updateFitScale(ViewOrientation orientation, const QPointF &preserveCenter = QPointF());
    void updateAllFitScales();
    void applyViewTransform(ViewOrientation orientation, const QPointF &preserveCenter = QPointF());
    void setUserZoomFactor(ViewOrientation orientation, double factor, bool preserveCenter);
    void setGlobalZoomFactor(double factor);
    void resetAllViewsToFit();
    void updateZoomLabel(ViewOrientation orientation);
    void updateGlobalZoomLabel();
    void refresh3DMaskSurface();
    void configure3DPositionPlanes();
    void update3DPlaneSlice(ViewOrientation orientation, int sliceIndex);
    void handleViewWheel(ViewOrientation orientation, QWheelEvent *event);
    void handleViewResized(ViewOrientation orientation);
    void updateToolState();
    void updateBrushCursor(ViewOrientation orientation, const QPointF &scenePos);
    void hideBrushCursor();
    void ensureWorkingMask();
    void applyBrushAtScenePoint(ViewOrientation orientation, const QPointF &scenePos);
    void beginBrushStroke(ViewOrientation orientation, const QPointF &scenePos);
    void continueBrushStroke(ViewOrientation orientation, const QPointF &scenePos);
    void finishBrushStroke();
    void resetActiveBrushStroke();
    bool stampBrushAtScenePoint(ViewOrientation orientation, const QPointF &scenePos);
    void mergeActiveBrushChange(const PixelChange &change);
    std::vector<VoxelCoord> computeBrushAffectedVoxels(ViewOrientation orientation, const QPointF &scenePos, double radiusMm) const;
    QRectF voxelSceneRect(ViewOrientation orientation, int x, int y, int z) const;
    bool scenePointToVoxel(ViewOrientation orientation, const QPointF &scenePos, int *x, int *y, int *z) const;
    bool slicePointToVoxel(ViewOrientation orientation, int u, int v, int sliceIndex, int *x, int *y, int *z) const;
    void applyEditOperation(const EditOperation &operation, bool useNewValues);
    void undoLastEdit();
    void redoLastEdit();
    int nextCorrectedMaskVersion() const;
    bool writeEditOperationsJson(int version) const;
    QJsonObject editOperationToJson(const EditOperation &operation) const;
    void setMaskEditDirty(bool dirty);
    void updateMaskStatusLabel();
    QImage renderCtSlice(ViewOrientation orientation, int sliceIndex) const;
    QImage renderMaskOverlay(ViewOrientation orientation, int sliceIndex, const MaskVolume &mask, const QColor &color, double opacity) const;
    QImage renderMaskDiffOverlay(ViewOrientation orientation, int sliceIndex) const;
    bool scenePointToVoxelContinuous(ViewOrientation orientation, const QPointF &scenePos, int sliceIndex, double *x, double *y, double *z) const;
    int16_t sampleCtLinear(double x, double y, double z) const;
    uint8_t sampleMaskNearest(const MaskVolume &mask, double x, double y, double z) const;

    std::array<ViewPanel, 3> m_viewPanels;
    QPushButton *m_viewPanButton = nullptr;
    QPushButton *m_brushAddButton = nullptr;
    QPushButton *m_brushEraseButton = nullptr;
    QPushButton *m_undoButton = nullptr;
    QPushButton *m_redoButton = nullptr;
    QPushButton *m_saveMaskButton = nullptr;
    QPushButton *m_fitAllButton = nullptr;
    QPushButton *m_refresh3DButton = nullptr;
    QPushButton *m_reset3DCameraButton = nullptr;
    QSpinBox *m_brushRadiusSpinBox = nullptr;
    QCheckBox *m_showAiMaskCheckBox = nullptr;
    QCheckBox *m_showWorkingMaskCheckBox = nullptr;
    QSlider *m_globalZoomSlider = nullptr;
    QLabel *m_globalZoomLabel = nullptr;
    QLabel *m_maskStatusLabel = nullptr;
    Mask3DViewerWidget *m_mask3DViewer = nullptr;
    QSlider *m_3DSurfaceOpacitySlider = nullptr;
    QLabel *m_3DSurfaceOpacityLabel = nullptr;
    QCheckBox *m_showAxial3DPlaneCheckBox = nullptr;
    QCheckBox *m_showCoronal3DPlaneCheckBox = nullptr;
    QCheckBox *m_showSagittal3DPlaneCheckBox = nullptr;
    QGridLayout *m_viewGridLayout = nullptr;
    QWidget *m_axialPanel = nullptr;
    QWidget *m_threeDPanel = nullptr;
    QWidget *m_coronalPanel = nullptr;
    QWidget *m_sagittalPanel = nullptr;
    ViewportId m_maximizedViewport = ViewportId::None;

    VolumeData m_volume;
    // aiMask is original AI output and must never be modified. Edits apply only to workingMask.
    MaskVolume m_aiMask;
    MaskVolume m_workingMask;
    bool m_hasMask = false;
    bool m_hasWorkingMask = false;
    bool m_hasUnsavedMaskEdits = false;
    bool m_usingSyntheticFallback = true;
    bool m_isBrushDragging = false;
    bool m_hasLastBrushPoint = false;
    ViewOrientation m_activeBrushOrientation = ViewOrientation::Axial;
    QPointF m_lastBrushScenePoint;
    EditOperation m_activeBrushOperation;
    std::unordered_map<size_t, size_t> m_activeBrushChangeIndexByOffset;
    double m_activeBrushDistanceMm = 0.0;
    double m_activeBrushStepMm = 0.0;
    int m_activeBrushStampCount = 0;
    int m_activeBrushChangedVoxelsHU130 = 0;
    int m_activeBrushNewlyAddedVsAi = 0;
    int m_activeBrushErasedAiVoxels = 0;
    int m_activeBrushNoOpVoxels = 0;
    int m_activeBrushMinChangedHU = 0;
    int m_activeBrushMaxChangedHU = 0;
    bool m_activeBrushHasChangedHu = false;
    ToolMode m_toolMode = ToolMode::ViewPan;
    std::vector<EditOperation> m_undoStack;
    std::vector<EditOperation> m_redoStack;
    QString m_caseCacheDir;
    double m_windowWidth = 700.0;
    double m_windowLevel = 150.0;
    double m_aiMaskOpacity = 0.35;
    double m_workingMaskOpacity = 0.55;
};
