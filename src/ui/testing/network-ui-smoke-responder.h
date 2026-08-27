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

// Linux GUI M2：真實網絡局入面代替真人操作嘅 UI 自動回應器。
//
// 佢唔係另一個 client，亦唔係 in-process fake：server 的 request 經真 TCP 到
// 呢個 process，由產品的 Client 派去產品的 RoomScene，RoomScene 照常建立 pending
// skill／dialog／target 選擇狀態，responder 只係代替滑鼠去揀真正被 enable 嘅
// CardItem／Photo／按鈕，最後行 RoomScene 自己的 doOkButton()／doCancelButton()
// 把回覆送返 server。
//
// 策略刻意保持「第一個合法選擇」而唔用隨機數：固定 seed 之下成局可重現。
//
// 保底：如果某個 request 喺 stallMs 之內都無法經 UI 回覆（例如遇到一個 M2 未覆蓋
// 的互動形態），就切 trustee 令對局一定行得完，並且把 trustee_fallback 記入
// report — 唔會靜靜當冇事發生。
class NetworkUiSmokeResponder final : public QObject
{
    Q_OBJECT

public:
    NetworkUiSmokeResponder(RoomScene *scene, int stallMs, QObject *parent = nullptr);
    ~NetworkUiSmokeResponder() override;

    static NetworkUiSmokeResponder *instance();
    static bool isActive();

    // RoomScene::chooseGeneral 的 smoke 入口。由 server 提供嘅清單揀第一個，
    // 保證固定 seed 下可重現，亦唔會落入「揀嘅武將唔喺清單」→ server 改用
    // _chooseDefaultGeneral 嘅不確定路徑。清單為空時回傳 false，交返 RoomScene
    // 行原本的 FreeChooseDialog 流程。
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

    // 用真正被 enable 嘅手牌 + 真正 selectable 嘅目標湊出一個合法出牌。
    // 成功時已經撳咗 ok_button。
    bool tryUseAnyCard(bool recordPlay);
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
