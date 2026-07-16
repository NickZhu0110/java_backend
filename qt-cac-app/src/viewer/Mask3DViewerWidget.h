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
class vtkImageData;
class vtkImageSlice;
class vtkImageSliceMapper;
class vtkOrientationMarkerWidget;

class Mask3DViewerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit Mask3DViewerWidget(QWidget *parent = nullptr);

    bool eventFilter(QObject *watched, QEvent *event) override;

    void clear();
    void resetCamera();
    void setCtVolume(const VolumeData &volume, double windowWidth, double windowLevel);
    void setAxialSlice(int index);
    void setCoronalSlice(int index);
    void setSagittalSlice(int index);
    void setAxialPlaneVisible(bool visible);
    void setCoronalPlaneVisible(bool visible);
    void setSagittalPlaneVisible(bool visible);
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
    void clearCtVolume();
    void setCtPlaneSlice(int orientation, int index);
    void setCtPlaneVisible(int orientation, bool visible);
    void updateCtPlaneCropping();
    void updateCtPlaneVisibility();
    bool ctGeometryMatchesMask(const MaskVolume &mask) const;

    QVTKOpenGLNativeWidget *m_vtkWidget = nullptr;
    vtkNew<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkNew<vtkRenderer> m_renderer;
    vtkSmartPointer<StrictTrackballCameraStyle> m_interactorStyle;
    vtkSmartPointer<vtkActor> m_maskActor;
    vtkSmartPointer<vtkAnnotatedCubeActor> m_orientationCube;
    vtkSmartPointer<vtkOrientationMarkerWidget> m_orientationMarker;
    vtkSmartPointer<vtkActor> m_boundsActor;
    vtkSmartPointer<vtkImageData> m_ctImageData;
    std::array<vtkSmartPointer<vtkImageSliceMapper>, 3> m_ctPlaneMappers;
    std::array<vtkSmartPointer<vtkImageSlice>, 3> m_ctPlaneActors;
    std::array<int, 3> m_ctDimensions = {0, 0, 0};
    std::array<double, 3> m_ctSpacing = {1.0, 1.0, 1.0};
    std::array<double, 3> m_ctOrigin = {0.0, 0.0, 0.0};
    std::array<double, 9> m_ctDirection = {1.0, 0.0, 0.0,
                                           0.0, 1.0, 0.0,
                                           0.0, 0.0, 1.0};
    std::array<int, 3> m_ctPlaneSlices = {0, 0, 0};
    std::array<bool, 3> m_ctPlaneVisibilityRequested = {false, false, true};
    double m_surfaceFrameBounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    bool m_hasSurfaceFrameBounds = false;
    bool m_hasRenderedMask = false;
    bool m_hasCtVolume = false;
    bool m_ctMaskGeometryAligned = false;
    double m_surfaceOpacity = 0.9;
    bool m_leftButtonDown = false;
    bool m_middleButtonDown = false;
    bool m_rightButtonDown = false;
};
