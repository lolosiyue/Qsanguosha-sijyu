#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariant>

namespace ControllerInteractionContract {

bool validateDescriptor(const QJsonObject &descriptor, QString *error = nullptr);
bool validateResponse(const QJsonObject &descriptor, const QVariant &response,
                      QString *error = nullptr);

} // namespace ControllerInteractionContract
