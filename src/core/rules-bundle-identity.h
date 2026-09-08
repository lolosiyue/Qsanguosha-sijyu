#ifndef QSAN_RULES_BUNDLE_IDENTITY_H
#define QSAN_RULES_BUNDLE_IDENTITY_H

#include <QVariantMap>

// Bootstrap-scoped compatibility metadata, not an authorization credential.
// No Player/Card/Lua pointers or hidden game state cross this boundary.
namespace QSanRulesIdentity {
void beginBootstrap();
void finishBootstrap(bool manualMode);
void clear();
QVariantMap current();
}

#endif
