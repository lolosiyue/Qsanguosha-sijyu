#include "sp.h"
//#include "client.h"
//#include "general.h"
//#include "skill.h"
//#include "standard-generals.h"
#include "engine.h"
#include "maneuvering.h"
//#include "json.h"
//#include "settings.h"
#include "clientplayer.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "yjcm2013.h"
#include "wind.h"

class SPMoonSpearSkill : public WeaponSkill
{
public:
    SPMoonSpearSkill() : WeaponSkill("sp_moonspear")
    {
        events << CardUsed << CardResponded;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->hasFlag("CurrentPlayer"))
            return false;

        const Card *card = nullptr;
        if (triggerEvent == CardUsed)
            card = data.value<CardUseStruct>().card;
        else if (triggerEvent == CardResponded)
            card = data.value<CardResponseStruct>().m_card;

        if (!card || card->getTypeId()<1 || !card->isBlack())
            return false;

        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *tmp, room->getAlivePlayers()) {
            if (player->inMyAttackRange(tmp))
                targets << tmp;
        }

        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@sp_moonspear", true, true);
        if (!target) return false;
        room->setEmotion(player, "weapon/moonspear");
        if (!room->askForCard(target, "jink", "@moon-spear-jink", QVariant(), Card::MethodResponse, player))
            room->damage(DamageStruct(objectName(), player, target));
        return false;
    }
};

SPMoonSpear::SPMoonSpear(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("sp_moonspear");
}

class MoonSpearSkill : public WeaponSkill
{
public:
    MoonSpearSkill() : WeaponSkill("moon_spear")
    {
        events << PreCardUsed << CardUsed << CardResponded;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->hasFlag("CurrentPlayer"))
            return false;
        if (triggerEvent == PreCardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Slash")&&player->hasFlag("MoonspearUse")){
				player->setFlags("-MoonspearUse");
				room->setEmotion(player, "weapon/moonspear");
				LogMessage log;
				log.type = "#InvokeSkill";
				log.from = player;
				log.arg = "moon_spear";
				room->sendLog(log);
				room->notifySkillInvoked(player, "moon_spear");
			}
            return false;
		}

        const Card *card = nullptr;
        if (triggerEvent == CardUsed)
            card = data.value<CardUseStruct>().card;
        else if (triggerEvent == CardResponded)
            card = data.value<CardResponseStruct>().m_card;

        if (!card || card->getTypeId()<1 || !card->isBlack())
            return false;

        player->setFlags("MoonspearUse");
        room->askForUseCard(player, "slash", "@moon-spear-slash", -1, Card::MethodUse, false);
        player->setFlags("-MoonspearUse");

        return false;
    }
};

MoonSpear::MoonSpear(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("moon_spear");
}

SPCardPackage::SPCardPackage()
    : Package("sp_cards")
{
    (new SPMoonSpear)->setParent(this);

    Card *moon_spear = new MoonSpear;
    moon_spear->setParent(this);

    skills << new SPMoonSpearSkill << new MoonSpearSkill;

    type = CardPack;
}

ADD_PACKAGE(SPCard)

HegemonySPPackage::HegemonySPPackage()
: Package("hegemony_sp")
{
    General *sp_heg_zhouyu = new General(this, "sp_heg_zhouyu", "wu", 3, true); // GSP 001
    sp_heg_zhouyu->addSkill("nosyingzi");
    sp_heg_zhouyu->addSkill("nosfanjian");

    General *sp_heg_xiaoqiao = new General(this, "sp_heg_xiaoqiao", "wu", 3, false); // GSP 002
    sp_heg_xiaoqiao->addSkill("tianxiang");
    sp_heg_xiaoqiao->addSkill("hongyan");
}
ADD_PACKAGE(HegemonySP)


Yongsi::Yongsi() : TriggerSkill("yongsi")
{
    events << DrawNCards << EventPhaseStart;
    frequency = Compulsory;
}

int Yongsi::getKingdoms(ServerPlayer *yuanshu) const
{
    QSet<QString> kingdom_set;
    Room *room = yuanshu->getRoom();
    foreach(ServerPlayer *p, room->getAlivePlayers())
        kingdom_set << p->getKingdom();

    return kingdom_set.size();
}

bool Yongsi::trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *yuanshu, QVariant &data) const
{
    if (triggerEvent == DrawNCards) {
        DrawStruct draw = data.value<DrawStruct>();
		if(draw.reason!="draw_phase") return false;
		int x = getKingdoms(yuanshu);
        draw.num += x;
		data = QVariant::fromValue(draw);

        Room *room = yuanshu->getRoom();
        LogMessage log;
        log.type = "#YongsiGood";
        log.from = yuanshu;
        log.arg = QString::number(x);
        log.arg2 = objectName();
        room->sendLog(log);

        room->broadcastSkillInvoke("yongsi", x % 2 + 1);
        room->notifySkillInvoked(yuanshu, objectName());
    } else if (triggerEvent == EventPhaseStart && yuanshu->getPhase() == Player::Discard) {
        int x = getKingdoms(yuanshu);
        LogMessage log;
        log.type = yuanshu->getCardCount() > x ? "#YongsiBad" : "#YongsiWorst";
        log.from = yuanshu;
        log.arg = QString::number(log.type == "#YongsiBad" ? x : yuanshu->getCardCount());
        log.arg2 = objectName();
        room->sendLog(log);
        room->notifySkillInvoked(yuanshu, objectName());
        if (x > 0)
            room->askForDiscard(yuanshu, "yongsi", x, x, false, true);
    }

    return false;
}

class WeidiViewAsSkill : public ViewAsSkill
{
public:
    WeidiViewAsSkill() : ViewAsSkill("weidi")
    {
    }

    static QList<const ViewAsSkill *> getLordViewAsSkills(const Player *player)
    {
        QList<const ViewAsSkill *> vs_skills;
        foreach (const Player *p, player->getAliveSiblings()) {
            if (p->isLord()) {
				foreach (const Skill *skill, p->getVisibleSkillList()) {
					if (skill->isLordSkill() && player->hasLordSkill(skill->objectName())) {
						const ViewAsSkill *vs = ViewAsSkill::parseViewAsSkill(skill);
						if (vs) vs_skills << vs;
					}
				}
            }
        }
        return vs_skills;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        foreach (const ViewAsSkill *skill, getLordViewAsSkills(player)) {
            if (skill->isEnabledAtPlay(player))
                return true;
        }
        return false;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        foreach (const ViewAsSkill *skill, getLordViewAsSkills(player)) {
            if (skill->isEnabledAtResponse(player, pattern))
                return true;
        }
        return false;
    }

    bool isEnabledAtNullification(const ServerPlayer *player) const
    {
        foreach (const ViewAsSkill *skill, getLordViewAsSkills(player)) {
            if (skill->isEnabledAtNullification(player))
                return true;
        }
        return false;
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        QString skill_name = Self->tag["weidi"].toString();
        if (skill_name.isEmpty()) return false;
        const ViewAsSkill *vs_skill = Sanguosha->getViewAsSkill(skill_name);
        if (vs_skill) return vs_skill->viewFilter(selected, to_select);
        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        QString skill_name = Self->tag["weidi"].toString();
        if (skill_name.isEmpty()) return nullptr;
        const ViewAsSkill *vs_skill = Sanguosha->getViewAsSkill(skill_name);
        if (vs_skill) return vs_skill->viewAs(cards);
        return nullptr;
    }
};

WeidiDialog *WeidiDialog::getInstance()
{
    static WeidiDialog *instance;
    if (instance == nullptr)
        instance = new WeidiDialog();

    return instance;
}

WeidiDialog::WeidiDialog()
{
    setObjectName("weidi");
    setWindowTitle(Sanguosha->translate("weidi"));
    group = new QButtonGroup(this);

    button_layout = new QVBoxLayout;
    setLayout(button_layout);
    connect(group, SIGNAL(buttonClicked(QAbstractButton *)), this, SLOT(selectSkill(QAbstractButton *)));
}

void WeidiDialog::popup()
{
    Self->tag.remove(objectName());
    foreach (QAbstractButton *button, group->buttons()) {
        button_layout->removeWidget(button);
        group->removeButton(button);
        delete button;
    }

    QList<const ViewAsSkill *> vs_skills = WeidiViewAsSkill::getLordViewAsSkills(Self);
    int count = 0;
    QString name;
    foreach (const ViewAsSkill *skill, vs_skills) {
        QAbstractButton *button = createSkillButton(skill->objectName());
        button->setEnabled(skill->isAvailable(Self, Sanguosha->currentRoomState()->getCurrentCardUseReason(),
            Sanguosha->currentRoomState()->getCurrentCardUsePattern()));
        if (button->isEnabled()) {
            count++;
            name = skill->objectName();
        }
        button_layout->addWidget(button);
    }

    if (count == 0) {
        emit onButtonClick();
        return;
    } else if (count == 1) {
        Self->tag[objectName()] = name;
        emit onButtonClick();
        return;
    }

    exec();
}

void WeidiDialog::selectSkill(QAbstractButton *button)
{
    Self->tag[objectName()] = button->objectName();
    emit onButtonClick();
    accept();
}

QAbstractButton *WeidiDialog::createSkillButton(const QString &skill_name)
{
    const Skill *skill = Sanguosha->getSkill(skill_name);
    if (!skill) return nullptr;

    QCommandLinkButton *button = new QCommandLinkButton(Sanguosha->translate(skill_name));
    button->setObjectName(skill_name);
    button->setToolTip(skill->getDescription(Self));

    group->addButton(button);
    return button;
}

class Weidi : public GameStartSkill
{
public:
    Weidi() : GameStartSkill("weidi")
    {
        frequency = Compulsory;
        view_as_skill = new WeidiViewAsSkill;
    }

    void onGameStart(ServerPlayer *) const
    {
        return;
    }

    QDialog *getDialog() const
    {
        return WeidiDialog::getInstance();
    }
};

class Yicong : public DistanceSkill
{
public:
    Yicong() : DistanceSkill("yicong")
    {
    }

    int getCorrect(const Player *from, const Player *to) const
    {
        int correct = 0;
        if (from->getHp()>2&&from->hasSkill(objectName()))
            correct--;
        if (to->getHp()<=2&&to->hasSkill(objectName()))
            correct++;
        return correct;
    }
};

class YicongEffect : public TriggerSkill
{
public:
    YicongEffect() : TriggerSkill("#yicong-effect")
    {
        events << HpChanged;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        int hp = player->getHp();
        int index = 0;
        int reduce = 0;
        if (data.canConvert<RecoverStruct>()) {
            int rec = data.value<RecoverStruct>().recover;
            if (hp > 2 && hp - rec <= 2)
                index = 1;
        } else {
            if (data.canConvert<DamageStruct>()) {
                DamageStruct damage = data.value<DamageStruct>();
                reduce = damage.damage;
            } else if (!data.isNull()) {
                reduce = data.toInt();
            }
            if (hp <= 2 && hp + reduce > 2)
                index = 2;
        }

        if (index > 0) {
            if (player->getGeneralName() == "gongsunzan"
                || (player->getGeneralName() != "st_gongsunzan" && player->getGeneral2Name() == "gongsunzan"))
                index += 2;
            room->broadcastSkillInvoke("yicong", index);
        }
        return false;
    }
};

class Yanyu : public TriggerSkill
{
public:
    Yanyu() : TriggerSkill("yanyu")
    {
        events << EventPhaseStart << BeforeCardsMove << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Play) {
			foreach (ServerPlayer *xiahou, room->findPlayersBySkillName(objectName())) {
				if (xiahou->canDiscard(xiahou, "he")) {
					const Card *card = room->askForCard(xiahou, "..", "@yanyu-discard", data, objectName());
					if (card) {
						room->broadcastSkillInvoke(objectName(), 1);
						xiahou->addMark(card->getType()+"YanyuDiscard-PlayClear", 3);
					}
				}
			}
        } else if (triggerEvent == BeforeCardsMove && TriggerSkill::triggerable(player)) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to_place == Player::DiscardPile) {
                QList<int> ids;
                foreach (int id, move.card_ids) {
                    const Card *card = Sanguosha->getCard(id);
                    if (player->getMark(card->getType()+"YanyuDiscard-PlayClear") > 0)
                        ids << id;
                }
                while (ids.length()>0&&player->isAlive()) {
                    room->fillAG(ids, player);
                    int card_id = room->askForAG(player, ids, true, objectName());
                    room->clearAG(player);
                    if (card_id < 0) break;
                    player->setMark("YanyuOnlyId", card_id + 1); // For AI
                    const Card *card = Sanguosha->getCard(card_id);
                    ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(),
                        QString("@yanyu-give:::%1:%2\\%3").arg(card->objectName())
                        .arg(card->getSuitString() + "_char")
                        .arg(card->getNumberString()),
                        true, true);
                    if (target) {
                        player->removeMark(card->getType()+"YanyuDiscard-PlayClear");
                        Player::Place place = move.from_places.at(move.card_ids.indexOf(card_id));
                        QList<int> _card_id;
                        _card_id << card_id;
                        move.removeCardIds(_card_id);
                        data = QVariant::fromValue(move);
                        ids.removeOne(card_id);
                        foreach (int id, ids) {
                            if (player->getMark(Sanguosha->getCard(id)->getTypeId()+"YanyuDiscard-PlayClear")<1) {
                                ids.removeOne(id);
                            }
                        }
                        if (move.from == target && place != Player::PlaceTable) {
                            // just indicate which card she chose...
                            LogMessage log;
                            log.type = "$MoveCard";
                            log.from = target;
                            log.to << target;
                            log.card_str = QString::number(card_id);
                            room->sendLog(log);
                        }
                        room->broadcastSkillInvoke(objectName(), 2);
                        target->obtainCard(card);
                    } else
                        break;
                }
            }
        }
        return false;
    }
};

class Xiaode : public TriggerSkill
{
public:
    Xiaode() : TriggerSkill("xiaode")
    {
        events << BuryVictim;
    }

    int getPriority(TriggerEvent) const
    {
        return -2;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const
    {
        ServerPlayer *xiahoushi = room->findPlayerBySkillName(objectName());
        if (!xiahoushi || !xiahoushi->tag["XiaodeSkill"].toString().isEmpty()) return false;
        QStringList skill_list = xiahoushi->tag["XiaodeVictimSkills"].toStringList();
        if (skill_list.isEmpty()) return false;
        if (!room->askForSkillInvoke(xiahoushi, objectName(), QVariant::fromValue(skill_list))) return false;
        QString skill_name = room->askForChoice(xiahoushi, objectName(), skill_list.join("+"));
        room->broadcastSkillInvoke(objectName());
        xiahoushi->tag["XiaodeSkill"] = skill_name;
        room->acquireSkill(xiahoushi, skill_name);
        return false;
    }
};

class XiaodeEx : public TriggerSkill
{
public:
    XiaodeEx() : TriggerSkill("#xiaode")
    {
        events << EventPhaseChanging << EventLoseSkill << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive) {
                QString skill_name = player->tag["XiaodeSkill"].toString();
                if (!skill_name.isEmpty()) {
                    room->detachSkillFromPlayer(player, skill_name, false, true);
                    player->tag.remove("XiaodeSkill");
                }
            }
        } else if (triggerEvent == EventLoseSkill && data.toString() == "xiaode") {
            QString skill_name = player->tag["XiaodeSkill"].toString();
            if (!skill_name.isEmpty()) {
                room->detachSkillFromPlayer(player, skill_name, false, true);
                player->tag.remove("XiaodeSkill");
            }
        } else if (triggerEvent == Death && TriggerSkill::triggerable(player)) {
            DeathStruct death = data.value<DeathStruct>();
            QStringList skill_list;
            skill_list.append(addSkillList(death.who->getGeneral()));
            skill_list.append(addSkillList(death.who->getGeneral2()));
            player->tag["XiaodeVictimSkills"] = QVariant::fromValue(skill_list);
        }
        return false;
    }

private:
    QStringList addSkillList(const General *general) const
    {
        if (!general) return QStringList();
        QStringList skill_list;
        foreach (const Skill *skill, general->getSkillList()) {
            if (skill->isVisible() && !skill->isLordSkill() && skill->getFrequency() != Skill::Wake)
                skill_list.append(skill->objectName());
        }
        return skill_list;
    }
};

MeibuFilter::MeibuFilter(const QString &skill_name)
 : FilterSkill(QString("#%1-filter").arg(skill_name)), n(skill_name)
{
}

bool MeibuFilter::viewFilter(const Card *to_select) const
{
    return to_select->getTypeId() == Card::TypeTrick;
}

const Card * MeibuFilter::viewAs(const Card *originalCard) const
{
    Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
    slash->setSkillName("_" + n);/*
    WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
    card->takeOver(slash);*/
    return slash;
}

XiemuCard::XiemuCard()
{
    target_fixed = true;
}

void XiemuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QString kingdom = room->askForKingdom(source, "xiemu");
    room->setPlayerMark(source, "@xiemu_" + kingdom, 1);
}

class XiemuViewAsSkill : public OneCardViewAsSkill
{
public:
    XiemuViewAsSkill() : OneCardViewAsSkill("xiemu")
    {
        filter_pattern = "Slash";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->canDiscard(player, "he") && !player->hasUsed("XiemuCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        XiemuCard *card = new XiemuCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class Xiemu : public TriggerSkill
{
public:
    Xiemu() : TriggerSkill("xiemu")
    {
        events << TargetConfirmed << EventPhaseStart;
        view_as_skill = new XiemuViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TargetConfirmed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.from && player != use.from && use.card->getTypeId() != Card::TypeSkill
                && use.card->isBlack() && use.to.contains(player)
                && player->getMark("@xiemu_" + use.from->getKingdom()) > 0) {
                LogMessage log;
                log.type = "#InvokeSkill";
                log.from = player;
                log.arg = objectName();
                room->sendLog(log);

                room->notifySkillInvoked(player, objectName());
                player->drawCards(2, objectName());
            }
        } else {
            if (player->getPhase() == Player::RoundStart) {
                foreach (QString kingdom, Sanguosha->getKingdoms()) {
                    QString markname = "@xiemu_" + kingdom;
                    if (player->getMark(markname) > 0)
                        room->setPlayerMark(player, markname, 0);
                }
            }
        }
        return false;
    }
};

class Naman : public TriggerSkill
{
public:
    Naman() : TriggerSkill("naman")
    {
        events << CardResponded;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardResponseStruct response = data.value<CardResponseStruct>();
		if (response.m_isUse||!response.m_card->isKindOf("Slash")) return false;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->hasSkill(objectName()) && room->getCardPlace(response.m_card->getEffectiveId()) == Player::DiscardPile
			&&room->askForSkillInvoke(p, objectName(), data)){
				room->broadcastSkillInvoke(objectName());
				room->obtainCard(p, response.m_card);/*
				CardMoveReason reason(CardMoveReason::S_REASON_GOTBACK, p->objectName());
				reason.m_extraData = response.m_card;
				room->moveCardTo(response.m_card, player, p, Player::PlaceHand, reason, true);*/
			}
		}
        return false;
    }
};

class FuluVS : public OneCardViewAsSkill
{
public:
    FuluVS() : OneCardViewAsSkill("fulu")
    {
        filter_pattern = "%slash";
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return Slash::IsAvailable(player);
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE &&
                (pattern.contains("slash") || pattern.contains("Slash"));
    }

    const Card *viewAs(const Card *originalCard) const
    {
        ThunderSlash *acard = new ThunderSlash(originalCard->getSuit(), originalCard->getNumber());
        acard->addSubcard(originalCard->getId());
        acard->setSkillName(objectName());
        return acard;
    }
};

class Fulu : public TriggerSkill
{
public:
    Fulu() : TriggerSkill("fulu")
    {
        events << ChangeSlash;
        view_as_skill = new FuluVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->objectName() != "slash") return false;
        bool has_changed = false;
        QString skill_name = use.card->getSkillName();
        if (!skill_name.isEmpty()) {
            const Skill *skill = Sanguosha->getSkill(skill_name);
            if (skill && !skill->inherits("FilterSkill") && !skill->objectName().contains("guhuo"))
                has_changed = true;
        }
        if (!has_changed || (use.card->isVirtualCard() && use.card->subcardsLength() == 0)) {
            ThunderSlash *thunder_slash = new ThunderSlash(use.card->getSuit(), use.card->getNumber());
            if (!use.card->isVirtualCard() || use.card->subcardsLength() > 0)
                thunder_slash->addSubcard(use.card);
            thunder_slash->setSkillName("fulu");
            bool can_use = true;
            foreach (ServerPlayer *p, use.to) {
                if (!player->canSlash(p, thunder_slash, false)) {
                    can_use = false;
                    break;
                }
            }
            if (can_use && room->askForSkillInvoke(player, "fulu", data, false)) {
                //room->broadcastSkillInvoke("fulu");
                use.changeCard(thunder_slash);
                data = QVariant::fromValue(use);
            }
        }
        return false;
    }
};

class Zhuji : public TriggerSkill
{
public:
    Zhuji() : TriggerSkill("zhuji")
    {
        events << DamageCaused << FinishJudge;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == DamageCaused) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.nature != DamageStruct::Thunder || !damage.from)
                return false;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (TriggerSkill::triggerable(p) && room->askForSkillInvoke(p, objectName(), data)) {
                    room->broadcastSkillInvoke(objectName());
                    JudgeStruct judge;
                    judge.good = true;
                    judge.play_animation = false;
                    judge.reason = objectName();
                    judge.pattern = ".";
                    judge.who = damage.from;

                    room->judge(judge);
                    if (judge.pattern == "black") {
                        LogMessage log;
                        log.type = "#ZhujiBuff";
                        log.from = p;
                        log.to << damage.to;
                        log.arg = QString::number(damage.damage);
                        log.arg2 = QString::number(++damage.damage);
                        room->sendLog(log);

                        data = QVariant::fromValue(damage);
                    }
                }
            }
        } else if (triggerEvent == FinishJudge) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge->reason == objectName()) {
                judge->pattern = (judge->card->isRed() ? "red" : "black");
                if (room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge && judge->card->isRed())
                    player->obtainCard(judge->card);
            }
        }
        return false;
    }
};

class Canshi : public TriggerSkill
{
public:
    Canshi() : TriggerSkill("canshi")
    {
        events << EventPhaseStart << CardUsed;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
            if (TriggerSkill::triggerable(player) && player->getPhase() == Player::Draw) {
                int n = 0;
                foreach (ServerPlayer *p, room->getAllPlayers()) {
                    if (p->isWounded())
                        ++n;
                }

                if (n > 0 && player->askForSkillInvoke(this)) {
                    room->broadcastSkillInvoke(objectName());
                    player->setFlags(objectName());
                    player->drawCards(n, objectName());
                    return true;
                }
            }
        } else {
            if (player->hasFlag(objectName())) {
                const Card *card = nullptr;
                if (triggerEvent == CardUsed)
                    card = data.value<CardUseStruct>().card;
                if (card&&(card->isKindOf("BasicCard")||card->isKindOf("TrickCard"))&&player->canDiscard(player,"he")) {
                    room->sendCompulsoryTriggerLog(player, objectName());
                    room->askForDiscard(player, objectName(), 1, 1, false, true, "@canshi-discard");
                }
            }
        }
        return false;
    }
};

class Chouhai : public TriggerSkill
{
public:
    Chouhai() : TriggerSkill("chouhai")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->isKongcheng()) {
            room->sendCompulsoryTriggerLog(player, objectName(), true);
            room->broadcastSkillInvoke(objectName());

            DamageStruct damage = data.value<DamageStruct>();
            ++damage.damage;
            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

class Guiming : public TriggerSkill // play audio effect only. This skill is coupled in Player::isWounded().
{
public:
    Guiming() : TriggerSkill("guiming$")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive() && target->hasLordSkill(this) && target->getPhase() == Player::RoundStart;
    }

    int getPriority(TriggerEvent) const
    {
        return 6;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        foreach (const ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getKingdom() == "wu" && p->isWounded() && p->getHp() == p->getMaxHp()) {
                if (player->hasSkill("weidi"))
                    room->broadcastSkillInvoke("weidi");
                else
                    room->broadcastSkillInvoke(objectName());
                return false;
            }
        }

        return false;
    }
};

class Conqueror : public TriggerSkill
{
public:
    Conqueror() : TriggerSkill("conqueror")
    {
        events << TargetSpecified;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card != nullptr && use.card->isKindOf("Slash")) {
            int n = 0;
            foreach (ServerPlayer *target, use.to) {
                if (player->askForSkillInvoke(this, QVariant::fromValue(target))) {
                    QString choice = room->askForChoice(player, objectName(), "BasicCard+EquipCard+TrickCard", QVariant::fromValue(target));

                    room->broadcastSkillInvoke(objectName(), 1);

                    const Card *c = room->askForCard(target, choice, QString("@conqueror-exchange:%1::%2").arg(player->objectName()).arg(choice), choice, Card::MethodNone);
                    if (c != nullptr) {
                        room->broadcastSkillInvoke(objectName(), 2);
                        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), objectName(), "");
                        room->obtainCard(player, c, reason);
                        use.nullified_list << target->objectName();
                        data = QVariant::fromValue(use);
                    } else {
                        room->broadcastSkillInvoke(objectName(), 3);
                        QVariantList jink_list = player->tag["Jink_" + use.card->toString()].toList();
                        jink_list[n] = QVariant(0);
                        player->tag["Jink_" + use.card->toString()] = QVariant::fromValue(jink_list);
                        LogMessage log;
                        log.type = "#NoJink";
                        log.from = target;
                        room->sendLog(log);
                    }
                }
                ++n;
            }
        }
        return false;
    }
};

class Fentian : public PhaseChangeSkill
{
public:
    Fentian() : PhaseChangeSkill("fentian")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *hanba, Room *room) const
    {
        if (hanba->getPhase() != Player::Finish)
            return false;

        if (hanba->getHandcardNum() >= hanba->getHp())
            return false;

        QList<ServerPlayer*> targets;

        foreach (ServerPlayer *p, room->getOtherPlayers(hanba)) {
            if (hanba->inMyAttackRange(p) && !p->isNude())
                targets << p;
        }

        if (targets.isEmpty())
            return false;

        room->broadcastSkillInvoke(objectName());
        ServerPlayer *target = room->askForPlayerChosen(hanba, targets, objectName(), "@fentian-choose", false, true);
        int id = room->askForCardChosen(hanba, target, "he", objectName());
        hanba->addToPile("burn", id);
        return false;
    }
};

class FentianRange : public AttackRangeSkill
{
public:
    FentianRange() : AttackRangeSkill("#fentian")
    {

    }

    int getExtra(const Player *target, bool) const
    {
        if (target->hasSkill(objectName()))
            return target->getPile("burn").length();

        return 0;
    }
};

class Zhiri : public PhaseChangeSkill
{
public:
    Zhiri() : PhaseChangeSkill("zhiri")
    {
        frequency = Wake;
        waked_skills = "xintan";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *hanba, Room *room) const
    {
        if (hanba->getPile("burn").length() >= 3) {
            LogMessage log;
            log.from = hanba;
            log.type = "#ZhiriWake";
            log.arg = QString::number(hanba->getPile("burn").length());
            log.arg2 = objectName();
            room->sendLog(log);
        }else if(!hanba->canWake(objectName()))
			return false;
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(hanba,objectName());
        room->doSuperLightbox(hanba, "zhiri");

        room->setPlayerMark(hanba, objectName(), 1);
        if (room->changeMaxHpForAwakenSkill(hanba, -1, objectName()))
            room->acquireSkill(hanba, "xintan");
        return false;
    }

};

XintanCard::XintanCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool XintanCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}

void XintanCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *hanba = effect.from;
    Room *room = hanba->getRoom();

    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, hanba->objectName(), objectName(), "");
    room->moveCardTo(this, nullptr, Player::DiscardPile, reason, true);

    room->loseHp(HpLostStruct(effect.to, 1, "xintan", hanba));
}

class Xintan : public ViewAsSkill
{
public:
    Xintan() : ViewAsSkill("xintan")
    {
        expand_pile = "burn";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getPile("burn").length() >= 2 && !player->hasUsed("XintanCard");
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (selected.length() < 2)
            return Self->getPile("burn").contains(to_select->getId());

        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() == 2) {
            XintanCard *xt = new XintanCard;
            xt->addSubcards(cards);
            return xt;
        }

        return nullptr;
    }
};

FanghunCard::FanghunCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
    m_skillName = "fanghun";
}

bool FanghunCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
    CardUseStruct::CardUseReason reason = Sanguosha->currentRoomState()->getCurrentCardUseReason();

    if (reason == CardUseStruct::CARD_USE_REASON_PLAY || pattern.contains("slash") || pattern.contains("Slash")) {
        Slash *slash = new Slash(NoSuit, 0);
        slash->setSkillName("_longdan");
        slash->addSubcards(getSubcards());
        slash->deleteLater();
        return slash->targetFilter(targets, to_select, Self);
    }
    return false;
}

bool FanghunCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    CardUseStruct::CardUseReason reason = Sanguosha->currentRoomState()->getCurrentCardUseReason();
    QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();

    if (reason == CardUseStruct::CARD_USE_REASON_PLAY || pattern.contains("slash") || pattern.contains("Slash")) {
        Slash *slash = new Slash(NoSuit, 0);
        slash->setSkillName("_longdan");
        slash->addSubcards(getSubcards());
        slash->deleteLater();
        return slash->targetsFeasible(targets, Self);
    }
    return targets.length() == 0;
}

const Card *FanghunCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer *player = card_use.from;
    Room *room = player->getRoom();

    CardUseStruct::CardUseReason reason = Sanguosha->currentRoomState()->getCurrentCardUseReason();
    QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();

    if (reason == CardUseStruct::CARD_USE_REASON_PLAY || pattern.contains("slash") || pattern.contains("Slash")) {
        Slash *slash = new Slash(NoSuit, 0);
        slash->addSubcards(getSubcards());
        slash->setSkillName("_longdan");
        slash->setFlags("JINGYIN");
		slash->deleteLater();
        for (int i = card_use.to.length() - 1; i >=0 ; i--) {
            if (!player->canSlash(card_use.to.at(i)))
                card_use.to.removeOne(card_use.to.at(i));
        }
        if (card_use.to.isEmpty()||player->isLocked(slash)) return nullptr;
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = player;
        log.arg = m_skillName;
        room->sendLog(log);
        room->broadcastSkillInvoke(m_skillName);
        room->notifySkillInvoked(player, m_skillName);
        player->loseMark("&meiying");
        room->setPlayerMark(player, m_skillName + "_id", getSubcards().first() + 1);
        //room->useCard(CardUseStruct(slash, player, card_use.to), player->getPhase() == Player::Play);
       // player->drawCards(1, objectName());
        return slash;
    } else {
        Jink *jink = new Jink(NoSuit, 0);
        jink->addSubcards(getSubcards());
        jink->setSkillName("_longdan");
        jink->setFlags("JINGYIN");
		jink->deleteLater();
        if (player->isLocked(jink)) return nullptr;
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = player;
        log.arg = m_skillName;
        room->sendLog(log);
        room->broadcastSkillInvoke(m_skillName);
        room->notifySkillInvoked(player, m_skillName);
        player->loseMark("&meiying");
        room->setPlayerMark(player, m_skillName + "_id", getSubcards().first() + 1);
        return jink;
    }
    return nullptr;
}

const Card *FanghunCard::validateInResponse(ServerPlayer *player) const
{
    Room *room = player->getRoom();
    QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();

    if (pattern == "jink") {
        Jink *jink = new Jink(NoSuit, 0);
        jink->addSubcards(getSubcards());
        jink->setSkillName("_longdan");
        jink->setFlags("JINGYIN");
		jink->deleteLater();
        if (player->isLocked(jink)) return nullptr;
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = player;
        log.arg = m_skillName;
        room->sendLog(log);
        room->broadcastSkillInvoke(m_skillName);
        room->notifySkillInvoked(player, m_skillName);
        player->loseMark("&meiying");
        room->setPlayerMark(player, m_skillName + "_id", getSubcards().first() + 1);
        return jink;
    } else {
        Slash *slash = new Slash(NoSuit, 0);
        slash->addSubcards(getSubcards());
        slash->setSkillName("_longdan");
        slash->setFlags("JINGYIN");
		slash->deleteLater();
        if (player->isLocked(slash)) return nullptr;
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = player;
        log.arg = m_skillName;
        room->sendLog(log);
        room->broadcastSkillInvoke(m_skillName);
        room->notifySkillInvoked(player, m_skillName);
        player->loseMark("&meiying");
        room->setPlayerMark(player, m_skillName + "_id", getSubcards().first() + 1);
        return slash;
    }
    return nullptr;
}

class FanghunViewAsSkill : public OneCardViewAsSkill
{
public:
    FanghunViewAsSkill(const QString &fanghun_skill) : OneCardViewAsSkill(fanghun_skill), fanghun_skill(fanghun_skill)
    {
        response_or_use = true;
    }

    bool viewFilter(const Card *to_select) const
    {
        switch (Sanguosha->currentRoomState()->getCurrentCardUseReason()) {
        case CardUseStruct::CARD_USE_REASON_PLAY: {
            return to_select->isKindOf("Jink");
        }
        case CardUseStruct::CARD_USE_REASON_RESPONSE:
        case CardUseStruct::CARD_USE_REASON_RESPONSE_USE: {
            QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
            if (pattern.contains("slash") || pattern.contains("Slash"))
                return to_select->isKindOf("Jink");
            else if (pattern == "jink")
                return to_select->isKindOf("Slash");
            return false;
        }
        default:
            return false;
        }
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return Slash::IsAvailable(player) && player->getMark("&meiying") > 0;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return (pattern == "jink" || pattern.contains("slash") || pattern.contains("Slash")) && player->getMark("&meiying") > 0;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        if (fanghun_skill == "fanghun") {
            FanghunCard *card = new FanghunCard;
            card->addSubcard(originalCard);
            return card;
        }
        if (fanghun_skill == "olfanghun") {
            OLFanghunCard *card = new OLFanghunCard;
            card->addSubcard(originalCard);
            return card;
        }
        if (fanghun_skill == "mobilefanghun") {
            MobileFanghunCard *card = new MobileFanghunCard;
            card->addSubcard(originalCard);
            return card;
        }
        if (fanghun_skill == "tenyearfanghun") {
            TenyearFanghunCard *card = new TenyearFanghunCard;
            card->addSubcard(originalCard);
            return card;
        }
        return nullptr;
    }

private:
    QString fanghun_skill;
};

class FanghunDraw : public TriggerSkill
{
public:
    FanghunDraw(const QString &fanghun_skill) : TriggerSkill("#" + fanghun_skill), fanghun_skill(fanghun_skill)
    {
        events << CardResponded << CardFinished << MarkChanged;
        global = true;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    int getPriority(TriggerEvent) const
    {
        return 0;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == CardResponded) {
            const Card *card = data.value<CardResponseStruct>().m_card;
            foreach(ServerPlayer *p, room->getAllPlayers()) {
                int marks = p->getMark(fanghun_skill + "_id") - 1;
                if (marks < 0) continue;
                if (marks == card->getEffectiveId()) {
                    room->setPlayerMark(player, fanghun_skill + "_id", 0);
                    p->drawCards(1, objectName());
                    break;
                }
            }
        } else if (triggerEvent == CardFinished) {
            const Card *card = data.value<CardUseStruct>().card;
            foreach(ServerPlayer *p, room->getAllPlayers()) {
                int marks = p->getMark(fanghun_skill + "_id") - 1;
                if (marks < 0) continue;
                if (marks == card->getEffectiveId()) {
                    room->setPlayerMark(player, fanghun_skill + "_id", 0);
                    p->drawCards(1, objectName());
                    break;
                }
            }
        } else {
			MarkStruct mark = data.value<MarkStruct>();
			if (mark.name == "&meiying" && mark.gain < 0)
				player->addMark("meiying", -mark.gain);
		}
        return false;
    }
private:
    QString fanghun_skill;
};

class Fanghun : public TriggerSkill
{
public:
    Fanghun() : TriggerSkill("fanghun")
    {
        events << Damage << Damaged;
        view_as_skill = new FanghunViewAsSkill("fanghun");
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.card->isKindOf("Slash")) return false;
        if ((triggerEvent == Damage && damage.by_user) || triggerEvent == Damaged) {
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            player->gainMark("&meiying");
        }
        return false;
     }
};

class Fuhan : public PhaseChangeSkill
{
public:
    Fuhan() : PhaseChangeSkill("fuhan")
    {
        frequency = Limited;
        limit_mark = "@fuhanMark";
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::RoundStart) return false;
        if (player->getMark("@fuhanMark") <= 0 || player->getMark("&meiying") <= 0) return false;
        QString num = QString::number(player->getMark("meiying") + player->getMark("&meiying"));
        if (!player->askForSkillInvoke("fuhan", QString("fuhan_invoke:%1").arg(num))) return false;
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox(player, "fuhan");
        room->removePlayerMark(player, "@fuhanMark");
        int meiying = player->getMark("&meiying");
        player->loseAllMarks("&meiying");
        player->drawCards(meiying, objectName());
        QStringList shus = Sanguosha->getLimitedGeneralNames("shu");
        QStringList five_shus;
        for (int i = 1; i < 6; i++) {
            if (shus.isEmpty()) break;
            QString name = shus.at((qrand() % shus.length()));
            five_shus << name;
            shus.removeOne(name);
        }
        if (five_shus.isEmpty()) return false;
        QString shu_general = room->askForGeneral(player, five_shus);
        room->changeHero(player, shu_general, false, false, (player->getGeneralName() != "zhaoxiang" && player->getGeneral2Name() == "zhaoxiang"));
        int n = player->getMark("meiying");
        room->setPlayerProperty(player, "maxhp", n);
        int hp = player->getHp();
        bool recover = true;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getHp() < hp) {
                recover = false;
                break;
            }
        }
        if (recover == false) return false;
        room->recover(player, RecoverStruct("fuhan", player));
        return false;
    }
};

OLFanghunCard::OLFanghunCard() : FanghunCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
    m_skillName = "olfanghun";
}

class OLFanghun : public TriggerSkill
{
public:
    OLFanghun() : TriggerSkill("olfanghun")
    {
        events << Damage << Damaged;
        view_as_skill = new FanghunViewAsSkill("olfanghun");
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Damage || triggerEvent == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("Slash")) return false;
            if ((triggerEvent == Damage && damage.by_user) || triggerEvent == Damaged) {
                room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                player->gainMark("&meiying", damage.damage);
            }
        }
        return false;
     }
};

class OLFuhan : public PhaseChangeSkill
{
public:
    OLFuhan() : PhaseChangeSkill("olfuhan")
    {
        frequency = Limited;
        limit_mark = "@olfuhanMark";
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::RoundStart) return false;
        if (player->getMark("@olfuhanMark") <= 0 || player->getMark("&meiying") <= 0) return false;
        int nn = player->getMark("meiying") + player->getMark("&meiying");
        nn = qMin(8, nn);
        nn = qMax(2, nn);
        QString num = QString::number(nn);
        if (!player->askForSkillInvoke("olfuhan", QString("olfuhan_invoke:%1").arg(num))) return false;
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox(player, "olfuhan");
        room->removePlayerMark(player, "@olfuhanMark");
        player->loseAllMarks("&meiying");
        QStringList shus = Sanguosha->getLimitedGeneralNames("shu");
        QStringList five_shus;
        for (int i = 1; i < 6; i++) {
            if (shus.isEmpty()) break;
            QString name = shus.at((qrand() % shus.length()));
            five_shus << name;
            shus.removeOne(name);
        }
        if (five_shus.isEmpty()) return false;
        QString shu_general = room->askForGeneral(player, five_shus);
        room->changeHero(player, shu_general, false, false, (player->getGeneralName() != "ol_zhaoxiang" && player->getGeneral2Name() == "ol_zhaoxiang"));
        int n = player->getMark("meiying");
        n = qMin(8, n);
        n = qMax(2, n);
        room->setPlayerProperty(player, "maxhp", n);
        room->recover(player, RecoverStruct("olfuhan", player));
        return false;
    }
};

MobileFanghunCard::MobileFanghunCard() : FanghunCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
    m_skillName = "mobilefanghun";
}

class MobileFanghun : public TriggerSkill
{
public:
    MobileFanghun() : TriggerSkill("mobilefanghun")
    {
        events << Damage;
        view_as_skill = new FanghunViewAsSkill("mobilefanghun");
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.card->isKindOf("Slash")) return false;
        if (damage.by_user) {
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            player->gainMark("&meiying", 1);
        }
        return false;
     }
};

TenyearFanghunCard::TenyearFanghunCard() : FanghunCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
    m_skillName = "tenyearfanghun";
}

class TenyearFanghun : public TriggerSkill
{
public:
    TenyearFanghun() : TriggerSkill("tenyearfanghun")
    {
        events << TargetSpecified << TargetConfirmed;
        view_as_skill = new FanghunViewAsSkill("tenyearfanghun");
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("Slash")) return false;
        if (triggerEvent == TargetSpecified || (triggerEvent == TargetConfirmed && use.to.contains(player))) {
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            player->gainMark("&meiying", 1);
        }
        return false;
     }
};

class Wuniang : public TriggerSkill
{
public:
    Wuniang() : TriggerSkill("wuniang")
    {
        events << CardUsed << CardResponded;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        const Card *card;
        if (triggerEvent == CardUsed) card = data.value<CardUseStruct>().card;
        else card = data.value<CardResponseStruct>().m_card;
        if (!card || !card->isKindOf("Slash")) return false;
        QList<ServerPlayer *> players;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!p->isNude())
                players << p;
        }
        if (players.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(player, players, objectName(), "@wuniang-invoke", true, true);
        if (!target) return false;
        room->broadcastSkillInvoke(objectName());
        if (target->isNude()) return false;
        int id = room->askForCardChosen(player, target, "he", objectName());
        room->obtainCard(player, id, false);
        if (target->isAlive()) target->drawCards(1, objectName());
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getGeneralName().contains("guansuo") || p->getGeneral2Name().contains("guansuo"))
                p->drawCards(1, objectName());
        }
        return false;
    }
};

class Xushen : public TriggerSkill
{
public:
    Xushen() : TriggerSkill("xushen")
    {
        events << QuitDying;
        frequency = Limited;
        limit_mark = "@xushenMark";
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getMark("@xushenMark") <= 0) return false;
        ServerPlayer *saver = player->getSaver();
        if (!saver || !saver->isMale() || saver == player) return false;
        bool guansuo = false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getGeneralName().contains("guansuo") || p->getGeneral2Name().contains("guansuo")) {
                guansuo = true;
                break;
            }
        }
        if (guansuo) return false;
        if (!saver->askForSkillInvoke(objectName(), player, false)) return false;
        LogMessage log;
        log.type = "#InvokeOthersSkill";
        log.from = saver;
        log.to << player;
        log.arg = "xushen";
        room->sendLog(log);
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(player, objectName());
        room->doSuperLightbox(player, "xushen");
        room->removePlayerMark(player, "@xushenMark");
        room->changeHero(saver, "guansuo", false, false);
        room->recover(player, RecoverStruct("xushen", player));
        if (!player->hasSkill("zhennan", true))
            room->handleAcquireDetachSkills(player, "zhennan");
        return false;
    }
};

class Zhennan : public TriggerSkill
{
public:
    Zhennan() : TriggerSkill("zhennan")
    {
        events << TargetConfirmed;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        const Card *card = data.value<CardUseStruct>().card;
        if (!card->isKindOf("SavageAssault")) return false;
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@zhennan-invoke", true, true);
        if (!target) return false;
        room->broadcastSkillInvoke(objectName());
        int n = qrand() % 3 + 1;
        room->damage(DamageStruct(objectName(), player, target, n));
        return false;
    }
};

ZhanyiViewAsBasicCard::ZhanyiViewAsBasicCard()
{
    m_skillName = "_zhanyi";
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool ZhanyiViewAsBasicCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card) card->deleteLater();
        return card && card->targetFilter(targets, to_select, Self);
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return false;
    }

    const Card *card = Self->tag.value("zhanyi").value<const Card *>();
    return card && card->targetFilter(targets, to_select, Self);
}

bool ZhanyiViewAsBasicCard::targetFixed() const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card) card->deleteLater();
        return card && card->targetFixed();
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return true;
    }

    const Card *card = Self->tag.value("zhanyi").value<const Card *>();
    return card && card->targetFixed();
}

bool ZhanyiViewAsBasicCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        const Card *card = nullptr;
        if (!user_string.isEmpty())
            card = Sanguosha->cloneCard(user_string.split("+").first());
        return card && card->targetsFeasible(targets, Self);
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return true;
    }

    const Card *card = Self->tag.value("zhanyi").value<const Card *>();
    return card && card->targetsFeasible(targets, Self);
}

const Card *ZhanyiViewAsBasicCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer *zhuling = card_use.from;
    Room *room = zhuling->getRoom();

    QString to_zhanyi = user_string;
    if (user_string == "slash" && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        QStringList guhuo_list;
        guhuo_list << "slash";
        if (!Sanguosha->getBanPackages().contains("maneuvering"))
            guhuo_list << "normal_slash" << "thunder_slash" << "fire_slash";
        to_zhanyi = room->askForChoice(zhuling, "zhanyi_slash", guhuo_list.join("+"));
    }

    const Card *card = Sanguosha->getCard(subcards.first());
    QString user_str;
    if (to_zhanyi == "slash") {
        if (card->isKindOf("Slash"))
            user_str = card->objectName();
        else
            user_str = "slash";
    } else if (to_zhanyi == "normal_slash")
        user_str = "slash";
    else
        user_str = to_zhanyi;
    Card *use_card = Sanguosha->cloneCard(user_str, card->getSuit(), card->getNumber());
    use_card->setSkillName("_zhanyi");
    use_card->addSubcard(subcards.first());
    use_card->deleteLater();
    return use_card;
}

const Card *ZhanyiViewAsBasicCard::validateInResponse(ServerPlayer *zhuling) const
{
    Room *room = zhuling->getRoom();

    QString to_zhanyi;
    if (user_string == "peach+analeptic") {
        QStringList guhuo_list;
        guhuo_list << "peach";
        if (!Sanguosha->getBanPackages().contains("maneuvering"))
            guhuo_list << "analeptic";
        to_zhanyi = room->askForChoice(zhuling, "zhanyi_saveself", guhuo_list.join("+"));
    } else if (user_string == "slash") {
        QStringList guhuo_list;
        guhuo_list << "slash";
        if (!Sanguosha->getBanPackages().contains("maneuvering"))
            guhuo_list << "normal_slash" << "thunder_slash" << "fire_slash";
        to_zhanyi = room->askForChoice(zhuling, "zhanyi_slash", guhuo_list.join("+"));
    } else
        to_zhanyi = user_string;

    QString user_str;
    const Card *card = Sanguosha->getCard(subcards.first());
    if (to_zhanyi == "slash") {
        if (card->isKindOf("Slash"))
            user_str = card->objectName();
        else
            user_str = "slash";
    } else if (to_zhanyi == "normal_slash")
        user_str = "slash";
    else
        user_str = to_zhanyi;
    Card *use_card = Sanguosha->cloneCard(user_str, card->getSuit(), card->getNumber());
    use_card->setSkillName("_zhanyi");
    use_card->addSubcard(subcards.first());
    use_card->deleteLater();
    return use_card;
}

ZhanyiCard::ZhanyiCard()
{
    target_fixed = true;
}

void ZhanyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->loseHp(HpLostStruct(source, 1, "zhanyi", source));
    if (source->isAlive()) {
        const Card *c = Sanguosha->getCard(subcards.first());
        if (c->getTypeId() == Card::TypeBasic) {
            room->setPlayerMark(source, "ViewAsSkill_zhanyiEffect", 1);
        } else if (c->getTypeId() == Card::TypeEquip)
            source->setFlags("zhanyiEquip");
        else if (c->getTypeId() == Card::TypeTrick) {
            source->drawCards(2, "zhanyi");
            room->setPlayerFlag(source, "zhanyiTrick");
        }
    }
}

class ZhanyiNoDistanceLimit : public TargetModSkill
{
public:
    ZhanyiNoDistanceLimit() : TargetModSkill("#zhanyi-trick")
    {
        pattern = ".";
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *) const
    {
        return from->hasFlag("zhanyiTrick") ? 999 : 0;
    }
};

class ZhanyiDiscard2 : public TriggerSkill
{
public:
    ZhanyiDiscard2() : TriggerSkill("#zhanyi-equip")
    {
        events << TargetSpecified;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive() && target->hasFlag("zhanyiEquip");
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card == nullptr || !use.card->isKindOf("Slash"))
            return false;


        foreach (ServerPlayer *p, use.to) {
            if (p->isNude())
                continue;

            if (p->getCardCount() <= 2) {
                DummyCard dummy;
                dummy.addSubcards(p->getCards("he"));
                room->throwCard(&dummy, p);
            } else
                room->askForDiscard(p, "zhanyi_equip", 2, 2, false, true, "@zhanyiequip_discard");
        }
        return false;
    }
};

class Zhanyi : public OneCardViewAsSkill
{
public:
    Zhanyi() : OneCardViewAsSkill("zhanyi")
    {

    }

    bool isResponseOrUse() const
    {
        return Self->getMark("ViewAsSkill_zhanyiEffect") > 0;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        if (!player->hasUsed("ZhanyiCard"))
            return true;

        if (player->getMark("ViewAsSkill_zhanyiEffect") > 0)
            return true;

        return false;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        if (player->getMark("ViewAsSkill_zhanyiEffect") == 0) return false;
        if (pattern.startsWith(".") || pattern.startsWith("@")) return false;
        if (pattern == "peach" && player->getMark("Global_PreventPeach") > 0) return false;
        for (int i = 0; i < pattern.length(); i++) {
            QChar ch = pattern[i];
            if (ch.isUpper() || ch.isDigit()) return false; // This is an extremely dirty hack!! For we need to prevent patterns like 'BasicCard'
        }
        return !(pattern == "nullification");
    }

    QDialog *getDialog() const
    {
        return GuhuoDialog::getInstance("zhanyi", true, false);
    }

    bool viewFilter(const Card *to_select) const
    {
        if (Self->getMark("ViewAsSkill_zhanyiEffect") > 0)
            return to_select->isKindOf("BasicCard");
        else
            return true;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        if (Self->getMark("ViewAsSkill_zhanyiEffect") == 0) {
            ZhanyiCard *zy = new ZhanyiCard;
            zy->addSubcard(originalCard);
            return zy;
        }

        if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE
            || Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
            ZhanyiViewAsBasicCard *card = new ZhanyiViewAsBasicCard;
            card->setUserString(Sanguosha->getCurrentCardUsePattern());
            card->addSubcard(originalCard);
            return card;
        }

        const Card *c = Self->tag.value("zhanyi").value<const Card *>();
        if (c && c->isAvailable(Self)) {
            ZhanyiViewAsBasicCard *card = new ZhanyiViewAsBasicCard;
            card->setUserString(c->objectName());
            card->addSubcard(originalCard);
            return card;
        }
		return nullptr;
    }
};

class ZhanyiRemove : public TriggerSkill
{
public:
    ZhanyiRemove() : TriggerSkill("#zhanyi-basic")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive() && target->getMark("ViewAsSkill_zhanyiEffect") > 0;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to == Player::NotActive)
            room->setPlayerMark(player, "ViewAsSkill_zhanyiEffect", 0);

        return false;
    }
};

class Tunchu : public DrawCardsSkill
{
public:
    Tunchu() : DrawCardsSkill("tunchu")
    {

    }

    int getDrawNum(ServerPlayer *player, int n) const
    {
        if (player->askForSkillInvoke("tunchu")) {
            player->setFlags("tunchu");
            player->getRoom()->broadcastSkillInvoke("tunchu");
            return n + 2;
        }

        return n;
    }
};

class TunchuEffect : public TriggerSkill
{
public:
    TunchuEffect() : TriggerSkill("#tunchu-effect")
    {
        events << AfterDrawNCards;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive() && target->hasFlag("tunchu");
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DrawStruct draw = data.value<DrawStruct>();
		if (draw.reason=="draw_phase"&&player->hasFlag("tunchu") && !player->isKongcheng()) {
            player->setFlags("-tunchu");
            const Card *c = room->askForExchange(player, "tunchu", 1, 1, false, "@tunchu-put");
            if (c != nullptr) player->addToPile("food", c);
        }

        return false;
    }
};

class TunchuLimit : public CardLimitSkill
{
public:
    TunchuLimit() : CardLimitSkill("#tunchu-limit")
    {
    }

    QString limitList(const Player *) const
    {
        return "use";
    }

    QString limitPattern(const Player *target) const
    {
        if (target->getPile("food").length()>0&&target->hasSkill("tunchu"))
            return "Slash,Duel";
        return "";
    }
};

ShuliangCard::ShuliangCard()
{
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void ShuliangCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    CardMoveReason r(CardMoveReason::S_REASON_REMOVE_FROM_PILE, source->objectName(), "shuliang", "");
    room->moveCardTo(this, nullptr, Player::DiscardPile, r, true);
}

class ShuliangVS : public OneCardViewAsSkill
{
public:
    ShuliangVS() : OneCardViewAsSkill("shuliang")
    {
        response_pattern = "@@shuliang";
        filter_pattern = ".|.|.|food";
        expand_pile = "food";
    }

    const Card *viewAs(const Card *originalCard) const
    {
        ShuliangCard *c = new ShuliangCard;
        c->addSubcard(originalCard);
        return c;
    }
};

class Shuliang : public TriggerSkill
{
public:
    Shuliang() : TriggerSkill("shuliang")
    {
        events << EventPhaseStart;
        view_as_skill = new ShuliangVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive() && target->getPhase() == Player::Finish && target->isKongcheng();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        foreach (ServerPlayer *const &p, room->getAllPlayers()) {
            if (player->isDead()) return false;
            if (!TriggerSkill::triggerable(p))
                continue;

            if (!p->getPile("food").isEmpty()) {
                if (room->askForUseCard(p, "@@shuliang", "@shuliang:" + player->objectName(), -1, Card::MethodNone))
                    player->drawCards(2, objectName());
            }
        }

        return false;
    }
};

class QingyiViewAsSkill : public ZeroCardViewAsSkill
{
public:
    QingyiViewAsSkill() : ZeroCardViewAsSkill("qingyi")
    {
        response_pattern = "@@qingyi";
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern == "@@qingyi";
    }

    const Card *viewAs() const
    {
		Slash *slash = new Slash(Card::NoSuit, 0);
		slash->setSkillName("qingyi");
        return slash;
    }
};

class Qingyi : public TriggerSkill
{
public:
    Qingyi() : TriggerSkill("qingyi")
    {
        events << EventPhaseChanging;
        view_as_skill = new QingyiViewAsSkill;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to == Player::Judge && !player->isSkipped(Player::Judge)
            && !player->isSkipped(Player::Draw)) {
            if (Slash::IsAvailable(player) && room->askForUseCard(player, "@@qingyi", "@qingyi-slash")) {
                player->skip(Player::Judge, true);
                player->skip(Player::Draw, true);
            }
        }
        return false;
    }
};

class Shixin : public TriggerSkill
{
public:
    Shixin() : TriggerSkill("shixin")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.nature == DamageStruct::Fire) {
            room->broadcastSkillInvoke(objectName());

            LogMessage log;
            log.type = "#ShixinProtect";
            log.from = player;
            log.arg = QString::number(damage.damage);
            log.arg2 = "fire_nature";
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());
            return true;
        }
        return false;
    }
};


SPPackage::SPPackage()
: Package("sp")
{

    General *sp_diaochan = new General(this, "sp_diaochan", "qun", 3, false); // SP 002
    sp_diaochan->addSkill("noslijian");
    sp_diaochan->addSkill("biyue");

    General *gongsunzan = new General(this, "gongsunzan", "qun"); // SP 003
    gongsunzan->addSkill(new Yicong);
    gongsunzan->addSkill(new YicongEffect);
    related_skills.insertMulti("yicong", "#yicong-effect");

    General *ol_sp_gongsunzan = new General(this, "ol_sp_gongsunzan", "qun", 4, true);
    ol_sp_gongsunzan->addSkill("olyicong");

    General *yuanshu = new General(this, "yuanshu", "qun"); // SP 004
    yuanshu->addSkill(new Yongsi);
    yuanshu->addSkill(new Weidi);

    General *sp_sunshangxiang = new General(this, "sp_sunshangxiang", "shu", 3, false); // SP 005
    sp_sunshangxiang->addSkill("jieyin");
    sp_sunshangxiang->addSkill("xiaoji");

    General *sp_pangde = new General(this, "sp_pangde", "wei", 4, true); // SP 006
    sp_pangde->addSkill("mashu");
    sp_pangde->addSkill("mengjin");

    General *sp_caiwenji = new General(this, "sp_caiwenji", "wei", 3, false); // SP 009
    sp_caiwenji->addSkill("beige");
    sp_caiwenji->addSkill("duanchang");

    General *sp_machao = new General(this, "sp_machao", "qun", 4, true); // SP 011
    sp_machao->addSkill("mashu");
    sp_machao->addSkill("nostieji");

    General *sp_jiaxu = new General(this, "sp_jiaxu", "wei", 3, true); // SP 012
    sp_jiaxu->addSkill("wansha");
    sp_jiaxu->addSkill("luanwu");
    sp_jiaxu->addSkill("weimu");

    General *sp_zhenji = new General(this, "sp_zhenji", "wei", 3, false); // SP 015
    sp_zhenji->addSkill("qingguo");
    sp_zhenji->addSkill("luoshen");
	
    General *sp_shenlvbu = new General(this, "sp_shenlvbu", "god", 5, true); // SP 022
    sp_shenlvbu->addSkill("kuangbao");
    sp_shenlvbu->addSkill("wumou");
    sp_shenlvbu->addSkill("wuqian");
    sp_shenlvbu->addSkill("shenfen");

    General *xiahoushi = new General(this, "xiahoushi", "shu", 3, false); // SP 023
    xiahoushi->addSkill(new Yanyu);
    xiahoushi->addSkill(new Xiaode);
    xiahoushi->addSkill(new XiaodeEx);
    related_skills.insertMulti("xiaode", "#xiaode");

    General *zhuling = new General(this, "zhuling", "wei");
    zhuling->addSkill(new Zhanyi);
    zhuling->addSkill(new ZhanyiDiscard2);
    zhuling->addSkill(new ZhanyiNoDistanceLimit);
    zhuling->addSkill(new ZhanyiRemove);
    related_skills.insertMulti("zhanyi", "#zhanyi-basic");
    related_skills.insertMulti("zhanyi", "#zhanyi-equip");
    related_skills.insertMulti("zhanyi", "#zhanyi-trick");
    addMetaObject<ZhanyiCard>();
    addMetaObject<ZhanyiViewAsBasicCard>();

    General *maliang = new General(this, "maliang", "shu", 3); // SP 035
    maliang->addSkill(new Xiemu);
    maliang->addSkill(new Naman);

    General *sp_ganfuren = new General(this, "sp_ganfuren", "shu", 3, false); // SP 037
    sp_ganfuren->addSkill("shushen");
    sp_ganfuren->addSkill("shenzhi");

    General *huangjinleishi = new General(this, "huangjinleishi", "qun", 3, false); // SP 038
    huangjinleishi->addSkill(new Fulu);
    huangjinleishi->addSkill(new Zhuji);

    General *sunhao = new General(this, "sunhao$", "wu", 5); // SP 041, SE 
    sunhao->addSkill(new Canshi);
    sunhao->addSkill(new Chouhai);
    sunhao->addSkill(new Guiming);

    General *zhaoxiang = new General(this, "zhaoxiang", "shu", 4, false);
    zhaoxiang->addSkill(new Fanghun);
    zhaoxiang->addSkill(new FanghunDraw("fanghun"));
    zhaoxiang->addSkill(new Fuhan);
    related_skills.insertMulti("fanghun", "#fanghun");

    related_skills.insertMulti("olfanghun", "#olfanghun");
	skills << new OLFanghun << new FanghunDraw("olfanghun") << new OLFuhan
	<< new MobileFanghun << new FanghunDraw("mobilefanghun");
    related_skills.insertMulti("mobilefanghun", "#mobilefanghun");
    addMetaObject<MobileFanghunCard>();

    skills << new TenyearFanghun << new FanghunDraw("tenyearfanghun");
    related_skills.insertMulti("tenyearfanghun", "#tenyearfanghun");
    addMetaObject<TenyearFanghunCard>();

    General *baosanniang = new General(this, "baosanniang", "shu", 3, false);
    baosanniang->addSkill(new Wuniang);
    baosanniang->addSkill(new Xushen);
    baosanniang->addRelateSkill("zhennan");

    addMetaObject<XiemuCard>();
    addMetaObject<FanghunCard>();
    addMetaObject<OLFanghunCard>();

    skills << new MeibuFilter("meibu")
           << new Zhennan;

    General *sunru = new General(this, "sunru", "wu", 3, false);
    sunru->addSkill(new Qingyi);
    sunru->addSkill(new SlashNoDistanceLimitSkill("qingyi"));
    sunru->addSkill(new Shixin);
    related_skills.insertMulti("qingyi", "#qingyi-slash-ndl");

    General *lifeng = new General(this, "lifeng", "shu", 3);
    lifeng->addSkill(new Tunchu);
    lifeng->addSkill(new TunchuEffect);
    lifeng->addSkill(new TunchuLimit);
    lifeng->addSkill(new Shuliang);
    related_skills.insertMulti("tunchu", "#tunchu-effect");
    related_skills.insertMulti("tunchu", "#tunchu-limit");
    addMetaObject<ShuliangCard>();

}
ADD_PACKAGE(SP)

MiscellaneousPackage::MiscellaneousPackage()
: Package("miscellaneous")
{
    General *wz_daqiao = new General(this, "wz_nos_daqiao", "wu", 3, false); // WZ 001
    wz_daqiao->addSkill("nosguose");
    wz_daqiao->addSkill("liuli");

    General *wz_xiaoqiao = new General(this, "wz_xiaoqiao", "wu", 3, false); // WZ 002
    wz_xiaoqiao->addSkill("tianxiang");
    wz_xiaoqiao->addSkill("hongyan");

    General *pr_shencaocao = new General(this, "pr_shencaocao", "god", 3, true); // PR LE 005
    pr_shencaocao->addSkill("guixin");
    pr_shencaocao->addSkill("feiying");

    General *pr_nos_simayi = new General(this, "pr_nos_simayi", "wei", 3, true); // PR WEI 002
    pr_nos_simayi->addSkill("nosfankui");
    pr_nos_simayi->addSkill("nosguicai");

    General *Caesar = new General(this, "caesar", "god", 4); // E.SP 001
    Caesar->addSkill(new Conqueror);

    General *hanba = new General(this, "hanba", "qun", 4, false);
    hanba->addSkill(new Fentian);
    hanba->addSkill(new Zhiri);
    hanba->addSkill(new FentianRange);
    related_skills.insertMulti("fentian", "#fentian");
    hanba->addRelateSkill("xintan");

    skills << new Xintan;

    addMetaObject<XintanCard>();
}
ADD_PACKAGE(Miscellaneous)