#pragma once

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QLabel>
#include <QPointF>
#include <QSlider>
#include <QWidget>

#include <cstdint>
#include <vector>

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
    struct VolumeData {
        int width = 0;
        int height = 0;
        int depth = 0;
        std::vector<int16_t> huVoxels;

        bool isValid() const;
        size_t offset(int x, int y, int z) const;
        int16_t value(int x, int y, int z) const;
    };

    struct MaskVolume {
        int width = 0;
        int height = 0;
        int depth = 0;
        std::vector<uint8_t> voxels;

        bool isValid() const;
        size_t offset(int x, int y, int z) const;
        uint8_t value(int x, int y, int z) const;
        void setValue(int x, int y, int z, uint8_t value);
    };

    class GraphicsView : public QGraphicsView
    {
    public:
        explicit GraphicsView(QWidget *parent = nullptr);

    protected:
        void wheelEvent(QWheelEvent *event) override;
    };

    void createSyntheticStudy();
    void setupUi();
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
    int m_sliceIndex = 0;
    double m_windowWidth = 700.0;
    double m_windowLevel = 150.0;
};
