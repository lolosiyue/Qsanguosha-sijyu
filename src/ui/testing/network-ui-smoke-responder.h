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

// Linux GUI M2: UI auto-responder that stands in for a human during a real network game.
//
// It is not another client and not an in-process fake: the server's request
// arrives over real TCP at this process, is dispatched by the product's Client
// to the product's RoomScene, and RoomScene builds its pending skill / dialog /
// target selection state as usual; the responder merely plays the mouse,
// picking the genuinely enabled CardItem / Photo / button, and finally runs
// RoomScene's own doOkButton() / doCancelButton() to send the reply back to the
// server.
//
// The strategy deliberately sticks to "the first legal choice" instead of random numbers: under a fixed seed the whole game is reproducible.
//
// Fallback: if some request cannot be answered through the UI within stallMs
// (e.g. an interaction form M2 does not cover), switch to trustee so the game
// is guaranteed to finish, and record trustee_fallback in the report — it is
// never silently ignored.
class NetworkUiSmokeResponder final : public QObject
{
    Q_OBJECT

public:
    NetworkUiSmokeResponder(RoomScene *scene, int stallMs, QObject *parent = nullptr);
    ~NetworkUiSmokeResponder() override;

    static NetworkUiSmokeResponder *instance();
    static bool isActive();

    // Smoke entry for RoomScene::chooseGeneral. Picks the first entry of the
    // list provided by the server, which guarantees reproducibility under a
    // fixed seed and avoids falling into the uncertain path where the server
    // falls back to _chooseDefaultGeneral because the chosen general is not in
    // the list. Returns false when the list is empty, leaving RoomScene to run
    // its original FreeChooseDialog flow.
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

    // 每個 handler 回傳 true = 已經經 UI 送出回覆／已推進一步。
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
    // 本次請求已經試過幾多張手牌(每格 event loop 試一張)。
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
