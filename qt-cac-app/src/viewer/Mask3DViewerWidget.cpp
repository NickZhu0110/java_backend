#include "viewer/Mask3DViewerWidget.h"

#include <QDebug>
#include <QEvent>
#include <QMouseEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkAnnotatedCubeActor.h>
#include <vtkAxesActor.h>
#include <vtkCamera.h>
#include <vtkCommand.h>
#include <vtkCubeSource.h>
#include <vtkDiscreteMarchingCubes.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>

#include <algorithm>
#include <cmath>

class StrictTrackballCameraStyle final : public vtkInteractorStyleTrackballCamera
{
public:
    static StrictTrackballCameraStyle *New();
    vtkTypeMacro(StrictTrackballCameraStyle, vtkInteractorStyleTrackballCamera);

    void OnMouseMove() override
    {
        if (!anyButtonDown()) {
            return;
        }
        Superclass::OnMouseMove();
    }

    void OnLeftButtonDown() override
    {
        m_leftButtonDown = true;
        Superclass::OnLeftButtonDown();
    }

    void OnLeftButtonUp() override
    {
        m_leftButtonDown = false;
        Superclass::OnLeftButtonUp();
    }

    void OnMiddleButtonDown() override
    {
        m_middleButtonDown = true;
        Superclass::OnMiddleButtonDown();
    }

    void OnMiddleButtonUp() override
    {
        m_middleButtonDown = false;
        Superclass::OnMiddleButtonUp();
    }

    void OnRightButtonDown() override
    {
        m_rightButtonDown = true;
        Superclass::OnRightButtonDown();
    }

    void OnRightButtonUp() override
    {
        m_rightButtonDown = false;
        Superclass::OnRightButtonUp();
    }

    void OnMouseWheelForward() override {}
    void OnMouseWheelBackward() override {}

    void forceEndInteraction()
    {
        m_leftButtonDown = false;
        m_middleButtonDown = false;
        m_rightButtonDown = false;

        Superclass::OnLeftButtonUp();
        Superclass::OnMiddleButtonUp();
        Superclass::OnRightButtonUp();
    }

private:
    bool anyButtonDown() const
    {
        return m_leftButtonDown || m_middleButtonDown || m_rightButtonDown;
    }

    bool m_leftButtonDown = false;
    bool m_middleButtonDown = false;
    bool m_rightButtonDown = false;
};

vtkStandardNewMacro(StrictTrackballCameraStyle);

namespace {

constexpr bool kVerbose3DMouseLogs = false;
constexpr bool kVerbose3DLogs = false;

double spacingValue(const MaskVolume &mask, int axis)
{
    if (axis >= 0 && axis < static_cast<int>(mask.spacing.size())
        && mask.spacing[static_cast<size_t>(axis)] > 0.0) {
        return mask.spacing[static_cast<size_t>(axis)];
    }
    return 1.0;
}

double originValue(const MaskVolume &mask, int axis)
{
    if (axis >= 0 && axis < static_cast<int>(mask.origin.size())) {
        return mask.origin[static_cast<size_t>(axis)];
    }
    return 0.0;
}

} // namespace

Mask3DViewerWidget::Mask3DViewerWidget(QWidget *parent)
    : QWidget(parent)
{
    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
    m_vtkWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_vtkWidget->setMouseTracking(false);
    m_vtkWidget->installEventFilter(this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_vtkWidget);

    m_vtkWidget->setRenderWindow(m_renderWindow);
    m_interactorStyle = vtkSmartPointer<StrictTrackballCameraStyle>::New();
    m_vtkWidget->interactor()->SetInteractorStyle(m_interactorStyle);
    m_renderer->SetBackground(0.05, 0.05, 0.06);
    m_renderWindow->AddRenderer(m_renderer);
    setupOrientationMarker();
}

void Mask3DViewerWidget::setupOrientationMarker()
{
    if (m_orientationMarker || !m_vtkWidget || !m_vtkWidget->interactor()) {
        return;
    }

    m_orientationCube = vtkSmartPointer<vtkAnnotatedCubeActor>::New();
    m_orientationCube->SetXPlusFaceText("+X");
    m_orientationCube->SetXMinusFaceText("-X");
    m_orientationCube->SetYPlusFaceText("+Y");
    m_orientationCube->SetYMinusFaceText("-Y");
    m_orientationCube->SetZPlusFaceText("+Z");
    m_orientationCube->SetZMinusFaceText("-Z");
    m_orientationCube->SetFaceTextScale(0.55);
    m_orientationCube->SetTextEdgesVisibility(1);
    m_orientationCube->GetCubeProperty()->SetColor(0.72, 0.74, 0.78);
    m_orientationCube->GetCubeProperty()->SetOpacity(0.62);
    m_orientationCube->GetTextEdgesProperty()->SetColor(0.08, 0.08, 0.10);
    m_orientationCube->GetXPlusFaceProperty()->SetColor(0.95, 0.28, 0.28);
    m_orientationCube->GetXMinusFaceProperty()->SetColor(0.95, 0.28, 0.28);
    m_orientationCube->GetYPlusFaceProperty()->SetColor(0.28, 0.90, 0.38);
    m_orientationCube->GetYMinusFaceProperty()->SetColor(0.28, 0.90, 0.38);
    m_orientationCube->GetZPlusFaceProperty()->SetColor(0.28, 0.55, 1.00);
    m_orientationCube->GetZMinusFaceProperty()->SetColor(0.28, 0.55, 1.00);

    m_orientationMarker = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
    m_orientationMarker->SetOrientationMarker(m_orientationCube);
    m_orientationMarker->SetInteractor(m_vtkWidget->interactor());
    m_orientationMarker->SetViewport(0.0, 0.0, 0.22, 0.22);
    m_orientationMarker->SetOutlineColor(0.72, 0.74, 0.78);
    m_orientationMarker->SetEnabled(1);
    m_orientationMarker->InteractiveOff();
}

void Mask3DViewerWidget::clearVolumeBoundsGuide()
{
    if (m_boundsActor) {
        m_renderer->RemoveActor(m_boundsActor);
        m_boundsActor = nullptr;
    }
    if (m_sceneAxes) {
        m_renderer->RemoveActor(m_sceneAxes);
        m_sceneAxes = nullptr;
    }
}

void Mask3DViewerWidget::updateVolumeBoundsGuide(const MaskVolume &mask)
{
    clearVolumeBoundsGuide();
    if (!mask.isValid()) {
        return;
    }

    const double spacingX = spacingValue(mask, 0);
    const double spacingY = spacingValue(mask, 1);
    const double spacingZ = spacingValue(mask, 2);
    const double sizeX = static_cast<double>(mask.width) * spacingX;
    const double sizeY = static_cast<double>(mask.height) * spacingY;
    const double sizeZ = static_cast<double>(mask.depth) * spacingZ;

    // vtkImageData stores voxel-center coordinates. Extending half a spacing
    // outside the first/last centers gives the physical N * spacing volume.
    const double centerX = originValue(mask, 0) + (static_cast<double>(mask.width) - 1.0) * spacingX * 0.5;
    const double centerY = originValue(mask, 1) + (static_cast<double>(mask.height) - 1.0) * spacingY * 0.5;
    const double centerZ = originValue(mask, 2) + (static_cast<double>(mask.depth) - 1.0) * spacingZ * 0.5;

    vtkNew<vtkCubeSource> boundsSource;
    boundsSource->SetCenter(centerX, centerY, centerZ);
    boundsSource->SetXLength(sizeX);
    boundsSource->SetYLength(sizeY);
    boundsSource->SetZLength(sizeZ);

    vtkNew<vtkPolyDataMapper> boundsMapper;
    boundsMapper->SetInputConnection(boundsSource->GetOutputPort());
    boundsMapper->ScalarVisibilityOff();

    m_boundsActor = vtkSmartPointer<vtkActor>::New();
    m_boundsActor->SetMapper(boundsMapper);
    m_boundsActor->GetProperty()->SetColor(0.82, 0.84, 0.88);
    m_boundsActor->GetProperty()->SetOpacity(0.05);
    m_boundsActor->GetProperty()->EdgeVisibilityOn();
    m_boundsActor->GetProperty()->SetEdgeColor(0.82, 0.84, 0.88);
    m_boundsActor->GetProperty()->SetEdgeOpacity(0.45);
    m_boundsActor->GetProperty()->SetLineWidth(1.25);
    m_renderer->AddActor(m_boundsActor);

    const double minX = centerX - sizeX * 0.5;
    const double minY = centerY - sizeY * 0.5;
    const double minZ = centerZ - sizeZ * 0.5;
    const double axisLength = std::max({sizeX, sizeY, sizeZ}) * 0.16;
    m_sceneAxes = vtkSmartPointer<vtkAxesActor>::New();
    m_sceneAxes->SetPosition(minX, minY, minZ);
    m_sceneAxes->SetTotalLength(axisLength, axisLength, axisLength);
    m_sceneAxes->SetNormalizedShaftLength(0.78, 0.78, 0.78);
    m_sceneAxes->SetNormalizedTipLength(0.22, 0.22, 0.22);
    m_sceneAxes->SetShaftTypeToCylinder();
    m_sceneAxes->AxisLabelsOn();
    m_renderer->AddActor(m_sceneAxes);

    if constexpr (kVerbose3DLogs) {
        qDebug() << "3D volume bounds guide size=" << sizeX << sizeY << sizeZ
                 << "center=" << centerX << centerY << centerZ;
    }
}

bool Mask3DViewerWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_vtkWidget) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseMove: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const Qt::MouseButtons buttons = mouseEvent->buttons();
        if (buttons == Qt::NoButton) {
            if (anyMouseButtonDown()) {
                m_leftButtonDown = false;
                m_middleButtonDown = false;
                m_rightButtonDown = false;
                forceEndInteraction();
            }
            if constexpr (kVerbose3DMouseLogs) {
                qDebug() << "3D MouseMove buttons=NoButton localDown="
                         << anyMouseButtonDown() << "swallowed";
            }
            return true;
        }

        m_leftButtonDown = buttons.testFlag(Qt::LeftButton);
        m_middleButtonDown = buttons.testFlag(Qt::MiddleButton);
        m_rightButtonDown = buttons.testFlag(Qt::RightButton);
        if constexpr (kVerbose3DMouseLogs) {
            qDebug() << "3D MouseMove buttons=" << static_cast<int>(buttons)
                     << "localDown=" << anyMouseButtonDown() << "passed";
        }
        return false;
    }
    case QEvent::MouseButtonDblClick:
        m_leftButtonDown = false;
        m_middleButtonDown = false;
        m_rightButtonDown = false;
        forceEndInteraction();
        emit viewportDoubleClicked();
        event->accept();
        return true;
    case QEvent::MouseButtonPress: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            m_leftButtonDown = true;
        } else if (mouseEvent->button() == Qt::MiddleButton) {
            m_middleButtonDown = true;
        } else if (mouseEvent->button() == Qt::RightButton) {
            m_rightButtonDown = true;
        }
        if constexpr (kVerbose3DMouseLogs) {
            qDebug() << "3D MousePress button=" << static_cast<int>(mouseEvent->button())
                     << "localDown=" << anyMouseButtonDown();
        }
        return false;
    }
    case QEvent::MouseButtonRelease: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            m_leftButtonDown = false;
        } else if (mouseEvent->button() == Qt::MiddleButton) {
            m_middleButtonDown = false;
        } else if (mouseEvent->button() == Qt::RightButton) {
            m_rightButtonDown = false;
        }
        if constexpr (kVerbose3DMouseLogs) {
            qDebug() << "3D MouseRelease button=" << static_cast<int>(mouseEvent->button())
                     << "remainingButtons=" << static_cast<int>(mouseEvent->buttons())
                     << "localDown=" << anyMouseButtonDown();
        }

        // Let QVTK process the release first, then guarantee that its style is idle.
        if (!anyMouseButtonDown()) {
            QTimer::singleShot(0, this, [this]() {
                if (!anyMouseButtonDown()) {
                    forceEndInteraction();
                }
            });
        }
        return false;
    }
    case QEvent::Leave:
    case QEvent::FocusOut:
        m_leftButtonDown = false;
        m_middleButtonDown = false;
        m_rightButtonDown = false;
        forceEndInteraction();
        return false;
    case QEvent::Wheel: {
        auto *wheelEvent = static_cast<QWheelEvent *>(event);
        const int angleDelta = wheelEvent->angleDelta().y();
        const int pixelDelta = wheelEvent->pixelDelta().y();
        const double delta = angleDelta != 0 ? static_cast<double>(angleDelta)
                                             : static_cast<double>(pixelDelta);
        if (delta == 0.0) {
            return true;
        }

        const double steps = std::clamp(delta / 120.0, -10.0, 10.0);
        const double zoomFactor = std::pow(1.1, steps);
        vtkCamera *camera = m_renderer->GetActiveCamera();
        if (camera) {
            if (camera->GetParallelProjection()) {
                camera->SetParallelScale(camera->GetParallelScale() / zoomFactor);
            } else {
                camera->Dolly(zoomFactor);
            }
            m_renderer->ResetCameraClippingRange();
            m_renderWindow->Render();
        }
        if constexpr (kVerbose3DMouseLogs) {
            qDebug() << "3D Wheel delta=" << delta << "zoomFactor=" << zoomFactor
                     << "consumed as zoom";
        }
        wheelEvent->accept();
        return true;
    }
    case QEvent::NativeGesture:
    case QEvent::Gesture:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
        if constexpr (kVerbose3DMouseLogs) {
            qDebug() << "3D gesture/touch event swallowed type=" << event->type();
        }
        event->accept();
        return true;
    default:
        return QWidget::eventFilter(watched, event);
    }
}

bool Mask3DViewerWidget::anyMouseButtonDown() const
{
    return m_leftButtonDown || m_middleButtonDown || m_rightButtonDown;
}

void Mask3DViewerWidget::forceEndInteraction()
{
    auto *interactor = m_vtkWidget ? m_vtkWidget->interactor() : nullptr;
    if (!interactor || !m_interactorStyle) {
        return;
    }

    m_interactorStyle->forceEndInteraction();
    interactor->InvokeEvent(vtkCommand::EndInteractionEvent);
}

void Mask3DViewerWidget::clear()
{
    if (m_maskActor) {
        m_renderer->RemoveActor(m_maskActor);
        m_maskActor = nullptr;
    }
    clearVolumeBoundsGuide();
    m_hasRenderedMask = false;
    m_renderWindow->Render();
}

void Mask3DViewerWidget::resetCamera()
{
    vtkRenderer *renderer = m_renderer.GetPointer();
    vtkRenderWindow *renderWindow = m_renderWindow.GetPointer();
    if (!renderer || !renderWindow) {
        qWarning() << "Reset 3D camera skipped: renderer/render window missing.";
        return;
    }
    if (!m_maskActor || !m_maskActor->GetVisibility()) {
        qWarning() << "Reset 3D camera skipped: no visible mask actor.";
        return;
    }

    double bounds[6];
    m_maskActor->GetBounds(bounds);
    const bool validBounds = std::all_of(std::begin(bounds), std::end(bounds), [](double value) {
        return std::isfinite(value);
    }) && bounds[0] <= bounds[1]
        && bounds[2] <= bounds[3]
        && bounds[4] <= bounds[5];
    if (!validBounds) {
        qWarning() << "Reset 3D camera skipped: mask actor bounds are invalid.";
        return;
    }

    vtkCamera *camera = renderer->GetActiveCamera();
    if (!camera) {
        qWarning() << "Reset 3D camera skipped: active camera missing.";
        return;
    }

    m_leftButtonDown = false;
    m_middleButtonDown = false;
    m_rightButtonDown = false;
    forceEndInteraction();

    const double centerX = (bounds[0] + bounds[1]) * 0.5;
    const double centerY = (bounds[2] + bounds[3]) * 0.5;
    const double centerZ = (bounds[4] + bounds[5]) * 0.5;

    // ResetCamera fits bounds but preserves view direction, so restore a known
    // isometric direction first to make Reset Camera visually deterministic.
    camera->SetFocalPoint(centerX, centerY, centerZ);
    camera->SetPosition(centerX + 1.0, centerY - 1.0, centerZ + 1.0);
    camera->SetViewUp(0.0, 0.0, 1.0);
    camera->OrthogonalizeViewUp();

    renderer->ResetCamera(bounds);
    renderer->ResetCameraClippingRange();
    renderWindow->Render();

    if constexpr (kVerbose3DLogs) {
        qDebug() << "Reset 3D camera with actor bounds:"
                 << bounds[0] << bounds[1]
                 << bounds[2] << bounds[3]
                 << bounds[4] << bounds[5];
    }
}

void Mask3DViewerWidget::setMaskVolume(const MaskVolume &mask)
{
    refreshFromMask(mask);
}

void Mask3DViewerWidget::refreshFromMask(const MaskVolume &mask)
{
    if (!mask.isValid()) {
        qWarning() << "Mask3DViewerWidget: invalid mask; clearing 3D surface.";
        clear();
        return;
    }

    qsizetype nonzeroCount = 0;
    for (uint8_t value : mask.voxels) {
        if (value != 0) {
            ++nonzeroCount;
        }
    }
    if (nonzeroCount == 0) {
        qInfo() << "Mask3DViewerWidget: mask has no foreground voxels; clearing 3D surface.";
        clear();
        return;
    }

    vtkNew<vtkImageData> imageData;
    imageData->SetDimensions(mask.width, mask.height, mask.depth);
    imageData->SetSpacing(spacingValue(mask, 0), spacingValue(mask, 1), spacingValue(mask, 2));
    imageData->SetOrigin(originValue(mask, 0), originValue(mask, 1), originValue(mask, 2));
    imageData->AllocateScalars(VTK_UNSIGNED_CHAR, 1);

    for (int z = 0; z < mask.depth; ++z) {
        for (int y = 0; y < mask.height; ++y) {
            for (int x = 0; x < mask.width; ++x) {
                auto *voxel = static_cast<unsigned char *>(imageData->GetScalarPointer(x, y, z));
                voxel[0] = mask.value(x, y, z) ? 1 : 0;
            }
        }
    }

    vtkNew<vtkDiscreteMarchingCubes> surfaceExtractor;
    surfaceExtractor->SetInputData(imageData);
    surfaceExtractor->GenerateValues(1, 1, 1);
    surfaceExtractor->Update();

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputConnection(surfaceExtractor->GetOutputPort());
    mapper->ScalarVisibilityOff();

    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(1.0, 0.72, 0.05);
    actor->GetProperty()->SetOpacity(0.70);
    actor->GetProperty()->SetSpecular(0.18);
    actor->GetProperty()->SetSpecularPower(18.0);

    if (m_maskActor) {
        m_renderer->RemoveActor(m_maskActor);
    }
    m_maskActor = actor;
    m_renderer->AddActor(m_maskActor);
    updateVolumeBoundsGuide(mask);

    const bool firstRenderedMask = !m_hasRenderedMask;
    m_hasRenderedMask = true;
    if (firstRenderedMask) {
        resetCamera();
    } else {
        m_renderer->ResetCameraClippingRange();
        m_renderWindow->Render();
    }

    qInfo() << "Mask3DViewerWidget: rendered working mask surface"
            << mask.width << "x" << mask.height << "x" << mask.depth
            << "spacing=(" << spacingValue(mask, 0) << "," << spacingValue(mask, 1) << "," << spacingValue(mask, 2) << ")"
            << "foreground voxels=" << nonzeroCount;
}
