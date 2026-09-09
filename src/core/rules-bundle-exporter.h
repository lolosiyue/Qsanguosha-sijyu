#ifndef QSAN_RULES_BUNDLE_EXPORTER_H
#define QSAN_RULES_BUNDLE_EXPORTER_H
#include "rules-content-manifest.h"
#include <QJsonObject>
class Engine;
namespace QSanRules {
// Phase one precedes config.lua; phase two precedes extension execution.
QJsonObject coreLuaSnapshot();
QJsonObject declaredLuaSnapshot(const ContentManifest &manifest);
bool contentScanIsDeclared(const ContentManifest &manifest);
QJsonObject exportCodeIdentity();
QJsonObject exportContentManifest(const Engine &engine);
QJsonObject exportIdentity(const Engine &engine, const QJsonObject &loadedLua);
QJsonObject exportRegistry(const Engine &engine);
}
#endif
