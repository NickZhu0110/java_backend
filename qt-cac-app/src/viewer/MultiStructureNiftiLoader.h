#pragma once

#include <QString>

#include "viewer/MultiStructureVolume.h"

class MultiStructureNiftiLoader
{
public:
    bool load(const QString &ctPath,
              const QString &segmentationPath,
              MultiStructureVolume *loadedVolume,
              QString *errorMessage = nullptr) const;
};
