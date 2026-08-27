#include "network-ui-smoke-responder.h"

#include "network-ui-smoke-report.h"

#include "carditem.h"
#include "cardcontainer.h"
#include "clientplayer.h"
#include "dashboard.h"
#include "engine.h"
#include "photo.h"
#include "playercardbox.h"
#include "qsanbutton.h"
#include "roomscene.h"

#include <QAbstractButton>
#include <QDialog>
#include <QJsonArray>
#include <QTimer>

#include <algorithm>

namespace {

// UI 動作之間留一格 event loop：dialog show()／pending skill 的動畫喺呢段時間
// 內安頓好，responder 先至讀 enabled 狀態。太短會讀到中途狀態，太長會拖慢成局。
const int kStepDelayMs = 60;
const int kStallPollMs = 1000;

// 單次出牌最多試幾多個目標。真實牌最多都係全場，呢個上限只係擋 pathological
// 情況下的無限迴圈。
const int kMaxTargetsPerCard = 16;

void collectCardItems(QGraphicsItem *parent, QList<CardItem *> *result)
{
    if (!parent)
        return;
    for (QGraphicsItem *child : parent->childItems()) {
        if (CardItem *card = dynamic_cast<CardItem *>(child))
            result->append(card);
        collectCardItems(child, result);
    }
}

} // namespace

NetworkUiSmokeResponder *NetworkUiSmokeResponder::s_instance = nullptr;

NetworkUiSmokeResponder::NetworkUiSmokeResponder(RoomScene *scene, int stallMs, QObject *parent)
    : QObject(parent), m_scene(scene), m_stallMs(stallMs)
{
    s_instance = this;

    m_stepTimer = new QTimer(this);
    m_stepTimer->setSingleShot(true);
    m_stepTimer->setInterval(kStepDelayMs);
    connect(m_stepTimer, &QTimer::timeout, this, &NetworkUiSmokeResponder::onStep);

    m_stallTimer = new QTimer(this);
    m_stallTimer->setInterval(kStallPollMs);
    connect(m_stallTimer, &QTimer::timeout, this, &NetworkUiSmokeResponder::onStallCheck);
    m_stallTimer->start();

    if (ClientInstance) {
        // RoomScene 喺自己的 constructor 已經接咗 status_changed，所以呢條連線
        // 一定行喺 RoomScene::updateStatus 之後：responder 睇到的係 RoomScene
        // 已經佈置好的按鈕／pending skill 狀態。
        connect(ClientInstance, &Client::status_changed,
            this, &NetworkUiSmokeResponder::onStatusChanged);
        connect(ClientInstance, &Client::server_request,
            this, &NetworkUiSmokeResponder::onServerRequest);
        connect(ClientInstance, &Client::server_reply,
            this, &NetworkUiSmokeResponder::onServerReply);
    }
}

NetworkUiSmokeResponder::~NetworkUiSmokeResponder()
{
    if (s_instance == this)
        s_instance = nullptr;
}

NetworkUiSmokeResponder *NetworkUiSmokeResponder::instance()
{
    return s_instance;
}

bool NetworkUiSmokeResponder::isActive()
{
    return s_instance != nullptr;
}

bool NetworkUiSmokeResponder::trusteeEngaged() const
{
    return m_trusteeEngaged;
}

QStringList NetworkUiSmokeResponder::coveredInteractions() const
{
    QStringList names = m_interactionCounts.keys();
    names.sort();
    return names;
}

QStringList NetworkUiSmokeResponder::coveredActions() const
{
    QStringList names = m_actionCounts.keys();
    names.sort();
    return names;
}

QJsonObject NetworkUiSmokeResponder::summary() const
{
    QJsonObject interactions;
    for (auto it = m_interactionCounts.cbegin(); it != m_interactionCounts.cend(); ++it)
        interactions.insert(it.key(), it.value());

    QJsonObject actions;
    for (auto it = m_actionCounts.cbegin(); it != m_actionCounts.cend(); ++it)
        actions.insert(it.key(), it.value());

    QJsonObject root;
    root.insert(QStringLiteral("server_requests"), m_requestCount);
    root.insert(QStringLiteral("client_replies"), m_replyCount);
    root.insert(QStringLiteral("interactions"), interactions);
    root.insert(QStringLiteral("ui_actions"), actions);
    root.insert(QStringLiteral("trustee_engaged"), m_trusteeEngaged);
    if (!m_trusteeReason.isEmpty())
        root.insert(QStringLiteral("trustee_reason"), m_trusteeReason);
    return root;
}

void NetworkUiSmokeResponder::recordInteraction(const QString &name)
{
    if (name.isEmpty())
        return;
    m_interactionCounts[name] = m_interactionCounts.value(name) + 1;
}

void NetworkUiSmokeResponder::recordAction(const QString &name)
{
    if (name.isEmpty())
        return;
    m_actionCounts[name] = m_actionCounts.value(name) + 1;
}

void NetworkUiSmokeResponder::onServerRequest(int commandType)
{
    ++m_requestCount;
    const QString name = NetworkUiSmokeReport::interactionName(commandType);
    if (name.isEmpty())
        return;
    recordInteraction(name);
    m_pendingInteraction = name;
    m_requestPending = true;
    m_pendingSince.start();
}

void NetworkUiSmokeResponder::onServerReply(int)
{
    ++m_replyCount;
    m_requestPending = false;
    m_pendingInteraction.clear();
}

void NetworkUiSmokeResponder::onStatusChanged(Client::Status, Client::Status)
{
    // 新一個請求 = 由第一張手牌重新試起。
    m_cardCursor = 0;
    scheduleStep();
}

void NetworkUiSmokeResponder::scheduleStep()
{
    if (m_trusteeEngaged || m_stepScheduled)
        return;
    m_stepScheduled = true;
    m_stepTimer->start();
}

void NetworkUiSmokeResponder::onStallCheck()
{
    if (m_trusteeEngaged || !m_requestPending || !m_pendingSince.isValid())
        return;
    if (m_pendingSince.elapsed() < m_stallMs)
        return;
    engageTrustee(QStringLiteral("no UI response for '%1' within %2ms")
        .arg(m_pendingInteraction.isEmpty() ? QStringLiteral("unknown") : m_pendingInteraction)
        .arg(m_stallMs));
}

void NetworkUiSmokeResponder::engageTrustee(const QString &reason)
{
    if (m_trusteeEngaged)
        return;
    m_trusteeEngaged = true;
    m_trusteeReason = reason;
    m_requestPending = false;
    recordAction(QLatin1String(NetworkUiSmokeReport::ActionTrusteeFallback));

    // 對局要行得完先驗證到 game over；切 trustee 之後 server 端 AI 接手，
    // 但呢件事一定會出現喺 report 同 stdout，唔會扮成正常路徑。
    fprintf(stderr, "network-ui-smoke: falling back to trustee (%s)\n",
        qPrintable(reason));
    fflush(stderr);
    if (m_scene && Self && Self->getState() != QStringLiteral("trust"))
        m_scene->trust();
}

void NetworkUiSmokeResponder::onStep()
{
    m_stepScheduled = false;
    if (m_trusteeEngaged || m_scene.isNull() || ClientInstance == nullptr || Self == nullptr)
        return;

    const Client::Status status = ClientInstance->getStatus();
    bool handled = false;
    switch (status & Client::ClientStatusBasicMask) {
    case Client::NotActive:
        return;
    case Client::Playing:
        handled = stepPlaying();
        break;
    case Client::Responding:
        handled = stepResponding(status);
        break;
    case Client::Discarding:
    case Client::Exchanging:
        handled = stepDiscarding(status);
        break;
    case Client::ExecDialog:
        handled = stepExecDialog();
        break;
    case Client::AskForSkillInvoke:
        handled = stepSkillInvoke();
        break;
    case Client::AskForPlayerChoose:
        handled = stepPlayerChoose();
        break;
    default:
        handled = false;
        break;
    }

    if (handled)
        return;

    // RoomScene::doTimeout() 係產品自己為每一個 status 定義嘅安全預設回覆
    // （撳 cancel／揀第一張 AG 牌／交空目標…）。M2 未特別處理嘅互動形態走呢條路，
    // 依然係經真正 RoomScene 出 reply，而唔係繞過 UI 直接砌 packet。
    m_scene->doTimeout();
    recordAction(QLatin1String(NetworkUiSmokeReport::ActionDecline));
}

QList<CardItem *> NetworkUiSmokeResponder::enabledHandCards() const
{
    QList<CardItem *> items;
    if (m_scene.isNull() || m_scene->dashboard == nullptr)
        return items;
    for (CardItem *item : m_scene->dashboard->getHandCards()) {
        if (item && item->isEnabled() && item->isVisible() && item->getCard())
            items << item;
    }
    // 固定 seed 下手牌顯示次序可能受動畫影響，用 card id 排序令選擇可重現。
    std::sort(items.begin(), items.end(), [](CardItem *left, CardItem *right) {
        return left->getCard()->getEffectiveId() < right->getCard()->getEffectiveId();
    });
    return items;
}

QList<PlayerCardContainer *> NetworkUiSmokeResponder::selectableTargets() const
{
    QList<PlayerCardContainer *> items;
    if (m_scene.isNull())
        return items;
    for (PlayerCardContainer *item : m_scene->item2player.keys()) {
        if (item == nullptr || item->isSelected())
            continue;
        if (!(item->flags() & QGraphicsItem::ItemIsSelectable))
            continue;
        items << item;
    }
    // QMap::keys() 已經按指標排序，指標次序唔穩定；改用玩家 objectName 排序。
    std::sort(items.begin(), items.end(),
        [this](PlayerCardContainer *left, PlayerCardContainer *right) {
            const ClientPlayer *leftPlayer = m_scene->item2player.value(left);
            const ClientPlayer *rightPlayer = m_scene->item2player.value(right);
            const QString leftName = leftPlayer ? leftPlayer->objectName() : QString();
            const QString rightName = rightPlayer ? rightPlayer->objectName() : QString();
            return leftName < rightName;
        });
    return items;
}

void NetworkUiSmokeResponder::clearSelection()
{
    if (m_scene.isNull())
        return;
    m_scene->unselectAllTargets();
    if (m_scene->dashboard)
        m_scene->dashboard->unselectAll();
}

bool NetworkUiSmokeResponder::clickButton(const QString &name)
{
    if (m_scene.isNull())
        return false;
    QSanButton *button = nullptr;
    if (name == QStringLiteral("ok"))
        button = m_scene->ok_button;
    else if (name == QStringLiteral("cancel"))
        button = m_scene->cancel_button;
    else if (name == QStringLiteral("discard"))
        button = m_scene->discard_button;
    if (button == nullptr || !button->isEnabled())
        return false;
    button->click();
    return true;
}

bool NetworkUiSmokeResponder::trySelectTargetsFor()
{
    if (m_scene.isNull())
        return false;

    for (int added = 0; added < kMaxTargetsPerCard; ++added) {
        if (m_scene->ok_button != nullptr && m_scene->ok_button->isEnabled())
            return true;
        const QList<PlayerCardContainer *> candidates = selectableTargets();
        if (candidates.isEmpty())
            return m_scene->ok_button != nullptr && m_scene->ok_button->isEnabled();
        // setSelected() 會經 selected_changed → RoomScene::updateSelectedTargets，
        // 即係同真人撳落去行同一條路，包括 targetFilter／targetsFeasible 重算。
        candidates.constFirst()->setSelected(true);
        recordAction(QLatin1String(NetworkUiSmokeReport::ActionSelectTarget));
    }
    return m_scene->ok_button != nullptr && m_scene->ok_button->isEnabled();
}

NetworkUiSmokeResponder::CardAttempt NetworkUiSmokeResponder::tryUseNextCard(bool recordPlay)
{
    if (m_scene.isNull() || m_scene->dashboard == nullptr)
        return CardAttempt::Exhausted;

    const QList<CardItem *> candidates = enabledHandCards();
    if (m_cardCursor >= candidates.size()) {
        clearSelection();
        return CardAttempt::Exhausted;
    }

    CardItem *item = candidates.at(m_cardCursor);
    ++m_cardCursor;

    clearSelection();
    item->clickItem();
    if (!item->isSelected() && m_scene->dashboard->getSelected() == nullptr
        && m_scene->dashboard->getPendings().isEmpty())
        return CardAttempt::Retry; // 撳唔郁（例如 view-as skill 拒絕呢張牌）

    if (trySelectTargetsFor() && clickButton(QStringLiteral("ok"))) {
        recordAction(recordPlay ? QLatin1String(NetworkUiSmokeReport::ActionPlayCard)
                                : QLatin1String(NetworkUiSmokeReport::ActionChooseCard));
        return CardAttempt::Sent;
    }
    clearSelection();
    return CardAttempt::Retry;
}

bool NetworkUiSmokeResponder::stepPlaying()
{
    switch (tryUseNextCard(true)) {
    case CardAttempt::Sent:
        return true;
    case CardAttempt::Retry:
        scheduleStep(); // 下一格 event loop 再試下一張,畀場景喘啖氣
        return true;
    case CardAttempt::Exhausted:
        break;
    }
    // 冇合法出牌就結束出牌階段：discard_button 喺 Playing 狀態即係「結束」，
    // RoomScene::doDiscardButton() 會 onPlayerResponseCard(nullptr)。
    if (clickButton(QStringLiteral("discard"))) {
        recordAction(QLatin1String(NetworkUiSmokeReport::ActionFinishPhase));
        return true;
    }
    return false;
}

bool NetworkUiSmokeResponder::stepResponding(Client::Status status)
{
    Q_UNUSED(status)
    // 有得回應就回應（證明 askForCard 真係經 UI 打得返出去），冇就 cancel。
    switch (tryUseNextCard(true)) {
    case CardAttempt::Sent:
        return true;
    case CardAttempt::Retry:
        scheduleStep();
        return true;
    case CardAttempt::Exhausted:
        break;
    }
    if (clickButton(QStringLiteral("cancel"))) {
        recordAction(QLatin1String(NetworkUiSmokeReport::ActionDecline));
        return true;
    }
    return false;
}

bool NetworkUiSmokeResponder::stepDiscarding(Client::Status status)
{
    Q_UNUSED(status)
    if (m_scene.isNull() || m_scene->dashboard == nullptr)
        return false;

    clearSelection();
    const QList<CardItem *> candidates = enabledHandCards();
    for (CardItem *item : candidates) {
        item->clickItem();
        if (m_scene->ok_button != nullptr && m_scene->ok_button->isEnabled()) {
            if (clickButton(QStringLiteral("ok"))) {
                recordAction(QLatin1String(NetworkUiSmokeReport::ActionChooseCard));
                return true;
            }
        }
    }
    clearSelection();
    if (clickButton(QStringLiteral("cancel"))) {
        recordAction(QLatin1String(NetworkUiSmokeReport::ActionDecline));
        return true;
    }
    return false;
}

bool NetworkUiSmokeResponder::stepExecDialog()
{
    if (m_scene.isNull())
        return false;

    // askForCardChosen：PlayerCardBox 蓋過 m_choiceDialog（RoomScene 自己的規則）。
    if (m_scene->m_playerCardBox != nullptr && m_scene->m_playerCardBox->isVisible()) {
        QList<CardItem *> items;
        collectCardItems(m_scene->m_playerCardBox, &items);
        for (CardItem *item : items) {
            if (item == nullptr || !item->isEnabled() || !item->isVisible())
                continue;
            item->clickItem();
            recordAction(QLatin1String(NetworkUiSmokeReport::ActionChooseCard));
            return true;
        }
        return false;
    }

    QDialog *dialog = m_scene->m_choiceDialog;
    if (dialog == nullptr || !dialog->isVisible())
        return false;

    // askForChoice／選花色／選勢力都係一堆 objectName 帶語意的按鈕，撳第一個
    // enabled 嘅，固定 seed 下可重現。
    const QList<QAbstractButton *> buttons = dialog->findChildren<QAbstractButton *>();
    for (QAbstractButton *button : buttons) {
        if (button == nullptr || !button->isEnabled() || !button->isVisible())
            continue;
        button->click();
        recordAction(QLatin1String(NetworkUiSmokeReport::ActionChooseOption));
        return true;
    }
    return false;
}

bool NetworkUiSmokeResponder::stepSkillInvoke()
{
    // 發動技能比拒絕更能證明 request → UI → reply 走通，亦更接近真人行為。
    if (clickButton(QStringLiteral("ok"))) {
        recordAction(QLatin1String(NetworkUiSmokeReport::ActionInvokeSkill));
        return true;
    }
    if (clickButton(QStringLiteral("cancel"))) {
        recordAction(QLatin1String(NetworkUiSmokeReport::ActionDecline));
        return true;
    }
    return false;
}

bool NetworkUiSmokeResponder::stepPlayerChoose()
{
    if (m_scene.isNull())
        return false;

    // choose_skill 已經由 RoomScene 起咗 pending，合法目標就係 selectable 的
    // Photo／Dashboard；撳夠人數之後 ok_button 會自己 enable。
    for (int added = 0; added < kMaxTargetsPerCard; ++added) {
        if (m_scene->ok_button != nullptr && m_scene->ok_button->isEnabled())
            break;
        const QList<PlayerCardContainer *> candidates = selectableTargets();
        if (candidates.isEmpty())
            break;
        candidates.constFirst()->setSelected(true);
        recordAction(QLatin1String(NetworkUiSmokeReport::ActionChoosePlayer));
    }
    if (clickButton(QStringLiteral("ok")))
        return true;
    clearSelection();
    if (clickButton(QStringLiteral("cancel"))) {
        recordAction(QLatin1String(NetworkUiSmokeReport::ActionDecline));
        return true;
    }
    return false;
}

bool NetworkUiSmokeResponder::answerChooseGeneral(const QStringList &generals)
{
    if (generals.isEmpty() || ClientInstance == nullptr)
        return false;
    QStringList sorted = generals;
    sorted.sort();
    const QString pick = sorted.constFirst();
    recordAction(QLatin1String(NetworkUiSmokeReport::ActionChooseOption));
    ClientInstance->onPlayerChooseGeneral(pick);
    return true;
}
