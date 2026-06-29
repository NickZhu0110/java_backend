#pragma once

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QLabel>
#include <QPointF>
#include <QSlider>
#include <QWidget>

#include "data/MaskVolume.h"
#include "data/VolumeData.h"

class CTViewerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CTViewerWidget(QWidget *parent = nullptr);
    void loadVolumeFromLocalPath(const QString &path);
    void loadMaskFromLocalPath(const QString &path);
    void loadJobFilesFromCache(const QString &caseCacheDir);

private slots:
    void setSliceIndex(int sliceIndex);

private:
    class GraphicsView : public QGraphicsView
    {
    public:
        explicit GraphicsView(QWidget *parent = nullptr);

    protected:
        void wheelEvent(QWheelEvent *event) override;
    };

    void createSyntheticStudy();
    void setupUi();
    void setVolumeAndMask(const VolumeData &volume, const MaskVolume &mask, bool hasMask);
    void updateSliceImages();
    void updateSliceLabel();
    QImage renderCtSlice() const;
    QImage renderMaskOverlay() const;

    GraphicsView *m_view = nullptr;
    QGraphicsScene *m_scene = nullptr;
    QGraphicsPixmapItem *m_ctLayer = nullptr;
    QGraphicsPixmapItem *m_maskLayer = nullptr;
    QSlider *m_sliceSlider = nullptr;
    QLabel *m_sliceLabel = nullptr;

    VolumeData m_volume;
    MaskVolume m_aiMask;
    bool m_hasMask = false;
    bool m_usingSyntheticFallback = true;
    int m_sliceIndex = 0;
    double m_windowWidth = 700.0;
    double m_windowLevel = 150.0;
};
