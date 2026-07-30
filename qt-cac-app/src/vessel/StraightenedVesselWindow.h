#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QImage>
#include <QWidget>

#include <vtkImageData.h>
#include <vtkSmartPointer.h>

#include <array>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;

class StraightenedVesselWindow : public QWidget
{
    Q_OBJECT

public:
    explicit StraightenedVesselWindow(QWidget *parent = nullptr);

    bool loadResult(const QJsonObject &result,
                    const QJsonArray &candidatePaths,
                    QString *errorMessage = nullptr);

signals:
    void anotherPathRequested();
    void centerlineVisibilityRequested(bool visible);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    bool readNrrd(const QString &path,
                  vtkSmartPointer<vtkImageData> *image,
                  QString *errorMessage);
    bool readCenterlineFrames(const QString &path,
                              QString *errorMessage);
    void updateImages();
    void updateMetadata();
    QImage renderLongitudinal() const;
    QImage renderCurvedCpr(bool orientationA) const;
    QImage renderStraightenedMpr(bool xzOrientation,
                                 bool thinSlabMip) const;
    QImage renderCrossSection(int slice) const;
    QRgb displayPixel(double ctValue, bool maskValue) const;
    void exportResult();

    QLabel *m_metadataLabel = nullptr;
    QLabel *m_longitudinalTitleLabel = nullptr;
    QLabel *m_longitudinalLabel = nullptr;
    QLabel *m_crossSectionLabel = nullptr;
    QLabel *m_positionLabel = nullptr;
    QSlider *m_positionSlider = nullptr;
    QSpinBox *m_windowWidthSpinBox = nullptr;
    QSpinBox *m_windowLevelSpinBox = nullptr;
    QSpinBox *m_slabThicknessSpinBox = nullptr;
    QCheckBox *m_maskOverlayCheckBox = nullptr;
    QComboBox *m_longitudinalModeComboBox = nullptr;
    vtkSmartPointer<vtkImageData> m_ctImage;
    vtkSmartPointer<vtkImageData> m_maskImage;
    QJsonObject m_result;
    QJsonArray m_candidatePaths;
    std::vector<std::array<double, 3>> m_centerlinePoints;
    std::vector<std::array<double, 3>> m_centerlineTangents;
    std::vector<std::array<double, 3>> m_centerlineNormals;
};
