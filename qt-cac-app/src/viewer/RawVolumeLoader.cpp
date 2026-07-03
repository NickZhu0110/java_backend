#include "viewer/RawVolumeLoader.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>

#include <cstring>
#include <limits>

namespace {

QString inputVolumeDirForCase(const QString &caseCacheDir)
{
    return QDir(caseCacheDir).filePath(QStringLiteral("input_volume"));
}

QJsonObject readJsonObject(const QString &path, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot open metadata file %1: %2").arg(path, file.errorString());
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid JSON metadata file %1: %2").arg(path, parseError.errorString());
        }
        return {};
    }
    return document.object();
}

std::vector<double> readDoubleArray(const QJsonObject &metadata, const QString &key)
{
    std::vector<double> values;
    const QJsonArray array = metadata.value(key).toArray();
    values.reserve(static_cast<size_t>(array.size()));
    for (const QJsonValue &value : array) {
        values.push_back(value.toDouble());
    }
    return values;
}

bool readDimensions(const QJsonObject &metadata, int *width, int *height, int *depth, QString *errorMessage)
{
    *width = metadata.value(QStringLiteral("width")).toInt();
    *height = metadata.value(QStringLiteral("height")).toInt();
    *depth = metadata.value(QStringLiteral("depth")).toInt();
    if (*width <= 0 || *height <= 0 || *depth <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid raw volume dimensions in metadata.");
        }
        return false;
    }
    return true;
}

} // namespace

bool RawVolumeLoader::hasPreprocessedFiles(const QString &caseCacheDir)
{
    const QString inputDir = inputVolumeDirForCase(caseCacheDir);
    return QFileInfo::exists(QDir(inputDir).filePath(QStringLiteral("ct_volume_int16.raw")))
        && QFileInfo::exists(QDir(inputDir).filePath(QStringLiteral("ct_volume_metadata.json")));
}

LoadedCaseVolume RawVolumeLoader::loadCase(const QString &caseCacheDir, QString *errorMessage)
{
    LoadedCaseVolume loaded;
    if (!loadVolume(inputVolumeDirForCase(caseCacheDir), &loaded.volume, errorMessage)) {
        return loaded;
    }

    QString maskError;
    loaded.hasMask = loadMask(caseCacheDir, &loaded.mask, &maskError);
    if (!loaded.hasMask && !maskError.isEmpty()) {
        loaded.warnings << maskError;
    }
    if (loaded.hasMask && !loaded.mask.hasSameDimensionsAs(loaded.volume)) {
        loaded.warnings << QStringLiteral("Mask dimensions do not match CT volume; mask overlay will be hidden.");
    }

    return loaded;
}

bool RawVolumeLoader::loadVolume(const QString &inputVolumeDir, VolumeData *volume, QString *errorMessage)
{
    const QString metadataPath = QDir(inputVolumeDir).filePath(QStringLiteral("ct_volume_metadata.json"));
    const QString rawPath = QDir(inputVolumeDir).filePath(QStringLiteral("ct_volume_int16.raw"));
    const QJsonObject metadata = readJsonObject(metadataPath, errorMessage);
    if (metadata.isEmpty()) {
        return false;
    }
    if (metadata.value(QStringLiteral("dtype")).toString() != QStringLiteral("int16")) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unsupported CT raw dtype; expected int16.");
        }
        return false;
    }

    int width = 0;
    int height = 0;
    int depth = 0;
    if (!readDimensions(metadata, &width, &height, &depth, errorMessage)) {
        return false;
    }

    const auto voxelCount = static_cast<quint64>(width) * height * depth;
    if (voxelCount > static_cast<quint64>(std::numeric_limits<int>::max()) * 8ULL) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("CT raw volume is unreasonably large.");
        }
        return false;
    }

    QFile rawFile(rawPath);
    if (!rawFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot open CT raw file %1: %2").arg(rawPath, rawFile.errorString());
        }
        return false;
    }

    const qint64 expectedBytes = static_cast<qint64>(voxelCount * sizeof(int16_t));
    if (rawFile.size() != expectedBytes) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("CT raw byte size mismatch. Expected %1, got %2.")
                .arg(expectedBytes)
                .arg(rawFile.size());
        }
        return false;
    }

    QByteArray bytes = rawFile.readAll();
    volume->width = width;
    volume->height = height;
    volume->depth = depth;
    volume->spacing = readDoubleArray(metadata, QStringLiteral("spacing"));
    volume->origin = readDoubleArray(metadata, QStringLiteral("origin"));
    volume->direction = readDoubleArray(metadata, QStringLiteral("direction"));
    volume->huVoxels.resize(static_cast<size_t>(voxelCount));
    std::memcpy(volume->huVoxels.data(), bytes.constData(), static_cast<size_t>(bytes.size()));
    return volume->isValid();
}

bool RawVolumeLoader::loadMask(const QString &caseCacheDir, MaskVolume *mask, QString *errorMessage)
{
    const QString metadataPath = QDir(caseCacheDir).filePath(QStringLiteral("mask_volume_metadata.json"));
    const QString rawPath = QDir(caseCacheDir).filePath(QStringLiteral("mask_volume_uint8.raw"));
    const QJsonObject metadata = readJsonObject(metadataPath, errorMessage);
    if (metadata.isEmpty()) {
        return false;
    }
    if (metadata.value(QStringLiteral("dtype")).toString() != QStringLiteral("uint8")) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unsupported mask raw dtype; expected uint8.");
        }
        return false;
    }

    int width = 0;
    int height = 0;
    int depth = 0;
    if (!readDimensions(metadata, &width, &height, &depth, errorMessage)) {
        return false;
    }

    const auto voxelCount = static_cast<quint64>(width) * height * depth;
    QFile rawFile(rawPath);
    if (!rawFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot open mask raw file %1: %2").arg(rawPath, rawFile.errorString());
        }
        return false;
    }
    if (rawFile.size() != static_cast<qint64>(voxelCount)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Mask raw byte size mismatch. Expected %1, got %2.")
                .arg(voxelCount)
                .arg(rawFile.size());
        }
        return false;
    }

    const QByteArray bytes = rawFile.readAll();
    mask->width = width;
    mask->height = height;
    mask->depth = depth;
    mask->spacing = readDoubleArray(metadata, QStringLiteral("spacing"));
    mask->origin = readDoubleArray(metadata, QStringLiteral("origin"));
    mask->direction = readDoubleArray(metadata, QStringLiteral("direction"));
    mask->voxels.assign(bytes.begin(), bytes.end());
    return mask->isValid();
}
