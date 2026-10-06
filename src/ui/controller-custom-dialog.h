#pragma once

#include <QVariantMap>
class RoomScene;

// Only the bounded, versioned controller_ui contract can replace a custom UI.
// An ordinary response schema or a QML path is never treated as proof of coverage.
bool presentControllerCustomInteraction(RoomScene *scene, const QVariantMap &parameters, QString *error);
void showControllerCoverageFailure(RoomScene *scene, const QString &reason);
