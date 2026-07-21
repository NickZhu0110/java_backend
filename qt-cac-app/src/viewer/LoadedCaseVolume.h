#pragma once

#include <QStringList>

#include "data/MaskVolume.h"
#include "data/VolumeData.h"

struct LoadedCaseVolume
{
    VolumeData volume;
    MaskVolume mask;
    bool hasMask = false;
    QStringList warnings;
};
