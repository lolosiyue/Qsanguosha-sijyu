#include "engine-bootstrap.h"
#include "game-session-controller.h"
#include "ai.h"
#include "card-movement-service.h"
#include "card-lifetime-manager.h"
#include "engine.h"
#include "json.h"
#include "player-decision-service.h"
#include "protocol.h"
#include "protocol/protocol-runtime.h"
#include "request-coordinator.h"
#include "room-test-access.h"
#include "room.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "settings.h"
#include "server-info.h"
#include "skill.h"
#include "structs.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QMap>

#include <atomic>
#include <cstdio>
#include <thread>
#include <type_traits>

// An unregistered Card-bearing metatype: not part of the N8.3 frozen matrix, so
// the manager must reject it instead of guessing at its layout.
struct UnregisteredCardPayload
{
    const Card *card = nullptr;
};
Q_DECLARE_METATYPE(UnregisteredCardPayload)

using namespace QSanProtocol;

using RegisterOverride = void (Room::*)(ServerPlayer *, const QString &, const QString &,
                                        const QVariant &);
using ClearOverrides = void (Room::*)();
using FindOverride = QVariant (Room::*)(ServerPlayer *, const QString &, const QString &) const;
using Activate = void (Room::*)(ServerPlayer *, CardUseStruct &);
using AskForSkillInvoke = bool (Room::*)(ServerPlayer *, const QString &, const QVariant &, bool);
using AskForChoice = QString (Room::*)(ServerPlayer *, const QString &, const QString &,
                                       const QVariant &, const QString &, const QString &);
using AskForSuit = Card::Suit (Room::*)(ServerPlayer *, const QString &);
using AskForKingdomList = QString (Room::*)(ServerPlayer *, const QString &, QStringList, bool);
using AskForKingdomString = QString (Room::*)(ServerPlayer *, const QString &, const QString &,
                                              bool);
using AskForGeneralList = QString (Room::*)(ServerPlayer *, const QStringList &, const QString &,
                                            const QString &);
using AskForCardChosen = int (Room::*)(ServerPlayer *, ServerPlayer *, const QString &,
                                       const QString &, bool, Card::HandlingMethod,
                                       const QList<int> &, bool);
using AskForCardSimple = const Card *(Room::*)(ServerPlayer *, const QString &, const QString &,
                                               const QVariant &, const QString &);
using AskForCardFull = const Card *(Room::*)(ServerPlayer *, const QString &, const QString &,
                                             const QVariant &, Card::HandlingMethod, ServerPlayer *,
                                             bool, const QString &, bool, const Card *);
using AskForPlayerChosen = ServerPlayer *(Room::*)(ServerPlayer *,
                                                    const QList<ServerPlayer *> &,
                                                    const QString &, const QString &, bool, bool);
using AskForPlayersChosen = QList<ServerPlayer *> (Room::*)(ServerPlayer *,
                                                            const QList<ServerPlayer *> &,
                                                            const QString &, int, int,
                                                            const QString &, bool, bool);
using AskForAG = int (Room::*)(ServerPlayer *, const QList<int> &, bool, const QString &,
                               const QString &);
using AskForCardShow = const Card *(Room::*)(ServerPlayer *, ServerPlayer *, const QString &);
using AskForPindian = const Card *(Room::*)(ServerPlayer *, ServerPlayer *, const QString &);
using AskForPindianRace = QList<const Card *> (Room::*)(ServerPlayer *, ServerPlayer *,
                                                        const QString &);
using AskForDiscard = Card *(Room::*)(ServerPlayer *, const QString &, int, int, bool, bool,
                                      const QString &, const QString &, const QString &);
using AskForExchange = Card *(Room::*)(ServerPlayer *, const QString &, int, int, bool,
                                       const QString &, bool, const QString &);
using AskForYiji = ServerPlayer *(Room::*)(ServerPlayer *, QList<int> &, const QString &, bool,
                                           bool, bool, int, QList<ServerPlayer *>, CardMoveReason,
                                           const QString &, bool);
using AskForYijiIds = QList<int> (Room::*)(ServerPlayer *, QList<int> &, const QString &, bool,
                                           bool, bool, int, QList<ServerPlayer *>, CardMoveReason,
                                           const QString &, bool);
using AskForYijiStruct = CardsMoveStruct (Room::*)(ServerPlayer *, QList<int> &, const QString &,
                                                   bool, bool, bool, int, QList<ServerPlayer *>,
                                                   CardMoveReason, const QString &, bool, bool);
using AskForGuanxing = QList<int> (Room::*)(ServerPlayer *, const QList<int> &, Room::GuanxingType,
                                            bool);
using AskForUseCard = const Card *(Room::*)(ServerPlayer *, const QString &, const QString &, int,
                                            Card::HandlingMethod, bool, ServerPlayer *,
                                            const Card *, QString);
using AskForUseCardStruct = CardUseStruct (Room::*)(ServerPlayer *, const QString &, const QString &,
                                                    int, Card::HandlingMethod, bool, ServerPlayer *,
                                                    const Card *, QString);
using AskForUseSlashToList = const Card *(Room::*)(ServerPlayer *, QList<ServerPlayer *>,
                                                   const QString &, bool, bool, bool, ServerPlayer *,
                                                   const Card *, QString);
using AskForUseSlashToOne = const Card *(Room::*)(ServerPlayer *, ServerPlayer *, const QString &,
                                                  bool, bool, bool, ServerPlayer *, const Card *,
                                                  QString);
using AskForUseSlashToStructList = CardUseStruct (Room::*)(ServerPlayer *, QList<ServerPlayer *>,
                                                           const QString &, bool, bool, bool,
                                                           ServerPlayer *, const Card *, QString);
using AskForUseSlashToStructOne = CardUseStruct (Room::*)(ServerPlayer *, ServerPlayer *,
                                                          const QString &, bool, bool, bool,
                                                          ServerPlayer *, const Card *, QString);
using AskForNullification = const Card *(Room::*)(const Card *, ServerPlayer *, ServerPlayer *,
                                                  bool);
using AskForNullificationHidden = const Card *(Room::*)(const Card *, ServerPlayer *, ServerPlayer *,
                                                        bool);
using AskForSinglePeach = const Card *(Room::*)(ServerPlayer *, ServerPlayer *);
using AskForTriggerOrder = QString (Room::*)(ServerPlayer *, const QString &, QList<SkillContext> &,
                                             bool, const QVariant &);
using AskForYishi = YishiStruct (Room::*)(ServerPlayer *, const QList<ServerPlayer *> &,
                                          const QString &);
using AskForQml = QVariant (Room::*)(ServerPlayer *, const QString &, const QVariantMap &, int);
using AskForLuckCard = void (Room::*)(QList<CardsMoveStruct> &);
using VerifyNullification = bool (Room::*)(ServerPlayer *, const QVariant &, void *);

static_assert(std::is_same_v<decltype(static_cast<RegisterOverride>(&Room::registerTestOverride)),
                             RegisterOverride>);
static_assert(std::is_same_v<decltype(static_cast<ClearOverrides>(&Room::clearTestOverrides)),
                             ClearOverrides>);
static_assert(std::is_same_v<decltype(static_cast<FindOverride>(&Room::findTestOverride)),
                             FindOverride>);
static_assert(std::is_same_v<decltype(static_cast<Activate>(&Room::activate)), Activate>);
static_assert(std::is_same_v<decltype(static_cast<AskForSkillInvoke>(&Room::askForSkillInvoke)),
                             AskForSkillInvoke>);
static_assert(std::is_same_v<decltype(static_cast<AskForChoice>(&Room::askForChoice)),
                             AskForChoice>);
static_assert(std::is_same_v<decltype(static_cast<AskForSuit>(&Room::askForSuit)), AskForSuit>);
static_assert(std::is_same_v<decltype(static_cast<AskForKingdomList>(&Room::askForKingdom)),
                             AskForKingdomList>);
static_assert(std::is_same_v<decltype(static_cast<AskForKingdomString>(&Room::askForKingdom)),
                             AskForKingdomString>);
static_assert(std::is_same_v<decltype(static_cast<AskForGeneralList>(&Room::askForGeneral)),
                             AskForGeneralList>);
static_assert(std::is_same_v<decltype(static_cast<AskForCardChosen>(&Room::askForCardChosen)),
                             AskForCardChosen>);
static_assert(std::is_same_v<decltype(static_cast<AskForCardSimple>(&Room::askForCard)),
                             AskForCardSimple>);
static_assert(std::is_same_v<decltype(static_cast<AskForCardFull>(&Room::askForCard)),
                             AskForCardFull>);
static_assert(std::is_same_v<decltype(static_cast<AskForPlayerChosen>(&Room::askForPlayerChosen)),
                             AskForPlayerChosen>);
static_assert(std::is_same_v<decltype(static_cast<AskForPlayersChosen>(&Room::askForPlayersChosen)),
                             AskForPlayersChosen>);
static_assert(std::is_same_v<decltype(static_cast<AskForAG>(&Room::askForAG)), AskForAG>);
static_assert(std::is_same_v<decltype(static_cast<AskForCardShow>(&Room::askForCardShow)),
                             AskForCardShow>);
static_assert(std::is_same_v<decltype(static_cast<AskForPindian>(&Room::askForPindian)),
                             AskForPindian>);
static_assert(std::is_same_v<decltype(static_cast<AskForPindianRace>(&Room::askForPindianRace)),
                             AskForPindianRace>);
static_assert(std::is_same_v<decltype(static_cast<AskForDiscard>(&Room::askForDiscard)),
                             AskForDiscard>);
static_assert(std::is_same_v<decltype(static_cast<AskForExchange>(&Room::askForExchange)),
                             AskForExchange>);
static_assert(std::is_same_v<decltype(static_cast<AskForYiji>(&Room::askForYiji)), AskForYiji>);
static_assert(std::is_same_v<decltype(static_cast<AskForYijiIds>(&Room::askForyiji)),
                             AskForYijiIds>);
static_assert(std::is_same_v<decltype(static_cast<AskForYijiStruct>(&Room::askForYijiStruct)),
                             AskForYijiStruct>);
static_assert(std::is_same_v<decltype(static_cast<AskForGuanxing>(&Room::askForGuanxing)),
                             AskForGuanxing>);
static_assert(std::is_same_v<decltype(static_cast<AskForUseCard>(&Room::askForUseCard)),
                             AskForUseCard>);
static_assert(std::is_same_v<decltype(static_cast<AskForUseCardStruct>(&Room::askForUseCardStruct)),
                             AskForUseCardStruct>);
static_assert(std::is_same_v<decltype(static_cast<AskForUseSlashToList>(&Room::askForUseSlashTo)),
                             AskForUseSlashToList>);
static_assert(std::is_same_v<decltype(static_cast<AskForUseSlashToOne>(&Room::askForUseSlashTo)),
                             AskForUseSlashToOne>);
static_assert(std::is_same_v<decltype(static_cast<AskForUseSlashToStructList>(
                                         &Room::askForUseSlashToStruct)),
                             AskForUseSlashToStructList>);
static_assert(std::is_same_v<decltype(static_cast<AskForUseSlashToStructOne>(
                                         &Room::askForUseSlashToStruct)),
                             AskForUseSlashToStructOne>);
static_assert(std::is_same_v<decltype(static_cast<AskForNullification>(&Room::askForNullification)),
                             AskForNullification>);
static_assert(std::is_same_v<decltype(static_cast<AskForNullificationHidden>(
                                         &Room::_askForNullification)),
                             AskForNullificationHidden>);
static_assert(std::is_same_v<decltype(static_cast<AskForSinglePeach>(&Room::askForSinglePeach)),
                             AskForSinglePeach>);
static_assert(std::is_same_v<decltype(static_cast<AskForTriggerOrder>(&Room::askForTriggerOrder)),
                             AskForTriggerOrder>);
static_assert(std::is_same_v<decltype(static_cast<AskForYishi>(&Room::askForYishi)), AskForYishi>);
static_assert(std::is_same_v<decltype(static_cast<AskForQml>(&Room::askForQml)), AskForQml>);
static_assert(std::is_same_v<decltype(static_cast<AskForLuckCard>(&Room::askForLuckCard)),
                             AskForLuckCard>);
static_assert(std::is_same_v<Room::ResponseVerifyFunction, VerifyNullification>);
static_assert(std::is_same_v<decltype(&Room::verifyNullificationResponse),
                             Room::ResponseVerifyFunction>);

class ScriptedAI : public TrustAI
{
public:
    explicit ScriptedAI(ServerPlayer *player)
        : TrustAI(player)
    {
    }

    bool invokeValue = false;
    QString choiceValue;
    Card::Suit suitValue = Card::NoSuit;
    QString kingdomValue;
    QString generalValue;
    ServerPlayer *playerChosenValue = nullptr;
    bool hasPlayerChosenValue = false;
    QList<ServerPlayer *> playersChosenValue;
    bool hasPlayersChosenValue = false;
    int agValue = -2;
    int cardChosenValue = -2;
    const Card *cardShowValue = nullptr;
    bool hasCardShowValue = false;
    const Card *pindianValue = nullptr;
    bool hasPindianValue = false;
    const Card *cardValue = nullptr;
    bool hasCardValue = false;
    QList<int> discardValue;
    bool hasDiscardValue = false;
    ServerPlayer *yijiTarget = nullptr;
    int yijiCardId = -1;
    bool hasYijiValue = false;
    QList<int> guanxingTop;
    QList<int> guanxingBottom;
    bool hasGuanxingValue = false;
    const Card *peachValue = nullptr;
    bool hasPeachValue = false;
    int peachUsesLeft = -1;
    const Card *nullificationValue = nullptr;
    bool hasNullificationValue = false;
    QString triggerOrderValue;

    bool askForSkillInvoke(const QString &skill_name, const QVariant &data) override
    {
        Q_UNUSED(skill_name);
        Q_UNUSED(data);
        return invokeValue;
    }

    QString askForChoice(const QString &skill_name, const QString &choice,
                         const QVariant &data) override
    {
        if (!choiceValue.isEmpty())
            return choiceValue;
        return TrustAI::askForChoice(skill_name, choice, data);
    }

    Card::Suit askForSuit(const QString &reason) override
    {
        if (suitValue != Card::NoSuit)
            return suitValue;
        return TrustAI::askForSuit(reason);
    }

    QString askForKingdom(QStringList kingdoms) override
    {
        if (!kingdomValue.isEmpty())
            return kingdomValue;
        return TrustAI::askForKingdom(kingdoms);
    }

    QString askForGeneral(const QStringList &generals, const QString &default_choice,
                          const QString &reason) override
    {
        if (!generalValue.isEmpty())
            return generalValue;
        return TrustAI::askForGeneral(generals, default_choice, reason);
    }

    ServerPlayer *askForPlayerChosen(const QList<ServerPlayer *> &targets,
                                     const QString &reason) override
    {
        if (hasPlayerChosenValue)
            return playerChosenValue;
        return TrustAI::askForPlayerChosen(targets, reason);
    }

    QList<ServerPlayer *> askForPlayersChosen(const QList<ServerPlayer *> &targets,
                                              const QString &reason, int max_num,
                                              int min_num) override
    {
        if (hasPlayersChosenValue)
            return playersChosenValue;
        return TrustAI::askForPlayersChosen(targets, reason, max_num, min_num);
    }

    int askForAG(const QList<int> &card_ids, bool refusable, const QString &reason) override
    {
        if (agValue != -2)
            return agValue;
        return TrustAI::askForAG(card_ids, refusable, reason);
    }

    int askForCardChosen(ServerPlayer *who, const QString &flags, const QString &reason,
                         Card::HandlingMethod method) override
    {
        if (cardChosenValue != -2)
            return cardChosenValue;
        return TrustAI::askForCardChosen(who, flags, reason, method);
    }

    const Card *askForCardShow(ServerPlayer *requestor, const QString &reason) override
    {
        if (hasCardShowValue)
            return cardShowValue;
        return TrustAI::askForCardShow(requestor, reason);
    }

    const Card *askForPindian(ServerPlayer *requestor, const QString &reason) override
    {
        if (hasPindianValue)
            return pindianValue;
        return TrustAI::askForPindian(requestor, reason);
    }

    const Card *askForCard(const QString &pattern, const QString &prompt, const QVariant &data,
                           const Card::HandlingMethod method) override
    {
        if (hasCardValue)
            return cardValue;
        return TrustAI::askForCard(pattern, prompt, data, method);
    }

    QList<int> askForDiscard(const QString &reason, int discard_num, int min_num, bool optional,
                             bool include_equip, const QString &pattern) override
    {
        if (hasDiscardValue)
            return discardValue;
        return TrustAI::askForDiscard(reason, discard_num, min_num, optional, include_equip,
                                      pattern);
    }

    ServerPlayer *askForYiji(const QList<int> &cards, const QString &reason, int &card_id) override
    {
        if (hasYijiValue) {
            card_id = yijiCardId;
            return yijiTarget;
        }
        return TrustAI::askForYiji(cards, reason, card_id);
    }

    void askForGuanxing(const QList<int> &cards, QList<int> &up, QList<int> &bottom,
                        int guanxing_type) override
    {
        if (hasGuanxingValue) {
            up = guanxingTop;
            bottom = guanxingBottom;
            return;
        }
        TrustAI::askForGuanxing(cards, up, bottom, guanxing_type);
    }

    const Card *askForSinglePeach(ServerPlayer *dying) override
    {
        if (hasPeachValue) {
            if (peachUsesLeft == 0)
                return nullptr;
            if (peachUsesLeft > 0)
                --peachUsesLeft;
            return peachValue;
        }
        return TrustAI::askForSinglePeach(dying);
    }

    const Card *askForNullification(const Card *trick, ServerPlayer *from, ServerPlayer *to,
                                    bool positive) override
    {
        if (hasNullificationValue)
            return nullificationValue;
        return TrustAI::askForNullification(trick, from, to, positive);
    }

    QString askForTriggerOrder(const QString &reason, QMap<ServerPlayer *, QStringList> &skills,
                               bool optional, const QVariant &data) override
    {
        if (!triggerOrderValue.isEmpty())
            return triggerOrderValue;
        return TrustAI::askForTriggerOrder(reason, skills, optional, data);
    }
};

class DecisionProbe : public TriggerSkill
{
public:
    DecisionProbe()
        : TriggerSkill(QStringLiteral("#player-decision-probe"))
    {
        events << InvokeSkill << EventAskForChoice << ChoiceMade << GeneralChoosing
               << GeneralChosen << CardAsked << ShowCards << EventPlayPhaseLoop
               << TrickCardCanceling;
        global = true;
    }

    QString forceChoice;
    bool cancelChoice = false;
    QStringList generalChoosingReplacement;
    QString generalChosenReplacement;
    const Card *providedCard = nullptr;

    struct Record
    {
        TriggerEvent event;
        QString payload;
    };
    mutable QList<Record> records;

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        Record record;
        record.event = event;
        if (event == CardAsked) {
            if (providedCard) {
                CardUseStruct provided;
                provided.card = providedCard;
                room->setTag(QStringLiteral("provided"), QVariant::fromValue(provided));
            }
            record.payload = data.toStringList().join(QLatin1Char('+'));
            records << record;
            return false;
        } else if (event == EventAskForChoice) {
            ChoiceData choiceData = data.value<ChoiceData>();
            if (cancelChoice)
                choiceData.canceled = true;
            if (!forceChoice.isEmpty())
                choiceData.forced_answer = forceChoice;
            data = QVariant::fromValue(choiceData);
            record.payload = choiceData.forced_answer;
        } else if (event == GeneralChoosing && !generalChoosingReplacement.isEmpty()) {
            data = generalChoosingReplacement.join(QLatin1Char('+'));
            record.payload = data.toString();
        } else if (event == GeneralChosen && !generalChosenReplacement.isEmpty()) {
            data = generalChosenReplacement;
            record.payload = data.toString();
        } else {
            record.payload = data.toString();
        }
        records << record;
        return false;
    }

    QStringList payloads(TriggerEvent event) const
    {
        QStringList values;
        foreach (const Record &record, records) {
            if (record.event == event)
                values << record.payload;
        }
        return values;
    }

    QList<TriggerEvent> recordedEvents() const
    {
        QList<TriggerEvent> values;
        foreach (const Record &record, records)
            values << record.event;
        return values;
    }
};

struct PlayerDecisionServiceTestAccess
{
    static PlayerDecisionService &service(Room &room)
    {
        return *room.m_playerDecisions;
    }

    static ServerPlayer *addPlayer(Room &room, const QString &objectName,
                                   const QString &state = QStringLiteral("robot"))
    {
        ServerPlayer *player = new ServerPlayer(&room);
        player->setObjectName(objectName);
        player->setState(state);
        player->setAlive(true);
        player->setRemoved(false);
        ScriptedAI *ai = new ScriptedAI(player);
        ai->setParent(player);
        player->setAI(ai);
        player->drainAllLocks();
        player->releaseLock(ServerPlayer::SEMA_MUTEX);
        room.addPlayerToRoster(player);
        return player;
    }

    static void attachThread(Room &room, const TriggerSkill *skill)
    {
        room.thread = new RoomThread(&room);
        if (skill)
            room.thread->addTriggerSkill(skill);
    }

    static void setGameState(Room &room, int state)
    {
        room.m_gameSession->m_state = state > 0
            ? GameSessionController::State::Playing
            : (state < 0 ? GameSessionController::State::Finished
                         : GameSessionController::State::Waiting);
    }

    static QString askForOrder(Room &room, ServerPlayer *player, const QString &defaultChoice)
    {
        return room.askForOrder(player, defaultChoice);
    }

    static QString askForRole(Room &room, ServerPlayer *player, const QStringList &roles,
                              const QString &scheme)
    {
        return room.askForRole(player, roles, scheme);
    }

    static ScriptedAI *ai(ServerPlayer *player)
    {
        return static_cast<ScriptedAI *>(player->getSmartAI());
    }

    static QList<int> &drawPile(Room &room)
    {
        return room.m_cardMovement->drawPile();
    }
};

namespace {

class RecordingEventDispatcher : public EventDispatcher
{
public:
    bool dispatch(TriggerEvent, ServerPlayer *, QVariant &) override
    {
        ++dispatchCount;
        return false;
    }

    void registerTriggerSkill(const TriggerSkill *) override
    {
        ++registrationCount;
    }

    int dispatchCount = 0;
    int registrationCount = 0;
};

struct RequestRecord
{
    CommandType command;
};

class RequestRecorder
{
public:
    void watch(ServerPlayer *player)
    {
        QObject::connect(player, &ServerPlayer::message_ready, player,
                         [this](const QByteArray &message) {
            ProtocolMessage packet;
            if (!ProtocolCodecRouter().decode(message, &packet).success) {
                parseFailed = true;
                return;
            }
            if (packet.type == ProtocolMessageType::Request)
                records << RequestRecord{static_cast<CommandType>(packet.command)};
            else if (packet.type == ProtocolMessageType::Notification)
                notifications << static_cast<CommandType>(packet.command);
        });
    }

    bool contains(CommandType command) const
    {
        foreach (const RequestRecord &record, records) {
            if (record.command == command)
                return true;
        }
        return false;
    }

    int count(CommandType command) const
    {
        int n = 0;
        foreach (const RequestRecord &record, records) {
            if (record.command == command)
                ++n;
        }
        return n;
    }

    int countNotification(CommandType command) const
    {
        int n = 0;
        foreach (CommandType recorded, notifications) {
            if (recorded == command)
                ++n;
        }
        return n;
    }

    bool containsNotification(CommandType command) const
    {
        return notifications.contains(command);
    }

    QList<RequestRecord> records;
    QList<CommandType> notifications;
    bool parseFailed = false;
};

class ClientReplyAgent
{
public:
    ClientReplyAgent(Room &room, ServerPlayer *player,
                     ProtocolVersion version = ProtocolVersion::V2)
        : m_room(room), m_player(player)
    {
        Q_UNUSED(version);
        QObject::connect(player, &ServerPlayer::message_ready, player,
                         [this](const QByteArray &message) {
            ProtocolMessage request;
            const ProtocolDecodeResult requestResult = m_router.decode(message, &request);
            if (!requestResult.success) {
                parseFailed = true;
                return;
            }
            if (request.type != ProtocolMessageType::Request)
                return;
            const CommandType requestCommand = static_cast<CommandType>(request.command);
            if (!replyByCommand.contains(requestCommand))
                return;
            lastRequestWire = message;
            CommandType replyCommand = requestCommand;
            if (replyCommand == S_COMMAND_SHOW_CARD || replyCommand == S_COMMAND_PINDIAN
                || replyCommand == S_COMMAND_PLAY_CARD || replyCommand == S_COMMAND_NULLIFICATION
                || replyCommand == S_COMMAND_ASK_PEACH)
                replyCommand = S_COMMAND_RESPONSE_CARD;
            else if (replyCommand == S_COMMAND_EXCHANGE_CARD)
                replyCommand = S_COMMAND_DISCARD_CARD;

            ProtocolMessage reply;
            reply.type = ProtocolMessageType::Reply;
            reply.source = ProtocolEndpoint::Client;
            reply.destination = ProtocolEndpoint::Room;
            reply.messageId = m_nextMessageId++;
            reply.replyTo = request.messageId;
            reply.command = replyCommand;
            reply.hasPayload = true;
            reply.payload = replyByCommand.value(requestCommand);

            QString error;
            lastReplyWire = m_router.encode(reply, &error);
            ProtocolMessage normalizedReply;
            const ProtocolDecodeResult replyResult = m_router.decode(
                lastReplyWire, &normalizedReply);
            if (lastReplyWire.isEmpty() || !replyResult.success) {
                parseFailed = true;
                return;
            }
            RoomTestAccess::dispatch(
                m_room, m_player, normalizedReply, QString::fromUtf8(lastReplyWire));
        });
    }

    QMap<CommandType, QVariant> replyByCommand;
    QByteArray lastRequestWire;
    QByteArray lastReplyWire;
    bool protocolReady = true;
    bool parseFailed = false;

private:
    Room &m_room;
    ServerPlayer *m_player;
    ProtocolCodecRouter m_router;
    quint64 m_nextMessageId = 1;
};

// A response-card reply travels as the domain array
// [card_text, targets, activation_skill_name, activation_skill_instance_id];
// commands such as SHOW_CARD and PINDIAN answer through that same reply.
static QVariant responseCardReply(const QString &cardText,
                                  const QVariantList &targets = QVariantList())
{
    return QVariantList{cardText, targets, QString(), 0};
}

struct DecisionFixture
{
    DecisionProbe probe;
    Room room;
    ServerPlayer *player;
    ServerPlayer *other;

    explicit DecisionFixture(const QString &state = QStringLiteral("robot"))
        : room(nullptr, QStringLiteral("02_1v1"))
    {
        player = PlayerDecisionServiceTestAccess::addPlayer(
            room, QStringLiteral("decision-player"), state);
        other = PlayerDecisionServiceTestAccess::addPlayer(
            room, QStringLiteral("decision-other"));
        PlayerDecisionServiceTestAccess::attachThread(room, &probe);
    }

    ScriptedAI *ai() const
    {
        return PlayerDecisionServiceTestAccess::ai(player);
    }
};

static bool expect(bool condition, const char *context)
{
    if (condition)
        return true;
    qCritical() << "player decision service test failed:" << context;
    QFile file(QStringLiteral(
        "L:/finaldebug/QSanguosha-v2/.omo/evidence/player-decision-service-20260817/task-5/ctest-fail.txt"));
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        file.write(context);
        file.write("\n");
    }
    return false;
}

static bool roomOverrideBehaviorIsCharacterized()
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer first(&room);
    ServerPlayer second(&room);
    first.setObjectName(QStringLiteral("decision:first"));
    second.setObjectName(QStringLiteral("decision:second"));

    room.registerTestOverride(&first, QStringLiteral("choice"), QStringLiteral("false"),
                              QVariant(false));
    room.registerTestOverride(&first, QStringLiteral("choice"), QStringLiteral("empty"),
                              QVariant(QString()));
    room.registerTestOverride(&first, QStringLiteral("choice"), QStringLiteral("zero"),
                              QVariant(0));

    const QVariant falseValue = room.findTestOverride(
        &first, QStringLiteral("choice"), QStringLiteral("false"));
    const QVariant emptyValue = room.findTestOverride(
        &first, QStringLiteral("choice"), QStringLiteral("empty"));
    const QVariant zeroValue = room.findTestOverride(
        &first, QStringLiteral("choice"), QStringLiteral("zero"));

    if (!expect(falseValue.isValid() && !falseValue.toBool(), "false remains valid")
        || !expect(emptyValue.isValid() && emptyValue.toString().isEmpty(),
                   "empty string remains valid")
        || !expect(zeroValue.isValid() && zeroValue.toInt() == 0, "zero remains valid")
        || !expect(!room.findTestOverride(&first, QStringLiteral("choice"),
                                          QStringLiteral("missing")).isValid(),
                   "missing key is invalid")
        || !expect(!room.findTestOverride(&second, QStringLiteral("choice"),
                                          QStringLiteral("false")).isValid(),
                   "player object name participates in the key")
        || !expect(!room.findTestOverride(&first, QStringLiteral("other"),
                                          QStringLiteral("false")).isValid(),
                   "query type participates in the key"))
        return false;

    room.clearTestOverrides();
    return expect(!room.findTestOverride(&first, QStringLiteral("choice"),
                                         QStringLiteral("false")).isValid(),
                  "clear removes registered values");
}

static bool serviceOwnsOverridesAndHandlesNullPlayer()
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer player(&room);
    player.setObjectName(QStringLiteral("service-player"));
    PlayerDecisionService &service = PlayerDecisionServiceTestAccess::service(room);

    service.registerTestOverride(&player, QStringLiteral("choice"), QStringLiteral("zero"),
                                 QVariant(0));
    const QVariant value = service.findTestOverride(
        &player, QStringLiteral("choice"), QStringLiteral("zero"));
    service.registerTestOverride(nullptr, QStringLiteral("choice"), QStringLiteral("ignored"),
                                 QVariant(true));

    return expect(value.isValid() && value.toInt() == 0,
                  "Room-owned service stores valid zero")
        && expect(!service.findTestOverride(nullptr, QStringLiteral("choice"),
                                            QStringLiteral("ignored")).isValid(),
                  "null player lookup is invalid");
}

static bool serviceConstructionDoesNotDispatch()
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    RecordingEventDispatcher dispatcher;
    {
        PlayerDecisionService service(room, dispatcher);
        Q_UNUSED(service);
    }
    return expect(dispatcher.dispatchCount == 0,
                  "service construction and destruction dispatch no events")
        && expect(dispatcher.registrationCount == 0,
                  "service construction and destruction register no skills");
}

static bool concurrentClearAndFindAreSafe()
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer player(&room);
    player.setObjectName(QStringLiteral("concurrent-player"));
    PlayerDecisionService &service = PlayerDecisionServiceTestAccess::service(room);

    for (int repeat = 0; repeat < 25; ++repeat) {
        std::atomic_bool invalidValue(false);

        service.registerTestOverride(&player, QStringLiteral("choice"), QStringLiteral("key"),
                                     QVariant(7));
        std::thread reader([&]() {
            for (int i = 0; i < 1000; ++i) {
                const QVariant value = service.findTestOverride(
                    &player, QStringLiteral("choice"), QStringLiteral("key"));
                if (value.isValid() && value.toInt() != 7)
                    invalidValue = true;
            }
        });
        std::thread writer([&]() {
            for (int i = 0; i < 1000; ++i) {
                service.clearTestOverrides();
                service.registerTestOverride(&player, QStringLiteral("choice"),
                                             QStringLiteral("key"), QVariant(7));
            }
        });
        reader.join();
        writer.join();
        if (!expect(!invalidValue, "concurrent clear/find preserves valid values"))
            return false;
    }
    return true;
}

static bool skillInvokeOverrideAndAiPreservePayloads()
{
    DecisionFixture fixture;
    RequestRecorder recorder;
    recorder.watch(fixture.player);
    fixture.room.registerTestOverride(fixture.player, QStringLiteral("skill_invoke"),
                                      QStringLiteral("tuxi"), QVariant(true));
    const bool overridden = fixture.room.askForSkillInvoke(
        fixture.player, QStringLiteral("tuxi$2"), QVariant(), true);
    const QStringList overridePayloads = fixture.probe.payloads(ChoiceMade);
    if (!expect(overridden, "bool override invokes the skill")
        || !expect(!recorder.contains(S_COMMAND_INVOKE_SKILL),
                   "override hit does not send a client invoke request")
        || !expect(overridePayloads
                       == (QStringList() << QStringLiteral("notifyInvoked:tuxi")
                                         << QStringLiteral("skillInvoke:tuxi:yes")),
                   "override ChoiceMade keeps notifyInvoked then skillInvoke yes")
        || !expect(fixture.probe.recordedEvents()
                       == (QList<TriggerEvent>() << InvokeSkill << ChoiceMade << ChoiceMade),
                   "invoke hook precedes both ChoiceMade events"))
        return false;

    fixture.probe.records.clear();
    fixture.room.clearTestOverrides();
    fixture.ai()->invokeValue = false;
    const bool aiNo = fixture.room.askForSkillInvoke(
        fixture.player, QStringLiteral("tuxi"), QVariant::fromValue(fixture.other), false);
    return expect(!aiNo, "TrustAI-style false remains the AI answer")
        && expect(fixture.probe.payloads(ChoiceMade)
                      == QStringList{QStringLiteral("skillInvoke:tuxi:decision-other:no")},
                  "AI ChoiceMade includes the target player name");
}

static bool skillInvokeClientAnswerAndNotifyFalse()
{
    DecisionFixture fixture(QStringLiteral("online"));
    ClientReplyAgent agent(fixture.room, fixture.player);
    agent.replyByCommand.insert(S_COMMAND_INVOKE_SKILL, QVariant(true));
    const bool invoked = fixture.room.askForSkillInvoke(
        fixture.player, QStringLiteral("tuxi"), QVariant(), false);
    return expect(invoked, "online reply true invokes the skill")
        && expect(fixture.probe.payloads(ChoiceMade)
                      == QStringList{QStringLiteral("skillInvoke:tuxi:yes")},
                  "client invoke ChoiceMade uses yes");
}

static bool choiceOverrideForceCancelAndFallback()
{
    DecisionFixture fixture;
    fixture.room.registerTestOverride(fixture.player, QStringLiteral("choice"),
                                      QStringLiteral("tuxi"), QStringLiteral("left"));
    const QString overridden = fixture.room.askForChoice(
        fixture.player, QStringLiteral("tuxi"), QStringLiteral("left+right"));
    if (!expect(overridden == QStringLiteral("left"), "string override is used")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("skillChoice:tuxi:left")},
                   "choice ChoiceMade uses skillChoice:<skill>:<answer>")
        || !expect(fixture.probe.recordedEvents()
                       == (QList<TriggerEvent>() << EventAskForChoice << ChoiceMade),
                   "EventAskForChoice precedes ChoiceMade"))
        return false;

    fixture.probe.records.clear();
    fixture.probe.forceChoice = QStringLiteral("forced");
    const QString forced = fixture.room.askForChoice(
        fixture.player, QStringLiteral("tuxi"), QStringLiteral("left+right"));
    if (!expect(forced == QStringLiteral("forced"), "EventAskForChoice forced answer wins")
        || !expect(fixture.probe.payloads(ChoiceMade).isEmpty(),
                   "forced answer emits no ChoiceMade"))
        return false;

    fixture.probe.forceChoice.clear();
    fixture.probe.cancelChoice = true;
    const QString canceled = fixture.room.askForChoice(
        fixture.player, QStringLiteral("tuxi"), QStringLiteral("left+right"));
    if (!expect(canceled.isEmpty(), "EventAskForChoice cancel returns empty")
        || !expect(fixture.probe.payloads(ChoiceMade).isEmpty(),
                   "cancel emits no ChoiceMade"))
        return false;

    fixture.probe.cancelChoice = false;
    fixture.probe.records.clear();
    fixture.room.clearTestOverrides();
    fixture.ai()->choiceValue = QStringLiteral("right");
    const QString aiAnswer = fixture.room.askForChoice(
        fixture.player, QStringLiteral("tuxi"), QStringLiteral("left+right"));
    if (!expect(aiAnswer == QStringLiteral("right"), "AI choice is accepted when listed")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("skillChoice:tuxi:right")},
                   "AI ChoiceMade uses the AI answer"))
        return false;

    fixture.ai()->choiceValue = QStringLiteral("invalid");
    qsanSeedRandom(1);
    const QString firstFallback = fixture.room.askForChoice(
        fixture.player, QStringLiteral("tuxi"), QStringLiteral("left+right"));
    qsanSeedRandom(1);
    const QString secondFallback = fixture.room.askForChoice(
        fixture.player, QStringLiteral("tuxi"), QStringLiteral("left+right"));
    const QString single = fixture.room.askForChoice(
        fixture.player, QStringLiteral("tuxi"), QStringLiteral("only"));
    return expect(firstFallback == secondFallback,
                  "invalid choice fallback is seed-deterministic")
        && expect(QStringList{QStringLiteral("left"), QStringLiteral("right")}.contains(firstFallback),
                  "invalid choice fallback stays in the list")
        && expect(single == QStringLiteral("only"),
                  "a single choice skips the + split path");
}

static bool choiceClientAnswer()
{
    DecisionFixture v1Fixture(QStringLiteral("online"));
    ClientReplyAgent v1Agent(v1Fixture.room, v1Fixture.player, ProtocolVersion::V2);
    v1Agent.replyByCommand.insert(S_COMMAND_MULTIPLE_CHOICE, QStringLiteral("right"));
    const QString v1Answer = v1Fixture.room.askForChoice(
        v1Fixture.player, QStringLiteral("tuxi"), QStringLiteral("left+right"));
    const QStringList v1Choices = v1Fixture.probe.payloads(ChoiceMade);
    if (!expect(v1Agent.protocolReady && !v1Agent.parseFailed,
                "V1 online choice protocol is ready")
        || !expect(v1Answer == QStringLiteral("right"),
                   "V1 online choice reply is accepted")
        || !expect(QJsonDocument::fromJson(v1Agent.lastRequestWire).isObject()
                       && QJsonDocument::fromJson(v1Agent.lastReplyWire).isObject(),
                   "first V2 choice request and reply are objects")) {
        return false;
    }

    DecisionFixture v2Fixture(QStringLiteral("online"));
    ClientReplyAgent v2Agent(v2Fixture.room, v2Fixture.player, ProtocolVersion::V2);
    v2Agent.replyByCommand.insert(S_COMMAND_MULTIPLE_CHOICE, QStringLiteral("right"));
    const QString v2Answer = v2Fixture.room.askForChoice(
        v2Fixture.player, QStringLiteral("tuxi"), QStringLiteral("left+right"));
    const QStringList v2Choices = v2Fixture.probe.payloads(ChoiceMade);
    if (!expect(v2Agent.protocolReady && !v2Agent.parseFailed,
                "V2 online choice protocol is ready")
        || !expect(v2Answer == QStringLiteral("right"),
                   "V2 online choice reply is accepted")
        || !expect(QJsonDocument::fromJson(v2Agent.lastRequestWire).isObject()
                       && QJsonDocument::fromJson(v2Agent.lastReplyWire).isObject(),
                   "V2 choice request and reply are objects")) {
        return false;
    }

    return expect(v1Answer == v2Answer, "V1 and V2 choice answers match")
        && expect(v1Choices == QStringList{QStringLiteral("skillChoice:tuxi:right")}
                      && v2Choices == v1Choices,
                  "V1 and V2 ChoiceMade payloads match exactly");
}

static bool simpleChoiceClientV1V2Parity()
{
    auto wireMatchesVersion = [](const ClientReplyAgent &agent,
                                 ProtocolVersion version) {
        const QJsonDocument request = QJsonDocument::fromJson(agent.lastRequestWire);
        const QJsonDocument reply = QJsonDocument::fromJson(agent.lastReplyWire);
        return version == ProtocolVersion::V2
            ? request.isObject() && reply.isObject()
            : request.isArray() && reply.isArray();
    };

    DecisionFixture generalV1(QStringLiteral("online"));
    ClientReplyAgent generalV1Agent(
        generalV1.room, generalV1.player, ProtocolVersion::V2);
    generalV1Agent.replyByCommand.insert(
        S_COMMAND_CHOOSE_GENERAL, QStringLiteral("liubei"));
    const QString generalV1Answer = generalV1.room.askForGeneral(
        generalV1.player,
        QStringList{QStringLiteral("caocao"), QStringLiteral("liubei")});

    DecisionFixture generalV2(QStringLiteral("online"));
    ClientReplyAgent generalV2Agent(
        generalV2.room, generalV2.player, ProtocolVersion::V2);
    generalV2Agent.replyByCommand.insert(
        S_COMMAND_CHOOSE_GENERAL, QStringLiteral("liubei"));
    const QString generalV2Answer = generalV2.room.askForGeneral(
        generalV2.player,
        QStringList{QStringLiteral("caocao"), QStringLiteral("liubei")});
    if (!expect(generalV1Answer == generalV2Answer
                    && generalV2Answer == QStringLiteral("liubei"),
                "V1/V2 choose general gameplay answers match")
        || !expect(wireMatchesVersion(generalV1Agent, ProtocolVersion::V2)
                       && wireMatchesVersion(generalV2Agent, ProtocolVersion::V2),
                   "choose general uses versioned request and reply wire")) {
        return false;
    }

    DecisionFixture suitV1(QStringLiteral("online"));
    ClientReplyAgent suitV1Agent(suitV1.room, suitV1.player, ProtocolVersion::V2);
    suitV1Agent.replyByCommand.insert(S_COMMAND_CHOOSE_SUIT, QStringLiteral("diamond"));
    const Card::Suit suitV1Answer = suitV1.room.askForSuit(
        suitV1.player, QStringLiteral("test"));

    DecisionFixture suitV2(QStringLiteral("online"));
    ClientReplyAgent suitV2Agent(suitV2.room, suitV2.player, ProtocolVersion::V2);
    suitV2Agent.replyByCommand.insert(S_COMMAND_CHOOSE_SUIT, QStringLiteral("diamond"));
    const Card::Suit suitV2Answer = suitV2.room.askForSuit(
        suitV2.player, QStringLiteral("test"));
    if (!expect(suitV1Answer == suitV2Answer && suitV2Answer == Card::Diamond,
                "V1/V2 choose suit gameplay answers match")
        || !expect(wireMatchesVersion(suitV1Agent, ProtocolVersion::V2)
                       && wireMatchesVersion(suitV2Agent, ProtocolVersion::V2),
                   "choose suit uses versioned request and reply wire")) {
        return false;
    }

    DecisionFixture kingdomV1(QStringLiteral("online"));
    ClientReplyAgent kingdomV1Agent(
        kingdomV1.room, kingdomV1.player, ProtocolVersion::V2);
    kingdomV1Agent.replyByCommand.insert(
        S_COMMAND_CHOOSE_KINGDOM, QStringLiteral("shu"));
    const QString kingdomV1Answer = kingdomV1.room.askForKingdom(
        kingdomV1.player, QStringLiteral("test"),
        QStringList{QStringLiteral("wei"), QStringLiteral("shu")}, false);

    DecisionFixture kingdomV2(QStringLiteral("online"));
    ClientReplyAgent kingdomV2Agent(
        kingdomV2.room, kingdomV2.player, ProtocolVersion::V2);
    kingdomV2Agent.replyByCommand.insert(
        S_COMMAND_CHOOSE_KINGDOM, QStringLiteral("shu"));
    const QString kingdomV2Answer = kingdomV2.room.askForKingdom(
        kingdomV2.player, QStringLiteral("test"),
        QStringList{QStringLiteral("wei"), QStringLiteral("shu")}, false);
    if (!expect(kingdomV1Answer == kingdomV2Answer
                    && kingdomV2Answer == QStringLiteral("shu"),
                "V1/V2 choose kingdom gameplay answers match")
        || !expect(wireMatchesVersion(kingdomV1Agent, ProtocolVersion::V2)
                       && wireMatchesVersion(kingdomV2Agent, ProtocolVersion::V2),
                   "choose kingdom uses versioned request and reply wire")) {
        return false;
    }

    DecisionFixture orderV1(QStringLiteral("online"));
    ClientReplyAgent orderV1Agent(orderV1.room, orderV1.player, ProtocolVersion::V2);
    orderV1Agent.replyByCommand.insert(
        S_COMMAND_CHOOSE_ORDER, static_cast<int>(S_CAMP_COOL));
    const QString orderV1Answer = PlayerDecisionServiceTestAccess::askForOrder(
        orderV1.room, orderV1.player, QStringLiteral("warm"));

    DecisionFixture orderV2(QStringLiteral("online"));
    ClientReplyAgent orderV2Agent(orderV2.room, orderV2.player, ProtocolVersion::V2);
    orderV2Agent.replyByCommand.insert(
        S_COMMAND_CHOOSE_ORDER, static_cast<int>(S_CAMP_COOL));
    const QString orderV2Answer = PlayerDecisionServiceTestAccess::askForOrder(
        orderV2.room, orderV2.player, QStringLiteral("warm"));
    if (!expect(orderV1Answer == orderV2Answer
                    && orderV2Answer == QStringLiteral("cool"),
                "V1/V2 choose order gameplay answers match")
        || !expect(wireMatchesVersion(orderV1Agent, ProtocolVersion::V2)
                       && wireMatchesVersion(orderV2Agent, ProtocolVersion::V2),
                   "choose order uses numeric V1 and enum-string V2 wire")) {
        return false;
    }

    DecisionFixture invokeV1(QStringLiteral("online"));
    ClientReplyAgent invokeV1Agent(invokeV1.room, invokeV1.player, ProtocolVersion::V2);
    invokeV1Agent.replyByCommand.insert(S_COMMAND_INVOKE_SKILL, true);
    const bool invokeV1Answer = invokeV1.room.askForSkillInvoke(
        invokeV1.player, QStringLiteral("test_skill"),
        QStringLiteral("playerdata:decision-other"), false);

    DecisionFixture invokeV2(QStringLiteral("online"));
    ClientReplyAgent invokeV2Agent(invokeV2.room, invokeV2.player, ProtocolVersion::V2);
    invokeV2Agent.replyByCommand.insert(S_COMMAND_INVOKE_SKILL, true);
    const bool invokeV2Answer = invokeV2.room.askForSkillInvoke(
        invokeV2.player, QStringLiteral("test_skill"),
        QStringLiteral("playerdata:decision-other"), false);
    return expect(invokeV1Answer == invokeV2Answer && invokeV2Answer,
                  "V1/V2 invoke skill gameplay answers match")
        && expect(wireMatchesVersion(invokeV1Agent, ProtocolVersion::V2)
                      && wireMatchesVersion(invokeV2Agent, ProtocolVersion::V2),
                  "invoke skill uses versioned request and reply wire")
        && expect(!generalV1Agent.parseFailed && !generalV2Agent.parseFailed
                      && !suitV1Agent.parseFailed && !suitV2Agent.parseFailed
                      && !kingdomV1Agent.parseFailed && !kingdomV2Agent.parseFailed
                      && !orderV1Agent.parseFailed && !orderV2Agent.parseFailed
                      && !invokeV1Agent.parseFailed && !invokeV2Agent.parseFailed,
                  "simple choice differential agents parse every frame");
}

static bool suitKingdomGeneralAndModeChoices()
{
    DecisionFixture fixture;
    fixture.ai()->suitValue = Card::Heart;
    if (!expect(fixture.room.askForSuit(fixture.player, QStringLiteral("luoyi")) == Card::Heart,
                "AI suit is used")
        || !expect(fixture.probe.payloads(ChoiceMade).isEmpty(),
                   "suit emits no ChoiceMade"))
        return false;

    fixture.ai()->kingdomValue = QStringLiteral("shu");
    const QString kingdom = fixture.room.askForKingdom(
        fixture.player, QString(),
        QStringList{QStringLiteral("wei"), QStringLiteral("shu")}, false);
    const QString kingdomWrapper = fixture.room.askForKingdom(
        fixture.player, QStringLiteral("gamerule_kingdom"), QStringLiteral("wei+shu"), false);
    if (!expect(kingdom == QStringLiteral("shu"), "AI kingdom is used")
        || !expect(kingdomWrapper == QStringLiteral("shu"),
                   "string kingdom overload splits on +")
        || !expect(fixture.probe.payloads(ChoiceMade).isEmpty(),
                   "kingdom emits no ChoiceMade"))
        return false;

    fixture.ai()->kingdomValue = QStringLiteral("wu");
    qsanSeedRandom(7);
    const QString invalidKingdom = fixture.room.askForKingdom(
        fixture.player, QString(),
        QStringList{QStringLiteral("wei"), QStringLiteral("shu")}, false);
    qsanSeedRandom(7);
    const QString invalidKingdomAgain = fixture.room.askForKingdom(
        fixture.player, QString(),
        QStringList{QStringLiteral("wei"), QStringLiteral("shu")}, false);
    if (!expect(invalidKingdom == invalidKingdomAgain,
                "invalid kingdom fallback is seed-deterministic")
        || !expect(QStringList{QStringLiteral("wei"), QStringLiteral("shu")}.contains(invalidKingdom),
                   "invalid kingdom fallback stays in the list"))
        return false;

    if (!expect(fixture.room.askForGeneral(fixture.player, QStringList())
                    == QStringLiteral("caocao"),
                "empty general list returns caocao")
        || !expect(fixture.room.askForGeneral(fixture.player, QStringList{QStringLiteral("zhangfei")})
                       == QStringLiteral("zhangfei"),
                   "singleton general list returns that general"))
        return false;

    fixture.ai()->generalValue = QStringLiteral("liubei");
    const QString general = fixture.room.askForGeneral(
        fixture.player, QStringList{QStringLiteral("caocao"), QStringLiteral("liubei")},
        QStringLiteral("caocao"));
    const QString generalWrapper = fixture.room.askForGeneral(
        fixture.player, QStringLiteral("caocao+liubei"), QStringLiteral("caocao"));
    if (!expect(general == QStringLiteral("liubei"), "AI general is used")
        || !expect(generalWrapper == QStringLiteral("liubei"),
                   "string general overload splits on +")
        || !expect(fixture.probe.payloads(ChoiceMade).isEmpty(),
                   "pre-game general selection emits no ChoiceMade"))
        return false;

    PlayerDecisionServiceTestAccess::setGameState(fixture.room, 1);
    fixture.probe.generalChoosingReplacement = QStringList{QStringLiteral("zhaoyun"),
                                                           QStringLiteral("machao")};
    fixture.probe.generalChosenReplacement = QStringLiteral("machao");
    fixture.ai()->generalValue = QStringLiteral("zhaoyun");
    const QString mutated = fixture.room.askForGeneral(
        fixture.player, QStringList{QStringLiteral("caocao"), QStringLiteral("liubei")});
    return expect(mutated == QStringLiteral("machao"),
                  "GeneralChoosing/GeneralChosen mutate the selected general")
        && expect(fixture.probe.recordedEvents().contains(GeneralChoosing)
                      && fixture.probe.recordedEvents().contains(GeneralChosen),
                  "in-game general selection dispatches both general events");
}

static bool suitKingdomGeneralClientAndInvalidReplies()
{
    DecisionFixture fixture(QStringLiteral("online"));
    ClientReplyAgent agent(fixture.room, fixture.player);
    agent.replyByCommand.insert(S_COMMAND_CHOOSE_SUIT, QStringLiteral("club"));
    agent.replyByCommand.insert(S_COMMAND_CHOOSE_KINGDOM, QStringLiteral("wei"));
    agent.replyByCommand.insert(S_COMMAND_CHOOSE_GENERAL, QStringLiteral("liubei"));
    const Card::Suit suit = fixture.room.askForSuit(fixture.player, QStringLiteral("luoyi"));
    const QString kingdom = fixture.room.askForKingdom(
        fixture.player, QStringLiteral("choice-reason"), QStringLiteral("wei+shu"), false);
    const QString general = fixture.room.askForGeneral(
        fixture.player, QStringList{QStringLiteral("caocao"), QStringLiteral("liubei")});
    if (!expect(suit == Card::Club, "online suit reply is mapped")
        || !expect(kingdom == QStringLiteral("wei"), "online kingdom reply is accepted")
        || !expect(general == QStringLiteral("liubei"), "online general reply is accepted"))
        return false;

    agent.replyByCommand.insert(S_COMMAND_CHOOSE_SUIT, QStringLiteral("not-a-suit"));
    qsanSeedRandom(3);
    const Card::Suit invalidSuit = fixture.room.askForSuit(fixture.player, QStringLiteral("luoyi"));
    qsanSeedRandom(3);
    const Card::Suit invalidSuitAgain = fixture.room.askForSuit(
        fixture.player, QStringLiteral("luoyi"));
    agent.replyByCommand.insert(S_COMMAND_CHOOSE_GENERAL, QStringLiteral("nobody"));
    const QString fallbackGeneral = fixture.room.askForGeneral(
        fixture.player, QStringList{QStringLiteral("caocao"), QStringLiteral("liubei")},
        QStringLiteral("caocao"));
    return expect(invalidSuit == invalidSuitAgain,
                  "unmapped suit reply keeps the seeded random default")
        && expect(fallbackGeneral == QStringLiteral("caocao"),
                  "invalid general reply uses the default choice");
}

static bool orderAndRolePreserveLegacyFallbacks()
{
    DecisionFixture robot;
    const QString aiOrder = PlayerDecisionServiceTestAccess::askForOrder(
        robot.room, robot.player, QStringLiteral("cool"));
    const QString aiRole = PlayerDecisionServiceTestAccess::askForRole(
        robot.room, robot.player,
        QStringList{QStringLiteral("lord"), QStringLiteral("loyalist"), QStringLiteral("lord")},
        QStringLiteral("3v3"));
    if (!expect(aiOrder == QStringLiteral("cool"), "AI order returns default_choice")
        || !expect(aiRole == QStringLiteral("abstain"),
                   "AI/timeout role falls back to abstain")
        || !expect(robot.probe.payloads(ChoiceMade).isEmpty(),
                   "Order/Role emit no ChoiceMade"))
        return false;

    DecisionFixture online(QStringLiteral("online"));
    ClientReplyAgent agent(online.room, online.player);
    agent.replyByCommand.insert(S_COMMAND_CHOOSE_ORDER, QVariant(int(S_CAMP_WARM)));
    agent.replyByCommand.insert(S_COMMAND_CHOOSE_ROLE_3V3, QStringLiteral("not-a-role"));
    const QString warm = PlayerDecisionServiceTestAccess::askForOrder(
        online.room, online.player, QStringLiteral("cool"));
    const QString anyRole = PlayerDecisionServiceTestAccess::askForRole(
        online.room, online.player, QStringList{QStringLiteral("lord")}, QStringLiteral("3v3"));
    agent.replyByCommand.insert(S_COMMAND_CHOOSE_ORDER, QVariant(int(S_CAMP_COOL)));
    const QString cool = PlayerDecisionServiceTestAccess::askForOrder(
        online.room, online.player, QStringLiteral("warm"));
    return expect(warm == QStringLiteral("warm"), "numeric warm maps to warm")
        && expect(cool == QStringLiteral("cool"), "any other numeric camp maps to cool")
        && expect(anyRole == QStringLiteral("not-a-role"),
                  "Role accepts any string reply")
        && expect(online.probe.payloads(ChoiceMade).isEmpty(),
                  "client Order/Role still emit no ChoiceMade");
}

static QStringList objectNames(const QList<ServerPlayer *> &players)
{
    QStringList names;
    foreach (ServerPlayer *player, players)
        names << player->objectName();
    return names;
}

static bool playerChosenEmptySingletonOverrideAndNotify()
{
    DecisionFixture fixture;
    RequestRecorder recorder;
    recorder.watch(fixture.player);

    ServerPlayer *emptyChoice = fixture.room.askForPlayerChosen(
        fixture.player, QList<ServerPlayer *>(), QStringLiteral("tuxi"));
    if (!expect(emptyChoice == nullptr, "empty targets return nullptr")
        || !expect(fixture.probe.payloads(ChoiceMade).isEmpty(),
                   "empty targets emit no ChoiceMade")
        || !expect(!recorder.contains(S_COMMAND_CHOOSE_PLAYER),
                   "empty targets send no choose-player request"))
        return false;

    fixture.probe.records.clear();
    ServerPlayer *singleton = fixture.room.askForPlayerChosen(
        fixture.player, QList<ServerPlayer *>{fixture.other}, QStringLiteral("tuxi"),
        QString(), false, true);
    if (!expect(singleton == fixture.other, "singleton returns the only target")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == (QStringList() << QStringLiteral("notifyInvoked:tuxi")
                                         << QStringLiteral("skillInvoke:tuxi:yes")
                                         << QStringLiteral("playerChosen:tuxi:decision-other")),
                   "notify singleton emits notifyInvoked, skillInvoke, then playerChosen")
        || !expect(!recorder.contains(S_COMMAND_CHOOSE_PLAYER),
                   "non-optional singleton skips the client request"))
        return false;

    fixture.probe.records.clear();
    fixture.room.registerTestOverride(fixture.player, QStringLiteral("player_chosen"),
                                      QStringLiteral("tuxi"),
                                      QStringLiteral("decision-other"));
    ServerPlayer *overridden = fixture.room.askForPlayerChosen(
        fixture.player,
        QList<ServerPlayer *>{fixture.player, fixture.other},
        QStringLiteral("tuxi$2"));
    if (!expect(overridden == fixture.other, "$ skill override key uses the stripped name")
        || !expect(!recorder.contains(S_COMMAND_CHOOSE_PLAYER),
                   "override hit does not send a client choose-player request")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("playerChosen:tuxi:decision-other")},
                   "override ChoiceMade uses playerChosen:<skill>:<objectName>"))
        return false;

    fixture.probe.records.clear();
    fixture.room.clearTestOverrides();
    fixture.room.registerTestOverride(fixture.player, QStringLiteral("player_chosen"),
                                      QStringLiteral("tuxi"), QVariant(QString()));
    ServerPlayer *emptyOverride = fixture.room.askForPlayerChosen(
        fixture.player,
        QList<ServerPlayer *>{fixture.player, fixture.other},
        QStringLiteral("tuxi"));
    if (!expect(emptyOverride == fixture.player,
                "empty-string override stays valid and keeps the first target")
        || !expect(!recorder.contains(S_COMMAND_CHOOSE_PLAYER),
                   "valid empty override does not fall through to AI or client")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("playerChosen:tuxi:decision-player")},
                   "empty override still emits playerChosen for the first target"))
        return false;

    fixture.probe.records.clear();
    fixture.room.clearTestOverrides();
    fixture.room.registerTestOverride(fixture.player, QStringLiteral("player_chosen"),
                                      QStringLiteral("tuxi"), QVariant(0));
    ServerPlayer *zeroOverride = fixture.room.askForPlayerChosen(
        fixture.player,
        QList<ServerPlayer *>{fixture.player, fixture.other},
        QStringLiteral("tuxi"));
    if (!expect(zeroOverride == fixture.player,
                "zero override stays valid and keeps the first target")
        || !expect(!recorder.contains(S_COMMAND_CHOOSE_PLAYER),
                   "valid zero override does not fall through to AI or client"))
        return false;

    fixture.room.clearTestOverrides();
    fixture.ai()->hasPlayerChosenValue = true;
    fixture.ai()->playerChosenValue = fixture.other;
    fixture.probe.records.clear();
    ServerPlayer *aiChoice = fixture.room.askForPlayerChosen(
        fixture.player,
        QList<ServerPlayer *>{fixture.player, fixture.other},
        QStringLiteral("tuxi"));
    return expect(aiChoice == fixture.other, "AI playerChosen is accepted")
        && expect(fixture.probe.payloads(ChoiceMade)
                      == QStringList{QStringLiteral("playerChosen:tuxi:decision-other")},
                  "AI ChoiceMade uses the AI target");
}

static bool playerChosenClientInvalidOptionalAndTimeout()
{
    DecisionFixture fixture(QStringLiteral("online"));
    ClientReplyAgent agent(fixture.room, fixture.player);
    agent.replyByCommand.insert(S_COMMAND_CHOOSE_PLAYER, QStringLiteral("decision-other"));
    ServerPlayer *clientChoice = fixture.room.askForPlayerChosen(
        fixture.player,
        QList<ServerPlayer *>{fixture.player, fixture.other},
        QStringLiteral("tuxi"));
    if (!expect(clientChoice == fixture.other, "online object-name reply is accepted")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("playerChosen:tuxi:decision-other")},
                   "client ChoiceMade uses the replied object name"))
        return false;

    fixture.probe.records.clear();
    agent.replyByCommand.insert(S_COMMAND_CHOOSE_PLAYER, QStringLiteral("nobody"));
    qsanSeedRandom(4);
    ServerPlayer *invalidFirst = fixture.room.askForPlayerChosen(
        fixture.player,
        QList<ServerPlayer *>{fixture.player, fixture.other},
        QStringLiteral("tuxi"));
    qsanSeedRandom(4);
    ServerPlayer *invalidAgain = fixture.room.askForPlayerChosen(
        fixture.player,
        QList<ServerPlayer *>{fixture.player, fixture.other},
        QStringLiteral("tuxi"));
    if (!expect(invalidFirst == invalidAgain,
                "invalid object name keeps the seeded random fallback")
        || !expect(invalidFirst == fixture.player || invalidFirst == fixture.other,
                   "invalid object name fallback stays in the target list")
        || !expect(fixture.probe.payloads(ChoiceMade).length() == 2,
                   "invalid object name still emits playerChosen after fallback"))
        return false;

    fixture.probe.records.clear();
    agent.replyByCommand.remove(S_COMMAND_CHOOSE_PLAYER);
    ServerPlayer *optionalCancel = fixture.room.askForPlayerChosen(
        fixture.player, QList<ServerPlayer *>{fixture.other}, QStringLiteral("tuxi"),
        QString(), true, false);
    return expect(optionalCancel == nullptr, "optional timeout/cancel returns nullptr")
        && expect(fixture.probe.payloads(ChoiceMade).isEmpty(),
                  "optional cancel emits no ChoiceMade");
}

static bool playersChosenMinMaxSortAndNegativeMin()
{
    DecisionFixture fixture;
    ServerPlayer *third = PlayerDecisionServiceTestAccess::addPlayer(
        fixture.room, QStringLiteral("decision-third"));
    RequestRecorder recorder;
    recorder.watch(fixture.player);
    const QList<ServerPlayer *> pair{fixture.other, third};

    fixture.probe.records.clear();
    QList<ServerPlayer *> allRequired = fixture.room.askForPlayersChosen(
        fixture.player, pair, QStringLiteral("tuxi"), 2, 2);
    if (!expect(objectNames(allRequired)
                    == (QStringList() << QStringLiteral("decision-other")
                                      << QStringLiteral("decision-third")),
                "candidate count equal to min_num returns every target")
        || !expect(!recorder.contains(S_COMMAND_CHOOSE_PLAYER),
                   "min_num covering all targets skips the request")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral(
                              "playerChosen:tuxi:decision-other+decision-third")},
                   "full-set ChoiceMade joins names with +"))
        return false;

    const QList<ServerPlayer *> three{fixture.player, fixture.other, third};
    fixture.ai()->hasPlayersChosenValue = true;
    fixture.ai()->playersChosenValue = QList<ServerPlayer *>{third, fixture.other};
    fixture.probe.records.clear();
    QList<ServerPlayer *> sorted = fixture.room.askForPlayersChosen(
        fixture.player, three, QStringLiteral("tuxi"), 1, 2, QString(), false, true);
    if (!expect(objectNames(sorted)
                    == (QStringList() << QStringLiteral("decision-other")
                                      << QStringLiteral("decision-third")),
                "sort_ActionOrder true reorders by roster")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral(
                              "playerChosen:tuxi:decision-other+decision-third")},
                   "sorted ChoiceMade uses roster order"))
        return false;

    fixture.probe.records.clear();
    QList<ServerPlayer *> unsorted = fixture.room.askForPlayersChosen(
        fixture.player, three, QStringLiteral("tuxi"), 1, 2, QString(), false, false);
    if (!expect(objectNames(unsorted)
                    == (QStringList() << QStringLiteral("decision-third")
                                      << QStringLiteral("decision-other")),
                "sort_ActionOrder false keeps AI order")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral(
                              "playerChosen:tuxi:decision-third+decision-other")},
                   "unsorted ChoiceMade keeps AI order"))
        return false;

    fixture.ai()->playersChosenValue = QList<ServerPlayer *>{fixture.other};
    fixture.probe.records.clear();
    QList<ServerPlayer *> cleared = fixture.room.askForPlayersChosen(
        fixture.player, three, QStringLiteral("tuxi"), -1, 2);
    return expect(cleared.isEmpty(),
                  "negative min_num clears a result whose count is not max_num")
        && expect(fixture.probe.payloads(ChoiceMade).isEmpty(),
                  "cleared negative-min result emits no ChoiceMade");
}

static bool playersChosenClientFillAndNotify()
{
    DecisionFixture fixture(QStringLiteral("online"));
    ServerPlayer *third = PlayerDecisionServiceTestAccess::addPlayer(
        fixture.room, QStringLiteral("decision-third"));
    ClientReplyAgent agent(fixture.room, fixture.player);
    const QList<ServerPlayer *> three{fixture.player, fixture.other, third};

    agent.replyByCommand.insert(S_COMMAND_CHOOSE_PLAYER,
                                QStringLiteral("decision-third+decision-other"));
    QList<ServerPlayer *> clientChoice = fixture.room.askForPlayersChosen(
        fixture.player, three, QStringLiteral("tuxi$2"), 1, 2, QString(), true, false);
    if (!expect(objectNames(clientChoice)
                    == (QStringList() << QStringLiteral("decision-third")
                                      << QStringLiteral("decision-other")),
                "client + joined names are parsed in reply order")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == (QStringList() << QStringLiteral("notifyInvoked:tuxi")
                                         << QStringLiteral("skillInvoke:tuxi:yes")
                                         << QStringLiteral(
                                                "playerChosen:tuxi:decision-third+decision-other")),
                   "notify multi-select emits notifyInvoked, skillInvoke, then joined playerChosen"))
        return false;

    fixture.probe.records.clear();
    agent.replyByCommand.insert(S_COMMAND_CHOOSE_PLAYER,
                                QStringLiteral("nobody+decision-other"));
    qsanSeedRandom(5);
    QList<ServerPlayer *> filled = fixture.room.askForPlayersChosen(
        fixture.player, three, QStringLiteral("tuxi"), 2, 2, QString(), false, false);
    qsanSeedRandom(5);
    QList<ServerPlayer *> filledAgain = fixture.room.askForPlayersChosen(
        fixture.player, three, QStringLiteral("tuxi"), 2, 2, QString(), false, false);
    return expect(filled.length() == 2, "min_num fills until the lower bound")
        && expect(filled.contains(fixture.other),
                  "valid names in a mixed reply are kept")
        && expect(objectNames(filled) == objectNames(filledAgain),
                  "min_num random fill is seed-deterministic")
        && expect(!filled.contains(nullptr), "invalid names are dropped before fill");
}

static bool agEmptySingletonRefusableInvalidAndClient()
{
    DecisionFixture fixture;
    RequestRecorder recorder;
    recorder.watch(fixture.player);

    const int emptyId = fixture.room.askForAG(fixture.player, QList<int>(), false,
                                              QStringLiteral("amazing_grace"));
    if (!expect(emptyId == -1, "empty AG list returns -1")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("AGChosen:amazing_grace:-1")},
                   "empty AG still emits AGChosen:<reason>:-1")
        || !expect(!recorder.contains(S_COMMAND_AMAZING_GRACE),
                   "empty AG sends no request"))
        return false;

    fixture.probe.records.clear();
    const int singleton = fixture.room.askForAG(
        fixture.player, QList<int>{7}, false, QStringLiteral("amazing_grace"));
    if (!expect(singleton == 7, "non-refusable singleton returns the only id")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("AGChosen:amazing_grace:7")},
                   "singleton AGChosen uses the only id")
        || !expect(!recorder.contains(S_COMMAND_AMAZING_GRACE),
                   "non-refusable singleton skips the AG request"))
        return false;

    fixture.probe.records.clear();
    fixture.ai()->agValue = -1;
    const int refused = fixture.room.askForAG(
        fixture.player, QList<int>{7, 11}, true, QStringLiteral("amazing_grace"));
    if (!expect(refused == -1, "refusable AI -1 is kept")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("AGChosen:amazing_grace:-1")},
                   "refusable cancel emits AGChosen:<reason>:-1"))
        return false;

    fixture.probe.records.clear();
    fixture.ai()->agValue = 11;
    const int aiId = fixture.room.askForAG(
        fixture.player, QList<int>{7, 11}, false, QStringLiteral("amazing_grace"));
    if (!expect(aiId == 11, "AI AG id is accepted when listed")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("AGChosen:amazing_grace:11")},
                   "AI AGChosen uses the AI id"))
        return false;

    fixture.probe.records.clear();
    fixture.ai()->agValue = 99;
    const int disabled = fixture.room.askForAG(
        fixture.player, QList<int>{7, 11}, false, QStringLiteral("amazing_grace"));
    if (!expect(disabled == 7, "id outside the list falls back to first")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("AGChosen:amazing_grace:7")},
                   "disabled/invalid AG fallback still emits one AGChosen"))
        return false;

    DecisionFixture online(QStringLiteral("online"));
    ClientReplyAgent agent(online.room, online.player);
    agent.replyByCommand.insert(S_COMMAND_AMAZING_GRACE, 11);
    const int clientId = online.room.askForAG(
        online.player, QList<int>{7, 11}, false, QStringLiteral("amazing_grace"));
    if (!expect(clientId == 11, "online numeric AG reply is accepted")
        || !expect(online.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("AGChosen:amazing_grace:11")},
                   "client AGChosen uses the replied id"))
        return false;

    online.probe.records.clear();
    agent.replyByCommand.remove(S_COMMAND_AMAZING_GRACE);
    const int timeoutForced = online.room.askForAG(
        online.player, QList<int>{7, 11}, false, QStringLiteral("amazing_grace"));
    const int timeoutRefusable = online.room.askForAG(
        online.player, QList<int>{7, 11}, true, QStringLiteral("amazing_grace"));
    return expect(timeoutForced == 7, "timeout non-refusable AG falls back to first")
        && expect(timeoutRefusable == -1, "timeout refusable AG returns -1")
        && expect(online.probe.payloads(ChoiceMade)
                      == (QStringList() << QStringLiteral("AGChosen:amazing_grace:7")
                                        << QStringLiteral("AGChosen:amazing_grace:-1")),
                  "timeout AG still emits exactly one AGChosen per call");
}

static void giveHand(ServerPlayer *player, const QList<int> &ids, Room *room = nullptr)
{
    foreach (int id, ids) {
        player->addCard(id, Player::PlaceHand);
        if (room)
            room->setCardMapping(id, player, Player::PlaceHand);
    }
}

static int findCardKind(const char *kind)
{
    if (!Sanguosha)
        return -1;
    for (int i = 0; i < Sanguosha->getCardCount(); ++i) {
        const Card *card = Sanguosha->getCard(i);
        if (card && card->isKindOf(kind))
            return i;
    }
    return -1;
}

struct CardTable
{
    explicit CardTable(Room &room)
        : scope(*Sanguosha, &room)
    {
        room.roomRuntime()->state().reset();
    }

    EngineRuntimeContextScope scope;
};

static bool cardChosenOverrideFallbackVisibleAndClient()
{
    if (!Sanguosha || Sanguosha->getCardCount() < 2)
        return expect(false, "engine has at least two cards");

    DecisionFixture fixture;
    CardTable cards(fixture.room);
    RequestRecorder recorder;
    recorder.watch(fixture.player);
    giveHand(fixture.other, QList<int>{0, 1});

    fixture.room.registerTestOverride(fixture.player, QStringLiteral("card_chosen"),
                                      QStringLiteral("snatch"), 1);
    const int overridden = fixture.room.askForCardChosen(
        fixture.player, fixture.other, QStringLiteral("h"), QStringLiteral("snatch"));
    if (!expect(overridden == 1, "int override is used")
        || !expect(!recorder.contains(S_COMMAND_CHOOSE_CARD),
                   "override hit does not send a choose-card request")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("cardChosen:snatch:1:decision-other:")},
                   "override ChoiceMade uses cardChosen:<reason>:<id>:<who>:"))
        return false;

    fixture.probe.records.clear();
    fixture.room.clearTestOverrides();
    fixture.room.registerTestOverride(fixture.player, QStringLiteral("card_chosen"),
                                      QStringLiteral("snatch"), QVariant(0));
    const int zeroOverride = fixture.room.askForCardChosen(
        fixture.player, fixture.other, QStringLiteral("h"), QStringLiteral("snatch"));
    if (!expect(zeroOverride == 0, "zero override stays valid and is used")
        || !expect(!recorder.contains(S_COMMAND_CHOOSE_CARD),
                   "valid zero override does not fall through to AI or client"))
        return false;

    fixture.room.clearTestOverrides();
    fixture.ai()->cardChosenValue = 1;
    fixture.probe.records.clear();
    const int aiId = fixture.room.askForCardChosen(
        fixture.player, fixture.other, QStringLiteral("h"), QStringLiteral("snatch"),
        false, Card::MethodNone, QList<int>{0});
    const QVariant tag = fixture.player->getTag(QStringLiteral("cardChosenForAI"));
    if (!expect(aiId == 1, "AI cardChosen is accepted")
        || !expect(tag.toList() == QVariantList{0},
                   "cardChosenForAI tag keeps disabled ids after return")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("cardChosen:snatch:1:decision-other:")},
                   "AI ChoiceMade uses the AI id"))
        return false;

    fixture.ai()->cardChosenValue = -2;
    fixture.probe.records.clear();
    const int fallback = fixture.room.askForCardChosen(
        fixture.player, fixture.other, QStringLiteral("h"), QStringLiteral("snatch"),
        false, Card::MethodNone, QList<int>{0});
    if (!expect(fallback == 1, "disabled id is skipped then first eligible is used")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("cardChosen:snatch:1:decision-other:")},
                   "disabled fallback still emits one cardChosen"))
        return false;

    fixture.probe.records.clear();
    const int canceled = fixture.room.askForCardChosen(
        fixture.player, fixture.other, QStringLiteral("h"), QStringLiteral("snatch"),
        false, Card::MethodNone, QList<int>(), true);
    if (!expect(canceled == -1, "can_cancel keeps TrustAI -1")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("cardChosen:snatch:-1:decision-other:")},
                   "cancel still emits cardChosen with -1"))
        return false;

    fixture.probe.records.clear();
    recorder.notifications.clear();
    const int visible = fixture.room.askForCardChosen(
        fixture.player, fixture.other, QStringLiteral("h"), QStringLiteral("snatch"), true);
    if (!expect(recorder.containsNotification(S_COMMAND_SET_KNOWN_CARDS),
                "visible hands send SET_KNOWN_CARDS")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral(
                              "cardChosen:snatch:0:decision-other:visible")},
                   "visible ChoiceMade ends with :visible"))
        return false;
    Q_UNUSED(visible);

    DecisionFixture online(QStringLiteral("online"));
    CardTable onlineCards(online.room);
    giveHand(online.other, QList<int>{0, 1});
    ClientReplyAgent agent(online.room, online.player);
    agent.replyByCommand.insert(S_COMMAND_CHOOSE_CARD, 1);
    const int clientId = online.room.askForCardChosen(
        online.player, online.other, QStringLiteral("h"), QStringLiteral("snatch"));
    if (!expect(clientId == 1, "online numeric cardChosen reply is accepted")
        || !expect(online.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("cardChosen:snatch:1:decision-other:")},
                   "client ChoiceMade uses the replied id"))
        return false;

    online.probe.records.clear();
    agent.replyByCommand.remove(S_COMMAND_CHOOSE_CARD);
    const int timeoutId = online.room.askForCardChosen(
        online.player, online.other, QStringLiteral("h"), QStringLiteral("snatch"));
    return expect(timeoutId == 0, "timeout non-cancel cardChosen falls back to first eligible")
        && expect(online.probe.payloads(ChoiceMade)
                      == QStringList{QStringLiteral("cardChosen:snatch:0:decision-other:")},
                  "timeout fallback still emits one cardChosen");
}

static bool cardShowSingletonClientAndRandom()
{
    if (!Sanguosha || Sanguosha->getCardCount() < 2)
        return expect(false, "engine has at least two cards");

    DecisionFixture fixture;
    CardTable cards(fixture.room);
    RequestRecorder recorder;
    recorder.watch(fixture.player);
    giveHand(fixture.player, QList<int>{0});
    const Card *singleton = fixture.room.askForCardShow(
        fixture.player, fixture.other, QStringLiteral("rende"));
    if (!expect(singleton != nullptr && singleton->getId() == 0,
                "singleton CardShow returns the only hand card")
        || !expect(!recorder.contains(S_COMMAND_SHOW_CARD),
                   "singleton CardShow skips the request")
        || !expect(fixture.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("cardShow:rende:0")},
                   "singleton ChoiceMade uses cardShow:<reason>:<id>"))
        return false;

    DecisionFixture two;
    CardTable twoCards(two.room);
    giveHand(two.player, QList<int>{0, 1});
    two.ai()->hasCardShowValue = true;
    two.ai()->cardShowValue = Sanguosha->getCard(1);
    const Card *aiShow = two.room.askForCardShow(
        two.player, two.other, QStringLiteral("rende"));
    if (!expect(aiShow == Sanguosha->getCard(1), "AI CardShow is accepted")
        || !expect(two.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("cardShow:rende:1")},
                   "AI ChoiceMade uses the shown id"))
        return false;

    DecisionFixture online(QStringLiteral("online"));
    CardTable onlineCards(online.room);
    giveHand(online.player, QList<int>{0, 1});
    ClientReplyAgent agent(online.room, online.player);
    agent.replyByCommand.insert(S_COMMAND_SHOW_CARD, responseCardReply(QStringLiteral("1")));
    const Card *clientShow = online.room.askForCardShow(
        online.player, online.other, QStringLiteral("rende"));
    if (!expect(clientShow == Sanguosha->getCard(1), "online CardShow parse is accepted")
        || !expect(online.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("cardShow:rende:1")},
                   "client ChoiceMade uses the parsed id"))
        return false;

    online.probe.records.clear();
    agent.replyByCommand.insert(S_COMMAND_SHOW_CARD, responseCardReply(QStringLiteral("not-a-card")));
    qsanSeedRandom(6);
    const Card *invalidFirst = online.room.askForCardShow(
        online.player, online.other, QStringLiteral("rende"));
    qsanSeedRandom(6);
    const Card *invalidAgain = online.room.askForCardShow(
        online.player, online.other, QStringLiteral("rende"));
    return expect(invalidFirst != nullptr && invalidFirst == invalidAgain,
                  "invalid CardShow parse keeps the seeded random hand card")
        && expect(invalidFirst->getId() == 0 || invalidFirst->getId() == 1,
                  "random CardShow fallback stays in hand");
}

static bool pindianEmitsNoChoiceMade()
{
    if (!Sanguosha || Sanguosha->getCardCount() < 2)
        return expect(false, "engine has at least two cards");

    DecisionFixture dead;
    dead.other->setAlive(false);
    const Card *deadCard = dead.room.askForPindian(
        dead.player, dead.other, QStringLiteral("tianyi"));
    if (!expect(deadCard == nullptr, "dead participant returns nullptr")
        || !expect(dead.probe.payloads(ChoiceMade).isEmpty(),
                   "dead Pindian emits no ChoiceMade"))
        return false;

    DecisionFixture singleton;
    CardTable singletonCards(singleton.room);
    RequestRecorder recorder;
    recorder.watch(singleton.player);
    giveHand(singleton.player, QList<int>{0});
    const Card *only = singleton.room.askForPindian(
        singleton.player, singleton.other, QStringLiteral("tianyi"));
    if (!expect(only != nullptr && only->getId() == 0, "singleton Pindian returns the only card")
        || !expect(!recorder.contains(S_COMMAND_PINDIAN),
                   "singleton Pindian skips the request")
        || !expect(singleton.probe.payloads(ChoiceMade).isEmpty(),
                   "singleton Pindian emits no ChoiceMade"))
        return false;

    DecisionFixture ai;
    CardTable aiCards(ai.room);
    giveHand(ai.player, QList<int>{0, 1});
    ai.ai()->hasPindianValue = true;
    ai.ai()->pindianValue = Sanguosha->getCard(1);
    const Card *aiCard = ai.room.askForPindian(
        ai.player, ai.other, QStringLiteral("tianyi"));
    if (!expect(aiCard == Sanguosha->getCard(1), "AI Pindian is accepted")
        || !expect(ai.probe.payloads(ChoiceMade).isEmpty(),
                   "AI Pindian emits no ChoiceMade"))
        return false;

    DecisionFixture online(QStringLiteral("online"));
    CardTable onlineCards(online.room);
    giveHand(online.player, QList<int>{0, 1});
    ClientReplyAgent agent(online.room, online.player);
    agent.replyByCommand.insert(S_COMMAND_PINDIAN, responseCardReply(QStringLiteral("1")));
    const Card *clientCard = online.room.askForPindian(
        online.player, online.other, QStringLiteral("tianyi"));
    if (!expect(clientCard == Sanguosha->getCard(1), "online Pindian parse is accepted")
        || !expect(online.probe.payloads(ChoiceMade).isEmpty(),
                   "client Pindian emits no ChoiceMade"))
        return false;

    agent.replyByCommand.insert(S_COMMAND_PINDIAN,
                                responseCardReply(QStringLiteral("not-a-card")));
    qsanSeedRandom(7);
    const Card *invalidFirst = online.room.askForPindian(
        online.player, online.other, QStringLiteral("tianyi"));
    qsanSeedRandom(7);
    const Card *invalidAgain = online.room.askForPindian(
        online.player, online.other, QStringLiteral("tianyi"));
    return expect(invalidFirst != nullptr && invalidFirst == invalidAgain,
                  "invalid Pindian parse keeps the seeded random hand card")
        && expect(online.probe.payloads(ChoiceMade).isEmpty(),
                  "invalid Pindian fallback still emits no ChoiceMade");
}

static bool pindianRaceBroadcastIndependentFallback()
{
    if (!Sanguosha || Sanguosha->getCardCount() < 4)
        return expect(false, "engine has at least four cards");

    DecisionFixture dead;
    dead.other->setAlive(false);
    QList<const Card *> deadRace = dead.room.askForPindianRace(
        dead.player, dead.other, QStringLiteral("tianyi"));
    if (!expect(deadRace.size() == 2 && deadRace[0] == nullptr && deadRace[1] == nullptr,
                "dead PindianRace returns [nullptr, nullptr]")
        || !expect(dead.probe.payloads(ChoiceMade).isEmpty(),
                   "dead PindianRace emits no ChoiceMade"))
        return false;

    DecisionFixture bothSingle;
    CardTable bothCards(bothSingle.room);
    RequestRecorder fromSingle;
    RequestRecorder toSingle;
    fromSingle.watch(bothSingle.player);
    toSingle.watch(bothSingle.other);
    giveHand(bothSingle.player, QList<int>{0});
    giveHand(bothSingle.other, QList<int>{1});
    QList<const Card *> singles = bothSingle.room.askForPindianRace(
        bothSingle.player, bothSingle.other, QStringLiteral("tianyi"));
    if (!expect(singles.size() == 2 && singles[0] == bothSingle.player->getHandcards().first()
                    && singles[1] == bothSingle.other->getHandcards().first(),
                "both-singleton PindianRace returns [from, to]")
        || !expect(!fromSingle.contains(S_COMMAND_PINDIAN)
                       && !toSingle.contains(S_COMMAND_PINDIAN),
                   "resolved singleton sides are not asked")
        || !expect(bothSingle.probe.payloads(ChoiceMade).isEmpty(),
                   "singleton PindianRace emits no ChoiceMade"))
        return false;

    DecisionFixture mixed;
    CardTable mixedCardsTable(mixed.room);
    mixed.other->setState(QStringLiteral("online"));
    giveHand(mixed.player, QList<int>{0, 1});
    giveHand(mixed.other, QList<int>{2, 3});
    mixed.ai()->hasPindianValue = true;
    mixed.ai()->pindianValue = Sanguosha->getCard(1);
    RequestRecorder fromMixed;
    RequestRecorder toMixed;
    fromMixed.watch(mixed.player);
    toMixed.watch(mixed.other);
    ClientReplyAgent toAgent(mixed.room, mixed.other);
    toAgent.replyByCommand.insert(S_COMMAND_PINDIAN, responseCardReply(QStringLiteral("3")));
    QList<const Card *> mixedCards = mixed.room.askForPindianRace(
        mixed.player, mixed.other, QStringLiteral("tianyi"));
    if (!expect(mixedCards.size() == 2 && mixedCards[0] == Sanguosha->getCard(1)
                    && mixedCards[1] == Sanguosha->getCard(3),
                "mixed PindianRace keeps [from AI, to client]")
        || !expect(!fromMixed.contains(S_COMMAND_PINDIAN),
                   "resolved AI side is not included in the broadcast")
        || !expect(toMixed.count(S_COMMAND_PINDIAN) == 1,
                   "unresolved human receives one PINDIAN request")
        || !expect(mixed.probe.payloads(ChoiceMade).isEmpty(),
                   "mixed PindianRace emits no ChoiceMade"))
        return false;

    DecisionFixture humans(QStringLiteral("online"));
    CardTable humanCards(humans.room);
    humans.other->setState(QStringLiteral("online"));
    giveHand(humans.player, QList<int>{0, 1});
    giveHand(humans.other, QList<int>{2, 3});
    RequestRecorder fromHumans;
    RequestRecorder toHumans;
    fromHumans.watch(humans.player);
    toHumans.watch(humans.other);
    ClientReplyAgent fromAgent(humans.room, humans.player);
    ClientReplyAgent otherAgent(humans.room, humans.other);
    fromAgent.replyByCommand.insert(S_COMMAND_PINDIAN, responseCardReply(QStringLiteral("0")));
    otherAgent.replyByCommand.insert(S_COMMAND_PINDIAN,
                                     responseCardReply(QStringLiteral("not-a-card")));
    qsanSeedRandom(8);
    QList<const Card *> independent = humans.room.askForPindianRace(
        humans.player, humans.other, QStringLiteral("tianyi"));
    qsanSeedRandom(8);
    QList<const Card *> independentAgain = humans.room.askForPindianRace(
        humans.player, humans.other, QStringLiteral("tianyi"));
    return expect(fromHumans.count(S_COMMAND_PINDIAN) >= 1
                      && toHumans.count(S_COMMAND_PINDIAN) >= 1,
                  "both unresolved humans receive PINDIAN together")
        && expect(independent.size() == 2 && independent[0] == Sanguosha->getCard(0),
                  "valid from reply is kept")
        && expect(independent[1] != nullptr
                      && (independent[1]->getId() == 2 || independent[1]->getId() == 3),
                  "malformed to reply uses that side's random hand")
        && expect(independent[0] == independentAgain[0]
                      && independent[1] == independentAgain[1],
                  "independent fallback is seed-deterministic")
        && expect(humans.probe.payloads(ChoiceMade).isEmpty(),
                  "human PindianRace emits no ChoiceMade");
}

static bool cardResponseOverrideProvidedAndRetry()
{
    if (!Sanguosha || Sanguosha->getCardCount() < 2)
        return expect(false, "engine has at least two cards");

    DecisionFixture fixture;
    CardTable cards(fixture.room);
    RequestRecorder recorder;
    recorder.watch(fixture.player);
    giveHand(fixture.player, QList<int>{0, 1}, &fixture.room);

    fixture.room.registerTestOverride(fixture.player, QStringLiteral("card"), QStringLiteral("."),
                                      1);
    const Card *overridden = fixture.room.askForCard(
        fixture.player, QStringLiteral("."), QStringLiteral("@discard"), QVariant(),
        Card::MethodDiscard, nullptr, true);
    if (!expect(overridden == Sanguosha->getCard(1), "int card override is used on retrial")
        || !expect(!recorder.contains(S_COMMAND_RESPONSE_CARD),
                   "retrial override does not send RESPONSE_CARD")
        || !expect(fixture.probe.payloads(ChoiceMade).isEmpty(),
                   "retrial returns before ChoiceMade"))
        return false;

    fixture.room.clearTestOverrides();
    fixture.probe.records.clear();
    fixture.room.registerTestOverride(fixture.player, QStringLiteral("card"), QStringLiteral("."),
                                      QVariant(0));
    const Card *zero = fixture.room.askForCard(
        fixture.player, QStringLiteral("."), QStringLiteral("@discard"), QVariant(),
        Card::MethodDiscard, nullptr, true);
    if (!expect(zero == Sanguosha->getCard(0), "zero card override stays valid"))
        return false;

    fixture.room.clearTestOverrides();
    fixture.probe.records.clear();
    fixture.room.setTag(QStringLiteral("provided"), QVariant::fromValue(CardUseStruct(
        Sanguosha->getCard(1), fixture.player)));
    const Card *provided = fixture.room.askForCard(
        fixture.player, QStringLiteral("."), QStringLiteral("@discard"), QVariant(),
        Card::MethodDiscard, nullptr, true);
    if (!expect(provided == Sanguosha->getCard(1), "provided tag is consumed on retrial")
        || !expect(!fixture.room.getTag(QStringLiteral("provided")).isValid(),
                   "provided tag is removed after use"))
        return false;

    fixture.probe.providedCard = Sanguosha->getCard(0);
    fixture.probe.records.clear();
    fixture.room.askForCard(
        fixture.player, QStringLiteral("."), QStringLiteral("slash-jink"), QVariant(),
        Card::MethodResponse, nullptr, false);
    if (!expect(fixture.probe.payloads(CardAsked)
                    == QStringList{QStringLiteral(".+slash-jink+response")},
                "CardAsked payload is pattern+prompt+response"))
        return false;

    fixture.probe.providedCard = nullptr;
    fixture.room.clearTestOverrides();
    fixture.ai()->hasCardValue = true;
    fixture.ai()->cardValue = Sanguosha->getCard(1);
    fixture.probe.records.clear();
    const Card *aiCard = fixture.room.askForCard(
        fixture.player, QStringLiteral("."), QStringLiteral("@discard"), QVariant(),
        Card::MethodDiscard, nullptr, true);
    if (!expect(aiCard == Sanguosha->getCard(1), "AI askForCard is accepted on retrial"))
        return false;

    DecisionFixture online(QStringLiteral("online"));
    CardTable onlineCards(online.room);
    giveHand(online.player, QList<int>{0, 1}, &online.room);
    ClientReplyAgent agent(online.room, online.player);
    agent.replyByCommand.insert(S_COMMAND_RESPONSE_CARD,
                                responseCardReply(Sanguosha->getCard(1)->toString()));
    const Card *clientCard = online.room.askForCard(
        online.player, QStringLiteral("."), QStringLiteral("@discard"), QVariant(),
        Card::MethodDiscard, nullptr, true);
    if (!expect(clientCard == Sanguosha->getCard(1), "human RESPONSE_CARD parse is accepted"))
        return false;

    agent.replyByCommand.clear();
    const Card *timeoutCard = online.room.askForCard(
        online.player, QStringLiteral("."), QStringLiteral("@discard"), QVariant(),
        Card::MethodDiscard, nullptr, true);
    return expect(timeoutCard == nullptr,
                  "timeout without AI returns null");
}

static bool discardExchangeYijiAndGuanxing()
{
    if (!Sanguosha || Sanguosha->getCardCount() < 3)
        return expect(false, "engine has at least three cards");

    DecisionFixture optional;
    CardTable optionalCards(optional.room);
    giveHand(optional.player, QList<int>{0, 1}, &optional.room);
    optional.ai()->hasDiscardValue = true;
    optional.ai()->discardValue = QList<int>();
    Card *canceled = optional.room.askForDiscard(
        optional.player, QStringLiteral("gamerule"), 1, 1, true);
    if (!expect(canceled == nullptr, "optional discard AI empty list cancels")
        || !expect(optional.probe.payloads(ChoiceMade).isEmpty(),
                   "canceled optional discard emits no ChoiceMade"))
        return false;

    DecisionFixture discard;
    CardTable discardCards(discard.room);
    giveHand(discard.player, QList<int>{0, 1}, &discard.room);
    discard.ai()->hasDiscardValue = true;
    discard.ai()->discardValue = QList<int>{1};
    Card *thrown = discard.room.askForDiscard(
        discard.player, QStringLiteral("gamerule"), 1, 1, false);
    if (!expect(thrown != nullptr && thrown->getSubcards() == QList<int>{1},
                "AI discard ids are kept")
        || !expect(discard.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("cardDiscard:gamerule:%1")
                                          .arg(thrown->toString())},
                   "discard ChoiceMade is cardDiscard:<reason>:<dummy>"))
        return false;

    DecisionFixture jilei;
    CardTable jileiCards(jilei.room);
    giveHand(jilei.player, QList<int>{0}, &jilei.room);
    jilei.player->setCardLimitation(QStringLiteral("discard"), QStringLiteral("."),
                                    QStringLiteral("test-jilei"), false);
    Card *jileiResult = jilei.room.askForDiscard(
        jilei.player, QStringLiteral("tuxi"), 1, 1, false);
    if (!expect(jileiResult == nullptr, "jilei-only discard returns null")
        || !expect(jilei.probe.recordedEvents().contains(ShowCards),
                   "jilei-only discard reveals via ShowCards"))
        return false;

    DecisionFixture exchange;
    CardTable exchangeCards(exchange.room);
    giveHand(exchange.player, QList<int>{0, 1}, &exchange.room);
    exchange.ai()->hasDiscardValue = true;
    exchange.ai()->discardValue = QList<int>{0};
    Card *exchanged = exchange.room.askForExchange(
        exchange.player, QStringLiteral("zhiheng"), 1, 1, false, QString(), true);
    if (!expect(exchanged != nullptr && exchanged->getSubcards() == QList<int>{0},
                "optional exchange keeps AI ids")
        || !expect(exchange.player->hasFlag(QStringLiteral("Global_AIDiscardExchanging")) == false,
                   "Global_AIDiscardExchanging is cleared"))
        return false;

    DecisionFixture yiji;
    CardTable yijiCards(yiji.room);
    QList<int> moving{0, 1};
    yiji.ai()->hasYijiValue = true;
    yiji.ai()->yijiTarget = yiji.other;
    yiji.ai()->yijiCardId = 0;
    CardsMoveStruct kept = yiji.room.askForYijiStruct(
        yiji.player, moving, QStringLiteral("yiji"), false, false, true, -1,
        QList<ServerPlayer *>(), CardMoveReason(), QString(), false, false);
    if (!expect(moving == QList<int>{1}, "Yiji get=false mutates the input list once")
        || !expect(kept.to == yiji.other && kept.card_ids == QList<int>{0},
                   "Yiji struct keeps target and card ids")
        || !expect(yiji.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral("Yiji:yiji:decision-other:0")},
                   "Yiji ChoiceMade is Yiji:<skill>:<target>:<ids>"))
        return false;

    QList<int> wrapperCards{0, 1};
    yiji.probe.records.clear();
    ServerPlayer *wrapperTarget = yiji.room.askForYiji(
        yiji.player, wrapperCards, QStringLiteral("yiji"), false, false, true, -1,
        QList<ServerPlayer *>(), CardMoveReason(), QString(), false);
    QList<int> idCards{0, 1};
    QList<int> wrapperIds = yiji.room.askForyiji(
        yiji.player, idCards, QStringLiteral("yiji"), false, false, true, -1,
        QList<ServerPlayer *>(), CardMoveReason(), QString(), false);
    if (!expect(wrapperTarget == yiji.other, "askForYiji returns the move target")
        || !expect(wrapperIds == QList<int>{0}, "askForyiji returns the move card ids"))
        return false;

    QList<int> gottenCards{0, 1};
    yiji.probe.records.clear();
    giveHand(yiji.player, gottenCards, &yiji.room);
    CardsMoveStruct gotten = yiji.room.askForYijiStruct(
        yiji.player, gottenCards, QStringLiteral("yiji"), false, false, true, -1,
        QList<ServerPlayer *>(), CardMoveReason(), QString(), false, true);
    if (!expect(gotten.to == yiji.other && gotten.card_ids == QList<int>{0},
                "Yiji get=true still returns the selected move")
        || !expect(gottenCards == QList<int>{1}, "Yiji get=true also mutates the input list"))
        return false;

    DecisionFixture guanxing;
    CardTable guanxingCards(guanxing.room);
    QList<int> pile{0, 1, 2};
    foreach (int id, pile) {
        guanxing.room.setCardMapping(id, nullptr, Player::DrawPile);
        PlayerDecisionServiceTestAccess::drawPile(guanxing.room) << id;
    }
    guanxing.ai()->hasGuanxingValue = true;
    guanxing.ai()->guanxingTop = QList<int>{2, 0};
    guanxing.ai()->guanxingBottom = QList<int>{1};
    RequestRecorder pileRecorder;
    pileRecorder.watch(guanxing.player);
    QList<int> both = guanxing.room.askForGuanxing(
        guanxing.player, pile, Room::GuanxingBothSides, false);
    if (!expect(both == QList<int>{2, 0}, "BothSides returns the selected top list")
        || !expect(PlayerDecisionServiceTestAccess::drawPile(guanxing.room).mid(0, 2)
                       == QList<int>{2, 0},
                   "Room apply puts top cards at the front")
        || !expect(PlayerDecisionServiceTestAccess::drawPile(guanxing.room).last() == 1,
                   "Room apply appends bottom cards")
        || !expect(pileRecorder.countNotification(S_COMMAND_UPDATE_PILE) == 1,
                   "draw-pile Guanxing emits exactly one UPDATE_PILE"))
        return false;

    QList<int> oneCard{0};
    QList<int> upOnly = guanxing.room.askForGuanxing(
        guanxing.player, oneCard, Room::GuanxingUpOnly, false);
    QList<int> downOnly = guanxing.room.askForGuanxing(
        guanxing.player, oneCard, Room::GuanxingDownOnly, false);
    if (!expect(upOnly == oneCard, "UpOnly singleton stays on top")
        || !expect(downOnly.isEmpty(), "DownOnly singleton returns empty top"))
        return false;

    DecisionFixture notPile;
    CardTable notPileCards(notPile.room);
    foreach (int id, pile)
        notPile.room.setCardMapping(id, notPile.player, Player::PlaceHand);
    RequestRecorder notPileRecorder;
    notPileRecorder.watch(notPile.player);
    notPile.ai()->hasGuanxingValue = true;
    notPile.ai()->guanxingTop = pile;
    QList<int> skipped = notPile.room.askForGuanxing(
        notPile.player, pile, Room::GuanxingUpOnly, false);
    if (!expect(skipped == pile, "non-draw-pile Guanxing still returns top")
        || !expect(!notPileRecorder.containsNotification(S_COMMAND_UPDATE_PILE),
                   "non-draw-pile Guanxing does not notify pile length"))
        return false;

    DecisionFixture online(QStringLiteral("online"));
    CardTable onlineCards(online.room);
    QList<int> onlinePile{0, 1};
    foreach (int id, onlinePile) {
        online.room.setCardMapping(id, nullptr, Player::DrawPile);
        PlayerDecisionServiceTestAccess::drawPile(online.room) << id;
    }
    ClientReplyAgent agent(online.room, online.player);
    JsonArray reply;
    reply << QVariant::fromValue(JsonArray() << 1) << QVariant::fromValue(JsonArray() << 0);
    agent.replyByCommand.insert(S_COMMAND_SKILL_GUANXING, QVariant::fromValue(reply));
    QList<int> human = online.room.askForGuanxing(
        online.player, QList<int>{0, 1}, Room::GuanxingBothSides, false);
    if (!expect(human == QList<int>{1}, "human Guanxing parses top list"))
        return false;

    agent.replyByCommand.clear();
    QList<int> timeout = online.room.askForGuanxing(
        online.player, QList<int>{0, 1}, Room::GuanxingBothSides, false);
    return expect(timeout.isEmpty(), "timeout Guanxing without AI leaves top empty");
}

static bool activateUseCardAndSlashFlags()
{
    DecisionFixture notPlay;
    CardUseStruct idle;
    RequestRecorder idleRecorder;
    idleRecorder.watch(notPlay.player);
    notPlay.player->setPhase(Player::NotActive);
    notPlay.room.activate(notPlay.player, idle);
    if (!expect(idle.card == nullptr, "activate outside Play returns without a card")
        || !expect(!idleRecorder.contains(S_COMMAND_PLAY_CARD),
                   "activate outside Play does not request PLAY_CARD")
        || !expect(notPlay.probe.recordedEvents().contains(EventPlayPhaseLoop),
                   "activate still dispatches EventPlayPhaseLoop"))
        return false;

    DecisionFixture terminated;
    terminated.player->setPhase(Player::Play);
    terminated.player->setFlags(QStringLiteral("Global_PlayPhaseTerminated"));
    CardUseStruct stopped;
    terminated.room.activate(terminated.player, stopped);
    if (!expect(stopped.card == nullptr, "terminated Play phase returns without a card")
        || !expect(!terminated.player->hasFlag(QStringLiteral("Global_PlayPhaseTerminated")),
                   "Global_PlayPhaseTerminated is cleared"))
        return false;

    DecisionFixture passing;
    passing.player->setPhase(Player::Play);
    RequestRecorder passRecorder;
    passRecorder.watch(passing.player);
    passing.room.registerTestOverride(passing.player, QStringLiteral("activate"),
                                      QStringLiteral("phase"), QStringLiteral("pass"));
    CardUseStruct passed;
    passing.room.activate(passing.player, passed);
    if (!expect(passed.card == nullptr, "activate override pass clears the card")
        || !expect(!passRecorder.contains(S_COMMAND_PLAY_CARD),
                   "activate override pass does not request PLAY_CARD"))
        return false;

    DecisionFixture slash;
    CardTable slashCards(slash.room);
    RequestRecorder slashRecorder;
    slashRecorder.watch(slash.player);
    CardUseStruct missed = slash.room.askForUseSlashToStruct(
        slash.player, slash.other, QStringLiteral("slash-jink"));
    const Card *wrapper = slash.room.askForUseSlashTo(
        slash.player, slash.other, QStringLiteral("slash-jink"));
    CardUseStruct multi = slash.room.askForUseSlashToStruct(
        slash.player, QList<ServerPlayer *>() << slash.other << slash.player,
        QStringLiteral("slash-jink"), false, true);
    if (!expect(missed.card == nullptr && wrapper == nullptr,
                "slash wrappers agree when no slash is used")
        || !expect(!slash.player->hasFlag(QStringLiteral("slashTargetFix")),
                   "failed slash clears slashTargetFix")
        || !expect(!slash.other->hasFlag(QStringLiteral("SlashAssignee")),
                   "failed slash clears SlashAssignee")
        || !expect(!slash.player->hasFlag(QStringLiteral("slashNoDistanceLimit")),
                   "failed slash clears slashNoDistanceLimit")
        || !expect(!slash.player->hasFlag(QStringLiteral("slashDisableExtraTarget")),
                   "failed slash clears slashDisableExtraTarget")
        || !expect(multi.card == nullptr, "multi-target slash miss still returns empty"))
        return false;

    DecisionFixture useCard;
    useCard.probe.records.clear();
    CardUseStruct unused = useCard.room.askForUseCardStruct(
        useCard.player, QStringLiteral("jink"), QStringLiteral("slash-jink"));
    const Card *unusedCard = useCard.room.askForUseCard(
        useCard.player, QStringLiteral("jink"), QStringLiteral("slash-jink"));
    return expect(unused.card == nullptr && unusedCard == nullptr,
                  "use-card wrappers agree on empty result")
        && expect(useCard.probe.payloads(CardAsked)
                      == QStringList{QStringLiteral("jink+slash-jink+use"),
                                     QStringLiteral("jink+slash-jink+use")},
                  "askForUseCard dispatches CardAsked with use")
        && expect(useCard.probe.payloads(ChoiceMade)
                      == QStringList{QStringLiteral("cardUsed:jink:slash-jink:"),
                                     QStringLiteral("cardUsed:jink:slash-jink:")},
                  "empty use-card ChoiceMade is cardUsed:<pattern>:<prompt>:");
}

static bool nullificationPeachTriggerOrderAndResidual()
{
    DecisionFixture emptyNull;
    CardTable cards(emptyNull.room);
    const int trickId = findCardKind("TrickCard");
    if (!expect(trickId >= 0, "engine has a trick card"))
        return false;
    const Card *none = emptyNull.room.askForNullification(
        Sanguosha->getCard(trickId), emptyNull.player, emptyNull.other, true);
    if (!expect(none == nullptr, "no nullification holder returns null")
        || !expect(!emptyNull.probe.recordedEvents().contains(TrickCardCanceling),
                   "players without nullification are not asked"))
        return false;

    QVariant illegal;
    if (!expect(!emptyNull.room.verifyNullificationResponse(emptyNull.player, illegal, nullptr),
                "malformed nullification reply fails the Room verifier"))
        return false;

    DecisionFixture peach;
    CardTable peachCards(peach.room);
    const int peachId = findCardKind("Peach");
    if (!expect(peachId >= 0, "engine has a peach"))
        return false;
    giveHand(peach.player, QList<int>{peachId}, &peach.room);
    peach.other->setHp(0);
    peach.ai()->hasPeachValue = true;
    peach.ai()->peachValue = Sanguosha->getCard(peachId);
    peach.ai()->peachUsesLeft = -1;
    const Card *saved = peach.room.askForSinglePeach(peach.player, peach.other);
    if (!expect(saved != nullptr && saved->isKindOf("Peach"), "AI peach is accepted")
        || !expect(!peach.probe.payloads(ChoiceMade).isEmpty()
                       && peach.probe.payloads(ChoiceMade).first().startsWith(
                           QStringLiteral("peach:decision-other:")),
                   "peach ChoiceMade starts with peach:<dying>:"))
        return false;

    peach.probe.records.clear();
    peach.ai()->peachValue = nullptr;
    peach.player->setCardLimitation(QStringLiteral("use"), Sanguosha->getCard(peachId)->toString(),
                                    QStringLiteral("test-limit"), false);
    peach.ai()->peachValue = Sanguosha->getCard(peachId);
    peach.ai()->peachUsesLeft = 1;
    const Card *limited = peach.room.askForSinglePeach(peach.player, peach.other);
    if (!expect(limited == nullptr,
                "limited peach retries then returns null when AI keeps the same card"))
        return false;

    DecisionFixture order;
    QList<SkillContext> noneCtx;
    if (!expect(order.room.askForTriggerOrder(order.player, QStringLiteral("tuxi"), noneCtx, true)
                    == QStringLiteral("cancel"),
                "empty optional TriggerOrder returns cancel")
        || !expect(order.room.askForTriggerOrder(order.player, QStringLiteral("tuxi"), noneCtx, false)
                       .isEmpty(),
                   "empty required TriggerOrder returns empty"))
        return false;

    SkillContext single{};
    single.skill_name = QStringLiteral("tuxi");
    single.instanceID = 2;
    single.owner = order.other;
    QList<SkillContext> oneCtx{single};
    order.probe.records.clear();
    const QString one = order.room.askForTriggerOrder(
        order.player, QStringLiteral("gameRule"), oneCtx, false);
    if (!expect(one == QStringLiteral("tuxi#2:decision-other"),
                "single context encodes name#id:owner")
        || !expect(order.probe.payloads(ChoiceMade)
                       == QStringList{QStringLiteral(
                              "triggerOrder:gameRule:tuxi#2:decision-other")},
                   "TriggerOrder ChoiceMade is triggerOrder:<reason>:<result>"))
        return false;

    SkillContext second{};
    second.skill_name = QStringLiteral("fankui");
    second.owner = order.player;
    QList<SkillContext> many{single, second};
    order.ai()->triggerOrderValue = QStringLiteral("fankui");
    order.probe.records.clear();
    const QString picked = order.room.askForTriggerOrder(
        order.player, QStringLiteral("gameRule"), many, false);
    if (!expect(picked == QStringLiteral("fankui"), "AI TriggerOrder matching owner-self has no suffix"))
        return false;

    order.ai()->triggerOrderValue = QStringLiteral("missing");
    qsanSeedRandom(9);
    const QString fallback = order.room.askForTriggerOrder(
        order.player, QStringLiteral("gameRule"), many, false);
    qsanSeedRandom(9);
    const QString fallbackAgain = order.room.askForTriggerOrder(
        order.player, QStringLiteral("gameRule"), many, false);
    if (!expect(fallback == fallbackAgain, "invalid TriggerOrder uses seed-deterministic fallback")
        || !expect(fallback == QStringLiteral("tuxi#2:decision-other")
                       || fallback == QStringLiteral("fankui"),
                   "invalid TriggerOrder falls back to a listed context"))
        return false;

    order.ai()->triggerOrderValue = QStringLiteral("cancel");
    const QString canceled = order.room.askForTriggerOrder(
        order.player, QStringLiteral("gameRule"), many, true);
    if (!expect(canceled == QStringLiteral("cancel"),
                "optional TriggerOrder cancel answer returns cancel"))
        return false;

    DecisionFixture yishi;
    YishiStruct skipped = yishi.room.askForYishi(
        yishi.player, QList<ServerPlayer *>() << yishi.player, QStringLiteral("yishi"));
    return expect(!skipped.started, "askForYishi stays on Room and bails with <2 participants");
}

static bool trickEffectTagPreservesNullificationTarget()
{
    ServerPlayer source(nullptr);
    ServerPlayer target(nullptr);
    DummyCard trick;

    CardEffectStruct copied;
    copied.card = &trick;
    copied.from = &source;
    copied.to = &target;
    CardEffectStruct moved(std::move(copied));
    const QVariant payload = QVariant::fromValue(moved);
    const QByteArray key("TrickEffectData");
    CardLifetimeManager &manager = globalCardLifetimeManager();
    const CardLifetimeGauge before = manager.gauge();
    QByteArray retainError;
    const bool retained = manager.retainVariantTag(&target, key, payload, &retainError);
    target.setTag(QString::fromLatin1(key), payload);
    const QVariant stored = target.getTag(QString::fromLatin1(key));
    const CardEffectStruct restored = stored.value<CardEffectStruct>();
    const CardLifetimeGauge afterSet = manager.gauge();
    const bool pointerPreserved = stored.isValid() && restored.to == &target
        && restored.from == &source && restored.card == &trick;
    std::fprintf(stderr,
                 "TAG_DISCRIMINATOR retainVariantTag=%d storedValid=%d toMatch=%d "
                 "unknown_qvariant_card_payload_delta=%llu error=%s\n",
                 retained ? 1 : 0, stored.isValid() ? 1 : 0,
                 restored.to == &target ? 1 : 0,
                 static_cast<unsigned long long>(afterSet.unknown_qvariant_card_payload
                                                 - before.unknown_qvariant_card_payload),
                 retainError.constData());
    if (!expect(retained, "CardEffectStruct is accepted by Variant tag retention")
        || !expect(pointerPreserved, "TrickEffectData preserves non-null to pointer")
        || !expect(afterSet.unknown_qvariant_card_payload
                       == before.unknown_qvariant_card_payload,
                   "known CardEffectStruct does not increment opaque QVariant counter"))
        return false;

    CardEffectStruct nullTarget = moved;
    nullTarget.to = nullptr;
    const QByteArray nullKey("TrickEffectData-null");
    const QVariant nullPayload = QVariant::fromValue(nullTarget);
    QByteArray nullError;
    const bool nullRetained = manager.retainVariantTag(&target, nullKey, nullPayload, &nullError);
    target.setTag(QString::fromLatin1(nullKey), nullPayload);
    const CardEffectStruct restoredNull = target.getTag(QString::fromLatin1(nullKey))
        .value<CardEffectStruct>();
    if (!expect(nullRetained, "CardEffectStruct with null to is accepted")
        || !expect(nullError.isEmpty(), "null CardEffectStruct has no retention error")
        || !expect(restoredNull.to == nullptr, "null TrickEffectData preserves null target"))
        return false;
    target.removeTag(QString::fromLatin1(nullKey));

    const quint64 opaqueBefore = afterSet.unknown_qvariant_card_payload;
    QByteArray opaqueError;
    const bool opaqueRetained = manager.retainVariantTag(
        &target, QByteArray("opaque-card-effect"),
        QVariant::fromValue(UnregisteredCardPayload()), &opaqueError);
    const CardLifetimeGauge afterOpaque = manager.gauge();
    if (!expect(!opaqueRetained, "unclassified Card payload remains rejected")
        || !expect(opaqueError == CardLifetimeManager::opaqueVariantError(),
                   "opaque Card payload reports the exact lifetime error")
        || !expect(afterOpaque.unknown_qvariant_card_payload == opaqueBefore + 1,
                   "opaque Card payload increments the diagnostic counter"))
        return false;

    // CardUseStruct is a frozen-matrix, self-leasing struct: production code stores it
    // in Room/Player tags (for example Nullification's "UseHistory" tag), so retention
    // must accept it without incrementing the opaque diagnostic counter.
    const quint64 useStructBefore = afterOpaque.unknown_qvariant_card_payload;
    QByteArray useStructError;
    const bool useStructRetained = manager.retainVariantTag(
        &target, QByteArray("card-use-struct"), QVariant::fromValue(CardUseStruct()),
        &useStructError);
    if (!expect(useStructRetained, "lease-bearing CardUseStruct tag is accepted")
        || !expect(useStructError.isEmpty(), "accepted CardUseStruct reports no error")
        || !expect(manager.gauge().unknown_qvariant_card_payload == useStructBefore,
                   "accepted CardUseStruct does not increment the opaque counter"))
        return false;
    manager.releaseVariantTag(&target, QByteArray("card-use-struct"));

    // SkillContext / CorrectSkillContext carry raw Card pointers whose type names do not
    // contain "Card", so the opaque-name heuristic never sees them. They are stored in
    // Room tags (Room::useCard / GameRule skill-card contexts), so a tag holding one must
    // lease every Card it names for as long as the tag lives.
    DummyCard useCard;
    DummyCard updatedCard;
    DummyCard nestedCard;
    const quint64 skillLeaseBefore = manager.gauge().native_leases;
    SkillContext skillCtx;
    skillCtx.use_card = &useCard;
    skillCtx.updated_card = &updatedCard;
    skillCtx.extra_data = QVariant::fromValue(static_cast<Card *>(&nestedCard));
    QByteArray skillError;
    const bool skillRetained = manager.retainVariantTag(
        &target, QByteArray("skill-context"), QVariant::fromValue(skillCtx), &skillError);
    if (!expect(skillRetained, "SkillContext tag is accepted")
        || !expect(skillError.isEmpty(), "accepted SkillContext reports no error")
        || !expect(manager.gauge().native_leases == skillLeaseBefore + 3,
                   "SkillContext tag leases use_card, updated_card and nested extra_data"))
        return false;
    manager.releaseVariantTag(&target, QByteArray("skill-context"));
    if (!expect(manager.gauge().native_leases == skillLeaseBefore,
                "releasing the SkillContext tag releases every Card lease it held"))
        return false;

    DummyCard correctCard;
    const quint64 correctLeaseBefore = manager.gauge().native_leases;
    CorrectSkillContext correctCtx;
    correctCtx.card = &correctCard;
    QByteArray correctError;
    const bool correctRetained = manager.retainVariantTag(
        &target, QByteArray("correct-skill-context"), QVariant::fromValue(correctCtx),
        &correctError);
    if (!expect(correctRetained, "CorrectSkillContext tag is accepted")
        || !expect(correctError.isEmpty(), "accepted CorrectSkillContext reports no error")
        || !expect(manager.gauge().native_leases == correctLeaseBefore + 1,
                   "CorrectSkillContext tag leases its Card pointer"))
        return false;
    manager.releaseVariantTag(&target, QByteArray("correct-skill-context"));
    if (!expect(manager.gauge().native_leases == correctLeaseBefore,
                "releasing the CorrectSkillContext tag releases its Card lease"))
        return false;

    target.removeTag(QString::fromLatin1(key));
    manager.releaseVariantTag(&target, QByteArray("opaque-card-effect"));
    return true;
}

static bool aiDelayIsHonoredWhenConfigured()
{
    DecisionFixture fixture;
    fixture.ai()->invokeValue = false;
    const int previousDelay = Config.AIDelay;
    Config.AIDelay = 50;
    QElapsedTimer timer;
    timer.start();
    fixture.room.askForSkillInvoke(fixture.player, QStringLiteral("tuxi"));
    const qint64 elapsed = timer.elapsed();
    Config.AIDelay = previousDelay;
    if (!expect(elapsed >= 50, "non-headless AI delay is not shorter than Config.AIDelay"))
        return false;

    Config.AIDelay = 0;
    timer.restart();
    fixture.room.askForSkillInvoke(fixture.player, QStringLiteral("tuxi"));
    return expect(timer.elapsed() < 40, "zero AIDelay adds no extra wait");
}

}

int runPlayerDecisionServiceTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    const int savedAIDelay = Config.AIDelay;
    const int savedOriginAIDelay = Config.OriginAIDelay;
    const int savedOperationTimeout = Config.OperationTimeout;
    const bool savedOperationNoLimit = Config.OperationNoLimit;
    Config.AIDelay = 0;
    Config.OriginAIDelay = 0;
    Config.OperationTimeout = 0;
    Config.OperationNoLimit = false;
    ServerInfo.OperationTimeout = 0;
    int status = 0;

    auto run = [&](bool (*fn)(), const char *name, int code) {
        if (status != 0)
            return;
        qWarning() << "running" << name;
        if (!fn()) {
            qCritical() << "failed" << name;
            status = code;
        }
    };

    run(roomOverrideBehaviorIsCharacterized, "overrides", 2);
    run(serviceOwnsOverridesAndHandlesNullPlayer, "service-owns", 3);
    run(serviceConstructionDoesNotDispatch, "no-dispatch", 4);
    run(concurrentClearAndFindAreSafe, "concurrent", 5);
    run(skillInvokeOverrideAndAiPreservePayloads, "skill-invoke", 6);
    run(skillInvokeClientAnswerAndNotifyFalse, "skill-invoke-client", 7);
    run(choiceOverrideForceCancelAndFallback, "choice", 8);
    run(choiceClientAnswer, "choice-client", 9);
    run(simpleChoiceClientV1V2Parity, "simple-choice-client", 10);
    run(suitKingdomGeneralAndModeChoices, "suit-kingdom-general", 11);
    run(suitKingdomGeneralClientAndInvalidReplies, "client-invalid", 12);
    run(orderAndRolePreserveLegacyFallbacks, "order-role", 13);
    run(playerChosenEmptySingletonOverrideAndNotify, "player-chosen", 14);
    run(playerChosenClientInvalidOptionalAndTimeout, "player-chosen-client", 15);
    run(playersChosenMinMaxSortAndNegativeMin, "players-chosen", 16);
    run(playersChosenClientFillAndNotify, "players-chosen-client", 17);
    run(agEmptySingletonRefusableInvalidAndClient, "ask-for-ag", 18);
    run(cardChosenOverrideFallbackVisibleAndClient, "card-chosen", 19);
    run(cardShowSingletonClientAndRandom, "card-show", 20);
    run(pindianEmitsNoChoiceMade, "pindian", 21);
    run(pindianRaceBroadcastIndependentFallback, "pindian-race", 22);
    run(cardResponseOverrideProvidedAndRetry, "card-response", 23);
    run(discardExchangeYijiAndGuanxing, "discard-yiji-guanxing", 24);
    run(activateUseCardAndSlashFlags, "activate-use-card", 25);
    run(nullificationPeachTriggerOrderAndResidual, "reactive-trigger-order", 26);
    run(trickEffectTagPreservesNullificationTarget, "tag-discriminator", 27);
    run(aiDelayIsHonoredWhenConfigured, "ai-delay", 28);

    Config.AIDelay = savedAIDelay;
    Config.OriginAIDelay = savedOriginAIDelay;
    Config.OperationTimeout = savedOperationTimeout;
    Config.OperationNoLimit = savedOperationNoLimit;

    if (status != 0)
        return status;

    qInfo() << "player decision service characterization passed";
    return 0;
}
