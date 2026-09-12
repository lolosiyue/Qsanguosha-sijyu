#ifndef QSAN_EXCEL_BRIDGE_H
#define QSAN_EXCEL_BRIDGE_H

#include "excel-interaction.h"
#include "excel-ipc-server.h"
#include "client-live-session.h"
#include "client/core/client-core.h"
#include "local-server-controller.h"

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QStringList>

struct ExcelBridgeOptions
{
    QString session;
    QString token;
    QString assetRoot;
    QString userDataRoot;
    bool legacy = false;
};

// One workbook, one serialized ClientCore and one owned helper. HTTP never
// becomes a second gameplay decoder, and a polling gap is never parent death.
class ExcelBridge final : public QObject
{
    Q_OBJECT
public:
    explicit ExcelBridge(const ExcelBridgeOptions &options, QObject *parent = nullptr);
    ~ExcelBridge() override;
    bool start(QString *error = nullptr);
    quint16 port() const;
    QJsonObject snapshot() const;
    void stop();

signals:
    void stopped(bool graceful);

private:
    ExcelIpcServer::Reply route(const QString &method, const QString &path,
        const QUrlQuery &query, const QJsonObject &body);
    QJsonObject command(const QJsonObject &body);
    QJsonObject execute(const QString &name, const QJsonObject &args, QString *error);
    bool connectionOptions(const QJsonObject &args, ClientLiveSessionOptions *options,
        QString *error) const;
    bool startHost(const QJsonObject &args, QString *error);
    bool checkInteractionEnvelope(const QJsonObject &body, QString *error) const;
    void changed();
    void event(const QString &kind, const QJsonObject &data = QJsonObject());
    void log(const QString &text);
    void finishStop(bool graceful);
    void tryFinishStop();

    ExcelBridgeOptions m_options;
    ClientCore m_core;
    ClientLiveSession m_session;
    ExcelInteractionAdapter m_interactions;
    ExcelIpcServer m_http;
    LocalServerController m_host;
    ClientLiveSessionOptions m_pendingConnection;
    QJsonObject m_selection;
    QJsonArray m_events;
    QStringList m_logs;
    struct CachedCommand { QByteArray fingerprint; QJsonObject response; };
    QHash<QString, CachedCommand> m_commands;
    QStringList m_commandOrder;
    quint64 m_largestCommand = 0;
    quint64 m_revision = 0;
    quint64 m_sequence = 0;
    int m_robotCount = -1;
    bool m_stopping = false;
    bool m_stopped = false;
    bool m_httpDrained = false;
    bool m_hostStopped = false;
    bool m_stopGraceful = true;
    bool m_trusted = false;
    bool m_privateGame = false;
};

#endif
