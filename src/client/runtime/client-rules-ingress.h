#ifndef CLIENT_RULES_INGRESS_H
#define CLIENT_RULES_INGRESS_H

#include "client-game-state.h"
#include "interaction-model.h"
#include "protocol/protocol-runtime.h"
#include "protocol/session/client-session-controller.h"

#include <QJsonObject>
#include <QMap>

// A serialized, transport-independent observer of the actual V2 byte stream.
// It never sends a frame, imports a JS state snapshot, or executes a game effect.
// The owning host supplies its own validated W2 identity on reset.
class ClientRulesIngress final
{
public:
    bool reset(int generation, const QJsonObject &localIdentity, QString *error = nullptr);
    bool acceptFrame(int generation, bool outgoing, const QByteArray &frame,
                     QString *error = nullptr);
    bool prepareQuery(int generation, int revision, const QString &requestId,
                      const QJsonObject &selection, QJsonObject *query,
                      QString *error = nullptr) const;
    QJsonObject status() const;
    QJsonObject view() const;
    void invalidate();

private:
    bool incoming(const QSanProtocol::ProtocolMessage &message, QString *error);
    bool outgoing(const QSanProtocol::ProtocolMessage &message, QString *error);
    bool reduce(const QSanProtocol::ProtocolMessage &message, QString *error);
    bool fail(const QString &reason, QString *error);
    void clearRequest();

    QSanProtocol::ProtocolCodecRouter m_codec;
    QSanProtocol::ClientSessionController m_session;
    ClientGameState m_state, m_pending;
    QJsonObject m_identity;
    QSanProtocol::ProtocolMessage m_request;
    InteractionRequest m_interaction;
    QMap<quint64, int> m_outgoingRequests;
    quint64 m_lastOutgoing = 0;
    int m_generation = -1;
    int m_revision = 0;
    QString m_syncId;
    bool m_syncActive = false;
    bool m_failed = false;
    bool m_hasRequest = false;
};

#endif
