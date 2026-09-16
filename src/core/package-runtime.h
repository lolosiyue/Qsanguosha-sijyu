#ifndef QSAN_PACKAGE_RUNTIME_H
#define QSAN_PACKAGE_RUNTIME_H

#include "rules-content-manifest.h"

namespace QSanPackages {
// Freeze content before any rules or skill constructors inspect assets.
bool prepareRuntime(QString *error);
QSanRules::ContentManifest effectiveContent(const QSanRules::ContentManifest &content,
                                          const QSanRules::ContentManifest &baseline);
bool completeBoot(QString *error);
}
#endif
