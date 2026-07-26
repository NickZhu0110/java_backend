#pragma once

#include <QPointF>
#include <QWidget>

#include "data/MaskVolume.h"
#include "data/VolumeData.h"
#include "viewer/MultiStructureVolume.h"

#include <vtkActor.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>

#include <array>
#include <cstdint>
#include <vector>

class QVTKOpenGLNativeWidget;
class StrictTrackballCameraStyle;
class vtkAnnotatedCubeActor;
class vtkCellPicker;
class vtkOrientationMarkerWidget;
class vtkPlaneSource;
class vtkPoints;
class vtkPolyData;
class vtkPolyDataMapper;
class vtkMatrix4x4;

class Mask3DViewerWidget : public QWidget
{
    Q_OBJECT

public:
    struct MultiStructureSurfaceInfo
    {
        int labelValue = 0;
        QString displayName;
        std::uint64_t voxelCount = 0;
        vtkIdType pointCount = 0;
        vtkIdType cellCount = 0;
        qint64 extractionMilliseconds = 0;
        std::array<double, 3> color = {1.0, 1.0, 1.0};
        double opacity = 0.0;
        bool visible = false;
    };

    explicit Mask3DViewerWidget(QWidget *parent = nullptr);

    bool eventFilter(QObject *watched, QEvent *event) override;

    void clear();
    void resetCamera();
    void setVolumeGeometry(const VolumeData &volume);
    void setAxialSlice(int index);
    void setCoronalSlice(int index);
    void setSagittalSlice(int index);
    void setAxialPlaneVisible(bool visible);
    void setCoronalPlaneVisible(bool visible);
    void setSagittalPlaneVisible(bool visible);
    void setMovePlanesEnabled(bool enabled);
    void updateMaskIntersections(const MaskVolume &mask,
                                 bool updateAxial,
                                 bool updateCoronal,
                                 bool updateSagittal);
    void setMaskVolume(const MaskVolume &mask);
    void setSurfaceOpacity(double opacity);
    double surfaceOpacity() const;
    void refreshFromMask(const MaskVolume &mask);
    bool loadMultiStructurePreview(const QString &ctPath,
                                   const QString &segmentationPath,
                                   QString *errorMessage = nullptr);
    bool setMultiStructurePreview(const MultiStructureVolume &volume,
                                  QString *errorMessage = nullptr);
    void clearMultiStructurePreview();
    bool isMultiStructurePreviewActive() const;
    std::vector<MultiStructureSurfaceInfo> multiStructureSurfaces() const;
    void setMultiStructureVisible(int labelValue, bool visible);
    void setMultiStructureOpacity(int labelValue, double opacity);
    const vtkImageData *activeNiftiCtImage() const;
    const NiftiVolumeGeometry *activeNiftiCtGeometry() const;
    const vtkImageData *activeNiftiLabel2000Mask() const;
    const vtkPolyData *activeNiftiLabel2000Surface() const;
    const vtkMatrix4x4 *activeNiftiSegmentationToCtIndexTransform() const;
    const vtkMatrix4x4 *activeNiftiSegmentationToCtPhysicalTransform() const;

signals:
    void viewportDoubleClicked();
    void axialPlaneSliceRequested(int index);
    void coronalPlaneSliceRequested(int index);
    void sagittalPlaneSliceRequested(int index);

private:
    struct MultiStructureSurface
    {
        MultiStructureLabelInfo label;
        vtkSmartPointer<vtkPolyData> polyData;
        vtkSmartPointer<vtkPolyDataMapper> mapper;
        vtkSmartPointer<vtkActor> actor;
        std::array<vtkSmartPointer<vtkPolyData>, 3> cardIntersectionData;
        std::array<vtkSmartPointer<vtkPolyDataMapper>, 3> cardIntersectionMappers;
        std::array<vtkSmartPointer<vtkActor>, 3> cardIntersectionActors;
        qint64 extractionMilliseconds = 0;
    };

    enum class DraggedPlane {
        None,
        Sagittal,
        Coronal,
        Axial
    };

    bool anyMouseButtonDown() const;
    void forceEndInteraction();
    bool beginPlaneDrag(const QPointF &widgetPosition);
    void updatePlaneDrag(const QPointF &widgetPosition);
    void endPlaneDrag();
    void updatePlanePickerList();
    void setPlaneDragHighlight(int axis, bool highlighted);
    int draggedPlaneAxis() const;
    QPointF widgetToVtkDisplay(const QPointF &widgetPosition) const;
    bool worldToDisplay(const std::array<double, 3> &world, QPointF *display) const;
    std::array<double, 3> worldToContinuousIndex(const std::array<double, 3> &world) const;
    void setupOrientationMarker();
    void updateOrientationLabels(const MaskVolume &mask);
    void updateOrientationLabelsForRasWorld();
    void setOrientationLabels(const char *const plusLabels[3], const char *const minusLabels[3]);
    void updateModelBoundsGuide(const double surfaceBounds[6]);
    void clearVolumeBoundsGuide();
    void removeMultiStructurePreview(bool restoreNormalMask);
    bool visibleMultiStructureBounds(double bounds[6]) const;
    void updateMultiStructureBoundsGuide();
    void clearMultiStructureBoundsGuide();
    void clearVolumeGeometry();
    void createPositionPlaneActors();
    bool setNiftiReviewVolumeGeometry(const NiftiVolumeGeometry &geometry,
                                      QString *errorMessage);
    void setPositionPlaneSlice(int axis, int index);
    void setPositionPlaneVisible(int axis, bool visible);
    void updatePositionPlaneGeometry(int axis);
    void updateAllPositionPlaneGeometry();
    void updatePositionPlaneMaskIntersection(int axis);
    void updateAllPositionPlaneMaskIntersections();
    void clearPositionPlaneMaskIntersections();
    void updatePositionPlaneCategoricalIntersections(int axis);
    void updateAllPositionPlaneCategoricalIntersections();
    int categoricalValueAtCtIndex(const std::array<double, 3> &ctIndex) const;
    void updatePositionPlaneVisibility();
    bool volumeGeometryMatchesMask(const MaskVolume &mask) const;
    std::array<double, 3> indexToWorld(const std::array<double, 3> &index) const;

    QVTKOpenGLNativeWidget *m_vtkWidget = nullptr;
    vtkNew<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkNew<vtkRenderer> m_renderer;
    vtkSmartPointer<StrictTrackballCameraStyle> m_interactorStyle;
    vtkSmartPointer<vtkCellPicker> m_planePicker;
    vtkSmartPointer<vtkActor> m_maskActor;
    vtkSmartPointer<vtkAnnotatedCubeActor> m_orientationCube;
    vtkSmartPointer<vtkOrientationMarkerWidget> m_orientationMarker;
    vtkSmartPointer<vtkActor> m_boundsActor;
    vtkSmartPointer<vtkActor> m_multiStructureBoundsActor;
    MultiStructureVolume m_multiStructureVolume;
    std::vector<MultiStructureSurface> m_multiStructureSurfaces;
    std::array<vtkSmartPointer<vtkPlaneSource>, 3> m_positionPlaneSources;
    std::array<vtkSmartPointer<vtkPolyDataMapper>, 3> m_positionPlaneFillMappers;
    std::array<vtkSmartPointer<vtkActor>, 3> m_positionPlaneFillActors;
    std::array<vtkSmartPointer<vtkPolyData>, 3> m_positionPlaneIntersectionData;
    std::array<vtkSmartPointer<vtkPolyDataMapper>, 3> m_positionPlaneIntersectionMappers;
    std::array<vtkSmartPointer<vtkActor>, 3> m_positionPlaneIntersectionActors;
    std::array<vtkSmartPointer<vtkPoints>, 3> m_positionPlaneBorderPoints;
    std::array<vtkSmartPointer<vtkPolyData>, 3> m_positionPlaneBorderData;
    std::array<vtkSmartPointer<vtkPolyDataMapper>, 3> m_positionPlaneBorderMappers;
    std::array<vtkSmartPointer<vtkActor>, 3> m_positionPlaneBorderActors;
    std::array<int, 3> m_volumeDimensions = {0, 0, 0};
    std::array<double, 3> m_volumeSpacing = {1.0, 1.0, 1.0};
    std::array<double, 3> m_volumeOrigin = {0.0, 0.0, 0.0};
    std::array<double, 9> m_volumeDirection = {1.0, 0.0, 0.0,
                                               0.0, 1.0, 0.0,
                                               0.0, 0.0, 1.0};
    std::array<int, 3> m_positionPlaneSlices = {0, 0, 0};
    std::array<bool, 3> m_positionPlaneVisibilityRequested = {false, false, true};
    MaskVolume m_intersectionMask;
    double m_surfaceFrameBounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    bool m_hasSurfaceFrameBounds = false;
    bool m_hasRenderedMask = false;
    bool m_hasVolumeGeometry = false;
    bool m_volumeMaskGeometryAligned = false;
    bool m_hasIntersectionMask = false;
    bool m_multiStructurePreviewActive = false;
    bool m_niftiReviewGeometryActive = false;
    double m_surfaceOpacity = 0.9;
    bool m_leftButtonDown = false;
    bool m_middleButtonDown = false;
    bool m_rightButtonDown = false;
    bool m_movePlanesEnabled = false;
    bool m_planeDragActive = false;
    bool m_planeDragProjectionValid = false;
    DraggedPlane m_draggedPlane = DraggedPlane::None;
    QPointF m_planeDragStartDisplayPosition;
    QPointF m_planeDragDisplayPerSlice;
    std::array<double, 3> m_planeDragStartWorldPosition = {0.0, 0.0, 0.0};
    std::array<double, 3> m_planeDragNormal = {0.0, 0.0, 1.0};
    int m_lastRequestedPlaneSlice = -1;
};
