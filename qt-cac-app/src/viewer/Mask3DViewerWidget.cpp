#include "viewer/Mask3DViewerWidget.h"
#include "viewer/MultiStructureNiftiLoader.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QEvent>
#include <QMouseEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkAnnotatedCubeActor.h>
#include <vtkCamera.h>
#include <vtkCallbackCommand.h>
#include <vtkCellArray.h>
#include <vtkCellPicker.h>
#include <vtkCommand.h>
#include <vtkDiscreteFlyingEdges3D.h>
#include <vtkDiscreteMarchingCubes.h>
#include <vtkExtractVOI.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkOutlineSource.h>
#include <vtkPlaneSource.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyLine.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkSphereSource.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

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
        const bool shiftPressed = m_shiftLeftPanRequested
            || (this->Interactor && this->Interactor->GetShiftKey());
        m_shiftLeftPanRequested = false;
        if (shiftPressed && this->Interactor) {
            const int *eventPosition = this->Interactor->GetEventPosition();
            this->FindPokedRenderer(eventPosition[0], eventPosition[1]);
            if (!this->CurrentRenderer) {
                m_leftButtonDown = false;
                return;
            }

            m_shiftLeftPanActive = true;
            this->GrabFocus(this->EventCallbackCommand);
            this->StartPan();
            return;
        }
        Superclass::OnLeftButtonDown();
    }

    void OnLeftButtonUp() override
    {
        m_leftButtonDown = false;
        if (m_shiftLeftPanActive) {
            if (this->State == VTKIS_PAN) {
                this->EndPan();
            }
            if (this->Interactor) {
                this->ReleaseFocus();
            }
            m_shiftLeftPanActive = false;
            m_shiftLeftPanRequested = false;
            return;
        }
        m_shiftLeftPanRequested = false;
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

        this->OnLeftButtonUp();
        Superclass::OnMiddleButtonUp();
        Superclass::OnRightButtonUp();
    }

    void setShiftLeftPanRequested(bool requested)
    {
        m_shiftLeftPanRequested = requested;
    }

private:
    bool anyButtonDown() const
    {
        return m_leftButtonDown || m_middleButtonDown || m_rightButtonDown;
    }

    bool m_leftButtonDown = false;
    bool m_middleButtonDown = false;
    bool m_rightButtonDown = false;
    bool m_shiftLeftPanRequested = false;
    bool m_shiftLeftPanActive = false;
};

vtkStandardNewMacro(StrictTrackballCameraStyle);

namespace {

constexpr bool kVerbose3DMouseLogs = false;
constexpr bool kVerbose3DLogs = false;
constexpr bool kVerbosePlaneRenderingLogs = false;
constexpr double kGeometryTolerance = 1e-6;
constexpr double kMinimumProjectedSlicePixels = 0.2;
constexpr double kPlaneBorderWidth = 2.0;
constexpr double kSelectedPlaneBorderWidth = 4.0;
constexpr double kPlaneColors[3][3] = {
    {1.00, 0.15, 0.75},
    {0.18, 0.95, 0.35},
    {0.10, 0.78, 1.00}
};

using PlaneCorners = std::array<std::array<double, 3>, 4>;

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

bool boundsAreValid(const double bounds[6])
{
    return std::all_of(bounds, bounds + 6, [](double value) {
        return std::isfinite(value);
    }) && bounds[0] <= bounds[1]
        && bounds[2] <= bounds[3]
        && bounds[4] <= bounds[5];
}

bool nearlyEqual(double left, double right)
{
    return std::abs(left - right) <= kGeometryTolerance;
}

bool directionIsIdentity(const std::vector<double> &direction)
{
    static constexpr double identity[9] = {
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0
    };
    if (direction.size() != 9) {
        return false;
    }
    for (int index = 0; index < 9; ++index) {
        if (!std::isfinite(direction[static_cast<size_t>(index)])
            || !nearlyEqual(direction[static_cast<size_t>(index)], identity[index])) {
            return false;
        }
    }
    return true;
}

const char *anatomicalLabel(int physicalAxis, bool positive)
{
    static constexpr const char *positiveLabels[3] = {"L", "P", "S"};
    static constexpr const char *negativeLabels[3] = {"R", "A", "I"};
    return positive ? positiveLabels[physicalAxis] : negativeLabels[physicalAxis];
}

const char *planeName(int axis)
{
    static constexpr const char *names[3] = {"Sagittal", "Coronal", "Axial"};
    return axis >= 0 && axis < 3 ? names[axis] : "Unknown";
}

void logPositionPlaneState(const char *phase,
                           int axis,
                           int sliceIndex,
                           vtkPlaneSource *fillSource,
                           vtkPolyDataMapper *fillMapper,
                           vtkActor *fillActor,
                           vtkPolyData *borderData,
                           const PlaneCorners &corners)
{
    if constexpr (!kVerbosePlaneRenderingLogs) {
        return;
    }
    if (!fillSource || !fillMapper || !fillActor || !borderData) {
        qDebug() << phase << planeName(axis) << "plane pipeline incomplete";
        return;
    }

    fillSource->Update();
    vtkPolyData *fillData = fillSource->GetOutput();
    vtkProperty *property = fillActor->GetProperty();
    qDebug() << phase << planeName(axis)
             << "slice=" << sliceIndex
             << "source=" << static_cast<const void *>(fillSource)
             << "mapperInput=" << static_cast<const void *>(fillMapper->GetInput())
             << "points=" << (fillData ? fillData->GetNumberOfPoints() : 0)
             << "polys=" << (fillData ? fillData->GetNumberOfPolys() : 0)
             << "lines=" << (fillData ? fillData->GetNumberOfLines() : 0)
             << "borderLines=" << borderData->GetNumberOfLines()
             << "visible=" << fillActor->GetVisibility()
             << "opacity=" << property->GetOpacity()
             << "representation=" << property->GetRepresentation()
             << "frontCull=" << property->GetFrontfaceCulling()
             << "backCull=" << property->GetBackfaceCulling();
    for (size_t corner = 0; corner < corners.size(); ++corner) {
        qDebug() << " corner" << corner << "="
                 << corners[corner][0] << corners[corner][1] << corners[corner][2];
    }
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
    m_planePicker = vtkSmartPointer<vtkCellPicker>::New();
    m_planePicker->PickFromListOn();
    m_planePicker->SetTolerance(0.005);
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
    static constexpr const char *plusLabels[3] = {"+X", "+Y", "+Z"};
    static constexpr const char *minusLabels[3] = {"-X", "-Y", "-Z"};
    setOrientationLabels(plusLabels, minusLabels);
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

void Mask3DViewerWidget::setOrientationLabels(const char *const plusLabels[3],
                                               const char *const minusLabels[3])
{
    if (!m_orientationCube) {
        return;
    }

    m_orientationCube->SetXPlusFaceText(plusLabels[0]);
    m_orientationCube->SetXMinusFaceText(minusLabels[0]);
    m_orientationCube->SetYPlusFaceText(plusLabels[1]);
    m_orientationCube->SetYMinusFaceText(minusLabels[1]);
    m_orientationCube->SetZPlusFaceText(plusLabels[2]);
    m_orientationCube->SetZMinusFaceText(minusLabels[2]);
}

void Mask3DViewerWidget::updateOrientationLabels(const MaskVolume &mask)
{
    static constexpr const char *fallbackPlus[3] = {"+X", "+Y", "+Z"};
    static constexpr const char *fallbackMinus[3] = {"-X", "-Y", "-Z"};
    if (mask.direction.size() != 9) {
        setOrientationLabels(fallbackPlus, fallbackMinus);
        qWarning() << "Medical orientation labels unavailable: mask direction matrix is missing.";
        return;
    }

    const char *plusLabels[3] = {};
    const char *minusLabels[3] = {};
    bool usedPhysicalAxes[3] = {false, false, false};
    for (int modelAxis = 0; modelAxis < 3; ++modelAxis) {
        int physicalAxis = 0;
        double dominantMagnitude = 0.0;
        for (int row = 0; row < 3; ++row) {
            const double component = mask.direction[static_cast<size_t>(row * 3 + modelAxis)];
            if (!std::isfinite(component)) {
                setOrientationLabels(fallbackPlus, fallbackMinus);
                qWarning() << "Medical orientation labels unavailable: mask direction matrix is invalid.";
                return;
            }
            if (std::abs(component) > dominantMagnitude) {
                dominantMagnitude = std::abs(component);
                physicalAxis = row;
            }
        }

        if (dominantMagnitude < 0.999 || usedPhysicalAxes[physicalAxis]) {
            setOrientationLabels(fallbackPlus, fallbackMinus);
            qWarning() << "Medical orientation labels unavailable: rendered axes are oblique or ambiguous.";
            return;
        }
        for (int row = 0; row < 3; ++row) {
            if (row != physicalAxis
                && std::abs(mask.direction[static_cast<size_t>(row * 3 + modelAxis)]) > 0.001) {
                setOrientationLabels(fallbackPlus, fallbackMinus);
                qWarning() << "Medical orientation labels unavailable: rendered axes are oblique.";
                return;
            }
        }

        usedPhysicalAxes[physicalAxis] = true;
        const bool plusIsPhysicalPositive = mask.direction[static_cast<size_t>(physicalAxis * 3 + modelAxis)] > 0.0;
        plusLabels[modelAxis] = anatomicalLabel(physicalAxis, plusIsPhysicalPositive);
        minusLabels[modelAxis] = anatomicalLabel(physicalAxis, !plusIsPhysicalPositive);
    }

    setOrientationLabels(plusLabels, minusLabels);
    qInfo() << "Verified medical orientation labels:"
            << "+X=" << plusLabels[0] << "-X=" << minusLabels[0]
            << "+Y=" << plusLabels[1] << "-Y=" << minusLabels[1]
            << "+Z=" << plusLabels[2] << "-Z=" << minusLabels[2];
}

void Mask3DViewerWidget::updateOrientationLabelsForRasWorld()
{
    static constexpr const char *plusLabels[3] = {"R", "A", "S"};
    static constexpr const char *minusLabels[3] = {"L", "P", "I"};
    setOrientationLabels(plusLabels, minusLabels);
    qInfo() << "Verified NIfTI preview orientation labels in RAS world coordinates."
            << "+X=R -X=L +Y=A -Y=P +Z=S -Z=I";
}

void Mask3DViewerWidget::clearVolumeBoundsGuide()
{
    if (m_boundsActor) {
        m_renderer->RemoveActor(m_boundsActor);
        m_boundsActor = nullptr;
    }
    m_hasSurfaceFrameBounds = false;
    updatePositionPlaneVisibility();
}

void Mask3DViewerWidget::updateModelBoundsGuide(const double surfaceBounds[6])
{
    clearVolumeBoundsGuide();
    if (!boundsAreValid(surfaceBounds)) {
        return;
    }

    const double sizeX = surfaceBounds[1] - surfaceBounds[0];
    const double sizeY = surfaceBounds[3] - surfaceBounds[2];
    const double sizeZ = surfaceBounds[5] - surfaceBounds[4];
    const double largestDimension = std::max({sizeX, sizeY, sizeZ});
    constexpr double marginRatio = 0.08;
    constexpr double minimumCubeSideLength = 1.0;
    const double cubeSideLength = std::max(largestDimension * (1.0 + 2.0 * marginRatio),
                                           minimumCubeSideLength);
    const double halfSide = cubeSideLength * 0.5;
    const double centerX = (surfaceBounds[0] + surfaceBounds[1]) * 0.5;
    const double centerY = (surfaceBounds[2] + surfaceBounds[3]) * 0.5;
    const double centerZ = (surfaceBounds[4] + surfaceBounds[5]) * 0.5;
    m_surfaceFrameBounds[0] = centerX - halfSide;
    m_surfaceFrameBounds[1] = centerX + halfSide;
    m_surfaceFrameBounds[2] = centerY - halfSide;
    m_surfaceFrameBounds[3] = centerY + halfSide;
    m_surfaceFrameBounds[4] = centerZ - halfSide;
    m_surfaceFrameBounds[5] = centerZ + halfSide;
    m_hasSurfaceFrameBounds = true;

    vtkNew<vtkOutlineSource> boundsSource;
    boundsSource->SetBounds(m_surfaceFrameBounds);

    vtkNew<vtkPolyDataMapper> boundsMapper;
    boundsMapper->SetInputConnection(boundsSource->GetOutputPort());
    boundsMapper->ScalarVisibilityOff();

    m_boundsActor = vtkSmartPointer<vtkActor>::New();
    m_boundsActor->SetMapper(boundsMapper);
    m_boundsActor->GetProperty()->SetColor(0.72, 0.75, 0.80);
    m_boundsActor->GetProperty()->SetOpacity(0.38);
    m_boundsActor->GetProperty()->SetLineWidth(1.25);
    m_renderer->AddActor(m_boundsActor);
    updateAllPositionPlaneGeometry();
    updateAllPositionPlaneMaskIntersections();
    updatePositionPlaneVisibility();

    qInfo() << "3D CAC guide cube center=" << centerX << centerY << centerZ
            << "side lengths=" << cubeSideLength << cubeSideLength << cubeSideLength
            << "margin ratio=" << marginRatio;

    if constexpr (kVerbose3DLogs) {
        qDebug() << "3D model bounds guide surface="
                 << surfaceBounds[0] << surfaceBounds[1]
                 << surfaceBounds[2] << surfaceBounds[3]
                 << surfaceBounds[4] << surfaceBounds[5]
                 << "cube side=" << cubeSideLength;
    }
}

int Mask3DViewerWidget::draggedPlaneAxis() const
{
    switch (m_draggedPlane) {
    case DraggedPlane::Sagittal:
        return 0;
    case DraggedPlane::Coronal:
        return 1;
    case DraggedPlane::Axial:
        return 2;
    case DraggedPlane::None:
        return -1;
    }
    return -1;
}

QPointF Mask3DViewerWidget::widgetToVtkDisplay(const QPointF &widgetPosition) const
{
    if (!m_vtkWidget || !m_renderWindow || m_vtkWidget->width() <= 0
        || m_vtkWidget->height() <= 0) {
        return {};
    }

    const int *renderSize = m_renderWindow->GetSize();
    const double scaleX = renderSize && renderSize[0] > 0
        ? static_cast<double>(renderSize[0]) / m_vtkWidget->width()
        : 1.0;
    const double scaleY = renderSize && renderSize[1] > 0
        ? static_cast<double>(renderSize[1]) / m_vtkWidget->height()
        : 1.0;
    return {widgetPosition.x() * scaleX,
            (m_vtkWidget->height() - 1.0 - widgetPosition.y()) * scaleY};
}

bool Mask3DViewerWidget::worldToDisplay(const std::array<double, 3> &world,
                                        QPointF *display) const
{
    if (!display || !m_renderer) {
        return false;
    }
    m_renderer->SetWorldPoint(world[0], world[1], world[2], 1.0);
    m_renderer->WorldToDisplay();
    const double *displayPoint = m_renderer->GetDisplayPoint();
    if (!displayPoint || !std::isfinite(displayPoint[0])
        || !std::isfinite(displayPoint[1]) || !std::isfinite(displayPoint[2])) {
        return false;
    }
    *display = QPointF(displayPoint[0], displayPoint[1]);
    return true;
}

std::array<double, 3> Mask3DViewerWidget::worldToContinuousIndex(
    const std::array<double, 3> &world) const
{
    std::array<double, 3> index = {0.0, 0.0, 0.0};
    const std::array<double, 3> relative = {
        world[0] - m_volumeOrigin[0],
        world[1] - m_volumeOrigin[1],
        world[2] - m_volumeOrigin[2]
    };
    for (int axis = 0; axis < 3; ++axis) {
        double projected = 0.0;
        for (int row = 0; row < 3; ++row) {
            projected += m_volumeDirection[static_cast<size_t>(row * 3 + axis)]
                * relative[static_cast<size_t>(row)];
        }
        index[static_cast<size_t>(axis)] =
            projected / m_volumeSpacing[static_cast<size_t>(axis)];
    }
    return index;
}

void Mask3DViewerWidget::setPlaneDragHighlight(int axis, bool highlighted)
{
    if (axis < 0 || axis > 2) {
        return;
    }
    vtkActor *borderActor = m_positionPlaneBorderActors[static_cast<size_t>(axis)];
    if (!borderActor) {
        return;
    }
    vtkProperty *property = borderActor->GetProperty();
    if (highlighted) {
        property->SetColor(1.0, 1.0, 1.0);
        property->SetLineWidth(kSelectedPlaneBorderWidth);
    } else {
        property->SetColor(kPlaneColors[axis][0], kPlaneColors[axis][1], kPlaneColors[axis][2]);
        property->SetLineWidth(kPlaneBorderWidth);
    }
}

void Mask3DViewerWidget::updatePlanePickerList()
{
    if (!m_planePicker) {
        return;
    }
    m_planePicker->InitializePickList();
    for (int axis = 0; axis < 3; ++axis) {
        vtkActor *fillActor = m_positionPlaneFillActors[static_cast<size_t>(axis)];
        const bool pickable = m_movePlanesEnabled && fillActor
            && fillActor->GetVisibility()
            && m_positionPlaneVisibilityRequested[static_cast<size_t>(axis)];
        if (fillActor) {
            fillActor->SetPickable(pickable);
        }
        if (pickable) {
            m_planePicker->AddPickList(fillActor);
        }
    }
}

bool Mask3DViewerWidget::beginPlaneDrag(const QPointF &widgetPosition)
{
    if (!m_movePlanesEnabled || !m_hasVolumeGeometry || !m_volumeMaskGeometryAligned
        || (!m_niftiReviewGeometryActive && !m_hasSurfaceFrameBounds)
        || !m_planePicker || !m_renderer) {
        return false;
    }

    const QPointF displayPosition = widgetToVtkDisplay(widgetPosition);
    if (!m_planePicker->Pick(displayPosition.x(), displayPosition.y(), 0.0, m_renderer)) {
        return false;
    }

    vtkActor *pickedActor = m_planePicker->GetActor();
    int axis = -1;
    for (int candidateAxis = 0; candidateAxis < 3; ++candidateAxis) {
        if (pickedActor == m_positionPlaneFillActors[static_cast<size_t>(candidateAxis)]
            && pickedActor && pickedActor->GetVisibility()) {
            axis = candidateAxis;
            break;
        }
    }
    if (axis < 0) {
        return false;
    }

    m_draggedPlane = axis == 0 ? DraggedPlane::Sagittal
        : axis == 1 ? DraggedPlane::Coronal
                    : DraggedPlane::Axial;
    m_planeDragActive = true;
    m_planeDragStartDisplayPosition = displayPosition;
    m_lastRequestedPlaneSlice = m_positionPlaneSlices[static_cast<size_t>(axis)];

    const double *pickPosition = m_planePicker->GetPickPosition();
    m_planeDragStartWorldPosition = {pickPosition[0], pickPosition[1], pickPosition[2]};
    double normalLengthSquared = 0.0;
    for (int row = 0; row < 3; ++row) {
        const double component = m_volumeDirection[static_cast<size_t>(row * 3 + axis)];
        m_planeDragNormal[static_cast<size_t>(row)] = component;
        normalLengthSquared += component * component;
    }
    const double normalLength = std::sqrt(normalLengthSquared);
    if (normalLength > kGeometryTolerance) {
        for (double &component : m_planeDragNormal) {
            component /= normalLength;
        }
    }

    constexpr int referenceSliceCount = 8;
    std::array<double, 3> referenceWorld = m_planeDragStartWorldPosition;
    for (int component = 0; component < 3; ++component) {
        referenceWorld[static_cast<size_t>(component)] +=
            m_planeDragNormal[static_cast<size_t>(component)]
            * m_volumeSpacing[static_cast<size_t>(axis)] * referenceSliceCount;
    }
    QPointF anchorDisplay;
    QPointF referenceDisplay;
    m_planeDragProjectionValid = worldToDisplay(m_planeDragStartWorldPosition, &anchorDisplay)
        && worldToDisplay(referenceWorld, &referenceDisplay);
    if (m_planeDragProjectionValid) {
        m_planeDragDisplayPerSlice =
            (referenceDisplay - anchorDisplay) / static_cast<double>(referenceSliceCount);
        const double projectedLength = std::hypot(m_planeDragDisplayPerSlice.x(),
                                                   m_planeDragDisplayPerSlice.y());
        m_planeDragProjectionValid = projectedLength >= kMinimumProjectedSlicePixels;
    }
    if (!m_planeDragProjectionValid) {
        qWarning() << planeName(axis)
                   << "plane drag paused: slice normal projects too close to the camera view direction.";
    }

    setPlaneDragHighlight(axis, true);
    m_vtkWidget->setCursor(Qt::ClosedHandCursor);
    m_renderWindow->Render();
    return true;
}

void Mask3DViewerWidget::updatePlaneDrag(const QPointF &widgetPosition)
{
    const int axis = draggedPlaneAxis();
    if (!m_planeDragActive || axis < 0 || !m_planeDragProjectionValid) {
        return;
    }

    const QPointF displayDelta =
        widgetToVtkDisplay(widgetPosition) - m_planeDragStartDisplayPosition;
    const double projectionLengthSquared =
        QPointF::dotProduct(m_planeDragDisplayPerSlice, m_planeDragDisplayPerSlice);
    if (projectionLengthSquared
        < kMinimumProjectedSlicePixels * kMinimumProjectedSlicePixels) {
        return;
    }

    const double continuousSliceDelta =
        QPointF::dotProduct(displayDelta, m_planeDragDisplayPerSlice)
        / projectionLengthSquared;
    std::array<double, 3> candidateWorld = m_planeDragStartWorldPosition;
    for (int component = 0; component < 3; ++component) {
        candidateWorld[static_cast<size_t>(component)] +=
            m_planeDragNormal[static_cast<size_t>(component)]
            * continuousSliceDelta * m_volumeSpacing[static_cast<size_t>(axis)];
    }
    const std::array<double, 3> continuousIndex = worldToContinuousIndex(candidateWorld);
    const int maximum = std::max(0, m_volumeDimensions[static_cast<size_t>(axis)] - 1);
    const int requestedSlice = std::clamp(
        static_cast<int>(std::lround(continuousIndex[static_cast<size_t>(axis)])),
        0,
        maximum);
    if (requestedSlice == m_lastRequestedPlaneSlice) {
        return;
    }
    m_lastRequestedPlaneSlice = requestedSlice;

    switch (m_draggedPlane) {
    case DraggedPlane::Axial:
        emit axialPlaneSliceRequested(requestedSlice);
        break;
    case DraggedPlane::Coronal:
        emit coronalPlaneSliceRequested(requestedSlice);
        break;
    case DraggedPlane::Sagittal:
        emit sagittalPlaneSliceRequested(requestedSlice);
        break;
    case DraggedPlane::None:
        break;
    }
}

void Mask3DViewerWidget::endPlaneDrag()
{
    const int axis = draggedPlaneAxis();
    const bool wasActive = m_planeDragActive;
    m_planeDragActive = false;
    m_planeDragProjectionValid = false;
    m_draggedPlane = DraggedPlane::None;
    m_lastRequestedPlaneSlice = -1;
    if (axis >= 0) {
        setPlaneDragHighlight(axis, false);
    }
    if (m_vtkWidget) {
        if (m_movePlanesEnabled) {
            m_vtkWidget->setCursor(Qt::OpenHandCursor);
        } else {
            m_vtkWidget->unsetCursor();
        }
    }
    if (wasActive && m_renderWindow) {
        m_renderWindow->Render();
    }
}

void Mask3DViewerWidget::setMovePlanesEnabled(bool enabled)
{
    if (m_movePlanesEnabled == enabled) {
        return;
    }
    endPlaneDrag();
    m_movePlanesEnabled = enabled;
    updatePlanePickerList();
    if (m_vtkWidget) {
        if (enabled) {
            m_vtkWidget->setCursor(Qt::OpenHandCursor);
        } else {
            m_vtkWidget->unsetCursor();
        }
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
        if (m_planeDragActive) {
            if (buttons.testFlag(Qt::LeftButton)
                && !mouseEvent->modifiers().testFlag(Qt::ShiftModifier)) {
                updatePlaneDrag(mouseEvent->position());
            } else {
                endPlaneDrag();
                m_leftButtonDown = false;
                forceEndInteraction();
            }
            mouseEvent->accept();
            return true;
        }
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
        endPlaneDrag();
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
            if (!mouseEvent->modifiers().testFlag(Qt::ShiftModifier)
                && beginPlaneDrag(mouseEvent->position())) {
                m_leftButtonDown = false;
                if (m_interactorStyle) {
                    m_interactorStyle->setShiftLeftPanRequested(false);
                }
                forceEndInteraction();
                mouseEvent->accept();
                return true;
            }
            m_leftButtonDown = true;
            if (m_interactorStyle) {
                m_interactorStyle->setShiftLeftPanRequested(
                    mouseEvent->modifiers().testFlag(Qt::ShiftModifier));
            }
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
        if (m_planeDragActive && mouseEvent->button() == Qt::LeftButton) {
            endPlaneDrag();
            m_leftButtonDown = false;
            m_middleButtonDown = false;
            m_rightButtonDown = false;
            forceEndInteraction();
            mouseEvent->accept();
            return true;
        }
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
        endPlaneDrag();
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
    const bool clearingNiftiReview = m_multiStructurePreviewActive;
    removeMultiStructurePreview(false);
    if (clearingNiftiReview) {
        clearVolumeGeometry();
    }
    if (m_maskActor) {
        m_renderer->RemoveActor(m_maskActor);
        m_maskActor = nullptr;
    }
    m_intersectionMask = {};
    m_hasIntersectionMask = false;
    clearPositionPlaneMaskIntersections();
    m_volumeMaskGeometryAligned = false;
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

    double bounds[6];
    const double *frameBounds = nullptr;
    if (m_multiStructurePreviewActive) {
        const auto &ctBounds =
            m_multiStructureVolume.ctGeometry.physicalBoundsRasMm;
        std::copy(ctBounds.cbegin(), ctBounds.cend(), bounds);
        if (!boundsAreValid(bounds)) {
            if (!visibleMultiStructureBounds(bounds)) {
                qWarning()
                    << "Reset 3D camera skipped: NIfTI CT bounds are invalid.";
                return;
            }
        }
        frameBounds = bounds;
    } else {
        if (!m_maskActor || !m_maskActor->GetVisibility()) {
            qWarning() << "Reset 3D camera skipped: no visible mask actor.";
            return;
        }
        m_maskActor->GetBounds(bounds);
        frameBounds = m_hasSurfaceFrameBounds ? m_surfaceFrameBounds : bounds;
    }
    if (!boundsAreValid(bounds)) {
        qWarning() << "Reset 3D camera skipped: actor bounds are invalid.";
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

    const double centerX = (frameBounds[0] + frameBounds[1]) * 0.5;
    const double centerY = (frameBounds[2] + frameBounds[3]) * 0.5;
    const double centerZ = (frameBounds[4] + frameBounds[5]) * 0.5;

    // ResetCamera fits bounds but preserves view direction, so restore a known
    // isometric direction first to make Reset Camera visually deterministic.
    camera->SetFocalPoint(centerX, centerY, centerZ);
    camera->SetPosition(centerX + 1.0, centerY - 1.0, centerZ + 1.0);
    camera->SetViewUp(0.0, 0.0, 1.0);
    camera->OrthogonalizeViewUp();

    renderer->ResetCamera(frameBounds);
    renderer->ResetCameraClippingRange();
    renderWindow->Render();

    if constexpr (kVerbose3DLogs) {
        qDebug() << "Reset 3D camera with actor bounds:"
                 << bounds[0] << bounds[1]
                 << bounds[2] << bounds[3]
                 << bounds[4] << bounds[5];
    }
}

void Mask3DViewerWidget::clearVolumeGeometry()
{
    endPlaneDrag();
    if (m_planePicker) {
        m_planePicker->InitializePickList();
    }
    for (int axis = 0; axis < 3; ++axis) {
        const size_t plane = static_cast<size_t>(axis);
        if (m_positionPlaneFillActors[plane]) {
            m_renderer->RemoveActor(m_positionPlaneFillActors[plane]);
        }
        if (m_positionPlaneBorderActors[plane]) {
            m_renderer->RemoveActor(m_positionPlaneBorderActors[plane]);
        }
        if (m_positionPlaneIntersectionActors[plane]) {
            m_renderer->RemoveActor(m_positionPlaneIntersectionActors[plane]);
        }
        m_positionPlaneSources[plane] = nullptr;
        m_positionPlaneFillMappers[plane] = nullptr;
        m_positionPlaneFillActors[plane] = nullptr;
        m_positionPlaneIntersectionData[plane] = nullptr;
        m_positionPlaneIntersectionMappers[plane] = nullptr;
        m_positionPlaneIntersectionActors[plane] = nullptr;
        m_positionPlaneBorderPoints[plane] = nullptr;
        m_positionPlaneBorderData[plane] = nullptr;
        m_positionPlaneBorderMappers[plane] = nullptr;
        m_positionPlaneBorderActors[plane] = nullptr;
    }
    m_hasVolumeGeometry = false;
    m_volumeMaskGeometryAligned = false;
    m_niftiReviewGeometryActive = false;
}

void Mask3DViewerWidget::createPositionPlaneActors()
{
    // This is the shared dev card implementation. Axis order is
    // X/Sagittal, Y/Coronal, Z/Axial.
    static constexpr double fillOpacity[3] = {0.28, 0.26, 0.28};
    static constexpr double intersectionColors[3][3] = {
        {1.00, 0.84, 0.98},
        {0.86, 1.00, 0.88},
        {0.82, 0.96, 1.00}
    };

    for (int axis = 0; axis < 3; ++axis) {
        const size_t plane = static_cast<size_t>(axis);
        auto source = vtkSmartPointer<vtkPlaneSource>::New();
        source->SetXResolution(1);
        source->SetYResolution(1);

        auto fillMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        fillMapper->SetInputConnection(source->GetOutputPort());
        fillMapper->ScalarVisibilityOff();

        auto fillActor = vtkSmartPointer<vtkActor>::New();
        fillActor->SetMapper(fillMapper);
        fillActor->SetPickable(false);
        fillActor->SetUseBounds(false);
        fillActor->GetProperty()->SetColor(kPlaneColors[axis][0],
                                           kPlaneColors[axis][1],
                                           kPlaneColors[axis][2]);
        fillActor->GetProperty()->SetRepresentationToSurface();
        fillActor->GetProperty()->SetOpacity(fillOpacity[axis]);
        fillActor->GetProperty()->LightingOff();
        fillActor->GetProperty()->SetAmbient(1.0);
        fillActor->GetProperty()->SetDiffuse(0.0);
        fillActor->GetProperty()->SetSpecular(0.0);
        fillActor->GetProperty()->BackfaceCullingOff();
        fillActor->GetProperty()->FrontfaceCullingOff();

        auto intersectionData = vtkSmartPointer<vtkPolyData>::New();
        auto intersectionMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        intersectionMapper->SetInputData(intersectionData);
        intersectionMapper->ScalarVisibilityOff();

        auto intersectionActor = vtkSmartPointer<vtkActor>::New();
        intersectionActor->SetMapper(intersectionMapper);
        intersectionActor->SetPickable(false);
        intersectionActor->SetUseBounds(false);
        intersectionActor->GetProperty()->SetColor(intersectionColors[axis][0],
                                                   intersectionColors[axis][1],
                                                   intersectionColors[axis][2]);
        intersectionActor->GetProperty()->SetRepresentationToSurface();
        intersectionActor->GetProperty()->SetOpacity(0.96);
        intersectionActor->GetProperty()->LightingOff();
        intersectionActor->GetProperty()->SetAmbient(1.0);
        intersectionActor->GetProperty()->SetDiffuse(0.0);
        intersectionActor->GetProperty()->SetSpecular(0.0);
        intersectionActor->GetProperty()->BackfaceCullingOff();
        intersectionActor->GetProperty()->FrontfaceCullingOff();

        auto borderPoints = vtkSmartPointer<vtkPoints>::New();
        borderPoints->SetNumberOfPoints(4);

        auto borderLine = vtkSmartPointer<vtkPolyLine>::New();
        borderLine->GetPointIds()->SetNumberOfIds(5);
        for (vtkIdType point = 0; point < 4; ++point) {
            borderLine->GetPointIds()->SetId(point, point);
        }
        borderLine->GetPointIds()->SetId(4, 0);

        auto borderLines = vtkSmartPointer<vtkCellArray>::New();
        borderLines->InsertNextCell(borderLine);

        auto borderData = vtkSmartPointer<vtkPolyData>::New();
        borderData->SetPoints(borderPoints);
        borderData->SetLines(borderLines);

        auto borderMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        borderMapper->SetInputData(borderData);
        borderMapper->ScalarVisibilityOff();

        auto borderActor = vtkSmartPointer<vtkActor>::New();
        borderActor->SetMapper(borderMapper);
        borderActor->SetPickable(false);
        borderActor->SetUseBounds(false);
        borderActor->GetProperty()->SetColor(kPlaneColors[axis][0],
                                             kPlaneColors[axis][1],
                                             kPlaneColors[axis][2]);
        borderActor->GetProperty()->SetOpacity(0.95);
        borderActor->GetProperty()->SetLineWidth(kPlaneBorderWidth);
        borderActor->GetProperty()->LightingOff();

        m_positionPlaneSources[plane] = source;
        m_positionPlaneFillMappers[plane] = fillMapper;
        m_positionPlaneFillActors[plane] = fillActor;
        m_positionPlaneIntersectionData[plane] = intersectionData;
        m_positionPlaneIntersectionMappers[plane] = intersectionMapper;
        m_positionPlaneIntersectionActors[plane] = intersectionActor;
        m_positionPlaneBorderPoints[plane] = borderPoints;
        m_positionPlaneBorderData[plane] = borderData;
        m_positionPlaneBorderMappers[plane] = borderMapper;
        m_positionPlaneBorderActors[plane] = borderActor;
        m_renderer->AddActor(fillActor);
        m_renderer->AddActor(intersectionActor);
        m_renderer->AddActor(borderActor);
    }

    updateAllPositionPlaneGeometry();
    updatePositionPlaneVisibility();
    m_renderWindow->Render();
}

void Mask3DViewerWidget::setVolumeGeometry(const VolumeData &volume)
{
    // A newly configured case must not briefly reuse intersection voxels retained
    // from the previous case before its working mask is supplied.
    m_intersectionMask = {};
    m_hasIntersectionMask = false;

    const bool dimensionsValid = volume.isValid();
    const bool geometryArraysValid = volume.spacing.size() == 3
        && volume.origin.size() == 3 && volume.direction.size() == 9;
    bool geometryValuesValid = geometryArraysValid;
    if (geometryValuesValid) {
        for (int axis = 0; axis < 3; ++axis) {
            geometryValuesValid = geometryValuesValid
                && std::isfinite(volume.spacing[static_cast<size_t>(axis)])
                && volume.spacing[static_cast<size_t>(axis)] > 0.0
                && std::isfinite(volume.origin[static_cast<size_t>(axis)]);
        }
        geometryValuesValid = geometryValuesValid
            && std::all_of(volume.direction.cbegin(), volume.direction.cend(), [](double value) {
                   return std::isfinite(value);
               });
    }

    // The existing CAC marching-cubes actor is axis-aligned. Until that pipeline
    // supports direction matrices, rendering an oblique plane would imply a false
    // alignment, so keep position indicators disabled for non-identity direction.
    if (!dimensionsValid || !geometryValuesValid || !directionIsIdentity(volume.direction)) {
        clearVolumeGeometry();
        qWarning() << "3D position planes disabled: invalid geometry or a direction matrix"
                      " not supported by the existing CAC surface pipeline.";
        m_renderWindow->Render();
        return;
    }

    clearVolumeGeometry();
    m_volumeDimensions = {volume.width, volume.height, volume.depth};
    for (int axis = 0; axis < 3; ++axis) {
        m_volumeSpacing[static_cast<size_t>(axis)] = volume.spacing[static_cast<size_t>(axis)];
        m_volumeOrigin[static_cast<size_t>(axis)] = volume.origin[static_cast<size_t>(axis)];
        const int maximum = std::max(0, m_volumeDimensions[static_cast<size_t>(axis)] - 1);
        m_positionPlaneSlices[static_cast<size_t>(axis)] = std::clamp(
            m_positionPlaneSlices[static_cast<size_t>(axis)], 0, maximum);
    }
    std::copy(volume.direction.cbegin(), volume.direction.cend(), m_volumeDirection.begin());
    m_hasVolumeGeometry = true;
    m_volumeMaskGeometryAligned = false;

    createPositionPlaneActors();

    qInfo() << "Configured solid-color 3D position planes"
            << volume.width << "x" << volume.height << "x" << volume.depth
            << "spacing=" << m_volumeSpacing[0] << m_volumeSpacing[1] << m_volumeSpacing[2]
            << "origin=" << m_volumeOrigin[0] << m_volumeOrigin[1] << m_volumeOrigin[2];
}

bool Mask3DViewerWidget::setNiftiReviewVolumeGeometry(
    const NiftiVolumeGeometry &geometry,
    QString *errorMessage)
{
    if (!geometry.indexToWorldRas
        || geometry.dimensions[0] <= 0
        || geometry.dimensions[1] <= 0
        || geometry.dimensions[2] <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("NIfTI CT geometry is incomplete.");
        }
        return false;
    }

    std::array<double, 3> spacing = {0.0, 0.0, 0.0};
    std::array<double, 9> direction = {};
    for (int column = 0; column < 3; ++column) {
        double lengthSquared = 0.0;
        for (int row = 0; row < 3; ++row) {
            const double value = geometry.indexToWorldRas->GetElement(row, column);
            if (!std::isfinite(value)) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("NIfTI CT affine contains a non-finite value.");
                }
                return false;
            }
            lengthSquared += value * value;
        }
        spacing[static_cast<size_t>(column)] = std::sqrt(lengthSquared);
        if (!std::isfinite(spacing[static_cast<size_t>(column)])
            || spacing[static_cast<size_t>(column)] <= kGeometryTolerance) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("NIfTI CT affine has a degenerate axis.");
            }
            return false;
        }
        for (int row = 0; row < 3; ++row) {
            direction[static_cast<size_t>(row * 3 + column)] =
                geometry.indexToWorldRas->GetElement(row, column)
                / spacing[static_cast<size_t>(column)];
        }
    }

    for (int left = 0; left < 3; ++left) {
        for (int right = left + 1; right < 3; ++right) {
            double dot = 0.0;
            for (int row = 0; row < 3; ++row) {
                dot += direction[static_cast<size_t>(row * 3 + left)]
                    * direction[static_cast<size_t>(row * 3 + right)];
            }
            if (std::abs(dot) > 1e-4) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral(
                        "NIfTI CT affine contains shear. The dev Move Planes mathematics "
                        "requires orthogonal image axes, so review was not changed.");
                }
                return false;
            }
        }
    }

    std::array<double, 3> origin = {};
    for (int row = 0; row < 3; ++row) {
        origin[static_cast<size_t>(row)] = geometry.indexToWorldRas->GetElement(row, 3);
        if (!std::isfinite(origin[static_cast<size_t>(row)])) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("NIfTI CT affine origin is not finite.");
            }
            return false;
        }
    }

    clearVolumeGeometry();
    m_volumeDimensions = geometry.dimensions;
    m_volumeSpacing = spacing;
    m_volumeOrigin = origin;
    m_volumeDirection = direction;
    for (int axis = 0; axis < 3; ++axis) {
        const int maximum = std::max(0, m_volumeDimensions[static_cast<size_t>(axis)] - 1);
        m_positionPlaneSlices[static_cast<size_t>(axis)] = std::clamp(
            m_positionPlaneSlices[static_cast<size_t>(axis)], 0, maximum);
    }
    m_hasVolumeGeometry = true;
    m_volumeMaskGeometryAligned = true;
    m_niftiReviewGeometryActive = true;
    createPositionPlaneActors();
    qInfo() << "Configured dev position cards for native NIfTI CT"
            << m_volumeDimensions[0] << "x"
            << m_volumeDimensions[1] << "x"
            << m_volumeDimensions[2]
            << "spacing=" << m_volumeSpacing[0]
            << m_volumeSpacing[1]
            << m_volumeSpacing[2]
            << "RAS origin=" << m_volumeOrigin[0]
            << m_volumeOrigin[1]
            << m_volumeOrigin[2];
    return true;
}

std::array<double, 3> Mask3DViewerWidget::indexToWorld(
    const std::array<double, 3> &index) const
{
    std::array<double, 3> world = m_volumeOrigin;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            world[static_cast<size_t>(row)] +=
                m_volumeDirection[static_cast<size_t>(row * 3 + column)]
                * index[static_cast<size_t>(column)]
                * m_volumeSpacing[static_cast<size_t>(column)];
        }
    }
    return world;
}

void Mask3DViewerWidget::setPositionPlaneSlice(int axis, int index)
{
    if (axis < 0 || axis > 2) {
        return;
    }
    const size_t plane = static_cast<size_t>(axis);
    const int maximum = std::max(0, m_volumeDimensions[plane] - 1);
    m_positionPlaneSlices[plane] = std::clamp(index, 0, maximum);
    if (m_positionPlaneSources[plane]) {
        updatePositionPlaneGeometry(axis);
        if (m_multiStructurePreviewActive) {
            updatePositionPlaneCategoricalIntersections(axis);
        } else {
            updatePositionPlaneMaskIntersection(axis);
        }
        m_renderWindow->Render();
    }
}

void Mask3DViewerWidget::setAxialSlice(int index)
{
    setPositionPlaneSlice(2, index);
}

void Mask3DViewerWidget::setCoronalSlice(int index)
{
    setPositionPlaneSlice(1, index);
}

void Mask3DViewerWidget::setSagittalSlice(int index)
{
    setPositionPlaneSlice(0, index);
}

void Mask3DViewerWidget::setPositionPlaneVisible(int axis, bool visible)
{
    if (axis < 0 || axis > 2) {
        return;
    }
    if (!visible && m_planeDragActive && draggedPlaneAxis() == axis) {
        endPlaneDrag();
    }
    m_positionPlaneVisibilityRequested[static_cast<size_t>(axis)] = visible;
    updatePositionPlaneVisibility();
    m_renderWindow->Render();
}

void Mask3DViewerWidget::setAxialPlaneVisible(bool visible)
{
    setPositionPlaneVisible(2, visible);
}

void Mask3DViewerWidget::setCoronalPlaneVisible(bool visible)
{
    setPositionPlaneVisible(1, visible);
}

void Mask3DViewerWidget::setSagittalPlaneVisible(bool visible)
{
    setPositionPlaneVisible(0, visible);
}

void Mask3DViewerWidget::updatePositionPlaneGeometry(int axis)
{
    if (axis < 0 || axis > 2 || !m_hasVolumeGeometry
        || (!m_niftiReviewGeometryActive && !m_hasSurfaceFrameBounds)) {
        return;
    }
    vtkPlaneSource *source = m_positionPlaneSources[static_cast<size_t>(axis)];
    vtkPoints *borderPoints = m_positionPlaneBorderPoints[static_cast<size_t>(axis)];
    vtkPolyData *borderData = m_positionPlaneBorderData[static_cast<size_t>(axis)];
    if (!source || !borderPoints || !borderData) {
        return;
    }

    PlaneCorners corners = {};

    if (m_niftiReviewGeometryActive) {
        const double slice = static_cast<double>(
            m_positionPlaneSlices[static_cast<size_t>(axis)]);
        PlaneCorners indexCorners = {};
        switch (axis) {
        case 0:
            indexCorners = {{{slice, -0.5, -0.5},
                             {slice, m_volumeDimensions[1] - 0.5, -0.5},
                             {slice, m_volumeDimensions[1] - 0.5,
                                     m_volumeDimensions[2] - 0.5},
                             {slice, -0.5, m_volumeDimensions[2] - 0.5}}};
            break;
        case 1:
            indexCorners = {{{-0.5, slice, -0.5},
                             {m_volumeDimensions[0] - 0.5, slice, -0.5},
                             {m_volumeDimensions[0] - 0.5, slice,
                                     m_volumeDimensions[2] - 0.5},
                             {-0.5, slice, m_volumeDimensions[2] - 0.5}}};
            break;
        case 2:
            indexCorners = {{{-0.5, -0.5, slice},
                             {m_volumeDimensions[0] - 0.5, -0.5, slice},
                             {m_volumeDimensions[0] - 0.5,
                                     m_volumeDimensions[1] - 0.5, slice},
                             {-0.5, m_volumeDimensions[1] - 0.5, slice}}};
            break;
        }
        for (size_t corner = 0; corner < corners.size(); ++corner) {
            corners[corner] = indexToWorld(indexCorners[corner]);
        }
    } else {
        std::array<double, 3> sliceIndex = {0.0, 0.0, 0.0};
        sliceIndex[static_cast<size_t>(axis)] =
            static_cast<double>(m_positionPlaneSlices[static_cast<size_t>(axis)]);
        const std::array<double, 3> sliceWorld = indexToWorld(sliceIndex);
        switch (axis) {
        case 0: // Sagittal: fixed world X, spans Y/Z guide bounds.
            corners = {{{sliceWorld[0], m_surfaceFrameBounds[2], m_surfaceFrameBounds[4]},
                        {sliceWorld[0], m_surfaceFrameBounds[3], m_surfaceFrameBounds[4]},
                        {sliceWorld[0], m_surfaceFrameBounds[3], m_surfaceFrameBounds[5]},
                        {sliceWorld[0], m_surfaceFrameBounds[2], m_surfaceFrameBounds[5]}}};
            break;
        case 1: // Coronal: fixed world Y, spans X/Z guide bounds.
            corners = {{{m_surfaceFrameBounds[0], sliceWorld[1], m_surfaceFrameBounds[4]},
                        {m_surfaceFrameBounds[1], sliceWorld[1], m_surfaceFrameBounds[4]},
                        {m_surfaceFrameBounds[1], sliceWorld[1], m_surfaceFrameBounds[5]},
                        {m_surfaceFrameBounds[0], sliceWorld[1], m_surfaceFrameBounds[5]}}};
            break;
        case 2: // Axial: fixed world Z, spans X/Y guide bounds.
            corners = {{{m_surfaceFrameBounds[0], m_surfaceFrameBounds[2], sliceWorld[2]},
                        {m_surfaceFrameBounds[1], m_surfaceFrameBounds[2], sliceWorld[2]},
                        {m_surfaceFrameBounds[1], m_surfaceFrameBounds[3], sliceWorld[2]},
                        {m_surfaceFrameBounds[0], m_surfaceFrameBounds[3], sliceWorld[2]}}};
            break;
        }
    }

    logPositionPlaneState("before update",
                          axis,
                          m_positionPlaneSlices[static_cast<size_t>(axis)],
                          source,
                          m_positionPlaneFillMappers[static_cast<size_t>(axis)],
                          m_positionPlaneFillActors[static_cast<size_t>(axis)],
                          borderData,
                          corners);

    source->SetOrigin(corners[0].data());
    source->SetPoint1(corners[1].data());
    source->SetPoint2(corners[3].data());
    source->Modified();
    source->Update();

    for (vtkIdType corner = 0; corner < 4; ++corner) {
        borderPoints->SetPoint(corner, corners[static_cast<size_t>(corner)].data());
    }
    borderPoints->Modified();
    borderData->Modified();

    logPositionPlaneState("after update",
                          axis,
                          m_positionPlaneSlices[static_cast<size_t>(axis)],
                          source,
                          m_positionPlaneFillMappers[static_cast<size_t>(axis)],
                          m_positionPlaneFillActors[static_cast<size_t>(axis)],
                          borderData,
                          corners);
}

void Mask3DViewerWidget::updateAllPositionPlaneGeometry()
{
    for (int axis = 0; axis < 3; ++axis) {
        updatePositionPlaneGeometry(axis);
    }
}

void Mask3DViewerWidget::clearPositionPlaneMaskIntersections()
{
    for (vtkSmartPointer<vtkPolyData> &data : m_positionPlaneIntersectionData) {
        if (!data) {
            continue;
        }
        data->SetPoints(nullptr);
        data->SetPolys(nullptr);
        data->Modified();
    }
}

void Mask3DViewerWidget::updatePositionPlaneMaskIntersection(int axis)
{
    if (axis < 0 || axis > 2) {
        return;
    }

    vtkPolyData *intersectionData =
        m_positionPlaneIntersectionData[static_cast<size_t>(axis)];
    if (!intersectionData) {
        return;
    }

    auto points = vtkSmartPointer<vtkPoints>::New();
    auto quads = vtkSmartPointer<vtkCellArray>::New();
    if (!m_hasVolumeGeometry || !m_hasSurfaceFrameBounds || !m_hasIntersectionMask
        || !volumeGeometryMatchesMask(m_intersectionMask)) {
        intersectionData->SetPoints(points);
        intersectionData->SetPolys(quads);
        intersectionData->Modified();
        return;
    }

    const int slice = m_positionPlaneSlices[static_cast<size_t>(axis)];
    const double visualOffset = std::max(1e-5,
        *std::min_element(m_volumeSpacing.cbegin(), m_volumeSpacing.cend()) * 0.01);
    const std::array<double, 3> normal = {
        m_volumeDirection[static_cast<size_t>(axis)],
        m_volumeDirection[static_cast<size_t>(3 + axis)],
        m_volumeDirection[static_cast<size_t>(6 + axis)]
    };

    auto appendVoxelPatch = [&](int x, int y, int z) {
        std::array<std::array<double, 3>, 4> indices = {};
        switch (axis) {
        case 0: // Sagittal: fixed X, one Y/Z voxel cell.
            indices = {{{static_cast<double>(x), y - 0.5, z - 0.5},
                        {static_cast<double>(x), y + 0.5, z - 0.5},
                        {static_cast<double>(x), y + 0.5, z + 0.5},
                        {static_cast<double>(x), y - 0.5, z + 0.5}}};
            break;
        case 1: // Coronal: fixed Y, one X/Z voxel cell.
            indices = {{{x - 0.5, static_cast<double>(y), z - 0.5},
                        {x + 0.5, static_cast<double>(y), z - 0.5},
                        {x + 0.5, static_cast<double>(y), z + 0.5},
                        {x - 0.5, static_cast<double>(y), z + 0.5}}};
            break;
        case 2: // Axial: fixed Z, one X/Y voxel cell.
            indices = {{{x - 0.5, y - 0.5, static_cast<double>(z)},
                        {x + 0.5, y - 0.5, static_cast<double>(z)},
                        {x + 0.5, y + 0.5, static_cast<double>(z)},
                        {x - 0.5, y + 0.5, static_cast<double>(z)}}};
            break;
        }

        std::array<std::array<double, 3>, 4> worldCorners = {};
        for (size_t corner = 0; corner < worldCorners.size(); ++corner) {
            worldCorners[corner] = indexToWorld(indices[corner]);
        }

        for (int worldAxis = 0; worldAxis < 3; ++worldAxis) {
            if (worldAxis == axis) {
                continue;
            }
            const double lower = m_surfaceFrameBounds[worldAxis * 2];
            const double upper = m_surfaceFrameBounds[worldAxis * 2 + 1];
            double patchLower = worldCorners[0][static_cast<size_t>(worldAxis)];
            double patchUpper = patchLower;
            for (size_t corner = 1; corner < worldCorners.size(); ++corner) {
                patchLower = std::min(patchLower, worldCorners[corner][static_cast<size_t>(worldAxis)]);
                patchUpper = std::max(patchUpper, worldCorners[corner][static_cast<size_t>(worldAxis)]);
            }
            if (patchUpper <= lower || patchLower >= upper) {
                return;
            }
            for (auto &corner : worldCorners) {
                corner[static_cast<size_t>(worldAxis)] = std::clamp(
                    corner[static_cast<size_t>(worldAxis)], lower, upper);
            }
        }

        vtkIdType pointIds[4] = {};
        for (size_t corner = 0; corner < worldCorners.size(); ++corner) {
            for (int component = 0; component < 3; ++component) {
                worldCorners[corner][static_cast<size_t>(component)] +=
                    normal[static_cast<size_t>(component)] * visualOffset;
            }
            pointIds[corner] = points->InsertNextPoint(worldCorners[corner].data());
        }
        quads->InsertNextCell(4, pointIds);
    };

    switch (axis) {
    case 0:
        for (int z = 0; z < m_intersectionMask.depth; ++z) {
            for (int y = 0; y < m_intersectionMask.height; ++y) {
                if (m_intersectionMask.value(slice, y, z) != 0) {
                    appendVoxelPatch(slice, y, z);
                }
            }
        }
        break;
    case 1:
        for (int z = 0; z < m_intersectionMask.depth; ++z) {
            for (int x = 0; x < m_intersectionMask.width; ++x) {
                if (m_intersectionMask.value(x, slice, z) != 0) {
                    appendVoxelPatch(x, slice, z);
                }
            }
        }
        break;
    case 2:
        for (int y = 0; y < m_intersectionMask.height; ++y) {
            for (int x = 0; x < m_intersectionMask.width; ++x) {
                if (m_intersectionMask.value(x, y, slice) != 0) {
                    appendVoxelPatch(x, y, slice);
                }
            }
        }
        break;
    }

    intersectionData->SetPoints(points);
    intersectionData->SetPolys(quads);
    intersectionData->Modified();

    if constexpr (kVerbosePlaneRenderingLogs) {
        qDebug() << planeName(axis) << "workingMask intersection slice=" << slice
                 << "foreground patches=" << quads->GetNumberOfCells();
    }
}

void Mask3DViewerWidget::updateAllPositionPlaneMaskIntersections()
{
    for (int axis = 0; axis < 3; ++axis) {
        updatePositionPlaneMaskIntersection(axis);
    }
}

int Mask3DViewerWidget::categoricalValueAtCtIndex(
    const std::array<double, 3> &ctIndex) const
{
    if (!m_multiStructurePreviewActive
        || !m_multiStructureVolume.segmentationImage
        || !m_multiStructureVolume.ctIndexToSegmentationIndex) {
        return 0;
    }

    const double input[4] = {ctIndex[0], ctIndex[1], ctIndex[2], 1.0};
    double segmentationIndex[4] = {};
    m_multiStructureVolume.ctIndexToSegmentationIndex->MultiplyPoint(
        input, segmentationIndex);
    const int x = static_cast<int>(std::lround(segmentationIndex[0]));
    const int y = static_cast<int>(std::lround(segmentationIndex[1]));
    const int z = static_cast<int>(std::lround(segmentationIndex[2]));
    const auto dimensions = m_multiStructureVolume.segmentationGeometry.dimensions;
    if (x < 0 || y < 0 || z < 0
        || x >= dimensions[0] || y >= dimensions[1] || z >= dimensions[2]) {
        return 0;
    }
    return static_cast<int>(std::lround(
        m_multiStructureVolume.segmentationImage->GetScalarComponentAsDouble(
            x, y, z, 0)));
}

void Mask3DViewerWidget::updatePositionPlaneCategoricalIntersections(int axis)
{
    if (axis < 0 || axis > 2) {
        return;
    }

    const size_t plane = static_cast<size_t>(axis);
    std::vector<vtkSmartPointer<vtkPoints>> pointsByLabel;
    std::vector<vtkSmartPointer<vtkCellArray>> quadsByLabel;
    pointsByLabel.reserve(m_multiStructureSurfaces.size());
    quadsByLabel.reserve(m_multiStructureSurfaces.size());
    for (MultiStructureSurface &surface : m_multiStructureSurfaces) {
        pointsByLabel.push_back(vtkSmartPointer<vtkPoints>::New());
        quadsByLabel.push_back(vtkSmartPointer<vtkCellArray>::New());
        if (surface.cardIntersectionData[plane]) {
            surface.cardIntersectionData[plane]->SetPoints(pointsByLabel.back());
            surface.cardIntersectionData[plane]->SetPolys(quadsByLabel.back());
            surface.cardIntersectionData[plane]->Modified();
        }
    }

    if (!m_multiStructurePreviewActive || !m_niftiReviewGeometryActive
        || !m_hasVolumeGeometry || m_multiStructureSurfaces.empty()) {
        return;
    }

    const int slice = m_positionPlaneSlices[plane];
    const int firstInPlaneAxis = axis == 0 ? 1 : 0;
    const int secondInPlaneAxis = axis == 2 ? 1 : 2;
    const double visualOffset = std::max(
        1e-5, m_volumeSpacing[plane] * 0.012);
    std::array<double, 3> normal = {
        m_volumeDirection[plane],
        m_volumeDirection[3 + plane],
        m_volumeDirection[6 + plane]
    };

    for (int second = 0;
         second < m_volumeDimensions[static_cast<size_t>(secondInPlaneAxis)];
         ++second) {
        for (int first = 0;
             first < m_volumeDimensions[static_cast<size_t>(firstInPlaneAxis)];
             ++first) {
            std::array<double, 3> center = {0.0, 0.0, 0.0};
            center[plane] = static_cast<double>(slice);
            center[static_cast<size_t>(firstInPlaneAxis)] =
                static_cast<double>(first);
            center[static_cast<size_t>(secondInPlaneAxis)] =
                static_cast<double>(second);
            const int labelValue = categoricalValueAtCtIndex(center);
            const auto surfaceIt = std::find_if(
                m_multiStructureSurfaces.cbegin(),
                m_multiStructureSurfaces.cend(),
                [labelValue](const MultiStructureSurface &surface) {
                    return surface.label.value == labelValue;
                });
            if (surfaceIt == m_multiStructureSurfaces.cend()) {
                continue;
            }
            const size_t labelIndex = static_cast<size_t>(
                std::distance(m_multiStructureSurfaces.cbegin(), surfaceIt));

            std::array<std::array<double, 3>, 4> cornerIndices = {
                center, center, center, center
            };
            cornerIndices[0][static_cast<size_t>(firstInPlaneAxis)] -= 0.5;
            cornerIndices[0][static_cast<size_t>(secondInPlaneAxis)] -= 0.5;
            cornerIndices[1][static_cast<size_t>(firstInPlaneAxis)] += 0.5;
            cornerIndices[1][static_cast<size_t>(secondInPlaneAxis)] -= 0.5;
            cornerIndices[2][static_cast<size_t>(firstInPlaneAxis)] += 0.5;
            cornerIndices[2][static_cast<size_t>(secondInPlaneAxis)] += 0.5;
            cornerIndices[3][static_cast<size_t>(firstInPlaneAxis)] -= 0.5;
            cornerIndices[3][static_cast<size_t>(secondInPlaneAxis)] += 0.5;

            vtkIdType pointIds[4] = {};
            for (size_t corner = 0; corner < cornerIndices.size(); ++corner) {
                std::array<double, 3> world = indexToWorld(cornerIndices[corner]);
                for (int component = 0; component < 3; ++component) {
                    world[static_cast<size_t>(component)] +=
                        normal[static_cast<size_t>(component)] * visualOffset;
                }
                pointIds[corner] =
                    pointsByLabel[labelIndex]->InsertNextPoint(world.data());
            }
            quadsByLabel[labelIndex]->InsertNextCell(4, pointIds);
        }
    }

    for (size_t labelIndex = 0;
         labelIndex < m_multiStructureSurfaces.size();
         ++labelIndex) {
        vtkPolyData *data =
            m_multiStructureSurfaces[labelIndex].cardIntersectionData[plane];
        if (data) {
            data->SetPoints(pointsByLabel[labelIndex]);
            data->SetPolys(quadsByLabel[labelIndex]);
            data->Modified();
        }
    }
}

void Mask3DViewerWidget::updateAllPositionPlaneCategoricalIntersections()
{
    for (int axis = 0; axis < 3; ++axis) {
        updatePositionPlaneCategoricalIntersections(axis);
    }
}

void Mask3DViewerWidget::updateMaskIntersections(const MaskVolume &mask,
                                                 bool updateAxial,
                                                 bool updateCoronal,
                                                 bool updateSagittal)
{
    if (!volumeGeometryMatchesMask(mask)) {
        qWarning() << "3D workingMask intersection update skipped: mask geometry mismatch.";
        m_intersectionMask = {};
        m_hasIntersectionMask = false;
        clearPositionPlaneMaskIntersections();
        m_renderWindow->Render();
        return;
    }

    m_intersectionMask = mask;
    m_hasIntersectionMask = true;
    if (updateSagittal) {
        updatePositionPlaneMaskIntersection(0);
    }
    if (updateCoronal) {
        updatePositionPlaneMaskIntersection(1);
    }
    if (updateAxial) {
        updatePositionPlaneMaskIntersection(2);
    }
    if (updateAxial || updateCoronal || updateSagittal) {
        m_renderWindow->Render();
    }
}

void Mask3DViewerWidget::updatePositionPlaneVisibility()
{
    const bool geometryReady = m_hasVolumeGeometry && m_volumeMaskGeometryAligned
        && (m_niftiReviewGeometryActive || m_hasSurfaceFrameBounds);
    for (int axis = 0; axis < 3; ++axis) {
        const size_t plane = static_cast<size_t>(axis);
        const bool visible = geometryReady && m_positionPlaneVisibilityRequested[plane];
        if (m_positionPlaneFillActors[plane]) {
            m_positionPlaneFillActors[plane]->SetVisibility(visible);
        }
        if (m_positionPlaneIntersectionActors[plane]) {
            m_positionPlaneIntersectionActors[plane]->SetVisibility(
                visible && !m_multiStructurePreviewActive);
        }
        if (m_positionPlaneBorderActors[plane]) {
            m_positionPlaneBorderActors[plane]->SetVisibility(visible);
        }
        for (MultiStructureSurface &surface : m_multiStructureSurfaces) {
            vtkActor *categoricalActor = surface.cardIntersectionActors[plane];
            if (categoricalActor) {
                const bool labelVisible = surface.actor && surface.actor->GetVisibility();
                categoricalActor->SetVisibility(
                    visible && m_multiStructurePreviewActive && labelVisible);
            }
        }
    }
    updatePlanePickerList();
}

bool Mask3DViewerWidget::volumeGeometryMatchesMask(const MaskVolume &mask) const
{
    if (!m_hasVolumeGeometry || !mask.isValid()
        || mask.width != m_volumeDimensions[0]
        || mask.height != m_volumeDimensions[1]
        || mask.depth != m_volumeDimensions[2]
        || mask.spacing.size() != 3
        || mask.origin.size() != 3
        || !directionIsIdentity(mask.direction)) {
        return false;
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(mask.spacing[static_cast<size_t>(axis)])
            || !std::isfinite(mask.origin[static_cast<size_t>(axis)])
            || !nearlyEqual(mask.spacing[static_cast<size_t>(axis)], m_volumeSpacing[static_cast<size_t>(axis)])
            || !nearlyEqual(mask.origin[static_cast<size_t>(axis)], m_volumeOrigin[static_cast<size_t>(axis)])) {
            return false;
        }
    }
    for (int index = 0; index < 9; ++index) {
        if (!nearlyEqual(mask.direction[static_cast<size_t>(index)],
                         m_volumeDirection[static_cast<size_t>(index)])) {
            return false;
        }
    }
    return true;
}

void Mask3DViewerWidget::setMaskVolume(const MaskVolume &mask)
{
    refreshFromMask(mask);
}

void Mask3DViewerWidget::setSurfaceOpacity(double opacity)
{
    m_surfaceOpacity = std::clamp(opacity, 0.0, 1.0);
    if (m_maskActor) {
        m_maskActor->GetProperty()->SetOpacity(m_surfaceOpacity);
        m_renderWindow->Render();
    }
}

double Mask3DViewerWidget::surfaceOpacity() const
{
    return m_surfaceOpacity;
}

void Mask3DViewerWidget::refreshFromMask(const MaskVolume &mask)
{
    const bool returningFromMultiStructurePreview = m_multiStructurePreviewActive;
    removeMultiStructurePreview(false);
    if (returningFromMultiStructurePreview) {
        clearVolumeGeometry();
    }
    if (!mask.isValid()) {
        qWarning() << "Mask3DViewerWidget: invalid mask; clearing 3D surface.";
        clear();
        return;
    }

    m_volumeMaskGeometryAligned = volumeGeometryMatchesMask(mask);
    m_intersectionMask = mask;
    m_hasIntersectionMask = m_volumeMaskGeometryAligned;
    if (m_hasVolumeGeometry && !m_volumeMaskGeometryAligned) {
        qWarning() << "3D position planes disabled: volume and mask physical geometry do not match,"
                      " or direction is not identity.";
    }
    updateOrientationLabels(mask);

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

    vtkPolyData *surface = surfaceExtractor->GetOutput();
    if (!surface || surface->GetNumberOfPoints() == 0 || surface->GetNumberOfCells() == 0) {
        qWarning() << "Mask3DViewerWidget: extracted surface is empty; clearing 3D surface.";
        clear();
        return;
    }
    double surfaceBounds[6];
    surface->GetBounds(surfaceBounds);
    if (!boundsAreValid(surfaceBounds)) {
        qWarning() << "Mask3DViewerWidget: extracted surface bounds are invalid; clearing 3D surface.";
        clear();
        return;
    }

    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputConnection(surfaceExtractor->GetOutputPort());
    mapper->ScalarVisibilityOff();

    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(1.0, 0.72, 0.05);
    actor->GetProperty()->SetOpacity(m_surfaceOpacity);
    actor->GetProperty()->SetSpecular(0.18);
    actor->GetProperty()->SetSpecularPower(18.0);

    if (m_maskActor) {
        m_renderer->RemoveActor(m_maskActor);
    }
    m_maskActor = actor;
    m_renderer->AddActor(m_maskActor);
    updateModelBoundsGuide(surfaceBounds);

    const bool firstRenderedMask = !m_hasRenderedMask || returningFromMultiStructurePreview;
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

bool Mask3DViewerWidget::loadMultiStructurePreview(const QString &ctPath,
                                                    const QString &segmentationPath,
                                                    QString *errorMessage)
{
    MultiStructureVolume loadedVolume;
    MultiStructureNiftiLoader loader;
    if (!loader.load(ctPath, segmentationPath, &loadedVolume, errorMessage)) {
        return false;
    }
    return setMultiStructurePreview(loadedVolume, errorMessage);
}

bool Mask3DViewerWidget::setMultiStructurePreview(
    const MultiStructureVolume &volume,
    QString *errorMessage)
{
    if (!volume.isValid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The NIfTI review volume is incomplete.");
        }
        return false;
    }
    MultiStructureVolume loadedVolume = volume;
    std::vector<MultiStructureSurface> extractedSurfaces;
    extractedSurfaces.reserve(loadedVolume.labels.size());
    for (const MultiStructureLabelInfo &label : loadedVolume.labels) {
        QElapsedTimer extractionTimer;
        extractionTimer.start();

        vtkNew<vtkExtractVOI> labelRegion;
        labelRegion->SetInputData(loadedVolume.segmentationImage);
        const auto dimensions = loadedVolume.segmentationGeometry.dimensions;
        labelRegion->SetVOI(
            std::max(0, label.indexBounds[0] - 1),
            std::min(dimensions[0] - 1, label.indexBounds[1] + 1),
            std::max(0, label.indexBounds[2] - 1),
            std::min(dimensions[1] - 1, label.indexBounds[3] + 1),
            std::max(0, label.indexBounds[4] - 1),
            std::min(dimensions[2] - 1, label.indexBounds[5] + 1));

        vtkNew<vtkDiscreteFlyingEdges3D> extractor;
        extractor->SetInputConnection(labelRegion->GetOutputPort());
        extractor->SetValue(0, static_cast<double>(label.value));
        extractor->Update();

        vtkPolyData *output = extractor->GetOutput();
        if (!output || output->GetNumberOfPoints() == 0 || output->GetNumberOfCells() == 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Surface extraction produced no geometry for %1.")
                                    .arg(label.displayName);
            }
            return false;
        }

        double localBounds[6] = {};
        output->GetBounds(localBounds);
        if (!boundsAreValid(localBounds)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Surface extraction produced invalid bounds for %1.")
                                    .arg(label.displayName);
            }
            return false;
        }
        double point[3] = {};
        for (vtkIdType pointId = 0; pointId < output->GetNumberOfPoints(); ++pointId) {
            output->GetPoint(pointId, point);
            if (!std::isfinite(point[0]) || !std::isfinite(point[1]) || !std::isfinite(point[2])) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Surface extraction produced non-finite coordinates for %1.")
                                        .arg(label.displayName);
                }
                return false;
            }
        }

        MultiStructureSurface surface;
        surface.label = label;
        surface.polyData = vtkSmartPointer<vtkPolyData>::New();
        surface.polyData->ShallowCopy(output);
        surface.mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        surface.mapper->SetInputData(surface.polyData);
        surface.mapper->ScalarVisibilityOff();
        surface.actor = vtkSmartPointer<vtkActor>::New();
        surface.actor->SetMapper(surface.mapper);
        surface.actor->SetUserMatrix(loadedVolume.segmentationGeometry.dataToWorldRas);
        surface.actor->SetPickable(false);
        surface.actor->GetProperty()->SetColor(label.color[0], label.color[1], label.color[2]);
        surface.actor->GetProperty()->SetOpacity(label.defaultOpacity);
        surface.actor->GetProperty()->SetSpecular(0.15);
        surface.actor->GetProperty()->SetSpecularPower(16.0);
        for (int axis = 0; axis < 3; ++axis) {
            const size_t plane = static_cast<size_t>(axis);
            surface.cardIntersectionData[plane] =
                vtkSmartPointer<vtkPolyData>::New();
            surface.cardIntersectionMappers[plane] =
                vtkSmartPointer<vtkPolyDataMapper>::New();
            surface.cardIntersectionMappers[plane]->SetInputData(
                surface.cardIntersectionData[plane]);
            surface.cardIntersectionMappers[plane]->ScalarVisibilityOff();
            surface.cardIntersectionActors[plane] =
                vtkSmartPointer<vtkActor>::New();
            surface.cardIntersectionActors[plane]->SetMapper(
                surface.cardIntersectionMappers[plane]);
            surface.cardIntersectionActors[plane]->SetPickable(false);
            surface.cardIntersectionActors[plane]->SetUseBounds(false);
            surface.cardIntersectionActors[plane]->GetProperty()->SetColor(
                label.color[0], label.color[1], label.color[2]);
            surface.cardIntersectionActors[plane]->GetProperty()
                ->SetRepresentationToSurface();
            surface.cardIntersectionActors[plane]->GetProperty()->SetOpacity(
                label.defaultOpacity);
            surface.cardIntersectionActors[plane]->GetProperty()->LightingOff();
            surface.cardIntersectionActors[plane]->GetProperty()->SetAmbient(1.0);
            surface.cardIntersectionActors[plane]->GetProperty()->SetDiffuse(0.0);
            surface.cardIntersectionActors[plane]->GetProperty()->SetSpecular(0.0);
            surface.cardIntersectionActors[plane]->GetProperty()->BackfaceCullingOff();
            surface.cardIntersectionActors[plane]->GetProperty()->FrontfaceCullingOff();
        }
        surface.extractionMilliseconds = extractionTimer.elapsed();

        double worldBounds[6] = {};
        surface.actor->GetBounds(worldBounds);
        if (!boundsAreValid(worldBounds)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Physical transform produced invalid world bounds for %1.")
                                    .arg(label.displayName);
            }
            return false;
        }

        qInfo() << "Extracted multi-structure surface"
                << label.displayName
                << "value=" << label.value
                << "points=" << surface.polyData->GetNumberOfPoints()
                << "cells=" << surface.polyData->GetNumberOfCells()
                << "time ms=" << surface.extractionMilliseconds
                << "world RAS bounds mm="
                << worldBounds[0] << worldBounds[1]
                << worldBounds[2] << worldBounds[3]
                << worldBounds[4] << worldBounds[5];
        extractedSurfaces.push_back(std::move(surface));
    }

    endPlaneDrag();
    if (!setNiftiReviewVolumeGeometry(loadedVolume.ctGeometry, errorMessage)) {
        return false;
    }
    removeMultiStructurePreview(false);
    if (m_maskActor) {
        m_maskActor->SetVisibility(false);
    }
    if (m_boundsActor) {
        m_boundsActor->SetVisibility(false);
    }
    m_multiStructureVolume = std::move(loadedVolume);
    m_multiStructureSurfaces = std::move(extractedSurfaces);
    m_multiStructurePreviewActive = true;
    for (const MultiStructureSurface &surface : m_multiStructureSurfaces) {
        m_renderer->AddActor(surface.actor);
        for (vtkActor *intersectionActor : surface.cardIntersectionActors) {
            if (intersectionActor) {
                m_renderer->AddActor(intersectionActor);
            }
        }
    }
    updateOrientationLabelsForRasWorld();
    updateMultiStructureBoundsGuide();
    updateAllPositionPlaneGeometry();
    updateAllPositionPlaneCategoricalIntersections();
    updatePositionPlaneVisibility();
    resetCamera();
    return true;
}

bool Mask3DViewerWidget::isMultiStructurePreviewActive() const
{
    return m_multiStructurePreviewActive;
}

std::vector<Mask3DViewerWidget::MultiStructureSurfaceInfo>
Mask3DViewerWidget::multiStructureSurfaces() const
{
    std::vector<MultiStructureSurfaceInfo> surfaceInfos;
    surfaceInfos.reserve(m_multiStructureSurfaces.size());
    for (const MultiStructureSurface &surface : m_multiStructureSurfaces) {
        MultiStructureSurfaceInfo info;
        info.labelValue = surface.label.value;
        info.displayName = surface.label.displayName;
        info.voxelCount = surface.label.voxelCount;
        info.pointCount = surface.polyData ? surface.polyData->GetNumberOfPoints() : 0;
        info.cellCount = surface.polyData ? surface.polyData->GetNumberOfCells() : 0;
        info.extractionMilliseconds = surface.extractionMilliseconds;
        info.color = surface.label.color;
        info.opacity = surface.actor ? surface.actor->GetProperty()->GetOpacity() : 0.0;
        info.visible = surface.actor && surface.actor->GetVisibility();
        surfaceInfos.push_back(info);
    }
    return surfaceInfos;
}

void Mask3DViewerWidget::setMultiStructureVisible(int labelValue, bool visible)
{
    for (MultiStructureSurface &surface : m_multiStructureSurfaces) {
        if (surface.label.value == labelValue && surface.actor) {
            surface.actor->SetVisibility(visible);
            for (vtkActor *intersectionActor : surface.cardIntersectionActors) {
                if (intersectionActor) {
                    intersectionActor->SetVisibility(visible);
                }
            }
            updateMultiStructureBoundsGuide();
            updatePositionPlaneVisibility();
            m_renderer->ResetCameraClippingRange();
            m_renderWindow->Render();
            return;
        }
    }
}

void Mask3DViewerWidget::setMultiStructureOpacity(int labelValue, double opacity)
{
    const double clampedOpacity = std::clamp(opacity, 0.0, 1.0);
    for (MultiStructureSurface &surface : m_multiStructureSurfaces) {
        if (surface.label.value == labelValue && surface.actor) {
            surface.actor->GetProperty()->SetOpacity(clampedOpacity);
            for (vtkActor *intersectionActor :
                 surface.cardIntersectionActors) {
                if (intersectionActor) {
                    intersectionActor->GetProperty()->SetOpacity(
                        clampedOpacity);
                }
            }
            m_renderWindow->Render();
            return;
        }
    }
}

void Mask3DViewerWidget::setNiftiCenterline(
    const std::vector<std::array<double, 3>> &pointsLpsMm)
{
    setNiftiCenterlineCandidates({pointsLpsMm}, 0, false);
}

bool Mask3DViewerWidget::setNiftiCenterlineCandidates(
    const std::vector<std::vector<std::array<double, 3>>> &pathsLpsMm,
    int selectedIndex,
    bool showAllCandidates)
{
    clearNiftiCenterline();
    if (!m_multiStructurePreviewActive || pathsLpsMm.empty()) {
        return false;
    }
    m_niftiCandidatePathsLpsMm = pathsLpsMm;
    m_showAllNiftiCandidates = showAllCandidates;
    for (const auto &path : pathsLpsMm) {
        if (path.size() < 2) {
            m_niftiCandidateCenterlineActors.push_back(nullptr);
            continue;
        }
        vtkNew<vtkPoints> points;
        for (const auto &pointLps : path) {
            points->InsertNextPoint(
                -pointLps[0], -pointLps[1], pointLps[2]);
        }
        vtkNew<vtkPolyLine> line;
        line->GetPointIds()->SetNumberOfIds(
            static_cast<vtkIdType>(path.size()));
        for (vtkIdType index = 0;
             index < static_cast<vtkIdType>(path.size());
             ++index) {
            line->GetPointIds()->SetId(index, index);
        }
        vtkNew<vtkCellArray> lines;
        lines->InsertNextCell(line);
        vtkNew<vtkPolyData> data;
        data->SetPoints(points);
        data->SetLines(lines);
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(data);
        mapper->ScalarVisibilityOff();
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->SetPickable(false);
        actor->SetUseBounds(false);
        actor->GetProperty()->SetColor(0.45, 0.65, 0.72);
        actor->GetProperty()->SetOpacity(0.38);
        actor->GetProperty()->SetLineWidth(1.2);
        actor->GetProperty()->LightingOff();
        actor->SetVisibility(showAllCandidates);
        m_renderer->AddActor(actor);
        m_niftiCandidateCenterlineActors.push_back(actor);
    }
    m_selectedNiftiCandidateIndex = selectedIndex;
    const bool selected = rebuildNiftiSelectedCenterline();
    m_renderWindow->Render();
    return selected;
}

bool Mask3DViewerWidget::selectNiftiCenterlineCandidate(
    int selectedIndex)
{
    if (selectedIndex < 0
        || static_cast<size_t>(selectedIndex)
            >= m_niftiCandidatePathsLpsMm.size()) {
        clearNiftiSelectedCenterline();
        m_selectedNiftiCandidateIndex = -1;
        m_renderWindow->Render();
        return false;
    }
    clearNiftiSelectedCenterline();
    m_selectedNiftiCandidateIndex = selectedIndex;
    const bool selected = rebuildNiftiSelectedCenterline();
    m_renderWindow->Render();
    return selected;
}

void Mask3DViewerWidget::setAllNiftiCenterlineCandidatesVisible(
    bool visible)
{
    m_showAllNiftiCandidates = visible;
    for (vtkActor *actor : m_niftiCandidateCenterlineActors) {
        if (actor) {
            actor->SetVisibility(visible);
        }
    }
    if (m_niftiCenterlineActor) {
        m_niftiCenterlineActor->SetVisibility(true);
    }
    for (vtkActor *actor : m_niftiCenterlineEndpointActors) {
        if (actor) {
            actor->SetVisibility(true);
        }
    }
    m_renderWindow->Render();
}

bool Mask3DViewerWidget::hasSelectedNiftiCenterlineCandidate() const
{
    return m_selectedNiftiCandidateIndex >= 0
        && m_niftiCenterlineActor
        && m_niftiCenterlineActor->GetVisibility()
        && m_niftiCenterlineEndpointActors.size() == 2
        && m_niftiCenterlineEndpointActors[0]->GetVisibility()
        && m_niftiCenterlineEndpointActors[1]->GetVisibility();
}

bool Mask3DViewerWidget::rebuildNiftiSelectedCenterline()
{
    if (m_selectedNiftiCandidateIndex < 0
        || static_cast<size_t>(m_selectedNiftiCandidateIndex)
            >= m_niftiCandidatePathsLpsMm.size()) {
        return false;
    }
    const auto &pointsLpsMm =
        m_niftiCandidatePathsLpsMm[
            static_cast<size_t>(m_selectedNiftiCandidateIndex)];
    if (pointsLpsMm.size() < 2) {
        return false;
    }
    vtkNew<vtkPoints> points;
    for (const auto &pointLps : pointsLpsMm) {
        points->InsertNextPoint(
            -pointLps[0], -pointLps[1], pointLps[2]);
    }
    vtkNew<vtkPolyLine> line;
    line->GetPointIds()->SetNumberOfIds(
        static_cast<vtkIdType>(pointsLpsMm.size()));
    for (vtkIdType index = 0;
         index < static_cast<vtkIdType>(pointsLpsMm.size());
         ++index) {
        line->GetPointIds()->SetId(index, index);
    }
    vtkNew<vtkCellArray> lines;
    lines->InsertNextCell(line);
    m_niftiCenterlineData = vtkSmartPointer<vtkPolyData>::New();
    m_niftiCenterlineData->SetPoints(points);
    m_niftiCenterlineData->SetLines(lines);
    m_niftiCenterlineMapper =
        vtkSmartPointer<vtkPolyDataMapper>::New();
    m_niftiCenterlineMapper->SetInputData(m_niftiCenterlineData);
    m_niftiCenterlineMapper->ScalarVisibilityOff();
    m_niftiCenterlineActor = vtkSmartPointer<vtkActor>::New();
    m_niftiCenterlineActor->SetMapper(m_niftiCenterlineMapper);
    m_niftiCenterlineActor->SetPickable(false);
    m_niftiCenterlineActor->SetUseBounds(false);
    m_niftiCenterlineActor->GetProperty()->SetColor(1.0, 0.88, 0.05);
    m_niftiCenterlineActor->GetProperty()->SetLineWidth(5.0);
    m_niftiCenterlineActor->GetProperty()->LightingOff();
    m_renderer->AddActor(m_niftiCenterlineActor);

    int endpointOrdinal = 0;
    for (vtkIdType endpointIndex :
         {vtkIdType{0},
          static_cast<vtkIdType>(pointsLpsMm.size() - 1)}) {
        double endpoint[3] = {};
        points->GetPoint(endpointIndex, endpoint);
        vtkNew<vtkSphereSource> sphere;
        sphere->SetCenter(endpoint);
        sphere->SetRadius(1.25);
        sphere->SetThetaResolution(20);
        sphere->SetPhiResolution(20);
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputConnection(sphere->GetOutputPort());
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->SetPickable(false);
        actor->SetUseBounds(false);
        if (endpointOrdinal == 0) {
            actor->GetProperty()->SetColor(0.1, 0.95, 0.25);
        } else {
            actor->GetProperty()->SetColor(0.95, 0.12, 0.1);
        }
        actor->GetProperty()->SetAmbient(0.35);
        m_renderer->AddActor(actor);
        m_niftiCenterlineEndpointActors.push_back(actor);
        ++endpointOrdinal;
    }
    m_renderer->ResetCameraClippingRange();
    return true;
}

void Mask3DViewerWidget::setNiftiCenterlineVisible(bool visible)
{
    if (m_niftiCenterlineActor) {
        m_niftiCenterlineActor->SetVisibility(visible);
    }
    for (vtkActor *actor : m_niftiCenterlineEndpointActors) {
        if (actor) {
            actor->SetVisibility(visible);
        }
    }
    m_renderWindow->Render();
}

void Mask3DViewerWidget::clearNiftiSelectedCenterline()
{
    if (m_niftiCenterlineActor) {
        m_renderer->RemoveActor(m_niftiCenterlineActor);
    }
    for (vtkActor *actor : m_niftiCenterlineEndpointActors) {
        if (actor) {
            m_renderer->RemoveActor(actor);
        }
    }
    m_niftiCenterlineEndpointActors.clear();
    m_niftiCenterlineActor = nullptr;
    m_niftiCenterlineMapper = nullptr;
    m_niftiCenterlineData = nullptr;
}

void Mask3DViewerWidget::clearNiftiCenterline()
{
    clearNiftiSelectedCenterline();
    for (vtkActor *actor : m_niftiCandidateCenterlineActors) {
        if (actor) {
            m_renderer->RemoveActor(actor);
        }
    }
    m_niftiCandidateCenterlineActors.clear();
    m_niftiCandidatePathsLpsMm.clear();
    m_selectedNiftiCandidateIndex = -1;
    m_showAllNiftiCandidates = true;
    if (m_renderWindow) {
        m_renderWindow->Render();
    }
}

void Mask3DViewerWidget::removeMultiStructurePreview(bool restoreNormalMask)
{
    const bool wasActive = m_multiStructurePreviewActive;
    clearNiftiCenterline();
    for (const MultiStructureSurface &surface : m_multiStructureSurfaces) {
        if (surface.actor) {
            m_renderer->RemoveActor(surface.actor);
        }
        for (vtkActor *intersectionActor : surface.cardIntersectionActors) {
            if (intersectionActor) {
                m_renderer->RemoveActor(intersectionActor);
            }
        }
    }
    m_multiStructureSurfaces.clear();
    m_multiStructureVolume = {};
    clearMultiStructureBoundsGuide();
    m_multiStructurePreviewActive = false;

    if (wasActive) {
        if (m_hasIntersectionMask) {
            updateOrientationLabels(m_intersectionMask);
        } else {
            static constexpr const char *fallbackPlus[3] = {"+X", "+Y", "+Z"};
            static constexpr const char *fallbackMinus[3] = {"-X", "-Y", "-Z"};
            setOrientationLabels(fallbackPlus, fallbackMinus);
        }
    }

    if (!restoreNormalMask) {
        return;
    }

    if (m_maskActor) {
        m_maskActor->SetVisibility(true);
    }
    if (m_boundsActor) {
        m_boundsActor->SetVisibility(true);
    }
    updatePositionPlaneVisibility();
    m_renderer->ResetCameraClippingRange();
    m_renderWindow->Render();
}

void Mask3DViewerWidget::clearMultiStructurePreview()
{
    if (!m_multiStructurePreviewActive) {
        return;
    }
    endPlaneDrag();
    removeMultiStructurePreview(false);
    clearVolumeGeometry();
    if (m_maskActor) {
        m_maskActor->SetVisibility(true);
    }
    if (m_boundsActor) {
        m_boundsActor->SetVisibility(true);
    }
    m_renderer->ResetCameraClippingRange();
    m_renderWindow->Render();
}

const vtkImageData *Mask3DViewerWidget::activeNiftiCtImage() const
{
    return m_multiStructurePreviewActive ? m_multiStructureVolume.ctImage.GetPointer()
                                         : nullptr;
}

const NiftiVolumeGeometry *Mask3DViewerWidget::activeNiftiCtGeometry() const
{
    return m_multiStructurePreviewActive ? &m_multiStructureVolume.ctGeometry
                                         : nullptr;
}

const vtkImageData *Mask3DViewerWidget::activeNiftiSegmentationImage() const
{
    return m_multiStructurePreviewActive
        ? m_multiStructureVolume.segmentationImage.GetPointer()
        : nullptr;
}

const vtkPolyData *Mask3DViewerWidget::activeNiftiSurfaceForLabel(int labelValue) const
{
    if (!m_multiStructurePreviewActive) {
        return nullptr;
    }
    for (const MultiStructureSurface &surface : m_multiStructureSurfaces) {
        if (surface.label.value == labelValue) {
            return surface.polyData;
        }
    }
    return nullptr;
}

const vtkMatrix4x4 *
Mask3DViewerWidget::activeNiftiSegmentationToCtIndexTransform() const
{
    return m_multiStructurePreviewActive
        ? m_multiStructureVolume.segmentationIndexToCtIndex.GetPointer()
        : nullptr;
}

const vtkMatrix4x4 *
Mask3DViewerWidget::activeNiftiSegmentationToCtPhysicalTransform() const
{
    return m_multiStructurePreviewActive
        ? m_multiStructureVolume.segmentationDataToCtData.GetPointer()
        : nullptr;
}

bool Mask3DViewerWidget::visibleMultiStructureBounds(double bounds[6]) const
{
    bounds[0] = bounds[2] = bounds[4] = std::numeric_limits<double>::infinity();
    bounds[1] = bounds[3] = bounds[5] = -std::numeric_limits<double>::infinity();
    bool foundVisibleSurface = false;
    for (const MultiStructureSurface &surface : m_multiStructureSurfaces) {
        if (!surface.actor || !surface.actor->GetVisibility()) {
            continue;
        }
        double actorBounds[6] = {};
        surface.actor->GetBounds(actorBounds);
        if (!boundsAreValid(actorBounds)) {
            continue;
        }
        foundVisibleSurface = true;
        for (int axis = 0; axis < 3; ++axis) {
            bounds[axis * 2] = std::min(bounds[axis * 2], actorBounds[axis * 2]);
            bounds[axis * 2 + 1] = std::max(bounds[axis * 2 + 1], actorBounds[axis * 2 + 1]);
        }
    }
    return foundVisibleSurface && boundsAreValid(bounds);
}

void Mask3DViewerWidget::clearMultiStructureBoundsGuide()
{
    if (m_multiStructureBoundsActor) {
        m_renderer->RemoveActor(m_multiStructureBoundsActor);
        m_multiStructureBoundsActor = nullptr;
    }
}

void Mask3DViewerWidget::updateMultiStructureBoundsGuide()
{
    clearMultiStructureBoundsGuide();
    if (!m_multiStructurePreviewActive) {
        return;
    }

    double bounds[6] = {};
    if (!visibleMultiStructureBounds(bounds)) {
        return;
    }

    const double sizeX = bounds[1] - bounds[0];
    const double sizeY = bounds[3] - bounds[2];
    const double sizeZ = bounds[5] - bounds[4];
    const double largestDimension = std::max({sizeX, sizeY, sizeZ});
    constexpr double marginRatio = 0.08;
    constexpr double minimumCubeSideLength = 1.0;
    const double cubeSideLength = std::max(largestDimension * (1.0 + 2.0 * marginRatio),
                                           minimumCubeSideLength);
    const double halfSide = cubeSideLength * 0.5;
    const double centerX = (bounds[0] + bounds[1]) * 0.5;
    const double centerY = (bounds[2] + bounds[3]) * 0.5;
    const double centerZ = (bounds[4] + bounds[5]) * 0.5;

    vtkNew<vtkOutlineSource> boundsSource;
    boundsSource->SetBounds(centerX - halfSide, centerX + halfSide,
                            centerY - halfSide, centerY + halfSide,
                            centerZ - halfSide, centerZ + halfSide);

    vtkNew<vtkPolyDataMapper> boundsMapper;
    boundsMapper->SetInputConnection(boundsSource->GetOutputPort());
    boundsMapper->ScalarVisibilityOff();

    m_multiStructureBoundsActor = vtkSmartPointer<vtkActor>::New();
    m_multiStructureBoundsActor->SetMapper(boundsMapper);
    m_multiStructureBoundsActor->SetPickable(false);
    m_multiStructureBoundsActor->GetProperty()->SetColor(0.72, 0.75, 0.80);
    m_multiStructureBoundsActor->GetProperty()->SetOpacity(0.38);
    m_multiStructureBoundsActor->GetProperty()->SetLineWidth(1.25);
    m_renderer->AddActor(m_multiStructureBoundsActor);
}
