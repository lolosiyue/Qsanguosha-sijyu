#ifndef QSAN_RULES_BUNDLE_IDENTITY_H
#define QSAN_RULES_BUNDLE_IDENTITY_H

#include <QJsonObject>
#include <QString>

namespace QSanRules {
constexpr int IdentitySchema = 1;
constexpr int BridgeSchema = 2;
QByteArray canonical(const QJsonValue &value);
QString digest(const QString &domain, const QJsonValue &value);
QJsonObject seal(QJsonObject identity);
// Seals the build-time half separately so a client can reject an incompatible
// runtime before downloading any content.
QJsonObject sealCode(QJsonObject identity);
bool validate(const QJsonObject &identity);
// An empty result means compatible. Missing metadata is allowed only on legacy TCP.
QString compatibilityError(const QJsonObject &server, const QJsonObject &client,
                           bool required);
}
#endif
