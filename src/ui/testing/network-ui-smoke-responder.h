#ifndef NETWORK_UI_SMOKE_RESPONDER_H
#define NETWORK_UI_SMOKE_RESPONDER_H

#include "client.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>

class CardItem;
class PlayerCardContainer;
class RoomScene;
class QTimer;

// UI responder for a real network game.

// Server requests arrive over TCP and pass through Client and RoomScene; the responder selects enabled native items and submits through RoomScene.






//
// First-legal-choice selection keeps fixed-seed runs reproducible.
//
// If the UI cannot answer within stallMs, switch to trustee and record trustee_fallback in the report.



class NetworkUiSmokeResponder final : public QObject
{
    Q_OBJECT

public:
    NetworkUiSmokeResponder(RoomScene *scene, int stallMs, QObject *parent = nullptr);
    ~NetworkUiSmokeResponder() override;

    static NetworkUiSmokeResponder *instance();
    static bool isActive();

    // Choose the first server-provided general for reproducible smoke runs.
    // If the list is empty, leave the original FreeChooseDialog flow intact.




    bool answerChooseGeneral(const QStringList &generals);

    bool trusteeEngaged() const;
    QStringList coveredInteractions() const;
    QStringList coveredActions() const;
    QJsonObject summary() const;

private slots:
    void onStatusChanged(Client::Status oldStatus, Client::Status newStatus);
    void onServerRequest(int commandType);
    void onServerReply(int commandType);
    void onStep();
    void onStallCheck();

private:
    void scheduleStep();
    void recordInteraction(const QString &name);
    void recordAction(const QString &name);
    void engageTrustee(const QString &reason);

    // True means the handler submitted a UI reply or advanced the request.
    bool stepPlaying();
    bool stepResponding(Client::Status status);
    bool stepDiscarding(Client::Status status);
    bool stepExecDialog();
    bool stepSkillInvoke();
    bool stepPlayerChoose();

    // Assemble a legal card play from a genuinely enabled hand card plus a
    // genuinely selectable target.
    //
    // One step tries one card only: every attempt makes RoomScene recompute
    // targets and re-lay the graphics effects, so blasting through the whole
    // hand in one go would churn the scene within a single event loop turn. A
    // human would not do that, and a 5-player QGraphicsScene cannot take it
    // either (see the rendering crashes recorded in docs).
    // Attempted means the reply has been sent; Retry means the next card is
    // tried on the next event loop turn.
    enum class CardAttempt { Sent, Retry, Exhausted };
    CardAttempt tryUseNextCard(bool recordPlay);
    bool trySelectTargetsFor();
    bool clickButton(const QString &name);
    void clearSelection();

    QList<CardItem *> enabledHandCards() const;
    QList<PlayerCardContainer *> selectableTargets() const;

    static NetworkUiSmokeResponder *s_instance;

    QPointer<RoomScene> m_scene;
    QTimer *m_stepTimer = nullptr;
    QTimer *m_stallTimer = nullptr;
    QElapsedTimer m_pendingSince;

    int m_stallMs;
    // Number of hand cards tried for this request, one per event-loop turn.
    int m_cardCursor = 0;
    bool m_stepScheduled = false;
    bool m_requestPending = false;
    bool m_trusteeEngaged = false;
    QString m_trusteeReason;
    QString m_pendingInteraction;

    QHash<QString, int> m_interactionCounts;
    QHash<QString, int> m_actionCounts;
    int m_requestCount = 0;
    int m_replyCount = 0;
};

#endif
