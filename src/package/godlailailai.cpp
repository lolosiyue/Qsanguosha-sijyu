#include "godlailailai.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "wrapped-card.h"


class Xiongshou : public TriggerSkill
{
public:
    Xiongshou() : TriggerSkill("xiongshou")
    {
        events << DamageCaused << TurnOver;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == TurnOver) return true;
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.by_user && damage.card && damage.card->isKindOf("Slash") && damage.to->getHp() < player->getHp()) {
            room->broadcastSkillInvoke(objectName());
            //room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, damage.from->objectName(), damage.to->objectName());
            LogMessage log;
            log.type = "#xiongshou";
            log.from = damage.from;
            log.arg = QString::number(damage.damage);
            log.arg2 = QString::number(++damage.damage);
            log.arg3 = objectName();
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());
            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

class XiongshouBf: public DistanceSkill {
public:
    XiongshouBf(): DistanceSkill("#xiongshoubf") {
    }

    int getCorrect(const Player *from, const Player *) const{
        if (from->hasSkill("xiongshou"))
            return -1;
        return 0;
    }
};

class Wuzang: public PhaseChangeSkill {
public:
    Wuzang(): PhaseChangeSkill("wuzang") {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const{
        if (target->getPhase() == Player::Draw){
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(target, objectName());
            target->drawCards(qMax(target->getHp()/2, 5), objectName());
            return true;
        }
        return false;
    }
};

class WuzangZ: public MaxCardsSkill {
public:
    WuzangZ(): MaxCardsSkill("#wuzang") {
    }

    int getFixed(const Player *target) const{
        if (target->hasSkill("wuzang"))
            return 0;
        return -1;
    }
};

class Xiangde : public TriggerSkill
{
public:
    Xiangde() : TriggerSkill("xiangde")
    {
        events << DamageCaused;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.from->getWeapon() && damage.to->hasSkill(objectName())) {
            room->broadcastSkillInvoke(objectName());
            //room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, damage.to->objectName(), damage.from->objectName());
            LogMessage log;
            log.type = "#xiongshou";
            log.from = damage.to;
            log.arg = QString::number(damage.damage);
            log.arg2 = QString::number(++damage.damage);
            log.arg3 = objectName();
            room->sendLog(log);
            room->notifySkillInvoked(damage.to, objectName());
            data = QVariant::fromValue(damage);
        }
        return false;
    }

    bool triggerable(const ServerPlayer *target) const{
        return target != nullptr;
    }
};

class Yinzei : public TriggerSkill
{
public:
    Yinzei() : TriggerSkill("yinzei")
    {
        events << Damage;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.from->isNude() && damage.to->hasSkill(objectName()) && damage.to->isKongcheng()) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(damage.to, objectName());
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, damage.to->objectName(), damage.from->objectName());
            room->throwCard(damage.from->getCards("he").at(qrand()%damage.from->getCards("he").length()), damage.from, damage.to);
        }
        return false;
    }

    bool triggerable(const ServerPlayer *target) const{
        return target != nullptr;
    }
};

class Zhue : public TriggerSkill
{
public:
    Zhue() : TriggerSkill("zhue")
    {
        events << Damage;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
            if (p != player && damage.from->isAlive()) {
                room->broadcastSkillInvoke(objectName());
                room->sendCompulsoryTriggerLog(p, objectName());
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, p->objectName(), damage.from->objectName());
                QList<ServerPlayer *> players;
                players << p << damage.from;
                room->sortByActionOrder(players);
                room->drawCards(players, 1, objectName());
            }
        }
        return false;
    }

    bool triggerable(const ServerPlayer *target) const{
        return target != nullptr;
    }
};

class Futai : public PhaseChangeSkill
{
public:
    Futai() : PhaseChangeSkill("futai")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const{
        if (target->getPhase() == Player::RoundStart) {
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (p->isWounded())
                    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, target->objectName(), p->objectName());
            }
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (p->isWounded())
                    room->recover(p, RecoverStruct(objectName(),target));
            }
        }
        return false;
    }
};

class FutaiLimit : public CardLimitSkill
{
public:
    FutaiLimit() : CardLimitSkill("#futai-limit")
    {
    }

    QString limitList(const Player *) const
    {
        return "use";
    }

    QString limitPattern(const Player *target) const
    {		
		foreach (const Player *p, target->getAliveSiblings()) {
			if (!p->hasFlag("CurrentPlayer") && p->hasSkill("futai"))
				return "Peach";
		}
		return "";
    }
};

class Yandu : public TriggerSkill
{
public:
    Yandu() : TriggerSkill("yandu") {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const {
        if (player->getPhase() == Player::NotActive && player->getMark("damage_point_round") == 0) {
            foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
                int card_id = room->askForCardChosen(p, player, "he", objectName());
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, p->objectName());
                room->obtainCard(p, Sanguosha->getCard(card_id), reason, room->getCardPlace(card_id) != Player::PlaceHand);
            }
        }
        return false;
    }
};

class Mingwan : public TriggerSkill
{
public:
    Mingwan() : TriggerSkill("mingwan")
    {
        events << Damage << CardUsed << CardResponded;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == Damage) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.to == player || !player->hasFlag("CurrentPlayer")) return false;
            room->sendCompulsoryTriggerLog(player, this);
            //room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), damage.to->objectName());
            room->addPlayerMark(damage.to, "@ming-Clear");
            room->addPlayerMark(damage.from, "@ming-Clear");
        } else {
            bool invoke = false;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                invoke = p->getMark("@ming-Clear") > 0;
                if (invoke) break;
            }
            const Card *card = nullptr;
            if (event == CardUsed)
                card = data.value<CardUseStruct>().card;
            else {
                CardResponseStruct response = data.value<CardResponseStruct>();
                if (response.m_isUse)
                   card = response.m_card;
            }
            if (card && card->getTypeId() != Card::TypeSkill && invoke)
                player->drawCards(1, "mingwan");
        }
        return false;
    }
};

class MingwanZ: public ProhibitSkill {
public:
    MingwanZ(): ProhibitSkill("#mingwan") {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *, const QList<const Player *> &) const{
		if (to!=from&&from->getMark("@ming-Clear")>0&&to->getMark("@ming-Clear")<1&&from->hasSkill("mingwan"))
			return true;
        return false;
    }
};

class Nitai : public TriggerSkill
{
public:
    Nitai() : TriggerSkill("nitai")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!player->hasFlag("CurrentPlayer")) {
            if (damage.nature == DamageStruct::Fire) {
                room->sendCompulsoryTriggerLog(player, objectName());
                room->broadcastSkillInvoke(objectName());
                LogMessage log;
                log.type = "#nitai";
                log.from = damage.to;
                log.arg = QString::number(damage.damage);
                log.arg2 = QString::number(++damage.damage);
                room->sendLog(log);
                data = QVariant::fromValue(damage);
            }
        } else {
            room->sendCompulsoryTriggerLog(player, objectName());
			room->broadcastSkillInvoke(objectName());
            return true;
        }
        return false;
    }
};

class Luanchang : public PhaseChangeSkill
{
public:
    Luanchang() : PhaseChangeSkill("luanchang")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const{
        if (target->getPhase() == Player::RoundStart) {
            SavageAssault *aa = new SavageAssault(Card::NoSuit, 0);
            aa->setSkillName("_" + objectName());
		   	aa->deleteLater();
            bool can_invoke = false;
            if (!target->isCardLimited(aa, Card::MethodUse)) {
                foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
                    if (!target->isProhibited(p, aa)) {
                        can_invoke = true;
                        break;
                    }
                }
            }
            if (!can_invoke) {
                return false;
            }
            room->sendCompulsoryTriggerLog(target, objectName());
            room->broadcastSkillInvoke(objectName());
            room->useCard(CardUseStruct(aa, target, QList<ServerPlayer *>()));
        } else if (target->getPhase() == Player::NotActive) {
            ArcheryAttack *sa = new ArcheryAttack(Card::NoSuit, 0);
            sa->setSkillName("_" + objectName());
		   	sa->deleteLater();
            bool can_invoke = false;
            if (!target->isCardLimited(sa, Card::MethodUse)) {
                foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
                    if (!target->isProhibited(p, sa)) {
                        can_invoke = true;
                        break;
                    }
                }
            }
            if (!can_invoke) {
                return false;
            }
            room->sendCompulsoryTriggerLog(target, objectName());
            room->broadcastSkillInvoke(objectName());
            room->useCard(CardUseStruct(sa, target, QList<ServerPlayer *>()));
        }
        return false;
    }
};

class Tanyu: public TriggerSkill {
public:
    Tanyu(): TriggerSkill("tanyu") {
        events << EventPhaseStart << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        if (triggerEvent == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::Discard) {
            room->sendCompulsoryTriggerLog(player, objectName());
            player->skip(Player::Discard);
        } else if (player->getPhase() == Player::Finish) {
            bool can_invoke = true;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (can_invoke && p->getHandcardNum() > player->getHandcardNum())
                    can_invoke = false;
            }
            if (can_invoke) {
                room->sendCompulsoryTriggerLog(player, objectName());
                room->loseHp(player);
            }
        }
        return false;
    }
};

class Cangmu: public DrawCardsSkill {
public:
    Cangmu(): DrawCardsSkill("cangmu") {
        frequency = Compulsory;
    }

    int getDrawNum(ServerPlayer *player, int) const{
        Room *room = player->getRoom();
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(player, objectName());
        return room->alivePlayerCount();
    }
};

class Jicai : public TriggerSkill
{
public:
    Jicai() : TriggerSkill("jicai")
    {
        events << HpRecover;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        RecoverStruct recover = data.value<RecoverStruct>();
        foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
            room->sendCompulsoryTriggerLog(p, this);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, p->objectName(), recover.who->objectName());
            QList<ServerPlayer *> players;
            players << p << player;
            room->sortByActionOrder(players);
            room->drawCards(players, 1, objectName());
        }
        return false;
    }

    bool triggerable(const ServerPlayer *target) const{
        return target != nullptr;
    }
};



class Yaoshou: public DistanceSkill {
public:
    Yaoshou(): DistanceSkill("yaoshou") {
    }

    int getCorrect(const Player *from, const Player *) const{
        if (from->hasSkill(objectName()))
            return -2;
        return 0;
    }
};

class Fengdong : public InvaliditySkill
{
public:
    Fengdong() : InvaliditySkill("fengdong")
    {
    }

    bool isSkillValid(const Player *player, const Skill *skill) const
    {
        if (skill->getFrequency(player)!=Skill::Compulsory){
			foreach (const Player *p, player->getAliveSiblings()) {
				if (p->hasFlag("CurrentPlayer")&&p->hasSkill(objectName()))
					return false;
			}
		}
        return true;
    }
};

class BossXunyou: public TriggerSkill {
public:
    BossXunyou(): TriggerSkill("boss_xunyou") {
        events << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        if (data.value<PhaseChangeStruct>().from == Player::NotActive) {
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (!p->hasSkill(objectName())) continue;
				room->sendCompulsoryTriggerLog(p, objectName());
				QList<const Card *> all_cards = player->getCards("hej");
				foreach (ServerPlayer *tp, room->getOtherPlayers(p))
					if (tp!=player) all_cards << tp->getCards("hej");
                if (all_cards.isEmpty()) continue;
				const Card *c = all_cards.at(qrand() % all_cards.length());
				ServerPlayer *to = room->getCardOwner(c->getId());
				room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, p->objectName(), to->objectName());
				CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, p->objectName(),to->objectName(),objectName(),"");
                room->obtainCard(p, c, reason, false);
				if (c->getTypeId()==3&&p->hasCard(c)&&c->isAvailable(p))
					room->useCard(CardUseStruct(c, p, QList<ServerPlayer *>()));
            }
        }
        return false;
    }
};

class Sipu : public TriggerSkill {
public:
    Sipu() : TriggerSkill("sipu") {
        events << CardFinished;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->getTypeId()>0&&player->getPhase()==Player::Play){
			room->addPlayerMark(player,"sipu-PlayClear");
		}
        return false;
    }
};

class SipuBf : public CardLimitSkill
{
public:
    SipuBf() : CardLimitSkill("#sipubf")
    {
    }

    QString limitList(const Player *) const
    {
        return "use,response";
    }

    QString limitPattern(const Player *target) const
    {		
		foreach (const Player *p, target->getAliveSiblings()) {
			if (p->getPhase() == Player::Play && p->getMark("sipu-PlayClear")<2 && p->hasSkill("sipu"))
				return ".";
		}
		return "";
    }
};

class Duqu : public FilterSkill
{
public:
    Duqu() : FilterSkill("duqu")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->isKindOf("Peach");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->setSkillName(objectName());/*
        WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
        card->takeOver(slash);*/
        return slash;
    }
};

class DuquBf : public TriggerSkill
{
public:
    DuquBf() : TriggerSkill("#duqubf")
    {
        events << DamageInflicted << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event==EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().from != Player::NotActive)
				return false;
			foreach (ServerPlayer *p, room->findPlayersBySkillName("duqu")) {
				int n = player->getMark("&boss_shendu+#"+p->objectName());
				if (n>0){
					room->sendCompulsoryTriggerLog(player, "boss_shendu");
					room->broadcastSkillInvoke("duqu");
					const Card*dc = nullptr;
					if (player->getCardCount()>=n)
						dc = room->askForDiscard(player, "duqu", n, n, true, true, "duqu0:"+QString::number(n));
					if (!dc) room->loseHp(player,n,true,p,"duqu");
					player->loseMark("&boss_shendu+#"+p->objectName());
				}
			}
			return false;
		}
		DamageStruct damage = data.value<DamageStruct>();
        if (damage.from&&damage.from!=player&&player->hasSkill("duqu")) {
			room->sendCompulsoryTriggerLog(player, "duqu");
			room->broadcastSkillInvoke("duqu");
			damage.from->gainMark("&boss_shendu+#"+player->objectName());
        }
        return false;
    }
};

class Jiushou: public MaxCardsSkill {
public:
    Jiushou(): MaxCardsSkill("jiushou") {
    }

    int getFixed(const Player *target) const{
        if (target->hasSkill(objectName()))
            return 9;
        return -1;
    }
};

class JiushouBf : public TriggerSkill
{
public:
    JiushouBf() : TriggerSkill("#jiushoubf")
    {
        events << EventPhaseStart << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const {
        return target && target->hasSkill("jiushou");
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event==EventPhaseChanging){
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::Draw){
				room->sendCompulsoryTriggerLog(player, "jiushou");
				room->broadcastSkillInvoke("jiushou");
				player->skip(Player::Draw);
			}else if (change.to == Player::NotActive){
				room->sendCompulsoryTriggerLog(player, "jiushou");
				room->broadcastSkillInvoke("jiushou");
				player->drawCards(player->getMaxCards()-player->getHandcardNum(),"jiushou");
			}
			return false;
		}
        if (player->getPhase()==Player::Play) {
			room->sendCompulsoryTriggerLog(player, "jiushou");
			room->broadcastSkillInvoke("jiushou");
			player->drawCards(player->getMaxCards()-player->getHandcardNum(),"jiushou");
        }
        return false;
    }
};

class Echou : public TriggerSkill {
public:
    Echou() : TriggerSkill("echou") {
        events << CardUsed;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Peach")||use.card->isKindOf("Analeptic")){
			foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
				if (p==use.from) continue;
				room->sendCompulsoryTriggerLog(p, this);
				room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, p->objectName(), use.from->objectName());
				use.from->gainMark("&boss_shendu+#"+p->objectName());
			}
		}
        return false;
    }
};

class Bingxian : public TriggerSkill {
public:
    Bingxian() : TriggerSkill("bingxian") {
        events << CardFinished << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const {
        if (event==EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to != Player::NotActive)
				return false;
			foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
				if (p!=player&&player->getMark("bingxian-Clear")<1){
					room->sendCompulsoryTriggerLog(player, objectName());
					room->broadcastSkillInvoke(objectName());
					Slash *slash = new Slash(Card::NoSuit,0);
					slash->setSkillName("_"+objectName());
					if (p->canUse(slash,player))
						room->useCard(CardUseStruct(slash, p, player));
					slash->deleteLater();
				}
			}
			return false;
		}
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Slash"))
			use.from->addMark("bingxian-Clear");
        return false;
    }
};

class Juyuan: public TargetModSkill {
public:
    Juyuan(): TargetModSkill("juyuan") {
    }

    int getExtraTargetNum(const Player *from, const Card *) const{
        if (from->getPhase()==Player::Play&&from->getHp()<from->getMark("&juyuanHp")&&from->hasSkill(objectName()))
            return 1;
        return 0;
    }
};

class JuyuanBf : public TriggerSkill {
public:
    JuyuanBf() : TriggerSkill("#juyuanbf") {
        events << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const {
        if (data.value<PhaseChangeStruct>().to == Player::NotActive)
			room->setPlayerMark(player,"&juyuanHp",player->getHp());
		return false;
    }
};

class BossXushi : public TriggerSkill
{
public:
    BossXushi() : TriggerSkill("boss_xushi")
    {
        events << EventPhaseEnd << TurnedOver;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (event==TurnedOver){
			if (player->faceUp()){
				room->sendCompulsoryTriggerLog(player, objectName());
				room->broadcastSkillInvoke(objectName());
				QList<int> ids;
				ids << 1 << 2;
				foreach (ServerPlayer *p, room->getOtherPlayers(player))
					room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), p->objectName());
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					int n = ids.at(qrand() % ids.length());
					room->damage(DamageStruct(objectName(), player, p, n));
				}
			}
			return false;
		}
        if (player->getPhase()==Player::Play) {
			room->sendCompulsoryTriggerLog(player, objectName());
			room->broadcastSkillInvoke(objectName());
			player->turnOver();
        }
        return false;
    }
};

class BossZhaohuo : public TriggerSkill
{
public:
    BossZhaohuo() : TriggerSkill("boss_zhaohuo")
    {
        events << ConfirmDamage << DamageForseen << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event==EventPhaseChanging){
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.from == Player::NotActive){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					p->addMark("boss_zhaohuo-Clear");
					p->addEquipsNullified("Armor");
				}
			}
			return false;
		}
		
		DamageStruct damage = data.value<DamageStruct>();
        if (event == ConfirmDamage) {
            if (damage.nature != DamageStruct::Fire) {
                room->sendCompulsoryTriggerLog(player, this);
                LogMessage log;
                log.type = "#boss_zhaohuo";
                log.from = damage.from;
                log.arg = "fire_nature";
                room->sendLog(log);
				damage.nature = DamageStruct::Fire;
                data = QVariant::fromValue(damage);
            }
        } else if (event == DamageForseen&&damage.nature==DamageStruct::Fire){
            room->sendCompulsoryTriggerLog(player, this);
            return true;
        }
        return false;
    }
};

class ZhaohuoBf : public TriggerSkill
{
public:
    ZhaohuoBf() : TriggerSkill("#boss_zhaohuobf")
    {
        events << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
		PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if (change.to == Player::NotActive){
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				for (int i = 0; i < p->getMark("boss_zhaohuo-Clear"); i++) {
					p->removeEquipsNullified("Armor");
				}
			}
		}
        return false;
    }
};

class Honglian : public TriggerSkill
{
public:
    Honglian() : TriggerSkill("honglian")
    {
        events << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event==EventPhaseChanging){
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.from == Player::NotActive){
                room->sendCompulsoryTriggerLog(player, objectName());
                room->broadcastSkillInvoke(objectName());
				QList<int> ids;
				ids << 0 << 1 << 2 << 3;
				int n = ids.at(qrand() % ids.length());
				DummyCard*dc = new DummyCard;
				foreach (int id, room->getDrawPile()) {
					if (dc->subcardsLength()<n&&Sanguosha->getCard(id)->isRed())
						dc->addSubcard(id);
				}
				if (dc->subcardsLength()>0)
					player->obtainCard(dc);
				n = 3-dc->subcardsLength();
				QList<ServerPlayer *> tos = room->getOtherPlayers(player);
				QList<ServerPlayer *> tos2;
				for (int i = 0; i < n; i++) {
					if (tos.isEmpty()) continue;
					ServerPlayer *p = tos.at(qrand() % tos.length());
					tos2 << p;
					tos.removeOne(p);
				}
				if (tos2.isEmpty()) return false;
				room->sortByActionOrder(tos2);
				foreach (ServerPlayer *p, tos2)
					room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), p->objectName());
				foreach (ServerPlayer *p, tos2) {
					room->damage(DamageStruct(objectName(), player, p, 1, DamageStruct::Fire));
				}
			}else if (change.to == Player::Discard){
                room->sendCompulsoryTriggerLog(player, objectName());
                room->broadcastSkillInvoke(objectName());
				foreach (const Card *h, player->getHandcards()) {
					if(h->isRed()) room->ignoreCards(player,h);
				}
			}
		}
        return false;
    }
};

class BossYanyu : public TriggerSkill {
public:
    BossYanyu() : TriggerSkill("boss_yanyu") {
        events << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const {
        if (event==EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().from != Player::NotActive)
				return false;
			foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
				if (p==player) continue;
				room->sendCompulsoryTriggerLog(p, objectName());
				room->broadcastSkillInvoke(objectName());
				room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, p->objectName(), player->objectName());
				for (int i = 0; i < 3; i++) {
					if (!player->isAlive()) break;
					JudgeStruct judge;
					judge.who = player;
					judge.reason = objectName();
					judge.good = false;
					judge.pattern = ".|red";
					room->judge(judge);
					if (judge.isBad())
						room->damage(DamageStruct(objectName(), p, player, 1, DamageStruct::Fire));
					else break;
				}
			}
		}
        return false;
    }
};





class Jielve : public TriggerSkill
{
public:
    Jielve() : TriggerSkill("jielve")
    {
        events << Damage;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.to->isAllNude() && damage.to != player) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(player, objectName());
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE,player->objectName(), damage.to->objectName());
            DummyCard *dummy = new DummyCard;
            if (!damage.to->isKongcheng()) {
                int id1 = room->askForCardChosen(player, damage.to, "h", objectName());
                dummy->addSubcard(id1);
            }
            if (!damage.to->getEquips().isEmpty()) {
                int id2 = room->askForCardChosen(player, damage.to, "e", objectName());
                dummy->addSubcard(id2);
            }
            if (!damage.to->getJudgingArea().isEmpty()) {
                int id3 = room->askForCardChosen(player, damage.to, "j", objectName());
                dummy->addSubcard(id3);
            }
            if (dummy->subcardsLength() > 0) {
                room->obtainCard(player, dummy, CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, player->objectName()), false);
                room->loseHp(player);
            }
			dummy->deleteLater();
        }
        return false;
    }
};

class Longying : public TriggerSkill
{
public:
    Longying() : TriggerSkill("longying")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getHp() > 0 && room->getLord() && room->getLord()->isWounded() && player->getPhase() == Player::Play) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(player, objectName());
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), room->getLord()->objectName());
            room->loseHp(player);
            room->recover(room->getLord(), RecoverStruct(objectName(),player));
            room->getLord()->drawCards(1, objectName());
        }
        return false;
    }
};

class Fangong : public TriggerSkill {
public:
    Fangong() : TriggerSkill("fangong") {
        events << CardFinished;
    }

    bool triggerable(const ServerPlayer *target) const {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const {
        CardUseStruct use = data.value<CardUseStruct>();
        foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
            if (use.from != p && use.card->getTypeId()>0 && use.to.contains(p))
                room->askForUseSlashTo(p, player, QString("@fangong-slash:%1").arg(player->objectName()), false);
        }
        return false;
    }
};

class Huying : public TriggerSkill
{
public:
    Huying() : TriggerSkill("huying")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() == Player::Play && room->getLord()) {
            QList<int> ids;
            foreach (const Card *card, player->getHandcards()) {
                if (card->isKindOf("Slash"))
                    ids << card->getId();
            }
            QList<ServerPlayer *> players;
            players << room->getLord();
               if (!room->askForYiji(player, ids, objectName(), false, false, true, 1, players, CardMoveReason(), "@huying-distribute", true)) {
                room->sendCompulsoryTriggerLog(player, objectName());
                room->loseHp(player);
                QList<int> slashes;
                   foreach (int card_id, room->getDrawPile()) {
                    if (Sanguosha->getCard(card_id)->isKindOf("Slash"))
                        slashes << card_id;
                }
                if (!slashes.isEmpty())
                    room->getLord()->obtainCard(Sanguosha->getCard(slashes.at(qrand() % slashes.length())));
            }
        }
        return false;
    }
};

class BossTunjun : public TriggerSkill
{
public:
    BossTunjun() : TriggerSkill("boss_tunjun")
    {
        events << RoundStart;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getMaxHp() != 1) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(player, objectName());
            room->loseMaxHp(player);
            player->drawCards(player->getMaxHp());
        }
        return false;
    }
};

class Jiaoxia: public MaxCardsSkill {
public:
    Jiaoxia(): MaxCardsSkill("jiaoxia") {
    }

    int getExtra(const Player *target) const{
        int m = 0;
		if(target->hasSkill(objectName())){
			foreach (const Card *card, target->getHandcards()) {
				if (card->isBlack()) m++;
			}
		}else{
			foreach (const Player *p, target->getAliveSiblings()) {
				if (p->hasSkill(objectName())&&p->isYourFriend(target)) {
					foreach (const Card *card, target->getHandcards()) {
						if (card->isBlack()) m++;
					}
					break;
				}
			}
		}
        return m;
    }
};

class BossFengying: public ProhibitSkill {
public:
    BossFengying(): ProhibitSkill("boss_fengying")
	{
    }

    bool isProhibited(const Player *from, const Player *to, const Card *, const QList<const Player *> &) const{
        if (!from->isYourFriend(to)){
			bool can = false;
			foreach (const Player *p, to->getAliveSiblings()) {
				if (p->isYourFriend(to)) {
					if (to->getHp()>=p->getHp()) return false;
					can = true;
				}
			}
			if (can){
				if (to->hasSkill(objectName())) return true;
				foreach (const Player *p, to->getAliveSiblings()) {
					if (p->hasSkill(objectName())&&p->isYourFriend(to))
						return true;
				}
			}
		}
        return false;
    }
};

KuangxiCard::KuangxiCard() {
}

bool KuangxiCard::targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const{
    return Self != to_select;
}

void KuangxiCard::onEffect(CardEffectStruct &effect) const{
    effect.from->getRoom()->damage(DamageStruct("kuangxi", effect.from, effect.to));
    effect.from->getRoom()->loseHp(effect.from);
}

class KuangxiViewAsSkill: public ZeroCardViewAsSkill {
public:
    KuangxiViewAsSkill(): ZeroCardViewAsSkill("kuangxi") {
    }

    const Card *viewAs() const{
        return new KuangxiCard;
    }

    bool isEnabledAtPlay(const Player *player) const{
        return player->getHp() > 0 && !player->hasFlag("KuangxiEnterDying");
    }
};

class Kuangxi: public TriggerSkill {
public:
    Kuangxi(): TriggerSkill("kuangxi") {
        events << QuitDying;
        view_as_skill = new KuangxiViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const{
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const{
        DyingStruct dying = data.value<DyingStruct>();
        if (dying.damage && dying.damage->getReason() == "kuangxi" && !dying.damage->chain && !dying.damage->transfer) {
            ServerPlayer *from = dying.damage->from;
            if (from && from->isAlive())
                room->setPlayerFlag(from, "KuangxiEnterDying");
        }
        return false;
    }
};

class Baoying : public TriggerSkill
{
public:
    Baoying() : TriggerSkill("baoying")
    {
        frequency = Limited;
        events << Dying;
        limit_mark = "@baoying";
    }
    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        ServerPlayer *who = data.value<DyingStruct>().who;
		if (who->getHp()<1&&player->isYourFriend(who)
		&& player->getMark("@baoying") > 0 && player->askForSkillInvoke(this, data)) {
			room->removePlayerMark(player, "@baoying");
			room->recover(who, RecoverStruct(objectName(), player, 1 - who->getHp()));
        }
        return false;
    }
};

class Yangwu : public TriggerSkill
{
public:
    Yangwu() : TriggerSkill("yangwu")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() == Player::Start) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(player, objectName());
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), p->objectName());
            }
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                room->damage(DamageStruct(objectName(), player, p));
            }
            room->loseHp(player);
        }
        return false;
    }
};

class Yanglie : public TriggerSkill
{
public:
    Yanglie() : TriggerSkill("yanglie")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() == Player::Start) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(player, objectName());
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), p->objectName());
            }
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (!p->isAllNude()) {
                    int card_id = room->askForCardChosen(player, p, "hej", objectName());
                    CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
                    room->obtainCard(player, Sanguosha->getCard(card_id), reason, false);
                }
            }
            room->loseHp(player);
        }
        return false;
    }
};

class Ruiqi: public DrawCardsSkill {
public:
    Ruiqi(): DrawCardsSkill("ruiqi") {
        frequency = Compulsory;
    }

    int getDrawNum(ServerPlayer *player, int n) const{
        Room *room = player->getRoom();
        foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
            if (TriggerSkill::triggerable(p) && p->isYourFriend(player)) {
                room->broadcastSkillInvoke(objectName());
                room->sendCompulsoryTriggerLog(p, objectName());
                ++n;
            }
        }
        return n;
    }

    bool triggerable(const ServerPlayer *target) const{
        return target != nullptr;
    }
};

class Jingqi: public DistanceSkill {
public:
    Jingqi(): DistanceSkill("jingqi") {
    }

    int getCorrect(const Player *from, const Player *to) const{
        foreach (const Player *p, from->getAliveSiblings()) {
            if (p->hasSkill(objectName())&&p->isYourFriend(from)&&!p->isYourFriend(to))
                return -1;
        }
        return 0;
    }
};

class Mojun : public TriggerSkill
{
public:
    Mojun() : TriggerSkill("mojun")
    {
        events << Damage;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
            if (damage.card && damage.card->isKindOf("Slash") && !damage.to->hasFlag("Global_DebutFlag") && !damage.chain && !damage.transfer && player->isYourFriend(p)) {
                room->broadcastSkillInvoke(objectName());
                room->sendCompulsoryTriggerLog(p, objectName());
                JudgeStruct judge;
                judge.who = player;
                judge.reason = objectName();
                judge.good = true;
                judge.pattern = ".|black";
                room->judge(judge);
                if (judge.isGood()) {
                    QList<ServerPlayer *> players;
                    foreach (ServerPlayer *pp, room->getAlivePlayers()) {
                        if (player->isYourFriend(pp)) {
                            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE,p->objectName(), pp->objectName());
                            players << pp;
                        }
                    }
                    room->sortByActionOrder(players);
                    room->drawCards(players, 1, objectName());
                }
            }
        }
        return false;
    }

    bool triggerable(const ServerPlayer *target) const{
        return target != nullptr;
    }
};

class Moqu: public TriggerSkill {
public:
    Moqu(): TriggerSkill("moqu") {
        events << EventPhaseChanging << Damaged;
    }

    bool triggerable(const ServerPlayer *target) const{
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const{
        if (event == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
                if (change.to == Player::NotActive && p->getHandcardNum() < p->getHp()) {
                    room->broadcastSkillInvoke(objectName());
                    room->sendCompulsoryTriggerLog(p, objectName());
                    p->drawCards(2, objectName());
                }
            }
        } else {
            foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
                if (p->isYourFriend(player))
                    room->askForDiscard(p, objectName(), 1, 1, false, true);
            }
        }
        return false;
    }
};


class GodBladeSkill: public WeaponSkill {
public:
    GodBladeSkill(): WeaponSkill("god_blade") {
        events << TargetSpecified;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("Slash") || !use.card->isRed())
            return false;
		room->sendCompulsoryTriggerLog(player, objectName());
        QVariantList jink_list = player->tag["Jink_" + use.card->toString()].toList();
        int index = 0;
        foreach (ServerPlayer *p, use.to) {
            LogMessage log;
            log.type = "#NoJink";
            log.from = p;
            room->sendLog(log);
            jink_list.replace(index, QVariant(0));
            ++index;
        }
        player->tag["Jink_" + use.card->toString()] = QVariant::fromValue(jink_list);
		return false;
    }
};

GodBlade::GodBlade(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("god_blade");
}

class GodDiagramSkill: public ArmorSkill {
public:
    GodDiagramSkill(): ArmorSkill("god_diagram") {
        events << CardEffected;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        CardEffectStruct effect = data.value<CardEffectStruct>();
        if (effect.card->isKindOf("Slash")) {
            LogMessage log;
            log.type = "#ArmorNullify";
            log.from = player;
            log.arg = objectName();
            log.arg2 = effect.card->objectName();
            player->getRoom()->sendLog(log);

            room->setEmotion(player, "armor/"+objectName());
            //effect.to->setFlags("Global_NonSkillNullify");
            return true;
        }
		return false;
    }
};

GodDiagram::GodDiagram(Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("god_diagram");
}

class GodPaoSkill: public ProhibitSkill {
public:
    GodPaoSkill(): ProhibitSkill("god_pao") {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const{
        return from != to && card->isNDTrick() && to->hasArmorEffect("god_pao");
    }
};

GodPao::GodPao(Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("god_pao");
}

class GodQinSkill: public WeaponSkill {
public:
    GodQinSkill(): WeaponSkill("god_qin") {
        events << ConfirmDamage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.nature != DamageStruct::Fire) {
			room->sendCompulsoryTriggerLog(player, objectName());
            room->setEmotion(player, "weapon/"+objectName());
			damage.nature = DamageStruct::Fire;
            data = QVariant::fromValue(damage);
		}
		return false;
    }
};

GodQin::GodQin(Suit suit, int number)
    : Weapon(suit, number, 4)
{
    setObjectName("god_qin");
}

class GodHalberdsSkill: public TargetModSkill {
public:
    GodHalberdsSkill(): TargetModSkill("#god_halberd") {
    }

    int getExtraTargetNum(const Player *from, const Card *) const{
        if (from->hasWeapon("god_halberd"))
            return 999;
        return 0;
    }
};

GodHalberd::GodHalberd(Suit suit, int number)
    : Weapon(suit, number, 4)
{
    setObjectName("god_halberd");
}

class GodHalberdSkill: public WeaponSkill {
public:
    GodHalberdSkill(): WeaponSkill("god_halberd") {
        events << ConfirmDamage;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        DamageStruct damage = data.value<DamageStruct>();
		if (damage.card && damage.card->isKindOf("Slash")) {
			room->setEmotion(player, "weapon/"+objectName());
			LogMessage log;
			log.type = "#xiongshou";
			log.from = damage.from;
			log.arg = QString::number(damage.damage);
			log.arg2 = QString::number(++damage.damage);
			log.arg3 = objectName();
			room->sendLog(log);
			room->notifySkillInvoked(player, objectName());
			data = QVariant::fromValue(damage);
		}
		return false;
    }
};

class GodHalberdSkillBf: public TriggerSkill {
public:
    GodHalberdSkillBf(): TriggerSkill("#god_halberdbf") {
        events << DamageComplete;
    }

    bool triggerable(const ServerPlayer *target) const{
        return target && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const{
        DamageStruct damage = data.value<DamageStruct>();
		if (damage.card && damage.card->isKindOf("Slash") && damage.from && damage.from->hasWeapon("god_halberd")) {
			room->recover(damage.to, RecoverStruct("god_halberd",damage.from));
		}
		return false;
    }
};

class GodHatSkill: public TreasureSkill {
public:
    GodHatSkill(): TreasureSkill("god_hat") {
        events << DrawNCards;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
		DrawStruct draw = data.value<DrawStruct>();
		if(draw.reason!="draw_phase") return false;
		room->sendCompulsoryTriggerLog(player, objectName());
        room->setEmotion(player, "treasure/"+objectName());
		draw.num += 2;
        data = QVariant::fromValue(draw);
        return false;
    }
};

GodHat::GodHat(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("god_hat");
}

class GodHatBuff: public MaxCardsSkill {
public:
    GodHatBuff(): MaxCardsSkill("#god_hat") {
    }

    int getExtra(const Player *target) const{
        if (target->hasTreasure("god_hat"))
            return -1;
        return 0;
    }
};

class GodSwordSkill: public WeaponSkill {
public:
    GodSwordSkill(): WeaponSkill("god_sword") {
        events << TargetSpecified << CardFinished;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const{
        CardUseStruct use = data.value<CardUseStruct>();
		if (event == TargetSpecified) {
			if (use.card->isKindOf("Slash")) {
				foreach (ServerPlayer *p, use.to) {
					room->setPlayerCardLimitation(p, "use,response", ".|.|.|hand", true);
					p->addMark("god_sword"+use.card->toString());
					p->addQinggangTag(use.card);
				}
				room->sendCompulsoryTriggerLog(use.from, objectName());
				room->setEmotion(use.from, "weapon/"+objectName());
			}
		} else {
			foreach (ServerPlayer *p, use.to) {
				if (p->getMark("god_sword"+use.card->toString())>0){
					p->removeMark("god_sword"+use.card->toString());
					room->removePlayerCardLimitation(p, "use,response", ".|.|.|hand$1");
				}
			}
		}
        return false;
    }
};

GodSword::GodSword(Suit suit, int number)
    : Weapon(suit, number, 2)
{
    setObjectName("god_sword");
}

class GodHorseSkill : public DistanceSkill
{
public:
    GodHorseSkill() : DistanceSkill("god_horse")
	{
    }

    int getCorrect(const Player *from, const Player *to) const {
		foreach (const Player *p, to->getAliveSiblings()) {
			if (p->hasDefensiveHorse(objectName())){
				QString p_role = p->getRole();
				if (p_role=="lord") p_role = "loyalist";
				else if (p_role=="renegade") continue;
				
				QString to_role = to->getRole();
				if (to_role=="lord") to_role = "loyalist";
				if (p_role!=to_role) continue;
				
				QString from_role = from->getRole();
				if (from_role=="lord") from_role = "loyalist";
				if (p_role==from_role) continue;
				static const DefensiveHorse *dh;
				if(dh==nullptr) dh = Sanguosha->findChild<const DefensiveHorse *>(objectName());
				if(dh) return dh->getCorrect();
			}
		}
        return 0;
    }
};

GodHorse::GodHorse(Suit suit, int number)
    : DefensiveHorse(suit, number, +1)
{
    setObjectName("god_horse");
}




class GodDoubleSwordSkill : public WeaponSkill
{
public:
    GodDoubleSwordSkill() : WeaponSkill("god_double_sword")
    {
        events << TargetSpecified;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        foreach (ServerPlayer *to, use.to) {
            if (use.card->isKindOf("ThunderSlash")||use.card->isKindOf("FireSlash")) {
                if (use.from->askForSkillInvoke(this,to)) {
                    to->getRoom()->setEmotion(use.from, "weapon/"+objectName());
                    bool draw_card = true;
                    if (to->canDiscard(to, "he")){
                        QString prompt = "god_double_card0:" + use.from->objectName();
                        if (room->askForCard(to, ".", prompt, data)) draw_card = false;
					}
                    if (draw_card)
                        use.from->drawCards(1, objectName());
                }
            }
        }
        return false;
    }
};

GodDoubleSword::GodDoubleSword(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("god_double_sword");
}

class GodDeerSkill: public TriggerSkill {
public:
    GodDeerSkill(): TriggerSkill("god_deer") {
        events << DamageCaused;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const{
        return target && target->hasOffensiveHorse(objectName());
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const{
        DamageStruct damage = data.value<DamageStruct>();
		if (damage.nature != DamageStruct::Normal) {
			room->setEmotion(player, "horse/"+objectName());
			LogMessage log;
			log.type = "#xiongshou";
			log.from = damage.from;
			log.arg = QString::number(damage.damage);
			log.arg2 = QString::number(++damage.damage);
			log.arg3 = objectName();
			room->sendLog(log);
			room->notifySkillInvoked(player, objectName());
			data = QVariant::fromValue(damage);
		}
		return false;
    }
};

GodDeer::GodDeer(Suit suit, int number)
    : OffensiveHorse(suit, number, -1)
{
    setObjectName("god_deer");
}

class GodBowSkill : public WeaponSkill
{
public:
    GodBowSkill() : WeaponSkill("god_bow")
    {
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (player != move.from || player->getPhase() != Player::Play || !move.from_places.contains(Player::PlaceHand))
            return false;
		int n = 0;
		for (int i = 0; i < move.card_ids.length(); i++) {
			if (move.from_places.at(i)==Player::PlaceHand)
				n++;
		}
		if (n<2) return false;
		ServerPlayer *to = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "god_bow0:"+QString::number(n), true, true);
		if (!to||to->isNude()) return false;
		room->setEmotion(player, "weapon/"+objectName());
		room->notifySkillInvoked(player, objectName());
		DummyCard*dc = new DummyCard;
		for (int i = 0; i < n; i++) {
			if (dc->subcardsLength()<to->getCardCount()){
				int id = room->askForCardChosen(player,to,"he",objectName(),false,Card::MethodDiscard,dc->getSubcards());
				if (dc->getSubcards().contains(id))
					foreach (int cid, to->handCards()) {
						if (!dc->getSubcards().contains(cid)){
							dc->addSubcard(cid);
							break;
						}
					}
				else
					dc->addSubcard(id);
			}
		}
		if (dc->subcardsLength()>0)
			room->throwCard(dc,to,player);
        dc->deleteLater();
        return false;
    }
};

GodBow::GodBow(Suit suit, int number)
    : Weapon(suit, number, 9)
{
    setObjectName("god_bow");
}

class GodAxeViewAsSkill : public ViewAsSkill
{
public:
    GodAxeViewAsSkill() : ViewAsSkill("god_axe")
    {
        response_pattern = "@god_axe";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (to_select->isEquipped() && to_select->objectName() == objectName()) return false;
        return selected.length() < 2 && !Self->isJilei(to_select);
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != 2)
            return nullptr;
        DummyCard *card = new DummyCard;
        card->setSkillName(objectName());
        card->addSubcards(cards);
        return card;
    }
};

class GodAxeSkill : public WeaponSkill
{
public:
    GodAxeSkill() : WeaponSkill("god_axe")
    {
        events << TargetSpecified;
        view_as_skill = new GodAxeViewAsSkill;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->getTypeId()>0&&use.to.length()==1&&player->getPhase()==Player::Play) {
			const Card *card = nullptr;
			if (player->getCardCount() > 1)
				card = room->askForCard(player, "@god_axe", "god_axe0:" + use.to.at(0)->objectName(), data, objectName());
			if (card) {
				room->setEmotion(player, "weapon/"+objectName());
				room->notifySkillInvoked(player, objectName());
				room->setPlayerCardLimitation(use.to.at(0), "use,response", ".", true);
				use.to.at(0)->addMark("god_axeArmorNullified-Clear");
				use.to.at(0)->addEquipsNullified("Armor");
				player->setFlags("god_axeArmorNullified");
			}
        }
        return false;
    }
};

class GodAxeSkillBf : public TriggerSkill
{
public:
    GodAxeSkillBf() : TriggerSkill("#god_axebf")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const{
        return target && target->hasFlag("god_axeArmorNullified");
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (event == EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to != Player::NotActive)
				return false;
			foreach (ServerPlayer *to, room->getAlivePlayers()) {
				for (int i = 0; i < to->getMark("god_axeArmorNullified-Clear"); i++) {
					room->removePlayerCardLimitation(to, "use,response", ".$1");
					to->removeEquipsNullified("Armor");
				}
			}
		}
        return false;
    }
};

GodAxe::GodAxe(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("god_axe");
}

class GodEdictSkill : public TreasureSkill
{
public:
    GodEdictSkill() : TreasureSkill("god_edict")
    {
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if (room->getTag("FirstRound").toBool())
			return false;
        ServerPlayer *cp = room->getCurrent();
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!move.to || !cp || player == move.to || move.to_place != Player::PlaceHand || move.to == cp || move.to->getMark("god_edict-Clear")>0)
            return false;
		move.to->addMark("god_edict-Clear");
		ServerPlayer *to = (ServerPlayer*)move.to;
		if (player->askForSkillInvoke(this,to)){
			room->setEmotion(player, "treasure/"+objectName());
			QVariant toData = QVariant::fromValue(to);
			const Card *card = room->askForCard(player, "^GodEdict", "god_edict0:"+to->objectName(), toData, Card::MethodNone);
			if (card) room->giveCard(player,to,card,objectName());
			else if (to->getCardCount()>0){
				card = room->askForExchange(to, objectName(), 1, 1, true, "god_edict1:"+player->objectName());
				room->giveCard(to, player, card, objectName());
			}
		}
        return false;
    }
};

GodEdict::GodEdict(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("god_edict");
}

class GodHeaddressSkill : public TreasureSkill
{
public:
    GodHeaddressSkill() : TreasureSkill("god_headdress")
    {
        events << EventPhaseEnd;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if (player->getPhase()==Player::Play&&player->askForSkillInvoke(this)) {
			room->setEmotion(player, "treasure/"+objectName());
			const Card *card = room->askForCard(player, "^GodHeaddress", "god_headdress0:", data, Card::MethodNone);
			if (card) player->addToPile(objectName(),card);
			else player->drawCards(1,objectName());
        }
        return false;
    }
};

class GodHeaddressBf : public TriggerSkill
{
public:
    GodHeaddressBf() : TriggerSkill("#god_headdressbf")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const{
        return target && !target->getPile("god_headdress").isEmpty();
    }

    bool trigger(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to != Player::NotActive)
				return false;
			DummyCard*dc = new DummyCard;
			dc->addSubcards(player->getPile("god_headdress"));
			player->obtainCard(dc,false);
			dc->deleteLater();
		}
        return false;
    }
};

GodHeaddress::GodHeaddress(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("god_headdress");
}

GodShip::GodShip(Suit suit, int number)
    : OffensiveHorse(suit, number, -2)
{
    setObjectName("god_ship");
}

void GodShip::onUninstall(ServerPlayer *player) const
{
	OffensiveHorse::onUninstall(player);
	if(!player->isAlive()) return;
	Room*room = player->getRoom();
	QList<ServerPlayer *> tos;
	foreach (ServerPlayer *to, room->getAlivePlayers()) {
		foreach (const Card*c, to->getCards("ej")) {
			if(c->getNumber()>1&&c->getNumber()<11&&!player->canDiscard(to,c->getId()))
				continue;
			tos << to;
			break;
		}
	}
	ServerPlayer *p = room->askForPlayerChosen(player,tos,objectName(),"god_ship0:",true,true);
	if(p){
		QList<int> ids;
		foreach (const Card*c, p->getCards("ej")) {
			if(c->getNumber()>1&&c->getNumber()<11&&!player->canDiscard(p,c->getId()))
				ids << c->getId();
			else if(c->getId()==getId())
				ids << c->getId();
		}
		int id = room->askForCardChosen(player,p,"ej",objectName(),false,Card::MethodNone,ids);
		if(id>-1){
			const Card*c = Sanguosha->getCard(id);
			if(c->getNumber()>1&&c->getNumber()<11)
				room->throwCard(c,objectName(),p,player);
			else
				player->obtainCard(c);
		}
	}
}


GodSlash::GodSlash(Suit suit, int number)
    : NatureSlash(suit, number, DamageStruct::God)
{
    setObjectName("_god_slash");
    damage_card = true;
    single_target = true;
}

GodNihilo::GodNihilo(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("god_nihilo");
}

bool GodNihilo::isAvailable(const Player *player) const
{
	QList<const Player *> targets = player->getAliveSiblings();
	targets << player;
	foreach (const Player *p, targets) {
		if(targetFilter(QList<const Player *>(), p, player))
			return SingleTargetTrick::isAvailable(player);
	}
	return false;
}

bool GodNihilo::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
	if (to_select->getKingdom() == "god" || to_select->getHandcardNum() <= qMin(to_select->getMaxHp(), 5)) {
		if (Self->isProhibited(to_select, this)) return false;
		if (targets.isEmpty()) return to_select == Self;
		return targets.length() <= Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this);
	}
	return false;
}

bool GodNihilo::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const{
    return Self->getKingdom() == "god" || Self->getHandcardNum() <= qMin(Self->getMaxHp(), 5) || targets.length() > 0;
}

void GodNihilo::onUse(Room *room, CardUseStruct &card_use) const{
    CardUseStruct use = card_use;
    if (use.to.isEmpty()) use.to << use.from;
    SingleTargetTrick::onUse(room, use);
}

void GodNihilo::onEffect(CardEffectStruct &effect) const{
    effect.to->drawCards(effect.to->getKingdom()=="god"?qMin(effect.to->getMaxHp(),5):qMin(effect.to->getMaxHp(),5)-effect.to->getHandcardNum(),objectName());
}

GodFlower::GodFlower(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("god_flower");
}

bool GodFlower::isAvailable(const Player *player) const
{
	foreach (const Player *p, player->getAliveSiblings()) {
		if(targetFilter(QList<const Player *>(), p, player))
			return SingleTargetTrick::isAvailable(player);
	}
	return false;
}

bool GodFlower::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
    return to_select != Self && targets.length() <= Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this)
	&& !Self->isProhibited(to_select, this);
}

void GodFlower::onEffect(CardEffectStruct &effect) const{
	QList<ServerPlayer *> targets;
    Room *room = effect.to->getRoom();
    foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if (effect.to->canSlash(p, false))
			targets << p;
    }
	if (!room->askForUseSlashTo(effect.to, targets, "@god_flower:"+effect.from->objectName(),true,false,false,effect.from,this)
		&& effect.to->getCardCount()>0 && effect.from->isAlive()) {
		const Card *card = room->askForExchange(effect.to, objectName(), 2, 2, true, "@god_flower0:"+effect.from->objectName());
		if(card) room->giveCard(effect.to, effect.from, card, objectName());
	}
}

GodSpeel::GodSpeel(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("god_speel");
}

bool GodSpeel::isAvailable(const Player *player) const
{
	foreach (const Player *p, player->getAliveSiblings()) {
		if(targetFilter(QList<const Player *>(), p, player))
			return SingleTargetTrick::isAvailable(player);
	}
	return false;
}

bool GodSpeel::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
	return to_select != Self && targets.length() <= Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this)
	&& !Self->isProhibited(to_select, this);
}

void GodSpeel::onEffect(CardEffectStruct &effect) const{
	QStringList sks,skills = effect.to->tag["god_speelSkills"].toStringList();
	Room *room = effect.to->getRoom();
	foreach(const Skill *skill, effect.to->getVisibleSkillList())
		if (!skill->isAttachedLordSkill()) sks << skill->objectName();
	if (sks.isEmpty()) return;
	QString sk = sks.at(qrand() % sks.length());
	if (!skills.contains(sk)){
		skills << sk;
		effect.to->tag["god_speelSkills"] = skills;
		room->addPlayerMark(effect.to,"Qingcheng"+sk);
		room->setPlayerMark(effect.to,"&god_speel+:+"+sk,1);
	}
}

GodlailailaiPackage::GodlailailaiPackage()
    : Package("Godlailailai")
{
    General *zhuyin = new General(this, "zhuyin", "qun", 4, true, true);
    zhuyin->addSkill(new Xiongshou);
    zhuyin->addSkill(new XiongshouBf);
    related_skills.insertMulti("xiongshou", "#xiongshoubf");

    General *hundun = new General(this, "hundun", "qun", 25, false, true);
    hundun->addSkill("xiongshou");
    hundun->addSkill(new Wuzang);
    hundun->addSkill(new WuzangZ);
    related_skills.insertMulti("wuzang", "#wuzang");
    hundun->addSkill(new Xiangde);
    hundun->addRelateSkill("yinzei");

    General *qiongqi = new General(this, "qiongqi", "qun", 25, true, true);
	qiongqi->setStartHp(20);
    qiongqi->addSkill("xiongshou");
    qiongqi->addSkill(new Zhue);
    qiongqi->addSkill(new Futai);
    qiongqi->addSkill(new FutaiLimit);
    related_skills.insertMulti("futai", "#futai-limit");
    qiongqi->addRelateSkill("yandu");

    General *taowu = new General(this, "taowu", "qun", 20, true, true);
    taowu->addSkill("xiongshou");
    taowu->addSkill(new Mingwan);
    taowu->addSkill(new MingwanZ);
    related_skills.insertMulti("mingwan", "#mingwan");
    taowu->addSkill(new Nitai);
    taowu->addRelateSkill("luanchang");

    General *taotie = new General(this, "taotie", "qun", 25, false, true);
    taotie->addSkill("xiongshou");
    taotie->addSkill(new Tanyu);
    taotie->addSkill(new Cangmu);
    taotie->addRelateSkill("jicai");

    General *yingzhao = new General(this, "yingzhao", "qun", 30, true, true);
	yingzhao->setStartHp(25);
    yingzhao->addSkill(new Yaoshou);
    yingzhao->addSkill(new Fengdong);
    yingzhao->addSkill(new BossXunyou);
    yingzhao->addRelateSkill("sipu");
    related_skills.insertMulti("sipu", "#sipubf");

    General *xiangliu = new General(this, "xiangliu", "qun", 25, false, true);
    xiangliu->addSkill("yaoshou");
    xiangliu->addSkill(new Duqu);
    xiangliu->addSkill(new DuquBf);
    xiangliu->addSkill(new Jiushou);
    xiangliu->addSkill(new JiushouBf);
    xiangliu->addRelateSkill("echou");
    related_skills.insertMulti("duqu", "#duqubf");
    related_skills.insertMulti("jiushou", "#jiushoubf");

    General *zhuyan = new General(this, "zhuyan", "qun", 30, true, true);
	zhuyan->setStartHp(25);
    zhuyan->addSkill("yaoshou");
    zhuyan->addSkill(new Bingxian);
    zhuyan->addSkill(new Juyuan);
    zhuyan->addSkill(new JuyuanBf);
    zhuyan->addRelateSkill("boss_xushi");
    related_skills.insertMulti("juyuan", "#juyuanbf");

    General *bifang = new General(this, "bifang", "qun", 25, false, true);
    bifang->addSkill("yaoshou");
    bifang->addSkill(new BossZhaohuo);
    bifang->addSkill(new ZhaohuoBf);
    bifang->addSkill(new Honglian);
    bifang->addRelateSkill("boss_yanyu");
    related_skills.insertMulti("boss_zhaohuo", "#boss_zhaohuobf");

    General *zhangji = new General(this, "godlai_zhangji", "qun", 4, true, true);
    zhangji->addSkill(new Mojun);
    zhangji->addSkill(new Jielve);

    General *longxiang = new General(this, "godlai_longxiang", "qun", 4, true, true);
    longxiang->addSkill(new Longying);

    General *fanchou = new General(this, "godlai_fanchou", "qun", 4, true, true);
    fanchou->addSkill("mojun");
    fanchou->addSkill(new Fangong);

    General *huben = new General(this, "godlai_huben", "qun", 5, true, true);
    huben->addSkill(new Huying);

    General *niufudongxie = new General(this, "godlai_niufudongxie", "qun", 4, true, true);
	niufudongxie->setGender(General::Sexless);
    niufudongxie->addSkill("mojun");
    niufudongxie->addSkill(new BossTunjun);
    niufudongxie->addSkill(new Jiaoxia);
   
    General *fengyao = new General(this, "godlai_fengyao", "qun", 3, false, true);
    fengyao->addSkill(new BossFengying);

    General *dongyue = new General(this, "godlai_dongyue", "qun", 4, true, true);
    dongyue->addSkill("mojun");
    dongyue->addSkill(new Kuangxi);

    General *baolve = new General(this, "godlai_baolve", "qun", 3, true, true);
    baolve->addSkill(new Baoying);

    General *lijue = new General(this, "godlai_lijue", "qun", 5, true, true);
    lijue->addSkill("mojun");
    lijue->addSkill(new Yangwu);

    General *guosi = new General(this, "godlai_guosi", "qun", 4, true, true);
    guosi->addSkill("mojun");
    guosi->addSkill(new Yanglie);

    General *feixiong_left = new General(this, "godlai_feixiong_left", "qun", 4, true, true);
    feixiong_left->addSkill(new Jingqi);

    General *feixiong_right = new General(this, "godlai_feixiong_right", "qun", 4, true, true);
    feixiong_right->addSkill(new Ruiqi);

    addMetaObject<KuangxiCard>();
    skills << new Yinzei << new Yandu << new Luanchang << new Jicai
	<< new Moqu << new Sipu << new SipuBf << new Echou << new BossXushi << new BossYanyu;

    Card *slash = new GodSlash(Card::NoSuit, 0);
    slash->addCharTag("GodStructEffect");
    QList<Card *> cards;
    cards << new GodBlade(Card::Spade, 5)
	      << new GodPao(Card::Spade, 9)
	      << new GodQin(Card::Diamond, 1)
	      << new GodDiagram(Card::Spade, 2)
	      << new GodDiagram(Card::Club, 2)
	      << new GodHorse(Card::Spade, 5)
	      << new GodHalberd(Card::Diamond, 12)
	      << new GodSword(Card::Spade, 6)
	      << new GodHat(Card::Club, 1)
		  << new GodDoubleSword(Card::Spade, 2)
		  << new GodDeer(Card::Heart, 13)
		  << new GodBow(Card::Heart, 5)
		  << new GodAxe(Card::Diamond, 5)
		  << new GodEdict(Card::Spade, 13)
		  << new GodHeaddress(Card::Club, 12)
		  << new GodShip(Card::Heart, 10)
          << slash
          << new GodNihilo(Card::Heart, 7)
          << new GodNihilo(Card::Heart, 8)
          << new GodNihilo(Card::Heart, 9)
          << new GodNihilo(Card::Heart, 11)
          << new GodFlower(Card::Club, 12)
          << new GodFlower(Card::Club, 13)
          << new GodSpeel(Card::Diamond, 7)
          << new GodSpeel(Card::Club, 5);

    foreach (Card *card, cards)
        card->setParent(this);

    skills << new GodDiagramSkill << new GodBladeSkill << new GodHalberdSkill << new GodHalberdsSkill
		<< new GodHorseSkill << new GodQinSkill << new GodHatSkill << new GodHatBuff << new GodSwordSkill
		<< new GodPaoSkill << new GodDoubleSwordSkill << new GodDeerSkill << new GodBowSkill << new GodAxeSkill
		<< new GodAxeSkillBf << new GodEdictSkill << new GodHeaddressSkill << new GodHeaddressBf << new GodHalberdSkillBf;
    related_skills.insertMulti("god_hat", "#god_hat");
    related_skills.insertMulti("god_axe", "#god_axebf");
    related_skills.insertMulti("god_headdress", "#god_headdressbf");
    related_skills.insertMulti("god_halberd", "#god_halberdbf");
}

ADD_PACKAGE(Godlailailai);