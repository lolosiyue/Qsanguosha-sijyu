#include "player-decision-service.h"

#include "ai.h"
#include "engine.h"
#include "event-dispatcher.h"
#include "general.h"
#include "json.h"
#include "protocol.h"
#include "room.h"
#include "roomthread.h"
#include "server-info.h"
#include "serverplayer.h"
#include "settings.h"
#include "skill-instance-utils.h"
#include "wrapped-card.h"
#include "standard.h"
#include "skill.h"
#include "util.h"
#include "card-lifetime-manager.h"

#include <QElapsedTimer>
#include <QMutexLocker>
#include <QSet>

#include <cstdio>

using namespace QSanProtocol;

PlayerDecisionService::PlayerDecisionService(Room &room, EventDispatcher &eventDispatcher)
    : m_room(room), m_eventDispatcher(eventDispatcher)
{
}

void PlayerDecisionService::registerTestOverride(ServerPlayer *player, const QString &queryType,
                                                 const QString &key, const QVariant &answer)
{
    if (player == nullptr)
        return;

    QMutexLocker locker(&m_testOverrideMutex);
    const QString overrideKey = QStringLiteral("%1:%2:%3").arg(
        player->objectName(), queryType, key);
    m_testOverrides[overrideKey] = answer;
}

void PlayerDecisionService::clearTestOverrides()
{
    QMutexLocker locker(&m_testOverrideMutex);
    m_testOverrides.clear();
}

QVariant PlayerDecisionService::findTestOverride(ServerPlayer *player, const QString &queryType,
                                                 const QString &key) const
{
    if (player == nullptr)
        return QVariant();

    QMutexLocker locker(&m_testOverrideMutex);
    const QString overrideKey = QStringLiteral("%1:%2:%3").arg(
        player->objectName(), queryType, key);
    const auto it = m_testOverrides.constFind(overrideKey);
    return it == m_testOverrides.constEnd() ? QVariant() : it.value();
}

bool PlayerDecisionService::askForSkillInvoke(ServerPlayer *player, const QString &skill_name,
                                              const QVariant &data, bool notify)
{
	CardLifetimeScope cardScope(globalCardLifetimeManager());
    QString skillName = skill_name;
    if (skill_name.contains("$"))
        skillName = skill_name.split("$").first();
    QVariant skill_data = skillName;
    m_eventDispatcher.dispatch(InvokeSkill, player, skill_data);

    m_room.tryPause();
    m_room.notifyMoveFocus(player, S_COMMAND_INVOKE_SKILL);

    bool invoked = false;
    ServerPlayer *tp = data.value<ServerPlayer *>();
    QVariant over = findTestOverride(player, "skill_invoke", skillName);
    if (over.isValid() && over.canConvert<bool>()) {
        invoked = over.toBool();
    } else {
        AI *ai = player->getAI();
        if (ai) {
            QElapsedTimer timer;
            timer.start();
            invoked = ai->askForSkillInvoke(skillName, data);
            if (Config.AIDelay > timer.elapsed())
                m_room.thread->delay(Config.AIDelay - timer.elapsed());
        } else {
            JsonArray skillCommand;
            skillCommand << skillName;
            if (data.userType() == QMetaType::QString)
                skillCommand << data.toString();
            else {
                if (tp)
                    skillCommand << "playerdata:" + tp->objectName();
                else
                    skillCommand << "";
            }
            if (m_room.doRequest(player, S_COMMAND_INVOKE_SKILL, skillCommand, true)) {
                skill_data = player->getClientReply();
                if (skill_data.canConvert<bool>())
                    invoked = skill_data.toBool();
            } else {
                ai = player->getAI();
                if (ai)
                    invoked = ai->askForSkillInvoke(skillName, data);
            }
        }
    }

    if (invoked && notify) {
        JsonArray msg;
        msg << skillName << player->objectName();
        m_room.doBroadcastNotify(S_COMMAND_INVOKE_SKILL, msg);
        if (skill_name.contains("$"))
            m_room.broadcastSkillInvoke(skillName, skill_name.split("$").last().toInt(), player);
        m_room.notifySkillInvoked(player, skillName);
    }
    skillName = "skillInvoke:" + skillName;
    if (tp)
        skillName.append(":" + tp->objectName());
    skill_data = skillName + ":" + (invoked ? "yes" : "no");
    m_eventDispatcher.dispatch(ChoiceMade, player, skill_data);
    return invoked;
}

QString PlayerDecisionService::askForChoice(ServerPlayer *player, const QString &skill_name,
                                            const QString &choices, const QVariant &data,
                                            const QString &except_choices, const QString &tip)
{
	CardLifetimeScope cardScope(globalCardLifetimeManager());
    m_room.tryPause();

    ChoiceData choiceData;
    choiceData.player = player;
    choiceData.skill_name = skill_name;
    choiceData.choices = choices;
    choiceData.except_choices = except_choices;
    choiceData.tip = tip;
    choiceData.forced_answer = QString();
    choiceData.canceled = false;

    QVariant choiceDataVar = QVariant::fromValue(choiceData);
    m_eventDispatcher.dispatch(EventAskForChoice, player, choiceDataVar);
    choiceData = choiceDataVar.value<ChoiceData>();

    if (choiceData.canceled)
        return QString();

    if (!choiceData.forced_answer.isEmpty())
        return choiceData.forced_answer;

    QString effectiveChoices = choiceData.choices;
    QString effectiveExceptChoices = choiceData.except_choices;
    QString effectiveTip = choiceData.tip;

    m_room.notifyMoveFocus(player, S_COMMAND_MULTIPLE_CHOICE);

    QString answer = effectiveChoices;
    if (effectiveChoices.contains("+")) {
        QVariant over = findTestOverride(player, "choice", skill_name);
        if (over.isValid() && over.canConvert<QString>()) {
            answer = over.toString();
        } else {
            AI *ai = player->getAI();
            if (ai) {
                QElapsedTimer timer;
                timer.start();
                answer = ai->askForChoice(skill_name, effectiveChoices, data);
                if (Config.AIDelay > timer.elapsed())
                    m_room.thread->delay(Config.AIDelay - timer.elapsed());
            } else {
                answer = "cancel";
                if (m_room.doRequest(player, S_COMMAND_MULTIPLE_CHOICE,
                                     JsonArray() << skill_name << effectiveChoices
                                                 << effectiveExceptChoices << effectiveTip,
                                     true)) {
                    QVariant clientReply = player->getClientReply();
                    if (clientReply.canConvert<QString>())
                        answer = clientReply.toString();
                } else {
                    ai = player->getAI();
                    if (ai)
                        answer = ai->askForChoice(skill_name, effectiveChoices, data);
                }
            }
        }
        if (!effectiveChoices.contains(answer)) {
            QStringList _choices = effectiveChoices.split("+");
            answer = _choices.at(qrand() % _choices.length());
        }
    }
    QVariant decisionData = "skillChoice:" + skill_name + ":" + answer;
    m_eventDispatcher.dispatch(ChoiceMade, player, decisionData);
    return answer;
}

Card::Suit PlayerDecisionService::askForSuit(ServerPlayer *player, const QString &reason)
{
	CardLifetimeScope cardScope(globalCardLifetimeManager());
    m_room.tryPause();
    m_room.notifyMoveFocus(player, S_COMMAND_CHOOSE_SUIT);

    Card::Suit suit = Card::AllSuits[qrand() % 4];
    AI *ai = player->getAI();
    if (ai)
        suit = ai->askForSuit(reason);
    else if (m_room.doRequest(player, S_COMMAND_CHOOSE_SUIT, QVariant(), true)) {
        if (player->getClientReply().toString() == "spade")
            suit = Card::Spade;
        else if (player->getClientReply().toString() == "club")
            suit = Card::Club;
        else if (player->getClientReply().toString() == "heart")
            suit = Card::Heart;
        else if (player->getClientReply().toString() == "diamond")
            suit = Card::Diamond;
    } else {
        ai = player->getAI();
        if (ai)
            suit = ai->askForSuit(reason);
    }
    return suit;
}

QString PlayerDecisionService::askForKingdom(ServerPlayer *player, const QString &reason,
                                             QStringList kingdoms, bool send_log)
{
	CardLifetimeScope cardScope(globalCardLifetimeManager());
    m_room.tryPause();
    m_room.notifyMoveFocus(player, S_COMMAND_CHOOSE_KINGDOM);
    if (reason.endsWith("_ChooseKingdom")) {
        QString new_reason = reason;
        new_reason.remove("_ChooseKingdom");
        const General *general = Sanguosha->getGeneral(new_reason);
        if (general)
            kingdoms = general->getKingdoms().split("+");
    }
    if (kingdoms.isEmpty() || kingdoms.first().isEmpty()) {
        kingdoms = Sanguosha->getKingdoms();
        kingdoms.removeOne("demon");
        kingdoms.removeOne("god");
    }
    if (kingdoms.isEmpty())
        return "god";
    QString result = kingdoms.first();
    AI *ai = player->getAI();
    if (ai) {
        if (reason.isEmpty() || reason.contains("gamerule_"))
            result = ai->askForKingdom(kingdoms);
        else
            result = ai->askForChoice(reason, kingdoms.join("+"), QVariant());
    } else {
        JsonArray arg;
        arg << kingdoms.join("+");
        if (m_room.doRequest(player, S_COMMAND_CHOOSE_KINGDOM, arg, true)) {
            const QVariant &clientReply = player->getClientReply();
            if (JsonUtils::isString(clientReply))
                result = clientReply.toString();
        } else {
            ai = player->getAI();
            if (ai) {
                if (reason.isEmpty() || reason.contains("gamerule_"))
                    result = ai->askForKingdom(kingdoms);
                else
                    result = ai->askForChoice(reason, kingdoms.join("+"), QVariant());
            }
        }
    }
    if (!kingdoms.contains(result))
        result = kingdoms.at(qrand() % kingdoms.length());
    if (send_log) {
        LogMessage log;
        log.type = "#ChooseKingdom";
        log.from = player;
        log.arg = result;
        m_room.sendLog(log);
    }
    return result;
}

QString PlayerDecisionService::askForGeneral(ServerPlayer *player, const QStringList &generals,
                                             const QString &default_choice, const QString &reason)
{
    CardLifetimeScope cardScope(globalCardLifetimeManager());
    m_room.tryPause();
    m_room.notifyMoveFocus(player, S_COMMAND_CHOOSE_GENERAL);

    if (generals.isEmpty())
        return "caocao";
    else if (generals.length() < 2)
        return generals.first();

    QStringList actualGenerals = generals;
    if (m_room.thread && m_room.game_state == 1) {
        QVariant data = generals.join("+");
        m_eventDispatcher.dispatch(GeneralChoosing, player, data);
        actualGenerals = data.toString().split("+");
    }

    if (actualGenerals.isEmpty())
        return "caocao";
    if (actualGenerals.length() < 2)
        return actualGenerals.first();

    if (m_room.game_state != 1) {
        QStringList hidden;
        for (int i = 0; i < actualGenerals.length(); i++)
            hidden << "unknown";
        m_room.doAnimate(S_ANIMATE_HUASHEN, player->objectName(), hidden.join(":"));
        if (!m_room.thread)
            m_room.thread = new RoomThread(&m_room);
        m_room.thread->delay();
    }

    QString chosenGeneral;
    AI *ai = player->getAI();

    if (ai) {
        QElapsedTimer timer;
        timer.start();
        chosenGeneral = ai->askForGeneral(actualGenerals, default_choice, reason);
        if (m_room.thread && Config.AIDelay - timer.elapsed() > 0)
            m_room.thread->delay(Config.AIDelay - timer.elapsed());
    } else if (player->isOnline()) {
        if (m_room.doRequest(player, S_COMMAND_CHOOSE_GENERAL,
                             JsonUtils::toJsonArray(actualGenerals), true)) {
            QVariant clientResponse = player->getClientReply();
            if (JsonUtils::isString(clientResponse)) {
                if (actualGenerals.contains(clientResponse.toString()) || Config.FreeChoose
                    || m_room.mode.startsWith("_mini_") || m_room.mode == "custom_scenario")
                    chosenGeneral = clientResponse.toString();
            }
        }
    }

    if (chosenGeneral.isEmpty()) {
        if (!default_choice.isEmpty() && actualGenerals.contains(default_choice))
            chosenGeneral = default_choice;
        else
            chosenGeneral = actualGenerals.at(qrand() % actualGenerals.length());
    }

    if (m_room.thread && m_room.game_state == 1) {
        QVariant data = chosenGeneral;
        m_eventDispatcher.dispatch(GeneralChosen, player, data);
        chosenGeneral = data.toString();
    }

    return chosenGeneral;
}

QString PlayerDecisionService::askForOrder(ServerPlayer *player, const QString &default_choice)
{
    CardLifetimeScope cardScope(globalCardLifetimeManager());
    m_room.tryPause();
    m_room.notifyMoveFocus(player, S_COMMAND_CHOOSE_ORDER);
    if (!player->getAI()
        && m_room.doRequest(player, S_COMMAND_CHOOSE_ORDER, (int)S_REASON_CHOOSE_ORDER_TURN, true)) {
        QVariant clientReply = player->getClientReply();
        if (JsonUtils::isNumber(clientReply))
            return (Game3v3Camp)clientReply.toInt() == S_CAMP_WARM ? "warm" : "cool";
    }
    return default_choice;
}

QString PlayerDecisionService::askForRole(ServerPlayer *player, const QStringList &roles,
                                          const QString &scheme)
{
    CardLifetimeScope cardScope(globalCardLifetimeManager());
    m_room.tryPause();
    m_room.notifyMoveFocus(player, S_COMMAND_CHOOSE_ROLE_3V3);

    QStringList squeezed = QSet<QString>(roles.begin(), roles.end()).values();
    QString result = "abstain";

    JsonArray arg;
    arg << scheme << JsonUtils::toJsonArray(squeezed);
    if (m_room.doRequest(player, S_COMMAND_CHOOSE_ROLE_3V3, arg, true)) {
        QVariant clientReply = player->getClientReply();
        if (JsonUtils::isString(clientReply))
            result = clientReply.toString();
    }
    return result;
}

int PlayerDecisionService::askForAG(ServerPlayer *player, const QList<int> &card_ids,
                                    bool refusable, const QString &reason,
                                    const QString &prompt)
{
    CardLifetimeScope cardScope(globalCardLifetimeManager());
    m_room.tryPause();
    m_room.notifyMoveFocus(player, S_COMMAND_AMAZING_GRACE);

    int card_id = -1;
    if (refusable || card_ids.length() > 1) {
        AI *ai = player->getAI();
        if (ai) {
            QElapsedTimer timer;
            timer.start();
            card_id = ai->askForAG(card_ids, refusable, reason);
            if (Config.AIDelay > timer.elapsed())
                m_room.thread->delay(Config.AIDelay - timer.elapsed());
        } else {
            if (m_room.doRequest(player, S_COMMAND_AMAZING_GRACE,
                                 JsonArray() << refusable << reason << prompt, true)) {
                const QVariant &clientReply = player->getClientReply();
                if (JsonUtils::isNumber(clientReply))
                    card_id = clientReply.toInt();
            } else {
                ai = player->getAI();
                if (ai)
                    card_id = ai->askForAG(card_ids, refusable, reason);
            }
        }
    }
    if (!card_ids.contains(card_id))
        card_id = (refusable || card_ids.isEmpty()) ? -1 : card_ids.first();
    QVariant decisionData = QString("AGChosen:%1:%2").arg(reason).arg(card_id);
    m_eventDispatcher.dispatch(ChoiceMade, player, decisionData);
    return card_id;
}

ServerPlayer *PlayerDecisionService::askForPlayerChosen(
    ServerPlayer *player, const QList<ServerPlayer *> &targets,
    const QString &skillName, const QString &prompt, bool optional,
    bool notify_skill)
{
    CardLifetimeScope cardScope(globalCardLifetimeManager());
    m_room.tryPause();
    m_room.notifyMoveFocus(player, S_COMMAND_CHOOSE_PLAYER);
    if (targets.isEmpty())
        return nullptr;
    ServerPlayer *choice = targets.first();
    LogMessage log;
    log.arg = skillName;
    if (skillName.contains("$"))
        log.arg = skillName.split("$").first();
    if (targets.length() > 1 || optional) {
        QVariant over = findTestOverride(player, "player_chosen", log.arg);
        if (over.isValid()) {
            QString objName = over.toString();
            foreach (ServerPlayer *t, targets) {
                if (t->objectName() == objName) {
                    choice = t;
                    break;
                }
            }
        } else {
            AI *ai = player->getAI();
            if (ai) {
                QElapsedTimer timer;
                timer.start();
                choice = ai->askForPlayerChosen(targets, log.arg);
                if (Config.AIDelay > timer.elapsed())
                    m_room.thread->delay(Config.AIDelay - timer.elapsed());
            } else {
                choice = nullptr;
                JsonArray req, req_targets;
                foreach (ServerPlayer *target, targets)
                    req_targets << target->objectName();
                req << QVariant(req_targets) << log.arg << prompt << 1 << (optional ? 0 : 1);
                if (m_room.doRequest(player, S_COMMAND_CHOOSE_PLAYER, req, true)) {
                    const QVariant &clientReply = player->getClientReply();
                    if (JsonUtils::isString(clientReply))
                        choice = m_room.findChild<ServerPlayer *>(clientReply.toString());
                } else {
                    ai = player->getAI();
                    if (ai)
                        choice = ai->askForPlayerChosen(targets, log.arg);
                }
            }
        }
        if (!choice && !optional)
            choice = targets.at(qrand() % targets.length());
    }
    if (choice) {
        if (notify_skill) {
            if (skillName.contains("$"))
                m_room.broadcastSkillInvoke(log.arg, skillName.split("$").last().toInt(), player);
            m_room.doAnimate(S_ANIMATE_INDICATE, player->objectName(), choice->objectName());
            log.type = "#ChoosePlayerWithSkill";
            log.from = player;
            log.to << choice;
            m_room.sendLog(log);
            m_room.notifySkillInvoked(player, log.arg);
            QVariant decisionData = "skillInvoke:" + log.arg + ":yes";
            m_eventDispatcher.dispatch(ChoiceMade, player, decisionData);
        }
        QVariant data = QString("playerChosen:%1:%2").arg(log.arg).arg(choice->objectName());
        m_eventDispatcher.dispatch(ChoiceMade, player, data);
    }
    return choice;
}

QList<ServerPlayer *> PlayerDecisionService::askForPlayersChosen(
    ServerPlayer *player, const QList<ServerPlayer *> &targets,
    const QString &skillName, int min_num, int max_num, const QString &prompt,
    bool notify_skill, bool sort_ActionOrder)
{
    CardLifetimeScope cardScope(globalCardLifetimeManager());
    m_room.tryPause();
    min_num = qMin(min_num, targets.length());
    max_num = qMin(max_num, targets.length());
    m_room.notifyMoveFocus(player, S_COMMAND_CHOOSE_PLAYER);
    LogMessage log;
    log.arg = skillName;
    log.to = targets;
    if (skillName.contains("$"))
        log.arg = skillName.split("$").first();
    if (targets.length() > min_num) {
        AI *ai = player->getAI();
        if (ai) {
            QElapsedTimer timer;
            timer.start();
            log.to = ai->askForPlayersChosen(targets, log.arg, max_num, min_num);
            if (Config.AIDelay > timer.elapsed())
                m_room.thread->delay(Config.AIDelay - timer.elapsed());
        } else {
            log.to.clear();
            JsonArray req, req_targets;
            foreach (ServerPlayer *target, targets)
                req_targets << target->objectName();
            req << QVariant(req_targets) << log.arg << prompt << max_num << min_num;
            if (m_room.doRequest(player, S_COMMAND_CHOOSE_PLAYER, req, true)) {
                const QVariant &clientReply = player->getClientReply();
                if (JsonUtils::isString(clientReply)) {
                    foreach (const QString &name, clientReply.toString().split("+")) {
                        ServerPlayer *p = m_room.findChild<ServerPlayer *>(name);
                        if (targets.contains(p))
                            log.to << p;
                    }
                }
            } else {
                ai = player->getAI();
                if (ai)
                    log.to = ai->askForPlayersChosen(targets, log.arg, max_num, min_num);
            }
        }
        if (log.to.length() < min_num) {
            QList<ServerPlayer *> copy = targets;
            foreach (ServerPlayer *p, log.to)
                copy.removeOne(p);
            while (log.to.length() < min_num && copy.length() > 0)
                log.to << copy.takeAt(qrand() % copy.length());
        } else if (min_num < 0 && log.to.length() != max_num)
            log.to.clear();
    }
    if (log.to.length() > 0) {
        if (sort_ActionOrder)
            m_room.sortByActionOrder(log.to);
        if (notify_skill) {
            if (skillName.contains("$"))
                m_room.broadcastSkillInvoke(log.arg, skillName.split("$").last().toInt(), player);
            foreach (ServerPlayer *p, log.to)
                m_room.doAnimate(S_ANIMATE_INDICATE, player->objectName(), p->objectName());
            log.type = "#ChoosePlayerWithSkill";
            log.from = player;
            m_room.sendLog(log);
            m_room.notifySkillInvoked(player, log.arg);
            QVariant decisionData = "skillInvoke:" + log.arg + ":yes";
            m_eventDispatcher.dispatch(ChoiceMade, player, decisionData);
        }
        QStringList names;
        foreach (ServerPlayer *p, log.to)
            names.append(p->objectName());
        QVariant data = QString("playerChosen:%1:%2").arg(log.arg).arg(names.join("+"));
        m_eventDispatcher.dispatch(ChoiceMade, player, data);
    }
    return log.to;
}

int PlayerDecisionService::askForCardChosen(ServerPlayer *player, ServerPlayer *who,
                                            const QString &flags, const QString &reason,
                                            bool handcard_visible, Card::HandlingMethod method,
                                            const QList<int> &disabled_ids, bool can_cancel)
{
    CardLifetimeScope cardScope(globalCardLifetimeManager());
    m_room.tryPause();
    m_room.notifyMoveFocus(player, S_COMMAND_CHOOSE_CARD);

    if (!handcard_visible && player->canSeeHandcard(who))
        handcard_visible = true;

    if (handcard_visible && !who->isKongcheng()) {
        JsonArray arg;
        arg << who->objectName() << JsonUtils::toJsonArray(who->handCards());
        m_room.doNotify(player, S_COMMAND_SET_KNOWN_CARDS, arg);
    }
    int card_id = -1;

    QVariant over = findTestOverride(player, "card_chosen", reason);
    if (over.isValid() && over.canConvert<int>()) {
        card_id = over.toInt();
    } else {
        AI *ai = player->getAI();
        if (ai) {
            QElapsedTimer timer;
            timer.start();
            player->setTag("cardChosenForAI", ListI2V(disabled_ids));
            card_id = ai->askForCardChosen(who, flags, reason, method);
            if (Config.AIDelay > timer.elapsed())
                m_room.thread->delay(Config.AIDelay - timer.elapsed());
        } else {
            JsonArray arg;
            arg << who->objectName() << flags << reason << handcard_visible;
            arg << (int)method << JsonUtils::toJsonArray(disabled_ids) << can_cancel;
            if (m_room.doRequest(player, S_COMMAND_CHOOSE_CARD, arg, true)) {
                const QVariant &clientReply = player->getClientReply();
                if (JsonUtils::isNumber(clientReply))
                    card_id = clientReply.toInt();
            } else {
                ai = player->getAI();
                if (ai) {
                    player->setTag("cardChosenForAI", ListI2V(disabled_ids));
                    card_id = ai->askForCardChosen(who, flags, reason, method);
                }
            }
        }
    }
    if (card_id == -1 && !can_cancel) {
        foreach (const Card *c, who->getCards(flags)) {
            if (disabled_ids.contains(c->getId()))
                continue;
            bool can_take = true;
            if (method == Card::MethodDiscard && !player->canDiscard(who, c->getId()))
                can_take = false;
            else if (method == Card::MethodGet && !player->canGet(who, c->getId()))
                can_take = false;
            if (can_take) {
                card_id = c->getId();
                break;
            }
        }
    }
    QVariant madeData = QString("cardChosen:%1:%2:%3:%4")
                            .arg(reason)
                            .arg(card_id)
                            .arg(who->objectName())
                            .arg(handcard_visible ? "visible" : "");
    m_eventDispatcher.dispatch(ChoiceMade, player, madeData);
    return card_id;
}

const Card *PlayerDecisionService::askForCardShow(ServerPlayer *player, ServerPlayer *requestor,
                                                  const QString &reason)
{
    CardLifetimeScope cardScope(globalCardLifetimeManager());
    m_room.tryPause();
    m_room.notifyMoveFocus(player, S_COMMAND_SHOW_CARD);
    const Card *card = nullptr;

    if (player->getHandcardNum() > 1) {
        AI *ai = player->getAI();
        if (ai)
            card = ai->askForCardShow(requestor, reason);
        else if (m_room.doRequest(player, S_COMMAND_SHOW_CARD, requestor->objectName(), true)) {
            JsonArray clientReply = player->getClientReply().value<JsonArray>();
            if (clientReply.size() > 0)
                card = Card::Parse(clientReply[0].toString());
        } else {
            ai = player->getAI();
            if (ai)
                card = ai->askForCardShow(requestor, reason);
        }
    }
    if (!card)
        card = player->getRandomHandCard();
    QVariant decisionData = "cardShow:" + reason + ":";
    if (card)
        decisionData = "cardShow:" + reason + ":" + card->toString();
    m_eventDispatcher.dispatch(ChoiceMade, player, decisionData);
    return card;
}

const Card *PlayerDecisionService::askForPindian(ServerPlayer *player, ServerPlayer *from,
                                                 const QString &reason)
{
    CardLifetimeScope cardScope(globalCardLifetimeManager());
    if (!from->isAlive() || !player->isAlive())
        return nullptr;
    m_room.tryPause();
    m_room.notifyMoveFocus(player, S_COMMAND_PINDIAN);

    if (player->getHandcardNum() == 1)
        return player->getHandcards().first();
    const Card *card = nullptr;
    AI *ai = player->getAI();
    if (ai) {
        QElapsedTimer timer;
        timer.start();
        card = ai->askForPindian(from, reason);
        if (Config.AIDelay > timer.elapsed())
            m_room.thread->delay(Config.AIDelay - timer.elapsed());
    } else if (m_room.doRequest(player, S_COMMAND_PINDIAN,
                                JsonArray() << from->objectName() << player->objectName(),
                                true)) {
        JsonArray clientReply = player->getClientReply().value<JsonArray>();
        if (clientReply.size() > 0)
            card = Card::Parse(clientReply[0].toString());
    } else {
        ai = player->getAI();
        if (ai)
            card = ai->askForPindian(from, reason);
    }
    if (!card)
        card = player->getRandomHandCard();
    return card;
}

QList<const Card *> PlayerDecisionService::askForPindianRace(ServerPlayer *from, ServerPlayer *to,
                                                             const QString &reason)
{
    CardLifetimeScope cardScope(globalCardLifetimeManager());
    if (!from->isAlive() || !to->isAlive())
        return QList<const Card *>() << nullptr << nullptr;
    m_room.tryPause();
    Countdown countdown;
    countdown.max = ServerInfo.getCommandTimeout(S_COMMAND_PINDIAN, S_CLIENT_INSTANCE);
    countdown.type = Countdown::S_COUNTDOWN_USE_SPECIFIED;
    m_room.notifyMoveFocus(QList<ServerPlayer *>() << from << to, S_COMMAND_PINDIAN, countdown);

    const Card *from_card = nullptr, *to_card = nullptr;

    if (from->getHandcardNum() == 1)
        from_card = from->getHandcards().first();
    if (to->getHandcardNum() == 1)
        to_card = to->getHandcards().first();

    QElapsedTimer timer;
    timer.start();
    if (!from_card) {
        AI *ai = from->getAI();
        if (ai)
            from_card = ai->askForPindian(from, reason);
    }
    if (!to_card) {
        AI *ai = to->getAI();
        if (ai)
            to_card = ai->askForPindian(from, reason);
    }
    QList<ServerPlayer *> players;
    if (!from_card) {
        JsonArray arr;
        arr << from->objectName() << to->objectName();
        from->m_commandArgs = arr;
        players << from;
    }
    if (!to_card) {
        JsonArray arr;
        arr << from->objectName() << to->objectName();
        to->m_commandArgs = arr;
        players << to;
    }
    if (players.isEmpty()) {
        if (Config.AIDelay > timer.elapsed())
            m_room.thread->delay(Config.AIDelay - timer.elapsed());
    } else {
        m_room.doBroadcastRequest(players, S_COMMAND_PINDIAN);
        foreach (ServerPlayer *player, players) {
            const Card *card = nullptr;
            if (player->m_isClientResponseReady) {
                JsonArray clientReply = player->getClientReply().value<JsonArray>();
                if (clientReply.size() > 0)
                    card = Card::Parse(clientReply[0].toString());
            } else {
                AI *ai = player->getAI();
                if (ai)
                    card = ai->askForPindian(from, reason);
            }
            if (card == nullptr)
                card = player->getRandomHandCard();
            if (player == from)
                from_card = card;
            else
                to_card = card;
        }
    }
    return QList<const Card *>() << from_card << to_card;
}


static int getConvertedPhysicalCardId(const Card *card)
{
    if (card == nullptr || card->isVirtualCard() || !card->isModified())
        return -1;

    const int cardId = card->getEffectiveId();
    if (cardId < 0 || cardId >= Sanguosha->getCardCount())
        return -1;

    const Card *originalCard = Sanguosha->getEngineCard(cardId);
    if (originalCard == nullptr)
        return -1;
    if (originalCard->objectName() == card->objectName()
        && !card->getSkillName(false).startsWith("_"))
        return -1;

    return cardId;
}

static bool hasGenericActiveSkillUsage(const ViewAsSkillV2 *skill)
{
    return skill && skill->getLimitScope() != Skill::Limit_None
        && skill->getLimitScope() != Skill::Limit_Custom;
}

QString PlayerDecisionService::askForTriggerOrder(ServerPlayer*player, const QString&reason, QList<SkillContext> &contexts,
                               bool optional, const QVariant&data)
{
    CardLifetimeScope cardScope(globalCardLifetimeManager());
    m_room.tryPause();
    m_room.notifyMoveFocus(player, S_COMMAND_TRIGGER_ORDER);

    if (contexts.isEmpty())
        return optional ? "cancel" : QString();

    QString answer;
    if (contexts.length() == 1) {
        const SkillContext &ctx = contexts.first();
        answer = ctx.skill_name;
        if (ctx.instanceID > 0) {
            answer += "#" + QString::number(ctx.instanceID);
        }
        if (ctx.owner && ctx.owner != player)
            answer += ":" + ctx.owner->objectName();
    } else {
        AI*ai = player->getAI();
        if (ai) {
            QElapsedTimer timer;
            timer.start();
            QStringList allSkills;
            foreach (const SkillContext &ctx, contexts) {
                allSkills << ctx.skill_name;
            }
            QMap<ServerPlayer*, QStringList> skillsMap;
            foreach (const SkillContext &ctx, contexts) {
                skillsMap[ctx.owner] << ctx.skill_name;
            }
            answer = ai->askForTriggerOrder(reason, skillsMap, optional, data);
            if (Config.AIDelay > timer.elapsed())
                m_room.thread->delay(Config.AIDelay - timer.elapsed());
        } else {
            JsonArray args;
            JsonArray skillOptions;
            foreach (const SkillContext &ctx, contexts) {
                skillOptions << ctx.toVariant();
            }
            args << QVariant(skillOptions);
            args << optional;
            if (m_room.doRequest(player, S_COMMAND_TRIGGER_ORDER, args, true)) {
                QVariant clientReply = player->getClientReply();
                if (clientReply.canConvert<QString>() && !clientReply.toString().isEmpty())
                    answer = clientReply.toString();
            }
        }
    }

    if (optional && (answer.isEmpty() || answer == "cancel"))
        return "cancel";

    // 格式二支援：返回值格式為 "skillName:ownerObjectName" 或 "skillName"
    QString result;
    if (answer.isEmpty() && !contexts.isEmpty()) {
        const SkillContext &ctx = contexts.at(qrand() % contexts.size());
        QString skillFullName = ctx.skill_name;
        if (ctx.instanceID > 0) {
            skillFullName += "#" + QString::number(ctx.instanceID);
        }
        if (ctx.owner && ctx.owner != player) {
            result = skillFullName + ":" + ctx.owner->objectName();
        } else {
            result = skillFullName;
        }
    } else {
        // 客戶端返回格式："skillName[#instanceId]:ownerName:invokerName..."，取前兩段
        QStringList replyParts = answer.split(":");
        QString replySkillName = replyParts.value(0);
        QString ownerObjectName = replyParts.value(1);

        // 解析客戶端回覆中的 #instanceID（考慮 # 開頭隱藏技能）
        QString replyBaseName;
        int replyInstanceId = SkillInstanceUtils::parseName(replySkillName, replyBaseName);
        replySkillName = replyBaseName;

        bool found = false;
        foreach (const SkillContext &ctx, contexts) {
            if (ctx.skill_name == replySkillName) {
                // 若客戶端回覆帶 instanceID，精確匹配；否則用第一個匹配
                if (replyInstanceId > 0 && ctx.instanceID != replyInstanceId)
                    continue;
                if (ownerObjectName.isEmpty()) {
                    found = true;
                    QString skillFullName = ctx.skill_name;
                    if (ctx.instanceID > 0) {
                        skillFullName += "#" + QString::number(ctx.instanceID);
                    }
                    if (ctx.owner && ctx.owner != player) {
                        result = skillFullName + ":" + ctx.owner->objectName();
                    } else {
                        result = skillFullName;
                    }
                    break;
                } else if (ctx.owner && ctx.owner->objectName() == ownerObjectName) {
                    found = true;
                    QString skillFullName = ctx.skill_name;
                    if (ctx.instanceID > 0) {
                        skillFullName += "#" + QString::number(ctx.instanceID);
                    }
                    result = skillFullName + ":" + ownerObjectName;
                    break;
                }
            }
        }

        if (!found && !contexts.isEmpty()) {
            const SkillContext &ctx = contexts.at(qrand() % contexts.size());
            QString skillFullName = ctx.skill_name;
            if (ctx.instanceID > 0) {
                skillFullName += "#" + QString::number(ctx.instanceID);
            }
            if (ctx.owner && ctx.owner != player) {
                result = skillFullName + ":" + ctx.owner->objectName();
            } else {
                result = skillFullName;
            }
        }
    }

    QVariant decisionData = "triggerOrder:" + reason + ":" + result;
    m_eventDispatcher.dispatch(ChoiceMade, player, decisionData);
    return result;
}

bool PlayerDecisionService::verifyNullificationResponse(ServerPlayer *player, const QVariant &response, void *)
{
	CardUseStruct use;
	use.from = player;
	return use.tryParse(response, &m_room) && m_room.resolveCardSkillInstance(use);
}

const Card* PlayerDecisionService::askForNullification(const Card*trick, ServerPlayer*from, ServerPlayer*to, bool positive)
{
	CardLifetimeScope cardScope(globalCardLifetimeManager());
	/*_NullificationAiHelper aiHelper;
	aiHelper.m_from = from;
	aiHelper.m_to = to;
	aiHelper.m_trick = trick;*/
	return _askForNullification(trick, from, to, positive);
}

const Card* PlayerDecisionService::_askForNullification(const Card*trick, ServerPlayer*from, ServerPlayer*to, bool positive)
{
	m_room.tryPause();
	CardUseStruct card_use = m_room.getTag("UseHistory"+trick->toString()).value<CardUseStruct>();
	if (card_use.no_respond_list.contains("_ALL_TARGETS")||card_use.no_offset_list.contains("_ALL_TARGETS")) return nullptr;
	
	m_room.m_runtime->state().setCurrentCardUsePattern("nullification");
	m_room.m_runtime->state().setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
	
	CardEffectStruct trickEffect,TrickEffect = to->getTag("TrickEffectData").value<CardEffectStruct>();
	if(TrickEffect.card==trick)
		trickEffect = TrickEffect;
	else{
		trickEffect.card = trick;
		trickEffect.from = from;
		trickEffect.to = to;
	}
	
	QList<ServerPlayer*> validPlayers, raceHumanPlayers, controlledHumanPlayers;
	QVariant data = QVariant::fromValue(trickEffect);
	foreach(ServerPlayer*player, m_room.getAllPlayers()){
		if (card_use.no_respond_list.contains(player->objectName())||card_use.no_offset_list.contains(player->objectName())) continue;
		if (player->hasNullification()){
			if(m_eventDispatcher.dispatch(TrickCardCanceling, player,data)) continue;
			validPlayers << player;
		}
	}
	if (validPlayers.isEmpty())
		return nullptr;
	
	JsonArray args;
	args << trick->objectName();
	args << (from?from->objectName():"");
	args << (to?to->objectName():"");
	foreach(ServerPlayer*player, validPlayers){
		if (!player->isOnline())
			continue;

		ServerPlayer *actual = m_room.getActualController(player);
		bool redirectedToController = actual != nullptr && actual != player && actual->isOnline() && m_room.getRequestTarget(player) == actual;
		if (redirectedToController)
			controlledHumanPlayers << player;
		else
			raceHumanPlayers << player;

		player->m_commandArgs = args;
		if (card_use.to.length()>1)
			m_room.doNotify(player, S_COMMAND_NULLIFICATION_ASKED, trick->objectName());
	}
	
	CardUseStruct use(nullptr,nullptr);
	time_t timeOut = ServerInfo.getCommandTimeout(S_COMMAND_NULLIFICATION, S_SERVER_INSTANCE);
	if (!raceHumanPlayers.isEmpty()) {
		use.from = m_room.doBroadcastRaceRequest(raceHumanPlayers, S_COMMAND_NULLIFICATION, timeOut, &Room::verifyNullificationResponse);
		if (use.from) {
			use.tryParse(use.from->getClientReply(), &m_room);
			if (!m_room.resolveCardSkillInstance(use))
				use.card = nullptr;
		}
	}
	if (!use.card) {
		foreach(ServerPlayer*player, controlledHumanPlayers){
			m_room.notifyMoveFocus(player, S_COMMAND_NULLIFICATION);
			if (!m_room.doRequest(player, S_COMMAND_NULLIFICATION, args, timeOut, true))
				continue;
			if (!m_room.verifyNullificationResponse(player, player->getClientReply(), nullptr))
				continue;

			CardUseStruct candidate;
			candidate.from = player;
			if (candidate.tryParse(player->getClientReply(), &m_room) && m_room.resolveCardSkillInstance(candidate)) {
				use = candidate;
				break;
			}
		}
	}
	if (!use.card){
		QElapsedTimer timer;
		timer.start();
		qShuffle(validPlayers);
		foreach(ServerPlayer*player, validPlayers){
			AI*ai = player->getAI();
			if (ai){
				if (qEnvironmentVariableIsSet("QSAN_TAG_DISCRIMINATOR_BOUNDARY")) {
					std::fprintf(stderr,
					             "NULLIFICATION_BOUNDARY TrickEffect.to=%p passed_to=%p "
					             "from=%p positive=%d\n",
					             static_cast<const void *>(TrickEffect.to),
					             static_cast<const void *>(to),
					             static_cast<const void *>(TrickEffect.from), positive ? 1 : 0);
				}
				use.card = ai->askForNullification(TrickEffect.card, TrickEffect.from, TrickEffect.to, positive);
				if (use.card){
					use.from = player;
					if (Config.AIDelay>timer.elapsed())
						m_room.thread->delay(Config.AIDelay-timer.elapsed());
					break;
				}
			}
		}
	}
	if (!use.card) return nullptr;
	use.whocard = trick;
	use.who = from;
	trickEffect.nullified = positive;
	use.from->setTag("NullifyingEffect", QVariant::fromValue(trickEffect));
	if (!m_room.useCard(use)) return _askForNullification(trick, from, to, positive);/*
	QString tn = use.from->objectName();
	if (to) tn = to->objectName();
	m_room.thread->delay(Config.AIDelay/2);
	data = "Nullification:"+trick->getClassName()+":"+tn+":"+(positive?"true":"false");
	m_eventDispatcher.dispatch(ChoiceMade, use.from, data);*/
	card_use = m_room.getTag("UseHistory"+use.card->toString()).value<CardUseStruct>();
	if(card_use.no_offset_list.contains("_HAS_EFFECT")) return use.card;
	return nullptr;/*
	if (useNullified(use.card))
		return _askForNullification(trick, from, to, positive);
	if (_askForNullification(use.card, use.from, to, !positive))
		return nullptr;
	return use.card;*/
}

const Card* PlayerDecisionService::askForCard(ServerPlayer*player, const QString&pattern, const QString&prompt,
	const QVariant&data, Card::HandlingMethod method, ServerPlayer*m_who, bool isRetrial, const QString&skill_name,
	bool isProvision, const Card*m_toCard)
{
	CardLifetimeScope cardScope(globalCardLifetimeManager());
	//Q_ASSERT(pattern != "slash" || method != Card::MethodUse); // use askForUseSlashTo instead
	if (!player->isAlive()) return nullptr;
	m_room.tryPause();
	m_room.notifyMoveFocus(player, S_COMMAND_RESPONSE_CARD);
	m_room.m_runtime->state().setCurrentCardUsePattern(pattern);
	CardUseStruct::CardUseReason u_reason = CardUseStruct::CARD_USE_REASON_UNKNOWN;
	if (method == Card::MethodResponse) u_reason = CardUseStruct::CARD_USE_REASON_RESPONSE;
	else if (method == Card::MethodUse) u_reason = CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
	m_room.m_runtime->state().setCurrentCardUseReason(u_reason);
	QString _pattern = pattern;
	if ((method==Card::MethodUse||method==Card::MethodResponse)&&!isRetrial){
		QStringList asked;
		asked << pattern << prompt << (method==Card::MethodUse?"use":"response");
		if (m_toCard){
			asked << m_toCard->toString();
			CardUseStruct use = m_room.getTag("UseHistory"+asked.last()).value<CardUseStruct>();
			if (use.no_respond_list.contains("_ALL_TARGETS")||use.no_respond_list.contains(player->objectName()))
				return nullptr;
		}
		QVariant askedData = asked;
		m_eventDispatcher.dispatch(CardAsked, player, askedData);
		_pattern = askedData.toStringList().first();
		m_room.m_runtime->state().setCurrentCardUsePattern(_pattern);
	}
	CardResponseStruct resp(nullptr, m_who, method == Card::MethodUse);
	for (int i = 0; i < 9; i++){
		if(!player->isAlive()) return nullptr;
		if(resp.m_card) break;
		CardUseStruct use = m_room.getTag("provided").value<CardUseStruct>();
		if (use.card){
			resp.m_card = use.card;
			m_room.tag.remove("provided");
		} else {
			m_room.tag.remove("AiResult");
			QVariant over = findTestOverride(player, "card", skill_name.isEmpty() ? _pattern : skill_name);
			if (over.isValid()) {
				if (over.canConvert<int>()) {
					int cardId = over.toInt();
					resp.m_card = Sanguosha->getCard(cardId);
				} else if (over.canConvert<QString>()) {
					resp.m_card = Card::Parse(over.toString());
				}
			} else {
			AI*ai = player->getAI();
			if (ai){
				QElapsedTimer timer;
				timer.start();
				resp.m_card = ai->askForCard(_pattern, prompt, data, method);
				if (Config.AIDelay>timer.elapsed())
					m_room.thread->delay(Config.AIDelay-timer.elapsed());
			} else {
				JsonArray arg;
				arg << _pattern << prompt << int(method);
				if (m_room.doRequest(player, S_COMMAND_RESPONSE_CARD, arg, true)){
					arg = player->getClientReply().value<JsonArray>();
					if (arg.size()>0){
						m_room.tag["AiResult"] = player->getClientReply();
						CardUseStruct parsed;
						parsed.from = player;
						if (parsed.tryParse(player->getClientReply(), &m_room) && m_room.resolveCardSkillInstance(parsed)) {
							resp.m_card = parsed.card;
							resp.sourceRef = parsed.sourceRef;
							resp.activationRef = parsed.activationRef;
						}
					}
				}else{
					ai = player->getAI();
					if (ai) resp.m_card = ai->askForCard(_pattern, prompt, data, method);
				}
			}
			}
		}
		if(resp.m_card){
			if(resp.m_card->isKindOf("DummyCard")&&resp.m_card->subcardsLength()==1)
				resp.m_card = Sanguosha->getCard(resp.m_card->getEffectiveId());
		}else{
			QVariant askedData = QString("cardResponded:%1:%2:").arg(_pattern).arg(prompt);
			m_eventDispatcher.dispatch(ChoiceMade, player, askedData);
			return nullptr;
		}
		if(method == Card::MethodUse || method == Card::MethodResponse){
			CardUseStruct responseUse(resp.m_card, player);
			if (!m_room.resolveCardSkillInstance(responseUse)) {
				resp.m_card = nullptr;
				continue;
			}
			resp.m_card = resp.m_card->validateInResponse(player);
			if(resp.m_card==nullptr) continue;
			if (responseUse.activationRef.isValid())
				const_cast<Card *>(resp.m_card)->setActivationSkill(responseUse.activationRef.key.skillName,
					responseUse.activationRef.key.instanceID);
			if (responseUse.sourceRef.isValid())
				const_cast<Card *>(resp.m_card)->setSourceSkill(responseUse.sourceRef.key.skillName,
					responseUse.sourceRef.key.instanceID);
		}
		if(player->isCardLimited(resp.m_card, method)){
			resp.m_card = nullptr;
		}else if(isRetrial)
			return resp.m_card;
	}
	CardUseStruct responseUse;
	responseUse.card = resp.m_card;
	responseUse.from = player;
	if (!m_room.resolveCardSkillInstance(responseUse))
		return nullptr;
	if (responseUse.sourceRef.isValid()) resp.sourceRef = responseUse.sourceRef;
	if (responseUse.activationRef.isValid()) resp.activationRef = responseUse.activationRef;
	// Pure responses do not enter Room::useCard().  Give SkillCard/ViewAs
	// responses the same execution-local context as the Play bridge before
	// CardResponded exposes the response to the rest of the engine.
	const bool isPureResponse = method == Card::MethodResponse && !isRetrial;
	const bool isSkillCardResponse = resp.m_card->isKindOf("SkillCard");
	const bool isViewAsResponse = !isSkillCardResponse && resp.m_card->isVirtualCard()
		&& !resp.m_card->getSkillName().isEmpty();
    SkillContext responseCtx;
    SkillContext responseIdentity;
    ServerPlayer *responseInvoker = nullptr;
    const ViewAsSkillV2 *responseActiveSkill = nullptr;
    bool responseUsageReserved = false;
    bool responseUsageCommitted = false;
    bool responseFinishStarted = false;
    QVariant responseCtxData;
    SkillExecutionRegistry::Guard responseExecution;
    auto restoreSkillContextIdentity = [](SkillContext &context, const SkillContext &identity) {
        context.skill_name = identity.skill_name;
        context.sourceRef = identity.sourceRef;
        context.activationRef = identity.activationRef;
        context.initiator = identity.initiator;
        context.instanceID = identity.instanceID;
    };
    auto finishResponseExecution = [&](SkillExecutionResult result = SkillExecutionCompleted) {
        if (responseExecution.executionID() == 0 || responseFinishStarted) return;
        responseFinishStarted = true;
        responseCtx = m_room.getSkillExecutionContext(responseExecution.executionID());
		responseCtx.current_event = EventSkillEffectFinished;
		responseCtxData = QVariant::fromValue(responseCtx);
        try {
            m_eventDispatcher.dispatch(EventSkillEffectFinished, player, responseCtxData);
        } catch (TriggerEvent) {
            responseCtx = responseCtxData.value<SkillContext>();
            restoreSkillContextIdentity(responseCtx, responseIdentity);
            if (responseInvoker) responseCtx.invoker = responseInvoker;
            m_room.setSkillExecutionContext(responseExecution.executionID(), responseCtx);
            m_room.recordSkillExecutionAudit(responseCtx, result);
            responseExecution.finish(result);
            throw;
        }
        responseCtx = responseCtxData.value<SkillContext>();
        restoreSkillContextIdentity(responseCtx, responseIdentity);
        if (responseInvoker) responseCtx.invoker = responseInvoker;
        m_room.setSkillExecutionContext(responseExecution.executionID(), responseCtx);
		m_room.recordSkillExecutionAudit(responseCtx, result);
		responseExecution.finish(result);
	};
	try {
	if (isPureResponse && (isSkillCardResponse || isViewAsResponse)) {
		const SkillCard *skillCard = isSkillCardResponse
			? qobject_cast<const SkillCard *>(resp.m_card->getRealCard()) : nullptr;
		responseCtx.skill_name = resp.sourceRef.isValid() ? resp.sourceRef.key.skillName
			: (resp.m_card->getSkillName().isEmpty() ? resp.m_card->objectName() : resp.m_card->getSkillName());
		responseCtx.sourceRef = resp.sourceRef;
		responseCtx.activationRef = resp.activationRef;
		responseCtx.initiator = player;
		responseCtx.invoker = player;
		responseCtx.owner = skillCard ? skillCard->getSkillOwner() : player;
		if (!responseCtx.owner) responseCtx.owner = player;
        responseCtx.instanceID = resp.activationRef.isValid() ? resp.activationRef.key.instanceID
            : (skillCard ? skillCard->getSkillInstanceId() : 0);
        responseCtx.use_card = resp.m_card;
		responseActiveSkill = dynamic_cast<const ViewAsSkillV2 *>(
			Sanguosha->getViewAsSkill(responseCtx.activationRef.key.skillName));
		if (responseActiveSkill) {
			bool amountOk = false;
			responseCtx.amount = m_room.getSkillInstanceAmount(
				responseActiveSkill->getAmountRef(responseCtx), &amountOk);
			if (!amountOk) responseCtx.amount = responseActiveSkill->getBaseAmount();
		}
        responseIdentity = responseCtx;
        responseExecution = m_room.beginSkillExecution(responseCtx, QVariant::fromValue(resp));
		resp.skillExecutionID = responseExecution.executionID();
		if (responseActiveSkill && !responseCtx.bypass_cost) {
			ActiveSkillRequest request;
			request.reason = m_room.m_runtime->state().getCurrentCardUseReason();
			request.pattern = m_room.m_runtime->state().getCurrentCardUsePattern();
			request.initiator = responseCtx.initiator;
            request.activationRef = responseCtx.activationRef;
            request.selectedCardIds = resp.m_card->getSubcards();
			const bool paidCost = responseActiveSkill->cost(&m_room, responseCtx, request);
            restoreSkillContextIdentity(responseCtx, responseIdentity);
            responseCtx.invoker = responseIdentity.invoker;
            if (!paidCost) {
                finishResponseExecution(SkillExecutionPayFailed);
                return nullptr;
			}
		}

		responseCtx.current_event = EventSkillWillInvoke;
        responseCtxData = QVariant::fromValue(responseCtx);
        m_eventDispatcher.dispatch(EventSkillWillInvoke, player, responseCtxData);
        responseCtx = responseCtxData.value<SkillContext>();
        restoreSkillContextIdentity(responseCtx, responseIdentity);
        if (responseCtx.is_canceled) {
			finishResponseExecution(SkillExecutionInvalidTargetUpdate);
            return nullptr;
        }
        responseInvoker = responseCtx.invoker ? responseCtx.invoker : responseIdentity.invoker;
        responseCtx.invoker = responseInvoker;
        if (responseCtx.invoker && responseCtx.invoker != player) {
			if (!responseCtx.invoker->isAlive()
				|| responseCtx.invoker->isCardLimited(resp.m_card, Card::MethodResponse)) {
				finishResponseExecution(SkillExecutionInvalidTargetUpdate);
				return nullptr;
			}
			player = responseCtx.invoker;
		}
		if (responseCtx.updated_card) {
			if (player->isCardLimited(responseCtx.updated_card, Card::MethodResponse)) {
				finishResponseExecution(SkillExecutionInvalidTargetUpdate);
				return nullptr;
			}
			resp.changeCard(const_cast<Card *>(responseCtx.updated_card));
		}
		if (responseActiveSkill && !m_room.reserveActiveSkillUsage(responseActiveSkill, responseCtx)) {
			finishResponseExecution(SkillExecutionPayFailed);
			return nullptr;
		}
		responseUsageReserved = hasGenericActiveSkillUsage(responseActiveSkill);
		if (!responseCtx.bypass_cost) {
			responseCtx.current_event = EventSkillPay;
            responseCtxData = QVariant::fromValue(responseCtx);
            m_eventDispatcher.dispatch(EventSkillPay, player, responseCtxData);
            responseCtx = responseCtxData.value<SkillContext>();
            restoreSkillContextIdentity(responseCtx, responseIdentity);
            responseCtx.invoker = responseInvoker;
			if (responseCtx.is_canceled) {
				if (responseUsageReserved)
					m_room.releaseActiveSkillUsage(responseActiveSkill, responseCtx);
				responseUsageReserved = false;
				finishResponseExecution(SkillExecutionPayFailed);
				return nullptr;
			}
		}
		if (responseActiveSkill && !responseCtx.bypass_cost) {
			ActiveSkillRequest request;
			request.reason = m_room.m_runtime->state().getCurrentCardUseReason();
			request.pattern = m_room.m_runtime->state().getCurrentCardUsePattern();
			request.initiator = responseCtx.initiator;
            request.activationRef = responseCtx.activationRef;
            request.selectedCardIds = resp.m_card->getSubcards();
			const bool paid = responseActiveSkill->pay(&m_room, responseCtx, request);
            restoreSkillContextIdentity(responseCtx, responseIdentity);
            responseCtx.invoker = responseInvoker;
			if (!paid || responseCtx.is_canceled) {
				if (responseUsageReserved)
					m_room.releaseActiveSkillUsage(responseActiveSkill, responseCtx);
				responseUsageReserved = false;
				finishResponseExecution(SkillExecutionPayFailed);
				return nullptr;
			}
		}
		if (responseActiveSkill) {
			m_room.commitActiveSkillUsage(responseActiveSkill, responseCtx);
			responseUsageCommitted = responseUsageReserved;
			responseUsageReserved = false;
		}
		responseCtx.current_event = EventSkillInvoking;
        responseCtxData = QVariant::fromValue(responseCtx);
        m_eventDispatcher.dispatch(EventSkillInvoking, player, responseCtxData);
        responseCtx = responseCtxData.value<SkillContext>();
        restoreSkillContextIdentity(responseCtx, responseIdentity);
        responseCtx.invoker = responseInvoker;
		responseCtx.current_event = EventSkillEffect;
		responseCtxData = QVariant::fromValue(responseCtx);
        const bool skipResponse = m_eventDispatcher.dispatch(EventSkillEffect, player, responseCtxData);
        responseCtx = responseCtxData.value<SkillContext>();
        restoreSkillContextIdentity(responseCtx, responseIdentity);
        responseCtx.invoker = responseInvoker;
        m_room.setSkillExecutionContext(responseExecution.executionID(), responseCtx);
		resp.nullified = responseCtx.is_canceled || skipResponse;
	}
	m_room.notifyCardProvenance(method == Card::MethodResponse ? "response" : "response_use", player,
		resp.m_card, resp.sourceRef, resp.activationRef);
	QList<int> ids;
	if (resp.m_card->isVirtualCard())
		ids = resp.m_card->getSubcards();
	else{
		ids << resp.m_card->getId();
		WrappedCard*wrapped = Sanguosha->getWrappedCard(ids.first());
		if (wrapped->isModified()) m_room.broadcastUpdateCard(m_room.getPlayers(), ids.first(), wrapped);
		//else broadcastResetCard(m_room.getPlayers(), ids.first());
	}
	QVariant askedData = QString("cardResponded:%1:%2:%3").arg(_pattern).arg(prompt).arg(resp.m_card->toString());
	m_eventDispatcher.dispatch(ChoiceMade, player, askedData);
	LogMessage log;
	log.from = player;
	CardMoveReason reason(CardMoveReason::S_REASON_LETUSE, player->objectName(), resp.m_card->getSkillName(), "");
	reason.m_extraData = QVariant::fromValue(resp.m_card);
	if (method == Card::MethodDiscard){
		log.type = "$DiscardCardWithSkill";
		log.card_str = ListI2S(ids).join("+");
		if (skill_name.isEmpty()){
			log.type = "$DiscardCard";
			m_room.sendLog(log);
		}else{
			log.arg = skill_name;
			if(skill_name.contains("$")){
				log.arg = skill_name.split("$").first();
				m_room.broadcastSkillInvoke(log.arg,skill_name.split("$").last().toInt(),player);
			}
			m_room.sendLog(log);
			reason.m_skillName = log.arg;
			m_room.notifySkillInvoked(player, log.arg);
		}
		reason.m_reason = CardMoveReason::S_REASON_THROW;
		m_room.moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::DiscardPile, reason), true);
	} else if (method == Card::MethodUse || method == Card::MethodResponse){
		foreach(int id, ids){
			resp.m_isHandcard = player->handCards().contains(id);
			if(!resp.m_isHandcard) break;
		}
		resp.m_toCard = m_toCard;
		askedData.setValue(resp);
		m_eventDispatcher.dispatch(PreCardResponded, player, askedData);
		log.type = "#UseCard";
		log.card_str = resp.m_card->toString();
		if(method == Card::MethodResponse){
			reason.m_reason = CardMoveReason::S_REASON_RESPONSE;
			log.type += "_Resp";
		}
		m_room.sendLog(log);
		// Pure responses bypass Room::useCard(), so publish converted-card visuals here.
		if (resp.m_card->getTypeId() != Card::TypeSkill
			&& (resp.m_card->isVirtualCard() || getConvertedPhysicalCardId(resp.m_card) >= 0))
			m_room.showVirtualCard(player, resp.m_card);
		m_room.moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable, reason), true);
		m_eventDispatcher.dispatch(CardResponded, player, askedData);
		if (!isProvision){
			foreach(int id, ids){
				if (m_room.getCardPlace(id) != Player::PlaceTable)
					ids.removeOne(id);
			}
			m_room.moveCardsAtomic(CardsMoveStruct(ids, player, nullptr, Player::PlaceTable, Player::DiscardPile, reason), true);
		}
		m_eventDispatcher.dispatch(PostCardResponded, player, askedData);
		m_room.clearCardFlag(resp.m_card);
		resp = askedData.value<CardResponseStruct>();
		if (resp.nullified) resp.m_card = nullptr;
	}
	finishResponseExecution(resp.nullified ? SkillExecutionEffectSkipped : SkillExecutionCompleted);
	} catch (TriggerEvent controlEvent) {
		if (responseUsageReserved && !responseUsageCommitted)
			m_room.releaseActiveSkillUsage(responseActiveSkill, responseCtx);
		responseUsageReserved = false;
		if (controlEvent == StageChange || controlEvent == TurnBroken) {
			try {
				finishResponseExecution(SkillExecutionNoResult);
			} catch (TriggerEvent) {
				// The original control event remains authoritative.
			}
		}
		throw controlEvent;
	}
	return resp.m_card;
}

CardUseStruct PlayerDecisionService::askForUseCardStruct(ServerPlayer*player, const QString&pattern, const QString&prompt, int notice_index,
	Card::HandlingMethod method, bool addHistory, ServerPlayer*who, const Card*whocard, QString flag)
{
	CardLifetimeScope cardScope(globalCardLifetimeManager());
	//Q_ASSERT(method != Card::MethodResponse);
	if (!player->isAlive()) return CardUseStruct();
	m_room.tryPause();
	m_room.notifyMoveFocus(player, S_COMMAND_RESPONSE_CARD);
	m_room.m_runtime->state().setCurrentCardUsePattern(pattern);
	if(method==Card::MethodPlay){
		m_room.m_runtime->state().setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
		method = Card::MethodUse;
	}else
		m_room.m_runtime->state().setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
	
	QStringList asked;
	asked << pattern << prompt << "use";
	if (whocard){
		asked << whocard->toString();
		CardUseStruct use = m_room.getTag("UseHistory"+whocard->toString()).value<CardUseStruct>();
		if (use.no_respond_list.contains("_ALL_TARGETS")||use.no_respond_list.contains(player->objectName()))
			return CardUseStruct();
	}
	QVariant asked_data = asked;
	m_eventDispatcher.dispatch(CardAsked, player, asked_data);
	for (int i = 0; i < 9; i++){
		if(!player->isAlive()) break;
		CardUseStruct card_use = m_room.getTag("provided").value<CardUseStruct>();
		card_use.from = player;
		card_use.whocard = whocard;
		card_use.who = who;
		if (card_use.card){
			m_room.tag.remove("provided");
		} else {
			AI*ai = player->getAI();
			if (ai){
				QElapsedTimer timer;
				timer.start();
				if (!m_room.decideAiSkillAction(player, m_room.m_runtime->state().getCurrentCardUseReason(), pattern,
					prompt, method, card_use)) {
					const AIRequest request = m_room.makeAIRequest(player, AIRequest::UseCard,
						m_room.m_runtime->state().getCurrentCardUseReason(), pattern, prompt, method);
					m_room.decideAiAction(player, request, card_use);
				}
				if (Config.AIDelay>timer.elapsed())
					m_room.thread->delay(Config.AIDelay-timer.elapsed());
			} else {
				JsonArray ask_str;
				ask_str << pattern << prompt << int(method) << notice_index;
				if (m_room.doRequest(player, S_COMMAND_RESPONSE_CARD, ask_str, true)){
					card_use.tryParse(player->getClientReply(), &m_room);
				}else{
					ai = player->getAI();
					if(ai){
						if (!m_room.decideAiSkillAction(player, m_room.m_runtime->state().getCurrentCardUseReason(),
							pattern, prompt, method, card_use)) {
							const AIRequest request = m_room.makeAIRequest(player, AIRequest::UseCard,
								m_room.m_runtime->state().getCurrentCardUseReason(), pattern, prompt, method);
							m_room.decideAiAction(player, request, card_use);
						}
					}
				}
			}
		}
		if (card_use.card!=nullptr){/*&&card_use.isValid(pattern)
			asked_data.setValue(card_use);
			m_eventDispatcher.dispatch(ChoiceMade, player, asked_data);*/
			if (!flag.isEmpty()) m_room.setCardFlag(card_use.card, flag);
			if (m_room.useCard(card_use, addHistory)) return card_use;
		} else {
			asked_data = "cardUsed:"+pattern+":"+prompt+":";
			m_eventDispatcher.dispatch(ChoiceMade, player, asked_data);
			break;
		}
	}
	return CardUseStruct();
}

CardUseStruct PlayerDecisionService::askForUseSlashToStruct(ServerPlayer*slasher, QList<ServerPlayer*> victims, const QString&prompt,
	bool distance_limit, bool disable_extra, bool addHistory, ServerPlayer*who, const Card*whocard, QString flag)
{
	CardLifetimeScope cardScope(globalCardLifetimeManager());
	// The realization of this function in the Slash::onUse and Slash::targetFilter.
	m_room.setPlayerFlag(slasher, "slashTargetFix");
	if (!distance_limit) m_room.setPlayerFlag(slasher, "slashNoDistanceLimit");
	if (disable_extra) m_room.setPlayerFlag(slasher, "slashDisableExtraTarget");
	if (victims.length() == 1) m_room.setPlayerFlag(slasher, "slashTargetFixToOne");
	foreach(ServerPlayer*victim, victims)
		m_room.setPlayerFlag(victim, "SlashAssignee");

	CardUseStruct use = askForUseCardStruct(slasher, "slash", prompt, -1, Card::MethodUse, addHistory, who, whocard, flag);
	if (use.card==nullptr){
		m_room.setPlayerFlag(slasher, "-slashTargetFix");
		m_room.setPlayerFlag(slasher, "-slashTargetFixToOne");
		foreach(ServerPlayer*victim, victims)
			m_room.setPlayerFlag(victim, "-SlashAssignee");
		m_room.setPlayerFlag(slasher, "-slashNoDistanceLimit");
		m_room.setPlayerFlag(slasher, "-slashDisableExtraTarget");
	}
	return use;
}

const Card* PlayerDecisionService::askForSinglePeach(ServerPlayer*player, ServerPlayer*dying)
{
	CardLifetimeScope cardScope(globalCardLifetimeManager());
	m_room.tryPause();
	m_room.notifyMoveFocus(player, S_COMMAND_ASK_PEACH);
	m_room.m_runtime->state().setCurrentCardUsePattern(player==dying?"peach+analeptic":"peach");
	m_room.m_runtime->state().setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_RESPONSE_USE);

	const Card*card = nullptr;
	SkillInstanceRef sourceRef;
	SkillInstanceRef activationRef;

	AI*ai = player->getAI();
	if (ai){
		QElapsedTimer timer;
		timer.start();
		card = ai->askForSinglePeach(dying);
		if (Config.AIDelay>timer.elapsed())
			m_room.thread->delay(Config.AIDelay-timer.elapsed());
	}else{
		JsonArray arg;
		arg << dying->objectName() << 1-dying->getHp();
		if (m_room.doRequest(player, S_COMMAND_ASK_PEACH, arg, true)){
			CardUseStruct response;
			response.from = player;
			if (response.tryParse(player->getClientReply(), &m_room) && m_room.resolveCardSkillInstance(response)) {
				card = response.card;
				sourceRef = response.sourceRef;
				activationRef = response.activationRef;
			}
		}else{
			ai = player->getAI();
			if(ai) card = ai->askForSinglePeach(dying);
		}
	}
	if (card){
		card = card->validateInResponse(player);
		if (!card||player->isCardLimited(card, Card::MethodUse))
			return askForSinglePeach(player, dying);
		else{
			m_room.notifyCardProvenance("response_use", player, card, sourceRef, activationRef);
			QVariant decisionData = QString("peach:%1:%2:%3").arg(dying->objectName()).arg(1-dying->getHp()).arg(card->toString());
			m_eventDispatcher.dispatch(ChoiceMade, player, decisionData);
		}
	}
	return card;
}

void PlayerDecisionService::activate(ServerPlayer*player, CardUseStruct&card_use)
{
	m_room.tryPause();

	QVariant playPhaseData;
	m_eventDispatcher.dispatch(EventPlayPhaseLoop, player, playPhaseData);
	
	if (player->getPhase()!=Player::Play||player->hasFlag("Global_PlayPhaseTerminated")){
		m_room.setPlayerFlag(player, "-Global_PlayPhaseTerminated");
		return;
	}

	m_room.notifyMoveFocus(player, S_COMMAND_PLAY_CARD);
	m_room.m_runtime->state().setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
	m_room.m_runtime->state().setCurrentCardUsePattern("");

	card_use.from = player;

	QVariant over = findTestOverride(player, "activate", "phase");
	if (over.isValid() && over.canConvert<QString>() && over.toString() == "pass") {
		card_use.card = nullptr;
		return;
	}

	AI*ai = player->getAI();
	if (ai){
		QElapsedTimer timer;
		timer.start();
		const AIRequest request = m_room.makeAIRequest(player, AIRequest::Activate,
			CardUseStruct::CARD_USE_REASON_PLAY, QString(), QString(), Card::MethodUse);
		m_room.decideAiAction(player, request, card_use);
		if (Config.AIDelay>timer.elapsed())
			m_room.thread->delay(Config.AIDelay-timer.elapsed());/*
		else if(Config.OperationTimeout*1000-timer.elapsed()<0)
			card_use.card = nullptr;*/
	} else {
		bool success = m_room.doRequest(player, S_COMMAND_PLAY_CARD, player->objectName(), true);

		if (m_room.m_surrenderRequestReceived){
			m_room.makeSurrender(player);
			if (m_room.game_state>0) activate(player, card_use);
		} else if(Config.EnableCheat&&m_room.makeCheat(player)){
			if (player->isAlive()) activate(player, card_use);
		}else if(success){
			const QVariant&clientReply = player->getClientReply();
			//if (clientReply.isNull()) return;
			if (!card_use.tryParse(clientReply, &m_room)){
				JsonArray client = clientReply.value<JsonArray>();
				m_room.room_message(Room::tr("Card cannot be parsed:\n %1").arg(client[0].toString()));
			}
		}else{
			ai = player->getAI();
			if(ai) {
				const AIRequest request = m_room.makeAIRequest(player, AIRequest::Activate,
					CardUseStruct::CARD_USE_REASON_PLAY, QString(), QString(), Card::MethodUse);
				m_room.decideAiAction(player, request, card_use);
			}
		}
	}
	/*if (!card_use.isValid("")) return;
	QVariant data = QVariant::fromValue(card_use);
	m_eventDispatcher.dispatch(ChoiceMade, player, data);*/
	if (card_use.card)
		card_use.m_validateTargets = true;
}

Card* PlayerDecisionService::askForDiscard(ServerPlayer*player, const QString&reason, int discard_num, int min_num,
	bool optional, bool include_equip, const QString&prompt, const QString&pattern, const QString&skill_name)
{
	CardLifetimeScope cardScope(globalCardLifetimeManager());
	if (!player->isAlive())
		return nullptr;
	m_room.tryPause();
	m_room.notifyMoveFocus(player, S_COMMAND_DISCARD_CARD);
	min_num = qMin(min_num, discard_num);
	QList<int> to_discard, jilei_list;
	QStringList ignore_list;
	QList<const Card*> cards = player->getHandcards();
	if (include_equip) cards << player->getEquips();
	foreach(const Card*c, cards){
		if (reason=="gamerule"&&player->isCardLimited(c,Card::MethodIgnore)){
			m_room.setPlayerCardLimitation(player, "discard", c->toString(), false);
			ignore_list << c->toString();
		}else if(Sanguosha->matchExpPattern(pattern,player, c)){
			if(player->isJilei(c,!include_equip)) jilei_list << c->getId();
			else to_discard << c->getId();
		}
	}
	if(to_discard.length()>min_num||(optional&&to_discard.length()==min_num)){
		AI*ai = player->getAI();
		if (ai){
			QElapsedTimer timer;
			timer.start();
			to_discard = ai->askForDiscard(reason, discard_num, min_num, optional, include_equip, pattern);
			if (Config.AIDelay>timer.elapsed())
				m_room.thread->delay(Config.AIDelay-timer.elapsed());
		} else {
			to_discard.clear();
			JsonArray ask_str;
			ask_str << discard_num << min_num << optional << include_equip << prompt << pattern;
			if(m_room.doRequest(player, S_COMMAND_DISCARD_CARD, ask_str, true)){
				JsonUtils::tryParse(player->getClientReply(),to_discard);
			}else{
				ai = player->getAI();
				if(ai) to_discard = ai->askForDiscard(reason, discard_num, min_num, optional, include_equip, pattern);
				//else if(!optional) to_discard = player->forceToDiscard(min_num, include_equip, true, pattern);
			}
		}
	}
	foreach(QString str, ignore_list)
		m_room.removePlayerCardLimitation(player, "discard", str);
	if(to_discard.length()<min_num){
		if(optional)
			to_discard.clear();
		else{
			qShuffle(cards);
			foreach(const Card*card, cards){
				if(to_discard.contains(card->getId())||jilei_list.contains(card->getId())||ignore_list.contains(card->toString())) continue;
				if(Sanguosha->matchExpPattern(pattern,player,card)){
					to_discard << card->getId();
					if(to_discard.length()>=min_num) break;
				}
			}
			if(to_discard.length()<min_num&&jilei_list.length()>0){
				foreach(int id, jilei_list){
					WrappedCard*wrapped = Sanguosha->getWrappedCard(id);
					if (wrapped->isModified()) m_room.broadcastUpdateCard(m_room.getPlayers(), id, wrapped);
					//else broadcastResetCard(m_room.getPlayers(), id);
					m_room.setCardFlag(id, "visible");
				}
				LogMessage log;
				log.type = "$JileiShowAllCards";
				log.from = player;
				log.card_str = ListI2S(jilei_list).join("+");
				m_room.sendLog(log);
				JsonArray gongxinArgs;
				gongxinArgs << player->objectName() << false << JsonUtils::toJsonArray(jilei_list);
				m_room.doBroadcastNotify(S_COMMAND_SHOW_ALL_CARDS, gongxinArgs);
				QVariant data = log.card_str;
				m_eventDispatcher.dispatch(ShowCards, player, data);
			}
		}
	}
	if (to_discard.isEmpty()) return nullptr;
	CardMoveReason mreason(CardMoveReason::S_REASON_THROW, player->objectName(), reason, "");
	if (reason == "gamerule") mreason.m_reason = CardMoveReason::S_REASON_RULEDISCARD;
	if (skill_name.isEmpty()){
		m_room.throwCard(to_discard, mreason, player);
	} else {
		LogMessage log;
		log.from = player;
		log.arg = skill_name;
		if(skill_name.contains("$")){
			ignore_list = skill_name.split("$");
			log.arg = ignore_list.first();
			m_room.broadcastSkillInvoke(log.arg,ignore_list.last().toInt(),player);
		}
		log.type = "$DiscardCardWithSkill";
		log.card_str = ListI2S(to_discard).join("+");
		m_room.sendLog(log);
		m_room.notifySkillInvoked(player, log.arg);
		m_room.moveCardsAtomic(CardsMoveStruct(to_discard, nullptr, Player::DiscardPile, mreason), true);
	}
	DummyCard*dummy_card = new DummyCard(to_discard);
	QVariant data = QString("cardDiscard:%1:%2").arg(reason).arg(dummy_card->toString());
	m_eventDispatcher.dispatch(ChoiceMade, player, data);
	dummy_card->deleteLater();
	return dummy_card;
}

Card* PlayerDecisionService::askForExchange(ServerPlayer*player, const QString&reason, int exchange_num, int min_num,
	bool include_equip, const QString&prompt, bool optional, const QString&pattern)
{
	CardLifetimeScope cardScope(globalCardLifetimeManager());
	if (!player->isAlive())
		return nullptr;
	m_room.tryPause();
	m_room.notifyMoveFocus(player, S_COMMAND_EXCHANGE_CARD);

	if(player->getCardCount(include_equip)<1) return nullptr;
	QList<const Card*> cards = player->getHandcards();
	if (include_equip) cards << player->getEquips();
	min_num = qMin(min_num, exchange_num);
	QList<int> to_exchange;
	foreach(const Card*card, cards){
		if(Sanguosha->matchExpPattern(pattern,player,card)){
			to_exchange << card->getId();
			if(to_exchange.length()>min_num) break;
		}
	}
	if(to_exchange.length()>min_num||(optional&&to_exchange.length()==min_num)){
		AI*ai = player->getAI();
		player->setFlags("Global_AIDiscardExchanging");
		if (ai){// share the same callback interface
			QElapsedTimer timer;
			timer.start();
			to_exchange = ai->askForDiscard(reason, exchange_num, min_num, optional, include_equip, pattern);
			if (Config.AIDelay>timer.elapsed())
				m_room.thread->delay(Config.AIDelay-timer.elapsed());
		} else {
			to_exchange.clear();
			JsonArray exchange_str;
			exchange_str << exchange_num << min_num << include_equip << prompt << optional << pattern;
			if (m_room.doRequest(player, S_COMMAND_EXCHANGE_CARD, exchange_str, true))
				JsonUtils::tryParse(player->getClientReply(), to_exchange);
			else{
				ai = player->getAI();
				if(ai) to_exchange = ai->askForDiscard(reason, exchange_num, min_num, optional, include_equip, pattern);
				//else if(!optional) to_exchange = player->forceToDiscard(min_num, include_equip, false, pattern);
			}
		}
		player->setFlags("-Global_AIDiscardExchanging");
	}
	if (to_exchange.length()<min_num){
		if(optional)
			to_exchange.clear();
		else{
			qShuffle(cards);
			foreach(const Card*card, cards){
				if(to_exchange.contains(card->getId())) continue;
				if(Sanguosha->matchExpPattern(pattern,player,card)){
					to_exchange << card->getId();
					if(to_exchange.length()>=min_num) break;
				}
			}
		}
	}
	if(to_exchange.isEmpty()) return nullptr;
	DummyCard*card = new DummyCard(to_exchange);
	card->deleteLater();
	return card;
}

PlayerDecisionService::GuanxingSelection PlayerDecisionService::askForGuanxingSelection(ServerPlayer *zhuge, const QList<int> &cards, int guanxing_type)
{
    CardLifetimeScope cardScope(globalCardLifetimeManager());
	m_room.tryPause();
	m_room.notifyMoveFocus(zhuge, S_COMMAND_SKILL_GUANXING);
	QList<int> top_cards, bottom_cards;

	if(cards.length() < 2&&guanxing_type != Room::GuanxingBothSides){
		if(guanxing_type == Room::GuanxingUpOnly) top_cards = cards;
		else bottom_cards = cards;
	}else{
		AI*ai = zhuge->getAI();
		if (ai){
			QElapsedTimer timer;
			timer.start();
			ai->askForGuanxing(cards, top_cards, bottom_cards, guanxing_type);
			if (Config.AIDelay>timer.elapsed())
				m_room.thread->delay(Config.AIDelay-timer.elapsed());
		} else {
			JsonArray guanxingArgs;
			guanxingArgs << JsonUtils::toJsonArray(cards) << guanxing_type;
			if(m_room.doRequest(zhuge, S_COMMAND_SKILL_GUANXING, guanxingArgs, true)){
				guanxingArgs = zhuge->getClientReply().value<JsonArray>();
				if (guanxingArgs.size() > 1){
					JsonUtils::tryParse(guanxingArgs[0], top_cards);
					JsonUtils::tryParse(guanxingArgs[1], bottom_cards);
					if (guanxing_type == Room::GuanxingDownOnly){
						bottom_cards << top_cards;
						top_cards.clear();
					}
				}
			}else{
				ai = zhuge->getAI();
				if(ai) ai->askForGuanxing(cards, top_cards, bottom_cards, guanxing_type);
			}
		}
	}/*
	if ((top_cards+bottom_cards).toSet()!=cards.toSet()){
		if (guanxing_type == GuanxingDownOnly){
			bottom_cards = cards;
			top_cards.clear();
		} else {
			top_cards = cards;
			bottom_cards.clear();
		}
	}*/
	GuanxingSelection selection;
	selection.top = top_cards;
	selection.bottom = bottom_cards;
	return selection;
}

CardsMoveStruct PlayerDecisionService::askForYijiStruct(ServerPlayer*guojia, QList<int>&cards, const QString&skill_name,
	bool is_preview, bool visible, bool optional, int max_num, QList<ServerPlayer*> players,
	CardMoveReason reason, const QString&prompt, bool notify_skill, bool get)
{
	CardLifetimeScope cardScope(globalCardLifetimeManager());
	CardsMoveStruct move;
	if (max_num == -1) max_num = cards.length();
	if (cards.isEmpty() || max_num == 0) return move;
	if (players.isEmpty()) players = m_room.getOtherPlayers(guojia);
	if (reason.m_reason == CardMoveReason::S_REASON_UNKNOWN){
		// when we use ? : here, compiling error occurs under debug mode...
		if (is_preview) reason.m_reason = CardMoveReason::S_REASON_PREVIEWGIVE;
		else reason.m_reason = CardMoveReason::S_REASON_GIVE;
		reason.m_playerId = guojia->objectName();
		reason.m_eventName = "yiji_give";
		reason.m_skillName = skill_name;
	}
	m_room.tryPause();
	m_room.notifyMoveFocus(guojia, S_COMMAND_SKILL_YIJI);
	ServerPlayer*target = nullptr;
	AI*ai = guojia->getAI();
	if (ai){
		QElapsedTimer timer;
		timer.start();
		int card_id = -1;
		QStringList player_names;
		foreach(ServerPlayer*p, players)
			player_names << p->objectName();
		guojia->setTag("yijiForAI", player_names);
		target = ai->askForYiji(cards, skill_name, card_id);
		if (card_id>=0) move.card_ids << card_id;
		if (Config.AIDelay>timer.elapsed())
			m_room.thread->delay(Config.AIDelay-timer.elapsed());
	} else {
		JsonArray arg, player_names;
		arg << JsonUtils::toJsonArray(cards) << optional << max_num;
		foreach(ServerPlayer*p, players)
			player_names << p->objectName();
		arg << QVariant(player_names) << prompt;
		if (m_room.doRequest(guojia, S_COMMAND_SKILL_YIJI, arg, true)){
			arg = guojia->getClientReply().value<JsonArray>();
			if (arg.size()>1&&JsonUtils::tryParse(arg[0],move.card_ids))
				target = m_room.findChild<ServerPlayer*>(arg[1].toString());
		}else{
			ai = guojia->getAI();
			if(ai){
				int card_id = -1;
				QStringList player_names;
				foreach(ServerPlayer*p, players)
					player_names << p->objectName();
				guojia->setTag("yijiForAI", player_names);
				target = ai->askForYiji(cards, skill_name, card_id);
				if (card_id>=0) move.card_ids << card_id;
			}
		}
	}
	if (!target) return move;
	reason.m_targetId = target->objectName();
	foreach(int id, move.card_ids)
		cards.removeOne(id);
	move.reason = reason;
	move.to = target;
	move.to_place = Player::PlaceHand;
	QVariant decisionData = QString("Yiji:%1:%2:%3").arg(skill_name)
		.arg(target->objectName()).arg(ListI2S(move.card_ids).join("+"));
	m_eventDispatcher.dispatch(ChoiceMade, guojia, decisionData);
	if (notify_skill){
		LogMessage log;
		log.type = "#InvokeSkill";
		log.from = guojia;
		log.arg = skill_name;
		m_room.sendLog(log);
		const Skill*skill = Sanguosha->getSkill(skill_name);
		if (skill){
			m_room.broadcastSkillInvoke(skill_name, skill->getEffectIndex(guojia, dummyCard(move.card_ids)));
			m_room.notifySkillInvoked(guojia, skill_name);
		}
	}
	if (get)
		m_room.moveCardsAtomic(move, visible);
	return move;
}
