#include "lua-wrapper.h"
#include "util.h"
#include "wind.h"
#include "mobile.h"
#include "ol.h"

LuaTriggerSkill::LuaTriggerSkill(const QString &name, Frequency frequency, const QString &limit_mark,
	bool change_skill, bool limited_skill, bool hide_skill, bool shiming_skill, const QString &waked_skills)
    : TriggerSkill(name), on_trigger(0), can_trigger(0), dynamic_frequency(0), can_wake(0)
{
    this->frequency = frequency;
    this->limit_mark = limit_mark;
    this->change_skill = change_skill;
    this->limited_skill = limited_skill;
    this->hide_skill = hide_skill;
    this->shiming_skill = shiming_skill;
    this->waked_skills = waked_skills;
    this->priority = 2;//(frequency == Skill::Wake) ? 3 : 2;
}

int LuaTriggerSkill::getPriority(TriggerEvent triggerEvent) const
{
    if (priority_table.keys().contains(triggerEvent))
        return priority_table[triggerEvent];
    return priority;//TriggerSkill::getPriority(triggerEvent);
}

QDialog *LuaTriggerSkill::getDialog() const
{
    if (guhuo_type != "") {
        return GuhuoDialog::getInstance(objectName(), guhuo_type.contains("l"), guhuo_type.contains("r"),
            !guhuo_type.startsWith("!"), guhuo_type.contains("s"), guhuo_type.contains("d"), guhuo_type.contains("u"));
    } else if (juguan_type != "") {
        return JuguanDialog::getInstance(objectName(), juguan_type);
    } else if (tiansuan_type != "") {
        return TiansuanDialog::getInstance(objectName(), tiansuan_type);
    }
    return nullptr;
}

LuaProhibitSkill::LuaProhibitSkill(const QString &name, Frequency frequency)
    : ProhibitSkill(name), is_prohibited(0)
{
    this->frequency = frequency;
}

LuaProhibitPindianSkill::LuaProhibitPindianSkill(const QString &name, Frequency frequency)
    : ProhibitPindianSkill(name), is_pindianprohibited(0)
{
    this->frequency = frequency;
}

LuaViewAsSkill::LuaViewAsSkill(const QString &name, const QString &response_pattern, bool response_or_use,
	const QString &expand_pile, Frequency frequency, const QString &limit_mark)
    : ViewAsSkill(name), view_filter(0), view_as(0), should_be_visible(0),
    enabled_at_play(0), enabled_at_response(0), enabled_at_nullification(0)
{
    this->response_pattern = response_pattern;
    this->response_or_use = response_or_use;
    this->expand_pile = expand_pile;
    this->frequency = frequency;
    this->limit_mark = QString(limit_mark);
}

QDialog *LuaViewAsSkill::getDialog() const
{
    if (guhuo_type != "") {
        return GuhuoDialog::getInstance(objectName(), guhuo_type.contains("l"), guhuo_type.contains("r"),
			!guhuo_type.startsWith("!"), guhuo_type.contains("s"), guhuo_type.contains("d"), guhuo_type.contains("u"));
    } else if (juguan_type != "") {
        return JuguanDialog::getInstance(objectName(), juguan_type);
    } else if (tiansuan_type != "") {
        return TiansuanDialog::getInstance(objectName(), tiansuan_type);
    }
    return nullptr;
}

LuaFilterSkill::LuaFilterSkill(const QString &name, Frequency frequency)
    : FilterSkill(name), view_filter(0), view_as(0)
{
    this->frequency = frequency;
}

LuaDistanceSkill::LuaDistanceSkill(const QString &name, Frequency frequency)
    : DistanceSkill(name), correct_func(0), fixed_func(0)
{
    this->frequency = frequency;
}

LuaMaxCardsSkill::LuaMaxCardsSkill(const QString &name, Frequency frequency)
    : MaxCardsSkill(name), extra_func(0), fixed_func(0)
{
    this->frequency = frequency;
}

LuaTargetModSkill::LuaTargetModSkill(const QString &name, const QString &pattern, Frequency frequency)
    : TargetModSkill(name), residue_func(0), distance_limit_func(0), extra_target_func(0)
{
    this->pattern = pattern;
    this->frequency = frequency;
}

LuaInvaliditySkill::LuaInvaliditySkill(const QString &name, Frequency frequency)
    : InvaliditySkill(name), skill_valid(0)
{
    this->frequency = frequency;
}

LuaAttackRangeSkill::LuaAttackRangeSkill(const QString &name, Frequency frequency)
    : AttackRangeSkill(name), extra_func(0), fixed_func(0)
{
    this->frequency = frequency;
}

LuaViewAsEquipSkill::LuaViewAsEquipSkill(const QString &name, Frequency frequency)
    : ViewAsEquipSkill(name), view_as_equip(0)
{
    this->frequency = frequency;
}

LuaCardLimitSkill::LuaCardLimitSkill(const QString &name, Frequency frequency)
    : CardLimitSkill(name), limit_list(0), limit_pattern(0)
{
    this->frequency = frequency;
}

static QHash<QString, const LuaSkillCard *> LuaSkillCards;

LuaSkillCard::LuaSkillCard(const QString &name, const QString &skillName)
    : SkillCard(), filter(0), feasible(0), about_to_use(0), on_use(0), on_effect(0), on_validate(0), on_validate_in_response(0)
{
	if(name.isEmpty()) return;
	setObjectName(name);
	LuaSkillCards.insert(name, this);
	if (skillName.isEmpty())
		m_skillName = name.toLower().remove("card");
	else
		m_skillName = skillName;
}

LuaSkillCard *LuaSkillCard::clone() const
{
    LuaSkillCard *new_card = new LuaSkillCard("", "");

    new_card->setObjectName(objectName());
    new_card->setSkillName(m_skillName);

    new_card->target_fixed = target_fixed;
    new_card->will_throw = will_throw;
    new_card->can_recast = can_recast;
    new_card->mute = mute;
    new_card->handling_method = handling_method;

    new_card->filter = filter;
    new_card->feasible = feasible;
    new_card->about_to_use = about_to_use;
    new_card->on_use = on_use;
    new_card->on_effect = on_effect;
    new_card->on_validate = on_validate;
    new_card->on_validate_in_response = on_validate_in_response;

    return new_card;
}

LuaSkillCard *LuaSkillCard::Parse(const QString &str)
{
    static QRegExp rx("#(\\w+):(.*):(.*)");
    static QRegExp e_rx("#(\\w*)\\[(\\w+):(.+)\\]:(.*):(.*)");

    QString name, suit, number, subcard_str, user_string;

    if (rx.exactMatch(str)) {
        QStringList texts = rx.capturedTexts();
        name = texts.at(1);
        subcard_str = texts.at(2);
        user_string = texts.at(3);
    } else if (e_rx.exactMatch(str)) {
        QStringList texts = e_rx.capturedTexts();
        name = texts.at(1);
        suit = texts.at(2);
        number = texts.at(3);
        subcard_str = texts.at(4);
        user_string = texts.at(5);
    } else
        return nullptr;

    const LuaSkillCard *c = LuaSkillCards.value(name, nullptr);
    if (c == nullptr) return nullptr;

    LuaSkillCard *new_card = c->clone();

    if (subcard_str != ".")
        new_card->addSubcards(ListS2I(subcard_str.split("+")));

    if (suit.length()>0){
		static QMap<QString, Card::Suit> suit_map;
		if (suit_map.isEmpty()) {
			suit_map.insert("spade", Card::Spade);
			suit_map.insert("club", Card::Club);
			suit_map.insert("heart", Card::Heart);
			suit_map.insert("diamond", Card::Diamond);
			suit_map.insert("no_suit_red", Card::NoSuitRed);
			suit_map.insert("no_suit_black", Card::NoSuitBlack);
			//suit_map.insert("no_suit", Card::NoSuit);
		}
        new_card->setSuit(suit_map.value(suit, Card::NoSuit));
	}
    if (number.length()>0) {
        if (number == "A") new_card->setNumber(1);
        else if (number == "J") new_card->setNumber(11);
        else if (number == "Q") new_card->setNumber(12);
        else if (number == "K") new_card->setNumber(13);
        else new_card->setNumber(number.toInt());
    }
    new_card->setUserString(user_string);

    return new_card;
}

QString LuaSkillCard::toString(bool hidden) const
{
    //Q_UNUSED(hidden);
	if(hidden) return QString("#%1[no_suit:0]:.:").arg(objectName());
    return QString("#%1[%2:%3]:%4:%5").arg(objectName())
        .arg(getSuitString()).arg(getNumberString())
        .arg(subcardString()).arg(user_string);
}

LuaBasicCard::LuaBasicCard(Card::Suit suit, int number, const QString &obj_name, const QString &class_name, const QString &subtype)
    : BasicCard(suit, number), filter(0), feasible(0), available(0), about_to_use(0), on_use(0), on_effect(0), on_validate(0), on_validate_in_response(0)
{
    setObjectName(obj_name);
    this->class_name = class_name;
    this->class_names = class_name.split("|");
    this->subtype = subtype;
}

LuaBasicCard *LuaBasicCard::clone(Card::Suit suit, int number) const
{
    if (suit == Card::SuitToBeDecided) suit = getSuit();
    if (number == -1) number = getNumber();
    LuaBasicCard *new_card = new LuaBasicCard(suit, number, objectName(), class_name, subtype);

    new_card->target_fixed = target_fixed;
    new_card->can_recast = can_recast;
    new_card->damage_card = damage_card;
    new_card->is_gift = is_gift;
    new_card->single_target = single_target;

    new_card->filter = filter;
    new_card->feasible = feasible;
    new_card->available = available;
    new_card->about_to_use = about_to_use;
    new_card->on_use = on_use;
    new_card->on_effect = on_effect;
    new_card->on_validate = on_validate;
    new_card->on_validate_in_response = on_validate_in_response;

    return new_card;
}

LuaTrickCard::LuaTrickCard(Card::Suit suit, int number, const QString &obj_name, const QString &class_name, const QString &subtype)
    : TrickCard(suit, number), filter(0), feasible(0), available(0), is_cancelable(0),
    about_to_use(0), on_use(0), on_effect(0), on_nullified(0), on_validate(0), on_validate_in_response(0)
{
    setObjectName(obj_name);
    this->class_name = class_name;
    this->class_names = class_name.split("|");
    this->subtype = subtype;
}

LuaTrickCard *LuaTrickCard::clone(Card::Suit suit, int number) const
{
    if (suit == Card::SuitToBeDecided) suit = getSuit();
    if (number == -1) number = getNumber();
    LuaTrickCard *new_card = new LuaTrickCard(suit, number, objectName(), class_name, subtype);
    new_card->subclass = subclass;

    new_card->target_fixed = target_fixed;
    new_card->can_recast = can_recast;
    new_card->damage_card = damage_card;
    new_card->is_gift = is_gift;
    new_card->single_target = single_target;

    new_card->filter = filter;
    new_card->feasible = feasible;
    new_card->available = available;
    new_card->is_cancelable = is_cancelable;
    new_card->about_to_use = about_to_use;
    new_card->on_use = on_use;
    new_card->on_effect = on_effect;
    new_card->on_nullified = on_nullified;
    new_card->on_validate = on_validate;
    new_card->on_validate_in_response = on_validate_in_response;

    return new_card;
}

LuaWeapon::LuaWeapon(Card::Suit suit, int number, int range, const QString &obj_name, const QString &class_name)
    : Weapon(suit, number, range), filter(0), feasible(0), available(0), on_install(0), on_uninstall(0)
{
    setObjectName(obj_name);
    this->class_name = class_name;
    this->class_names = class_name.split("|");
}

LuaWeapon *LuaWeapon::clone(Card::Suit suit, int number) const
{
    if (suit == Card::SuitToBeDecided) suit = getSuit();
    if (number == -1) number = getNumber();
    LuaWeapon *new_card = new LuaWeapon(suit, number, getRange(), objectName(), class_name);

    new_card->filter = filter;
    new_card->feasible = feasible;
    new_card->target_fixed = target_fixed;
    new_card->is_gift = is_gift;
    new_card->available = available;
    new_card->on_install = on_install;
    new_card->on_uninstall = on_uninstall;

    return new_card;
}

LuaArmor::LuaArmor(Card::Suit suit, int number, const QString &obj_name, const QString &class_name)
    : Armor(suit, number), filter(0), feasible(0), available(0), on_install(0), on_uninstall(0)
{
    setObjectName(obj_name);
    this->class_name = class_name;
    this->class_names = class_name.split("|");
}

LuaArmor *LuaArmor::clone(Card::Suit suit, int number) const
{
    if (suit == Card::SuitToBeDecided) suit = getSuit();
    if (number == -1) number = getNumber();
    LuaArmor *new_card = new LuaArmor(suit, number, objectName(), class_name);

    new_card->filter = filter;
    new_card->feasible = feasible;
    new_card->target_fixed = target_fixed;
    new_card->is_gift = is_gift;
    new_card->available = available;
    new_card->on_install = on_install;
    new_card->on_uninstall = on_uninstall;

    return new_card;
}

LuaHorse::LuaHorse(Card::Suit suit, int number, int correct, const QString &obj_name, const QString &class_name)
    : Horse(suit, number, correct), filter(0), feasible(0), available(0), on_install(0), on_uninstall(0), correct_func(0)
{
    setObjectName(obj_name);
    this->class_name = class_name;
    this->class_names = class_name.split("|");
}

LuaHorse *LuaHorse::clone(Card::Suit suit, int number) const
{
    if (suit == Card::SuitToBeDecided) suit = getSuit();
    if (number == -1) number = getNumber();
    LuaHorse *new_card = new LuaHorse(suit, number, getCorrect(), objectName(), class_name);

    new_card->filter = filter;
    new_card->feasible = feasible;
    new_card->target_fixed = target_fixed;
    new_card->is_gift = is_gift;
    new_card->available = available;
    new_card->on_install = on_install;
    new_card->on_uninstall = on_uninstall;
    new_card->correct_func = correct_func;

    return new_card;
}

LuaOffensiveHorse::LuaOffensiveHorse(Card::Suit suit, int number, int correct, const QString &obj_name, const QString &class_name)
    : OffensiveHorse(suit, number, correct), filter(0), feasible(0), available(0), on_install(0), on_uninstall(0), correct_func(0)
{
    setObjectName(obj_name);
    this->class_name = class_name;
    this->class_names = class_name.split("|");
}

LuaOffensiveHorse *LuaOffensiveHorse::clone(Card::Suit suit, int number) const
{
    if (suit == Card::SuitToBeDecided) suit = getSuit();
    if (number == -1) number = getNumber();
    LuaOffensiveHorse *new_card = new LuaOffensiveHorse(suit, number, getCorrect(), objectName(), class_name);

    new_card->filter = filter;
    new_card->feasible = feasible;
    new_card->target_fixed = target_fixed;
    new_card->is_gift = is_gift;
    new_card->available = available;
    new_card->on_install = on_install;
    new_card->on_uninstall = on_uninstall;
    new_card->correct_func = correct_func;
    new_card->occupy_locations = occupy_locations;

    return new_card;
}

LuaDefensiveHorse::LuaDefensiveHorse(Card::Suit suit, int number, int correct, const QString &obj_name, const QString &class_name)
    : DefensiveHorse(suit, number, correct), filter(0), feasible(0), available(0), on_install(0), on_uninstall(0), correct_func(0)
{
    setObjectName(obj_name);
    this->class_name = class_name;
    this->class_names = class_name.split("|");
}

LuaDefensiveHorse *LuaDefensiveHorse::clone(Card::Suit suit, int number) const
{
    if (suit == Card::SuitToBeDecided) suit = getSuit();
    if (number == -1) number = getNumber();
    LuaDefensiveHorse *new_card = new LuaDefensiveHorse(suit, number, getCorrect(), objectName(), class_name);

    new_card->filter = filter;
    new_card->feasible = feasible;
    new_card->target_fixed = target_fixed;
    new_card->is_gift = is_gift;
    new_card->available = available;
    new_card->on_install = on_install;
    new_card->on_uninstall = on_uninstall;
    new_card->correct_func = correct_func;
    new_card->occupy_locations = occupy_locations;

    return new_card;
}

LuaTreasure::LuaTreasure(Card::Suit suit, int number, const QString &obj_name, const QString &class_name)
    : Treasure(suit, number), filter(0), feasible(0), available(0), on_install(0), on_uninstall(0)
{
    setObjectName(obj_name);
    this->class_name = class_name;
    this->class_names = class_name.split("|");
}

LuaTreasure *LuaTreasure::clone(Card::Suit suit, int number) const
{
    if (suit == Card::SuitToBeDecided) suit = getSuit();
    if (number == -1) number = getNumber();
    LuaTreasure *new_card = new LuaTreasure(suit, number, objectName(), class_name);

    new_card->filter = filter;
    new_card->feasible = feasible;
    new_card->target_fixed = target_fixed;
    new_card->is_gift = is_gift;
    new_card->available = available;
    new_card->on_install = on_install;
    new_card->on_uninstall = on_uninstall;

    return new_card;
}


LuaScenarioRule::LuaScenarioRule(Scenario *scenario)
    : ScenarioRule(scenario), can_trigger(0), on_trigger(0)
{
    //events << GameReady << EventPhaseStart << FetchDrawPileCard;
}

