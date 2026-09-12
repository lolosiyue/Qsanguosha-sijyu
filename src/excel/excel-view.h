#ifndef QSAN_EXCEL_VIEW_H
#define QSAN_EXCEL_VIEW_H

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariant>

class ClientCore;

namespace ExcelView {

QJsonObject catalog(const ClientCore &core, const QString &assetRoot, bool legacy);
QJsonObject snapshotView(const ClientCore &core, const QString &assetRoot,
                         const QStringList &logs = QStringList());
QJsonObject interactionUi(const ClientCore &core, const QString &assetRoot,
                          const QJsonObject &selection = QJsonObject());
QJsonObject details(const ClientCore &core, const QString &assetRoot,
                   const QString &kind, const QString &key, QString *error = nullptr);

QString presentationText(ClientCore &core, int command, const QString &fallback,
                         const QVariant &payload = QVariant());
bool playPresentationAudio(int command, const QVariant &payload,
                           const QString &assetRoot);

} // namespace ExcelView

#endif
