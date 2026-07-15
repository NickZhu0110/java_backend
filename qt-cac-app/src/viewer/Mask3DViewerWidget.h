#pragma once

#include <QWidget>

#include "data/MaskVolume.h"

#include <vtkActor.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>

class QVTKOpenGLNativeWidget;
class StrictTrackballCameraStyle;
class vtkAnnotatedCubeActor;
class vtkOrientationMarkerWidget;

class Mask3DViewerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit Mask3DViewerWidget(QWidget *parent = nullptr);

    bool eventFilter(QObject *watched, QEvent *event) override;

    void clear();
    void resetCamera();
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

    QVTKOpenGLNativeWidget *m_vtkWidget = nullptr;
    vtkNew<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkNew<vtkRenderer> m_renderer;
    vtkSmartPointer<StrictTrackballCameraStyle> m_interactorStyle;
    vtkSmartPointer<vtkActor> m_maskActor;
    vtkSmartPointer<vtkAnnotatedCubeActor> m_orientationCube;
    vtkSmartPointer<vtkOrientationMarkerWidget> m_orientationMarker;
    vtkSmartPointer<vtkActor> m_boundsActor;
    double m_surfaceFrameBounds[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    bool m_hasSurfaceFrameBounds = false;
    bool m_hasRenderedMask = false;
    double m_surfaceOpacity = 0.9;
    bool m_leftButtonDown = false;
    bool m_middleButtonDown = false;
    bool m_rightButtonDown = false;
};
