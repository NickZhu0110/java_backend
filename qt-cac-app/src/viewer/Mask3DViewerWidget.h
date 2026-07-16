#pragma once

#include <QWidget>

#include "data/MaskVolume.h"
#include "data/VolumeData.h"

#include <vtkActor.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>

#include <array>

class QVTKOpenGLNativeWidget;
class StrictTrackballCameraStyle;
class vtkAnnotatedCubeActor;
class vtkOrientationMarkerWidget;
class vtkPlaneSource;
class vtkPoints;
class vtkPolyData;
class vtkPolyDataMapper;

class Mask3DViewerWidget : public QWidget
{
    Q_OBJECT

public:
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
    void updateMaskIntersections(const MaskVolume &mask,
                                 bool updateAxial,
                                 bool updateCoronal,
                                 bool updateSagittal);
    void setMaskVolume(const MaskVolume &mask);
    void setSurfaceOpacity(double opacity);
    double surfaceOpacity() const;
    void refreshFromMask(const MaskVolume &mask);

signals:
    void viewportDoubleClicked();

private:
    bool anyMouseButtonDown() const;
    void forceEndInteraction();
    void setupOrientationMarker();
    void updateOrientationLabels(const MaskVolume &mask);
    void setOrientationLabels(const char *const plusLabels[3], const char *const minusLabels[3]);
    void updateModelBoundsGuide(const double surfaceBounds[6]);
    void clearVolumeBoundsGuide();
    void clearVolumeGeometry();
    void setPositionPlaneSlice(int axis, int index);
    void setPositionPlaneVisible(int axis, bool visible);
    void updatePositionPlaneGeometry(int axis);
    void updateAllPositionPlaneGeometry();
    void updatePositionPlaneMaskIntersection(int axis);
    void updateAllPositionPlaneMaskIntersections();
    void clearPositionPlaneMaskIntersections();
    void updatePositionPlaneVisibility();
    bool volumeGeometryMatchesMask(const MaskVolume &mask) const;
    std::array<double, 3> indexToWorld(const std::array<double, 3> &index) const;

    QVTKOpenGLNativeWidget *m_vtkWidget = nullptr;
    vtkNew<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkNew<vtkRenderer> m_renderer;
    vtkSmartPointer<StrictTrackballCameraStyle> m_interactorStyle;
    vtkSmartPointer<vtkActor> m_maskActor;
    vtkSmartPointer<vtkAnnotatedCubeActor> m_orientationCube;
    vtkSmartPointer<vtkOrientationMarkerWidget> m_orientationMarker;
    vtkSmartPointer<vtkActor> m_boundsActor;
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
    double m_surfaceOpacity = 0.9;
    bool m_leftButtonDown = false;
    bool m_middleButtonDown = false;
    bool m_rightButtonDown = false;
};
