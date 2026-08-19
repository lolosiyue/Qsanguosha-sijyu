#ifndef PLAYER_DECISION_SERVICE_H
#define PLAYER_DECISION_SERVICE_H

#include "card.h"
#include "skill.h"
#include "structs.h"

#include <QList>
#include <QMap>
#include <QMutex>
#include <QStringList>
#include <QVariant>

class EventDispatcher;
class Room;
class ServerPlayer;

class PlayerDecisionService
{
public:
    PlayerDecisionService(Room &room, EventDispatcher &eventDispatcher);

    void registerTestOverride(ServerPlayer *player, const QString &queryType,
                              const QString &key, const QVariant &answer);
    void clearTestOverrides();
    QVariant findTestOverride(ServerPlayer *player, const QString &queryType,
                              const QString &key) const;

    bool askForSkillInvoke(ServerPlayer *player, const QString &skill_name,
                           const QVariant &data, bool notify);
    QString askForChoice(ServerPlayer *player, const QString &skill_name,
                         const QString &choices, const QVariant &data,
                         const QString &except_choices, const QString &tip);
    Card::Suit askForSuit(ServerPlayer *player, const QString &reason);
    QString askForKingdom(ServerPlayer *player, const QString &reason,
                          QStringList kingdoms, bool send_log);
    QString askForGeneral(ServerPlayer *player, const QStringList &generals,
                          const QString &default_choice, const QString &reason);
    QString askForOrder(ServerPlayer *player, const QString &default_choice);
    QString askForRole(ServerPlayer *player, const QStringList &roles,
                       const QString &scheme);
    int askForAG(ServerPlayer *player, const QList<int> &card_ids, bool refusable,
                 const QString &reason, const QString &prompt);
    ServerPlayer *askForPlayerChosen(ServerPlayer *player,
                                     const QList<ServerPlayer *> &targets,
                                     const QString &skillName, const QString &prompt,
                                     bool optional, bool notify_skill);
    QList<ServerPlayer *> askForPlayersChosen(ServerPlayer *player,
                                              const QList<ServerPlayer *> &targets,
                                              const QString &skillName, int min_num,
                                              int max_num, const QString &prompt,
                                              bool notify_skill, bool sort_ActionOrder);
    int askForCardChosen(ServerPlayer *player, ServerPlayer *who, const QString &flags,
                         const QString &reason, bool handcard_visible,
                         Card::HandlingMethod method, const QList<int> &disabled_ids,
                         bool can_cancel);
    const Card *askForCardShow(ServerPlayer *player, ServerPlayer *requestor,
                               const QString &reason);
    const Card *askForPindian(ServerPlayer *player, ServerPlayer *from,
                              const QString &reason);
    QList<const Card *> askForPindianRace(ServerPlayer *from, ServerPlayer *to,
                                          const QString &reason);

    struct GuanxingSelection
    {
        QList<int> top;
        QList<int> bottom;
    };

    const Card *askForCard(ServerPlayer *player, const QString &pattern, const QString &prompt,
                           const QVariant &data, Card::HandlingMethod method, ServerPlayer *m_who,
                           bool isRetrial, const QString &skill_name, bool isProvision,
                           const Card *m_toCard);
    Card *askForDiscard(ServerPlayer *player, const QString &reason, int discard_num, int min_num,
                        bool optional, bool include_equip, const QString &prompt,
                        const QString &pattern, const QString &skill_name);
    Card *askForExchange(ServerPlayer *player, const QString &reason, int exchange_num, int min_num,
                         bool include_equip, const QString &prompt, bool optional,
                         const QString &pattern);
    CardsMoveStruct askForYijiStruct(ServerPlayer *guojia, QList<int> &cards,
                                     const QString &skill_name, bool is_preview, bool visible,
                                     bool optional, int max_num, QList<ServerPlayer *> players,
                                     CardMoveReason reason, const QString &prompt, bool notify_skill,
                                     bool get);
    GuanxingSelection askForGuanxingSelection(ServerPlayer *zhuge, const QList<int> &cards,
                                              int guanxing_type);

    void activate(ServerPlayer *player, CardUseStruct &card_use);
    CardUseStruct askForUseCardStruct(ServerPlayer *player, const QString &pattern,
                                      const QString &prompt, int notice_index,
                                      Card::HandlingMethod method, bool addHistory,
                                      ServerPlayer *who, const Card *whocard, QString flag);
    CardUseStruct askForUseSlashToStruct(ServerPlayer *slasher, QList<ServerPlayer *> victims,
                                         const QString &prompt, bool distance_limit,
                                         bool disable_extra, bool addHistory, ServerPlayer *who,
                                         const Card *whocard, QString flag);

    bool verifyNullificationResponse(ServerPlayer *player, const QVariant &response, void *arg);
    const Card *askForNullification(const Card *trick, ServerPlayer *from, ServerPlayer *to,
                                    bool positive);
    const Card *_askForNullification(const Card *trick, ServerPlayer *from, ServerPlayer *to,
                                     bool positive);
    const Card *askForSinglePeach(ServerPlayer *player, ServerPlayer *dying);
    QString askForTriggerOrder(ServerPlayer *player, const QString &reason,
                               QList<SkillContext> &contexts, bool optional, const QVariant &data);

private:
    Room &m_room;
    EventDispatcher &m_eventDispatcher;
    QMap<QString, QVariant> m_testOverrides;
    mutable QMutex m_testOverrideMutex;
};

#endif
