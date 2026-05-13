#include "meleemode.h"
#include "standard.h"
#include "room.h"
#include "engine.h"
#include "settings.h"
#include "roomthread.h"
#include "util.h"
#include <QDateTime>

class MeleeMode : public TriggerSkill
{
    public:
	MeleeMode() : TriggerSkill("melee_mode")
	{
		events << GameStart << TurnStart << RoundStart << BuryVictim;
		global = true;
	}
    bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
    bool MeleeMode::checkMeleeCondition(Room *room) const
    {
        if (!Config.EnableMeleeMode)
            return false;

        return room->getTag("MeleeModeActive").toBool();
    }
    bool MeleeMode::trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (!Config.EnableMeleeMode)
            return false;

        QString mode = room->getMode();
        int modePlayerCount = Sanguosha->getPlayerCount(mode);
        bool isDoudizhu = mode.startsWith("02_1v2") || mode == "03_1v2" || mode.contains("doudizhu");
        bool isHegemonyMode = Config.EnableHegemony;
        bool isSupportedIdentityMode = !isHegemonyMode && isNormalGameMode(mode) && modePlayerCount >= 8;
        bool usesAliveThreshold = isHegemonyMode || isSupportedIdentityMode;

        if (!isDoudizhu && !usesAliveThreshold)
            return false;

        switch (triggerEvent) {
        case GameStart: {
            room->setTag("MeleeModeStartTime", QVariant(QDateTime::currentDateTime()));
            break;
        }
        
        case TurnStart: {
            if (room->getTag("MeleeModeActive").toBool())
                break;

            int aliveCount = room->getAlivePlayers().count();
            bool shouldActivate = false;

            if (usesAliveThreshold) {
                int playerCount = room->getPlayers().count();
                int threshold = qMax(3, playerCount / 2);
                if (aliveCount <= threshold)
                    shouldActivate = true;
            }

            if (shouldActivate && !room->getTag("MeleeModeActive").toBool()) {
                room->setTag("MeleeModeActive", true);
                LogMessage log;
                log.type = "#MeleeModeStart";
                room->sendLog(log);

                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    room->setPlayerCardLimitation(p, "use", "Peach", false);
                    room->attachSkillToPlayer(p, "melee_peach");
                }
            }
            break;
        }
        
        case RoundStart: {
            if (room->getTag("MeleeModeActive").toBool())
                break;
            bool shouldActivate = false;

            if (isDoudizhu) {
                QDateTime startTime = room->getTag("MeleeModeStartTime").toDateTime();
                if (startTime.isValid()) {
                    int elapsedSeconds = startTime.secsTo(QDateTime::currentDateTime());
                    if (elapsedSeconds >= 300)
                        shouldActivate = true;
                }
            }

            if (shouldActivate && !room->getTag("MeleeModeActive").toBool()) {
                room->setTag("MeleeModeActive", true);
                LogMessage log;
                log.type = "#MeleeModeStartDoudizhu";
                room->sendLog(log);

                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    room->addSlashCishu(p, 1);
                    room->attachSkillToPlayer(p, "melee_peach_filter");
                    room->filterCards(p, p->getHandcards(), false);
                }
            }
            break;
        }
        case BuryVictim: {
            if (room->getTag("MeleeModeActive").toBool())
                break;

            int aliveCount = room->getAlivePlayers().count();
            int playerCount = room->getPlayers().count();
            int threshold = qMax(3, playerCount / 2);

            if (aliveCount <= threshold && usesAliveThreshold) {
                room->setTag("MeleeModeActive", true);
                LogMessage log;
                log.type = "#MeleeModeStart";
                room->sendLog(log);

                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    room->setPlayerCardLimitation(p, "use", "Peach", false);
                }
            }
            break;
        }
        default:
            break;
        }
        return false;
    }
};

class MeleePeach : public OneCardViewAsSkill
{
public:
	MeleePeach() : OneCardViewAsSkill("melee_peach")
	{
		filter_pattern = "Peach|.|.|hand!";
        attached_lord_skill = true;
	}

    const Card *MeleePeach::viewAs(const Card *originalCard) const
    {
        CardUseStruct::CardUseReason reason = Sanguosha->getCurrentCardUseReason();
        QString pattern = Sanguosha->getCurrentCardUsePattern();
        if (reason == CardUseStruct::CARD_USE_REASON_PLAY || pattern.contains("slash", Qt::CaseInsensitive)) {
            Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
            slash->addSubcard(originalCard);
            slash->setSkillName("_" + objectName());
            return slash;
        } else if (pattern.contains("jink", Qt::CaseInsensitive)) {
            Jink *jink = new Jink(originalCard->getSuit(), originalCard->getNumber());
            jink->addSubcard(originalCard);
            jink->setSkillName("_" + objectName());
            return jink;
        }
        return nullptr;
    }

    bool MeleePeach::isEnabledAtPlay(const Player *player) const
    {
        return Slash::IsAvailable(player);
    }

    bool MeleePeach::isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern.contains("slash", Qt::CaseInsensitive) || pattern.contains("jink", Qt::CaseInsensitive);
    }
};

class MeleePeachFilter : public FilterSkill
{
public:
    MeleePeachFilter() : FilterSkill("melee_peach_filter")
    {
        attached_lord_skill = true;
    }
    
    bool MeleePeachFilter::viewFilter(const Card *to_select) const
    {
        return to_select->isKindOf("Peach") && Sanguosha->getCardPlace(to_select->getId()) == Player::PlaceHand;
    }

    const Card *MeleePeachFilter::viewAs(const Card *originalCard) const
    {
        MeleeSlashJink *card = new MeleeSlashJink(originalCard->getSuit(), originalCard->getNumber());
        card->setSkillName("_" + objectName());
        return card;
    }

};

MeleeSlashJink::MeleeSlashJink(Card::Suit suit, int number)
    : Slash(suit, number)
{
    setObjectName("melee_slash_jink");
    target_fixed = false;
}

QString MeleeSlashJink::getSubtype() const
{
    return "attack_card";
}

bool MeleeSlashJink::isKindOf(const char *cardType) const
{
    if (strcmp(cardType, "Slash") == 0 || strcmp(cardType, "Jink") == 0)
        return true;
    return BasicCard::isKindOf(cardType);
}

bool MeleeSlashJink::isAvailable(const Player *player) const
{
    return Slash::IsAvailable(player);
}

bool MeleeSlashJink::targetFixed() const
{
    CardUseStruct::CardUseReason reason = Sanguosha->getCurrentCardUseReason();
    return reason == CardUseStruct::CARD_USE_REASON_RESPONSE
        || (reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && Sanguosha->getCurrentCardUsePattern().contains("jink", Qt::CaseInsensitive));
}

bool MeleeSlashJink::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    if (targetFixed())
        return true;

    return Card::targetsFeasible(targets, Self);
}

bool MeleeSlashJink::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (targetFixed())
        return false;

    return Slash::IsAvailable(Self) && Self->inMyAttackRange(to_select) && !targets.contains(to_select);
}

void MeleeSlashJink::onUse(Room *room, CardUseStruct &card_use) const
{
    QString pattern = Sanguosha->getCurrentCardUsePattern();
    QString skillName = card_use.card->getSkillName(false);
    if (skillName.isEmpty())
        skillName = "_" + objectName();
    if (pattern.contains("jink", Qt::CaseInsensitive)) {
        Jink *jink = new Jink(card_use.card->getSuit(), card_use.card->getNumber());
        jink->setSkillName(skillName);
        jink->addSubcard(card_use.card);
        card_use.card = jink;
        BasicCard::onUse(room, card_use);
    } else {
        Slash *slash = new Slash(card_use.card->getSuit(), card_use.card->getNumber());
        slash->setSkillName(skillName);
        slash->addSubcard(card_use.card);
        card_use.card = slash;
        BasicCard::onUse(room, card_use);
    }
}

void MeleeSlashJink::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    BasicCard::use(room, source, targets);
}

void MeleeSlashJink::onEffect(CardEffectStruct &effect) const
{
    BasicCard::onEffect(effect);
}

MeleeModePackage::MeleeModePackage() : Package("MeleeMode", Package::CardPack) 
{
    skills << new MeleeMode << new MeleePeach << new MeleePeachFilter;
    Card *card = new MeleeSlashJink(Card::NoSuit, 0);
    card->setParent(this);
}

ADD_PACKAGE(MeleeMode)