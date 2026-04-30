#include "skill.h"
#include "settings.h"
#include "engine.h"
//#include "player.h"
#include "room.h"
//#include "client.h"
//#include "standard.h"
//#include "scenario.h"
#include "clientplayer.h"
#include "clientstruct.h"
//#include "util.h"
#include "exppattern.h"
#include <src/util/ThreadSafeHelper.h>

Skill::Skill(const QString &name, Frequency frequency)
    : frequency(frequency), attached_lord_skill(name.endsWith("&")), change_skill(false),
	hide_skill(false), shiming_skill(false), lord_skill(name.endsWith("$"))
{
    limited_skill = frequency == Limited;
    QString copy = name;
    if (lord_skill||attached_lord_skill)
        copy.chop(1);
    setObjectName(copy);
	if(!name.startsWith("#"))
		initMediaSource();
}

bool Skill::isLordSkill() const
{
    return lord_skill;
}

bool Skill::isAttachedLordSkill() const
{
    return attached_lord_skill;
}

bool Skill::isChangeSkill() const
{
    return change_skill;
}

bool Skill::isLimitedSkill() const
{
    return getFrequency() == Limited || limited_skill;
}

bool Skill::isHideSkill() const
{
    return hide_skill;
}

bool Skill::isShiMingSkill() const
{
    return shiming_skill;
}

bool Skill::shouldBeVisible(const Player *Self) const
{
    return Self != nullptr;
}

QString Skill::getDescription(const Player *target) const
{
	QString des_src;
	if(ServerInfo.DuringGame && isNormalGameMode(ServerInfo.GameMode))
		des_src = Sanguosha->translate(":"+objectName()+"_p");
	if (des_src.isEmpty() || des_src.startsWith(":"))
		des_src = Sanguosha->translate(":"+objectName());

	if (target){
		QString data = target->property(("changeTranslation"+objectName()).toStdString().c_str()).toString();
		if(data.length()==1) des_src = Sanguosha->translate(":"+objectName()+data);
		else if(data.length()>1) des_src = QByteArray::fromBase64(data.toLatin1());
		QHash<QString, QString> swap = target->getSkillDescriptionSwap(objectName());
		foreach (QString key, swap.keys())
			des_src.replace(key, swap[key]);
	}
	/*else
		des_src = Sanguosha->translate(":" + objectName(), true);
	
	if (Self) {
		name = objectName();

		QString arg = "SkillDescriptionRecord_" + name, choice_record1 = "SkillDescriptionChoiceRecord1_" + name,
				choice_record2 = "SkillDescriptionChoiceRecord2_" + name;

		QString record = Self->property(arg.toStdString().c_str()).toString();
		if (!record.isEmpty()) {
			QStringList records = record.split("+");
			QString _record;
			int length = records.length();
			for (int i = 0; i < length; i++) {
				_record.append(Sanguosha->translate(records.at(i)));
				if (i == length - 1) break;
				_record.append(",");
			}
			if (!_record.isEmpty())
				des_src.replace("%arg11", _record);
		}
		
		QString choice1 = Self->property(choice_record1.toStdString().c_str()).toString();
		if (!choice1.isEmpty()) {
			QStringList choices1 = choice1.split("+");
			QString _record;
			int length = choices1.length();
			for (int i = 0; i < length; i++) {
				_record.append(Sanguosha->translate(name + "_" + choices1.at(i)));
				if (i == length - 1) break;
				_record.append(";");
			}
			if (!_record.isEmpty())
				des_src.replace("%arg21", _record);
		}
		
		QString choice2 = Self->property(choice_record2.toStdString().c_str()).toString();
		if (!choice2.isEmpty()) {
			QStringList choices2 = choice1.split("+");
			QString _record;
			int length = choices2.length();
			for (int i = 0; i < length; i++) {
				_record.append(Sanguosha->translate(name + "_" + choices2.at(i)));
				if (i == length - 1) break;
				_record.append(";");
			}
			if (!_record.isEmpty())
				des_src.replace("%arg22", _record);
		}

		int mark1 = Self->getMark("SkillDescriptionArg1_" + name), mark2 = Self->getMark("SkillDescriptionArg2_" + name),
			mark3 = Self->getMark("SkillDescriptionArg3_" + name), mark4 = Self->getMark("SkillDescriptionArg4_" + name),
			mark5 = Self->getMark("SkillDescriptionArg5_" + name), mark6 = Self->getMark("SkillDescriptionArg6_" + name);
		des_src.replace("%arg1", QString::number(mark1));
		des_src.replace("%arg2", QString::number(mark2));
		des_src.replace("%arg3", QString::number(mark3));
		des_src.replace("%arg4", QString::number(mark4));
		des_src.replace("%arg5", QString::number(mark5));
		des_src.replace("%arg6", QString::number(mark6));

		QString suit1 = "SkillDescriptionSuit1_" + name, suit2 = "SkillDescriptionSuit2_" + name,
				suit3 = "SkillDescriptionSuit3_" + name, suit4 = "SkillDescriptionSuit4_" + name;

		QString s1 = Self->property(suit1.toStdString().c_str()).toString();
		QString s2 = Self->property(suit2.toStdString().c_str()).toString();
		QString s3 = Self->property(suit3.toStdString().c_str()).toString();
		QString s4 = Self->property(suit4.toStdString().c_str()).toString();

		if (!s1.isEmpty())
			des_src.replace("%suit1", Sanguosha->translate(s1 + "_char"));
		if (!s2.isEmpty())
			des_src.replace("%suit2", Sanguosha->translate(s2 + "_char"));
		if (!s3.isEmpty())
			des_src.replace("%suit3", Sanguosha->translate(s3 + "_char"));
		if (!s4.isEmpty())
			des_src.replace("%suit4", Sanguosha->translate(s4 + "_char"));
	}*/

	if (des_src.startsWith(":")) return "<font color=\"#bab8ba\">描述缺失~</font>";
	else if (des_src.startsWith("[NoAutoRep]")) return des_src.mid(11);

	QString mark = getLimitMark();
	if (mark!="")
		des_src.prepend("<img src=\"image/mark/"+mark+".png\">");
	if (Config.value("AutoSkillTypeColorReplacement").toBool()) {
		QMap<QString, QColor> colorMap = Sanguosha->getSkillTypeColorMap();
		foreach (QString skill_type, colorMap.keys()) {
			mark = Sanguosha->translate(skill_type);
			if(des_src.contains(mark))
				des_src.replace(mark, QString("<font color=%1><b>%2</b></font>").arg(colorMap[skill_type].name()).arg(mark));
		}
		static QStringList skillNames;
		if(skillNames.isEmpty()){
			foreach (QString sn, Sanguosha->getSkillNames()) {
				mark = Sanguosha->translate(sn);
				if(mark!=sn) skillNames << mark;
			}
		}
		foreach (QString snt, skillNames) {
			if(des_src.contains(snt))
				des_src.replace(QString("“%1”").arg(snt), QString("“<b>%1</b>”").arg(snt));
		}
	}
	if (Config.value("AutoSuitReplacement").toBool()) {
		for (int i = 0; i < 4; i++) {
			QString suit = Card::Suit2String((Card::Suit)i);
			mark = Sanguosha->translate(suit + "_char");
			QString red_char = mark;
			if (i > 1){
				red_char = QString("<font color=red>%1</font>").arg(mark);
				des_src.replace(mark, red_char);
			}
			des_src.replace(Sanguosha->translate(suit), red_char);
		}
	}
	return des_src;
}

QString Skill::getNotice(int index) const
{
    if (index == -1) return Sanguosha->translate("~"+objectName());
    return Sanguosha->translate(QString("~%1%2").arg(objectName()).arg(index));
}

QString Skill::getOracleText(const Player *target) const
{
    Q_UNUSED(target);
    return Sanguosha->translate("^" + objectName());
}

bool Skill::isVisible() const
{
    return !(objectName().startsWith("#")||inherits("SPConvertSkill"));
}

int Skill::getEffectIndex(const ServerPlayer *, const Card *) const
{
    return -1;
}

void Skill::initMediaSource()
{
    sources.clear();

    for (int i = 1;; i++) {
        QString effect_file = QString("audio/skill/%1%2.ogg").arg(objectName()).arg(i);
        if (QFile::exists(effect_file)) sources << effect_file;
        else break;
    }

    if (sources.isEmpty()) {
        QString effect_file = QString("audio/skill/%1.ogg").arg(objectName());
        if (QFile::exists(effect_file)) sources << effect_file;/*
		else if(objectName().contains("_")){
			QString Name2 = objectName().split("_").last();
			for (int i = 1;; i++) {
				QString effect_file = QString("audio/skill/%1%2.ogg").arg(Name2).arg(i);
				if (QFile::exists(effect_file)) sources << effect_file;
				else break;
			}
		}*/
    }
}

void Skill::playAudioEffect(int index, bool superpose) const
{
    if (sources.length()>0) {
        if (index == -1)
            index = qrand() % sources.length();
        else
            index--;

        // check length
        QString filename = sources.first();
		if(index >= 0){
			if(index < sources.length())
				filename = sources.at(index);
			else{
				while (index >= sources.length())
					index -= sources.length();
				filename = sources.at(index);
			}
		}
        Sanguosha->playAudioEffect(filename, superpose);
    }
}

Skill::Frequency Skill::getFrequency(const Player *) const
{
    return frequency;
}

QString Skill::getLimitMark() const
{
    return limit_mark;
}

QString Skill::getWakedSkills() const
{
    return waked_skills;
}

QStringList Skill::getSources() const
{
    return sources;
}

QDialog *Skill::getDialog() const
{
    return nullptr;
}

ViewAsSkill::ViewAsSkill(const QString &name)
    : Skill(name), response_or_use(false)
{
}

bool ViewAsSkill::isAvailable(const Player *invoker,
    CardUseStruct::CardUseReason reason, const QString &pattern) const
{
    if(invoker->getMark("ViewAsSkill_"+objectName()+"Effect")>0
	// For skills like Shuangxiong(ViewAsSkill effect remains even if the player has lost the skill)
	||invoker->hasSkill(objectName())){//||invoker->hasLordSkill(objectName())
		switch (reason) {
		case CardUseStruct::CARD_USE_REASON_PLAY: return isEnabledAtPlay(invoker);
		case CardUseStruct::CARD_USE_REASON_RESPONSE:
		case CardUseStruct::CARD_USE_REASON_RESPONSE_USE:
			return isEnabledAtResponse(invoker, pattern);
				//|| invoker->property("PingjianNowUseSkill").toStringList().contains(pattern);
		default:
			break;
		}
	}
	return false;
}

bool ViewAsSkill::isEnabledAtPlay(const Player *) const
{
    return response_pattern.isEmpty();
}

bool ViewAsSkill::isEnabledAtResponse(const Player *, const QString &pattern) const
{
    if(response_pattern.isEmpty()) return false;
	return response_pattern.contains(pattern);//pattern == response_pattern;
}

bool ViewAsSkill::isEnabledAtNullification(const ServerPlayer *) const
{
    return false;//return response_pattern.contains("nullification");
}

const ViewAsSkill *ViewAsSkill::parseViewAsSkill(const Skill *skill)
{
    if (skill){
		if (skill->inherits("ViewAsSkill"))
			return qobject_cast<const ViewAsSkill *>(skill);
		if (skill->inherits("TriggerSkill"))
			return qobject_cast<const TriggerSkill *>(skill)->getViewAsSkill();
	}
    return nullptr;
}

ZeroCardViewAsSkill::ZeroCardViewAsSkill(const QString &name)
    : ViewAsSkill(name)
{
}

const Card *ZeroCardViewAsSkill::viewAs(const QList<const Card *> &cards) const
{
    if (cards.isEmpty())
        return viewAs();
    return nullptr;
}

bool ZeroCardViewAsSkill::viewFilter(const QList<const Card *> &, const Card *) const
{
    return false;
}

OneCardViewAsSkill::OneCardViewAsSkill(const QString &name)
    : ViewAsSkill(name)
{
}

bool OneCardViewAsSkill::viewFilter(const QList<const Card *> &selected, const Card *to_select) const
{
    return selected.isEmpty() && viewFilter(to_select);
}

bool OneCardViewAsSkill::viewFilter(const Card *to_select) const
{
    if(filter_pattern.isEmpty()||to_select->hasFlag("using")) return false;
	QString pat = filter_pattern;
	if (pat.endsWith("!")) {
		if (Self->isJilei(to_select)) return false;
		pat.chop(1);
	} else if (isResponseOrUse() && pat.contains("hand")) {
		QStringList handlist;
		handlist.append("hand");
		foreach (const QString &pile, Self->getPileNames()) {
			if (pile.startsWith("&") || pile == "wooden_ox")
				handlist.append(pile);
		}
		pat.replace("hand", handlist.join(","));
	}
	return Sanguosha->matchExpPattern(pat,Self,to_select);
}

const Card *OneCardViewAsSkill::viewAs(const QList<const Card *> &cards) const
{
	if (cards.length()>0)
		return viewAs(cards.first());
	return nullptr;
}

FilterSkill::FilterSkill(const QString &name)
    : OneCardViewAsSkill(name)
{
    frequency = Compulsory;
	response_pattern = "..";
}

TriggerSkill::TriggerSkill(const QString &name)
    : Skill(name), view_as_skill(nullptr), global(false), dynamic_priority(0.0)
{
}

const ViewAsSkill *TriggerSkill::getViewAsSkill() const
{
    return view_as_skill;
}

QList<TriggerEvent> TriggerSkill::getTriggerEvents() const
{
    return events;
}

int TriggerSkill::getPriority(TriggerEvent) const
{
    //return (frequency == Wake) ? 3 : 2;
    return 2;
}

bool TriggerSkill::hasCan(ServerPlayer *target, ServerPlayer *owner) const
{
    return target&&target->isAlive()&&owner->isAlive()&&owner->hasSkill(objectName());
}

bool TriggerSkill::triggerable(ServerPlayer *target, Room *room, TriggerEvent event, ServerPlayer *owner, QVariant) const
{
    return target==owner&&triggerable(target,room,event);
}

bool TriggerSkill::triggerable(const ServerPlayer *target, Room *, TriggerEvent) const
{
    return triggerable(target);
}

bool TriggerSkill::triggerable(const ServerPlayer *target) const
{
    return target && (global || (target->isAlive() && target->hasSkill(objectName())));
}

bool TriggerSkill::canWake(TriggerEvent, ServerPlayer *player, QVariant, Room *) const
{
    return player->getMark(objectName())<1;//||getFrequency(player) != Skill::Wake;
}

bool TriggerSkill::trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data, ServerPlayer *) const
{
    return trigger(triggerEvent,room,player,data);
}

bool TriggerSkill::hasEvent(TriggerEvent triggerEvent) const
{
    //if (!triggerEvent) return false;
    return events.contains(triggerEvent);
}

ScenarioRule::ScenarioRule(Scenario *scenario)
    :TriggerSkill(scenario->objectName())
{
    setParent(scenario);
}

int ScenarioRule::getPriority(TriggerEvent) const
{
    return 0;
}

bool ScenarioRule::triggerable(const ServerPlayer *) const
{
    return true;
}

MasochismSkill::MasochismSkill(const QString &name)
    : TriggerSkill(name)
{
    events << Damaged;
}

bool MasochismSkill::trigger(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const
{
    DamageStruct damage = data.value<DamageStruct>();
    onDamaged(player, damage);
    return false;
}

PhaseChangeSkill::PhaseChangeSkill(const QString &name)
    : TriggerSkill(name)
{
    events << EventPhaseStart;
}

bool PhaseChangeSkill::trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
{
    return onPhaseChange(player, room);
}

DrawCardsSkill::DrawCardsSkill(const QString &name, bool is_initial)
    : TriggerSkill(name), is_initial(is_initial)
{
    events << DrawNCards;
}

bool DrawCardsSkill::triggerable(ServerPlayer *target, Room *, TriggerEvent, ServerPlayer *owner, QVariant data) const
{
    DrawStruct draw = data.value<DrawStruct>();
	if(is_initial){
		if(draw.reason!="InitialHandCards") return false;
	}else
		if(draw.reason!="draw_phase") return false;
    return target==owner&&owner->isAlive()&&owner->hasSkill(objectName());
}

bool DrawCardsSkill::trigger(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const
{
    DrawStruct draw = data.value<DrawStruct>();
	if(is_initial){
		if(draw.reason!="InitialHandCards") return false;
	}else
		if(draw.reason!="draw_phase") return false;
    draw.num = getDrawNum(player, draw.num);
	data = QVariant::fromValue(draw);
    return false;
}

GameStartSkill::GameStartSkill(const QString &name)
    : TriggerSkill(name)
{
    events << GameStart;
}

bool GameStartSkill::trigger(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const
{
    onGameStart(player);
    return false;
}

RetrialSkill::RetrialSkill(const QString &name, bool exchange)
    : TriggerSkill(name)
{
    events << AskForRetrial;
    this->exchange = exchange;
}

bool RetrialSkill::trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
{
    JudgeStruct *judge = data.value<JudgeStruct *>();
    const Card *retrial_card = onRetrial(player, judge);
    if (retrial_card)
		room->retrial(retrial_card, player, judge, objectName(), exchange);
    return false;
}

SPConvertSkill::SPConvertSkill(const QString &from, const QString &to)
    : GameStartSkill(QString("cv_%1").arg(from)), from(from), to(to)
{
    to_list = to.split("+");
}

bool SPConvertSkill::triggerable(const ServerPlayer *target) const
{
    if (target == nullptr) return false;
    if (Config.EnableHegemony) return false;
    if (!Config.value("EnableSPConvert", true).toBool()) return false;
    if (!isNormalGameMode(Config.GameMode.mode_id)) return false;
    foreach (QString to_gen, to_list) {
        if(Config.value("Banlist/Roles").toStringList().contains(to_gen)) continue;
		const General *gen = Sanguosha->getGeneral(to_gen);
        if (gen && !Sanguosha->getBanPackages().contains(gen->getPackage())) {
            return GameStartSkill::triggerable(target)
			&& (target->getGeneralName() == from || target->getGeneral2Name() == from);
        }
    }
    return false;
}

void SPConvertSkill::onGameStart(ServerPlayer *player) const
{
    QStringList choicelist;
    foreach (QString to_gen, to_list) {
        if(Config.value("Banlist/Roles").toStringList().contains(to_gen)) continue;
        const General *gen = Sanguosha->getGeneral(to_gen);
        if (gen && !Sanguosha->getBanPackages().contains(gen->getPackage()))
            choicelist << to_gen;
    }
    QString data = choicelist.join("\\,\\");
    if (choicelist.length() >= 2)
        data.replace("\\,\\" + choicelist.last(), "\\or\\" + choicelist.last());
    if (player->askForSkillInvoke(this, data, false)) {
		Room *room = player->getRoom();
        QString to_cv;
        if (player->getAI()) to_cv = room->askForChoice(player, objectName(), choicelist.join("+"));
        else to_cv = room->askForGeneral(player, choicelist);

        bool isSecondaryHero = (player->getGeneralName() != from && player->getGeneral2Name() == from);

        room->changeHero(player, to_cv, true, false, isSecondaryHero);

        const General *general = Sanguosha->getGeneral(to_cv);
        const QString kingdom = general->getKingdom();
        if (!isSecondaryHero && kingdom != "god" && kingdom != player->getKingdom())
            room->setPlayerProperty(player, "kingdom", kingdom);
    }
}

ProhibitSkill::ProhibitSkill(const QString &name)
    : Skill(name, Skill::Compulsory)
{
}

ProhibitPindianSkill::ProhibitPindianSkill(const QString &name)
    : Skill(name, Skill::Compulsory)
{
}

DistanceSkill::DistanceSkill(const QString &name)
    : Skill(name, Skill::Compulsory)
{
}

int DistanceSkill::getCorrect(const Player *, const Player *) const
{
    return 0;
}

int DistanceSkill::getFixed(const Player *, const Player *) const
{
    return 0;
}

MaxCardsSkill::MaxCardsSkill(const QString &name)
    : Skill(name, Skill::Compulsory)
{
}

int MaxCardsSkill::getExtra(const Player *) const
{
    return 0;
}

int MaxCardsSkill::getFixed(const Player *) const
{
    return -1;
}

TargetModSkill::TargetModSkill(const QString &name)
    : Skill(name, Skill::Compulsory)
{
    pattern = "Slash";
}

QString TargetModSkill::getPattern() const
{
    return pattern;
}

int TargetModSkill::getResidueNum(const Player *, const Card *, const Player *) const
{
    return 0;
}

int TargetModSkill::getDistanceLimit(const Player *, const Card *, const Player *) const
{
    return 0;
}

int TargetModSkill::getExtraTargetNum(const Player *, const Card *) const
{
    return 0;
}

SlashNoDistanceLimitSkill::SlashNoDistanceLimitSkill(const QString &skill_name)
    : TargetModSkill(QString("#%1-slash-ndl").arg(skill_name)), name(skill_name)
{
}

InvaliditySkill::InvaliditySkill(const QString &name)
    : Skill(name)
{
}

int SlashNoDistanceLimitSkill::getDistanceLimit(const Player *from, const Card *card, const Player *) const
{
    if (card->getSkillName() == name && from->hasSkill(name))
        return 999;
    return 0;
}

AttackRangeSkill::AttackRangeSkill(const QString &name) : Skill(name, Skill::Compulsory)
{
}

int AttackRangeSkill::getExtra(const Player *, bool) const
{
    return 0;
}

int AttackRangeSkill::getFixed(const Player *, bool) const
{
    return -1;
}

ViewAsEquipSkill::ViewAsEquipSkill(const QString &name) : Skill(name, Skill::Compulsory)
{
}

QString ViewAsEquipSkill::viewAsEquip(const Player *) const
{
    return "";
}

CardLimitSkill::CardLimitSkill(const QString &name) : Skill(name, Skill::Compulsory)
{
}

QString CardLimitSkill::limitList(const Player *) const
{
    return "";
}

QString CardLimitSkill::limitPattern(const Player *) const
{
    return "";
}

QString CardLimitSkill::limitList(const Player *player, const Card *) const
{
    return limitList(player);
}

QString CardLimitSkill::limitPattern(const Player *player, const Card *) const
{
    return limitPattern(player);
}

DetachEffectSkill::DetachEffectSkill(const QString &skillname, const QString &pilename)
    : TriggerSkill(QString("#%1-clear").arg(skillname)), name(skillname), pile_name(pilename)
{
    events << EventLoseSkill;
}

bool DetachEffectSkill::triggerable(const ServerPlayer *target) const
{
    return target != nullptr;
}

bool DetachEffectSkill::trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
{
    if (data.toString() == name) {
        if (pile_name.isEmpty())
            onSkillDetached(room, player);
        else
            player->clearOnePrivatePile(pile_name);
    }
    return false;
}

void DetachEffectSkill::onSkillDetached(Room *, ServerPlayer *) const
{
}

WeaponSkill::WeaponSkill(const QString &name)
    : TriggerSkill(name)
{
	attached_lord_skill = true;
    //global = true;
}

bool WeaponSkill::triggerable(const ServerPlayer *target) const
{
    return target && target->hasWeapon(objectName());
}

ArmorSkill::ArmorSkill(const QString &name)
    : TriggerSkill(name)
{
	attached_lord_skill = true;
    //global = true;
}

bool ArmorSkill::triggerable(const ServerPlayer *target) const
{
    return target && target->hasArmorEffect(objectName());
}

TreasureSkill::TreasureSkill(const QString &name)
    : TriggerSkill(name)
{
	attached_lord_skill = true;
    //global = true;
}

bool TreasureSkill::triggerable(const ServerPlayer *target) const
{
    return target && target->hasTreasure(objectName());
}

MarkAssignSkill::MarkAssignSkill(const QString &mark, int n)
    : GameStartSkill(QString("#%1-%2").arg(mark).arg(n)), mark_name(mark), n(n)
{
}

void MarkAssignSkill::onGameStart(ServerPlayer *player) const
{
    player->getRoom()->setPlayerMark(player, mark_name, player->getMark(mark_name) + n);
}

bool Skill::setProperty(const char* name, const QVariant& value) {
	return ThreadSafeHelper::setProperty(this, name, value);
}