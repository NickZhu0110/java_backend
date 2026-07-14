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
class vtkAxesActor;
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
    void refreshFromMask(const MaskVolume &mask);

signals:
    void viewportDoubleClicked();

private:
    bool anyMouseButtonDown() const;
    void forceEndInteraction();
    void setupOrientationMarker();
    void updateVolumeBoundsGuide(const MaskVolume &mask);
    void clearVolumeBoundsGuide();

    QVTKOpenGLNativeWidget *m_vtkWidget = nullptr;
    vtkNew<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkNew<vtkRenderer> m_renderer;
    vtkSmartPointer<StrictTrackballCameraStyle> m_interactorStyle;
    vtkSmartPointer<vtkActor> m_maskActor;
    vtkSmartPointer<vtkAnnotatedCubeActor> m_orientationCube;
    vtkSmartPointer<vtkOrientationMarkerWidget> m_orientationMarker;
    vtkSmartPointer<vtkActor> m_boundsActor;
    vtkSmartPointer<vtkAxesActor> m_sceneAxes;
    bool m_hasRenderedMask = false;
    bool m_leftButtonDown = false;
    bool m_middleButtonDown = false;
    bool m_rightButtonDown = false;
};
