#include "card.h"
#include "settings.h"
#include "engine.h"
//#include "client.h"
#include "room.h"
//#include "structs.h"
#include "lua-wrapper.h"
//#include "standard.h"
#include "clientplayer.h"
#include "wrapped-card.h"
#include "roomthread.h"

const int Card::S_UNKNOWN_CARD_ID = -1;

const Card::Suit Card::AllSuits[4] = {
	Card::Spade,
	Card::Club,
	Card::Heart,
	Card::Diamond
};

static unsigned int cardId = 0;

Card::Card(Suit suit, int number, bool target_fixed, bool damage_card, bool is_gift, bool single_target)
	:target_fixed(target_fixed), mute(false), will_throw(true), has_preact(false), can_recast(false),
	m_suit(suit), m_number(number), m_id(--cardId), is_gift(is_gift), damage_card(damage_card),
	single_target(single_target),handling_method(MethodUse)
{
}

QString Card::getSuitString() const
{
	return Suit2String(getSuit());
}

QString Card::Suit2String(Suit suit)
{
	switch (suit) {
	case Spade: return "spade";
	case Heart: return "heart";
	case Club: return "club";
	case Diamond: return "diamond";
	case NoSuitBlack: return "no_suit_black";
	case NoSuitRed: return "no_suit_red";
	default: return "no_suit";
	}
}

bool Card::isRed() const
{
	return getColor() == Red;
}

bool Card::isBlack() const
{
	return getColor() == Black;
}

int Card::getId() const
{
	return m_id;
}

void Card::setId(int id)
{
	this->m_id = id;
	if (id>=0){
		subcards.clear();
		subcards << id;
	}
}

int Card::getEffectiveId() const
{
	if (isVirtualCard()&&subcards.size()>0)
		return subcards.first();
	return m_id;
}

int Card::getNumber() const
{
	/*foreach (QString flag, getFlags()) {
		if (flag.startsWith("CardInformationHelper|")){
			QStringList info = flag.split("|");
			if (info.length() == 3)
				return info.last().toInt();
		}
	}*/
	if(m_number>0) return m_number;
	if(isVirtualCard()&&subcards.size()==1)
		return Sanguosha->getCard(subcards.first())->getNumber();
	if(objectName().contains("_zhizhe_")){
		const Card*zhizhe = Sanguosha->getCard(m_id);
		if(zhizhe) return zhizhe->getNumber();
	}
	return 0;
}

void Card::setNumber(int number)
{
	this->m_number = number;
}

QString Card::getNumberString() const
{
	int num = getNumber();
	if (num == 10) return "10";
	if (num > 13) return "JOKER";
	static const char*number_string = "0A23456789-JQK";
	return QString(number_string[num]);
}

Card::Suit Card::getSuit() const
{
	/*foreach (QString flag, getFlags()) {
		if (flag.startsWith("CardInformationHelper|")){
			QString suit_str = flag.split("|").at(1);
			if (suit_str == "spade") return Spade;
			if (suit_str == "heart") return Heart;
			if (suit_str == "club") return Club;
			if (suit_str == "diamond") return Diamond;
			if (suit_str == "no_suit_black") return NoSuitBlack;
			if (suit_str == "no_suit_red") return NoSuitRed;
			return NoSuit;
		}
	}*/
	if(m_suit>-1&&m_suit<6) return m_suit;

	if(isVirtualCard()&&subcards.length()>0){
		if(subcards.length()==1) return Sanguosha->getCard(subcards.first())->getSuit();
		Color color = Sanguosha->getCard(subcards.first())->getColor();
		for (int i = 1; i < subcards.length(); i++) {
			if (color!=Sanguosha->getCard(subcards[i])->getColor())
				return NoSuit;
		}
		if(color == Red) return NoSuitRed;
		else if(color == Black) return NoSuitBlack;
	}
	if(objectName().contains("_zhizhe_")){
		const Card*zhizhe = Sanguosha->getCard(m_id);
		if(zhizhe) return zhizhe->getSuit();
	}
	return NoSuit;
}

void Card::setSuit(Suit suit)
{
	this->m_suit = suit;
}

bool Card::sameColorWith(const Card*other) const
{
	return other && getColor() == other->getColor();
}

Card::Color Card::getColor() const
{
	switch (getSuit()) {
	case Spade:
	case Club:
	case NoSuitBlack:
		return Black;
	case Heart:
	case Diamond:
	case NoSuitRed:
		return Red;
	default:
		return Colorless;
	}
}

QString Card::getColorString() const
{
	switch (getColor()) {
	case Black:
		return "black";
	case Red:
		return "red";
	default:
		return "no_color";
	}
}

bool Card::hasSuit() const
{
	return getSuit() <= 3;
}

bool Card::isEquipped() const
{
	return Self && Self->hasEquip(this);
}

bool Card::match(const QString &pattern) const
{
	foreach(QString ptn, pattern.split("+"))
		if (objectName() == ptn || getType() == ptn || getSubtype() == ptn)
			return true;
	return false;
}

bool Card::CompareByNumber(const Card*a, const Card*b)
{
	if (a->m_number != b->m_number)
		return a->m_number < b->m_number;
	static Suit new_suits[] = { Spade, Heart, Club, Diamond, NoSuitBlack, NoSuitRed, NoSuit };
	return new_suits[a->getSuit()] < new_suits[b->getSuit()];
}

bool Card::CompareBySuit(const Card*a, const Card*b)
{
	static Suit new_suits[] = { Spade, Heart, Club, Diamond, NoSuitBlack, NoSuitRed, NoSuit };
	Suit suit1 = new_suits[a->getSuit()];
	Suit suit2 = new_suits[b->getSuit()];

	if (suit1 != suit2) return suit1 < suit2;
	return a->m_number < b->m_number;
}

bool Card::CompareByType(const Card*a, const Card*b)
{
	int order1 = a->getTypeId();
	int order2 = b->getTypeId();
	if (order1 != order2)
		return order1 < order2;
	else {
		switch (a->getTypeId()) {
		case TypeBasic: {
			static QStringList basic;
			if (basic.isEmpty()) basic << "slash" << "thunder_slash" << "fire_slash" << "jink" << "peach" << "analeptic";
			foreach (QString object_name, basic) {
				if (a->objectName() == object_name) {
					if (b->objectName() == object_name)
						return CompareBySuit(a, b);
					return true;
				}
				if (b->objectName() == object_name)
					return false;
			}
			return CompareBySuit(a, b);
			break;
		}
		case TypeTrick: {
			if (a->objectName() == b->objectName())
				return CompareBySuit(a, b);
			return a->objectName() < b->objectName();
			break;
		}
		case TypeEquip: {
			const EquipCard*eq_a = qobject_cast<const EquipCard*>(a->getRealCard());
			const EquipCard*eq_b = qobject_cast<const EquipCard*>(b->getRealCard());
			if (eq_a->location() == eq_b->location()) {
				if (eq_a->isKindOf("Weapon")) {
					const Weapon*wep_a = qobject_cast<const Weapon*>(eq_a);
					const Weapon*wep_b = qobject_cast<const Weapon*>(eq_b);
					if (wep_a->getRange() == wep_b->getRange())
						return CompareBySuit(a, b);
					return wep_a->getRange() < wep_b->getRange();
				} else {
					if (a->objectName() == b->objectName())
						return CompareBySuit(a, b);
					return a->objectName() < b->objectName();
				}
			} else
				return eq_a->location() < eq_b->location();
			break;
		}
		default:
			return CompareBySuit(a, b);
		}
	}
}

bool Card::isNDTrick() const
{
	return getTypeId() == TypeTrick && !isKindOf("DelayedTrick");
}

QString Card::getPackage() const
{
	if (parent())
		return parent()->objectName();
	return "";
}

QString Card::getFullName(bool include_suit) const
{
	if (include_suit)
		return QString("%1%2 %3").arg(Sanguosha->translate(getSuitString())).arg(getNumberString()).arg(getName());
	return QString("%1 %2").arg(getNumberString()).arg(getName());
}

QString Card::getLogName() const
{
	QString suit_char, number_string;

	switch (getSuit()) {
	case Spade:
	case Heart:
	case Club:
	case Diamond: {
		suit_char = QString("<img src='image/system/log/%1.png' height=12/>").arg(getSuitString());
		break;
	}
	case NoSuitRed: {
		suit_char = tr("NoSuitRed");
		break;
	}
	case NoSuitBlack: {
		suit_char = tr("NoSuitBlack");
		break;
	}
	case NoSuit: {
		suit_char = tr("NoSuit");
		break;
	}
	default:
		break;
	}

	int num = getNumber();
	if (num > 0 && num <= 13)
		number_string = getNumberString();

	return QString("%1[%2%3]").arg(getName()).arg(suit_char).arg(number_string);
}

QString Card::getCommonEffectName() const
{
	return "";
}

QString Card::getName() const
{
	if(objectName().isEmpty())
		return Sanguosha->translate(getClassName());
	return Sanguosha->translate(objectName());
}

int Card::nameLength() const
{
	if(isKindOf("Slash")) return 1;
	return getName().length();
}

QString Card::getSkillName(bool removePrefix) const
{
	if (removePrefix && m_skillName.startsWith("_"))
		return m_skillName.mid(1);
	return m_skillName;
}

void Card::setSkillName(const QString &name)
{
	this->m_skillName = name;
}

bool Card::isGift() const
{
	return getRealCard()->is_gift;
}

void Card::setGift(bool flag)
{
	if (this->is_gift != flag)
		this->is_gift = flag;
}

bool Card::isDamageCard() const
{
	return getRealCard()->damage_card;
}

void Card::setDamageCard(bool flag)
{
	if (this->damage_card != flag)
		this->damage_card = flag;
}

bool Card::isSingleTargetCard() const
{
	return isKindOf("EquipCard") || isKindOf("DelayedTrick")
	|| (isKindOf("SingleTargetTrick") && !isKindOf("Nullification"))
	|| getRealCard()->single_target;
}

void Card::setSingleTargetCard(bool flag)
{
	if (this->single_target != flag)
		this->single_target = flag;
}

bool Card::isZhinangCard() const
{
	foreach (QString name, Sanguosha->getZhinangCards()) {
		if (getClassName().contains(name))
			return true;
	}
	return false;
}

void Card::addCharTag(QString tag)
{
	QStringList CharTag = property("CharTag").toStringList();
	CharTag << tag;
	setProperty("CharTag", CharTag);
}

QString Card::getDescription() const
{
	QString desc = Sanguosha->translate(":" + objectName());
	QString schar = property("YingBianEffects").toString();
	if(!schar.isEmpty())
		desc.append(QString("<br/><font color=red><b>%1</b></font>").arg(Sanguosha->translate(":" + schar)));
	foreach (QString t, property("CharTag").toStringList())
		desc.append(QString("<br/><font color=red><b>%1</b></font>").arg(Sanguosha->translate(":" + t)));
	if (m_id>0&&Sanguosha->getEngineCard(m_id)->objectName().contains("_zhizhe_"))
		desc.append("<br/><br/>").append(Sanguosha->translate("zhizhe_card"));
	if (desc.startsWith("[NoAutoRep]"))
		desc = desc.mid(11);
	else {
		if (Config.value("AutoSkillTypeColorReplacement").toBool()) {
			QMap<QString, QColor> color_map = Sanguosha->getSkillTypeColorMap();
			foreach (QString skill_type, color_map.keys()) {
				schar = Sanguosha->translate(skill_type);
				if(desc.contains(schar))
					desc.replace(schar, QString("<font color=%1><b>%2</b></font>").arg(color_map[skill_type].name()).arg(schar));
			}
		}
		if (Config.value("AutoSuitReplacement").toBool()) {
			for (int i = 0; i < 4; i++) {
				QString suit = Card::Suit2String((Card::Suit)i);
				schar = Sanguosha->translate(suit + "_char");
				QString red_char = schar;
				if (i > 1){
					red_char = QString("<font color=red>%1</font>").arg(schar);
					desc.replace(schar, red_char);
				}
				desc.replace(Sanguosha->translate(suit), red_char);
			}
		}
	}
	desc.replace("\n", "<br/>");
	if(Config.EnableCardDescription)
		return QString("<b>【%1】</b> %2 %3").arg(getName()).arg(getClassName()).arg(desc);
	return QString("<b>【%1】</b> %2").arg(getName()).arg(desc);
}

QString Card::toString(bool hidden) const
{
	Q_UNUSED(hidden);
	if (isVirtualCard())
		return QString("%1:%2[%3:%4]=%5").arg(objectName()).arg(m_skillName)
		.arg(getSuitString()).arg(getNumberString()).arg(subcardString());
	return QString::number(m_id);
}

QString Card::subcardString() const
{
	if (subcards.isEmpty())
		return ".";

	QStringList str;
	foreach(int id, subcards)
		str << QString::number(id);

	return str.join("+");
}

void Card::addSubcards(const QList<const Card*> &cards)
{
	foreach(const Card*card, cards)
		subcards << card->getEffectiveId();
}

void Card::addSubcards(const QList<int> &subcards_list)
{
	subcards << subcards_list;
}

int Card::subcardsLength() const
{
	return subcards.length();
}

bool Card::isVirtualCard(bool include_filter) const
{
	return m_id < 0 || (include_filter && Sanguosha->getWrappedCard(m_id)->isModified());
}

const Card*Card::Parse(const QString &str)
{
	static QMap<QString, Card::Suit> suit_map;
	if (suit_map.isEmpty()) {
		suit_map.insert("spade", Spade);
		suit_map.insert("club", Club);
		suit_map.insert("heart", Heart);
		suit_map.insert("diamond", Diamond);
		suit_map.insert("no_suit_red", NoSuitRed);
		suit_map.insert("no_suit_black", NoSuitBlack);
		//suit_map.insert("no_suit", NoSuit);
		//suit_map.insert("to_be_decided", SuitToBeDecided);
	}
	QString copy = str;
	if (str.contains("->")) copy = copy.split("->").first();
	if (str.startsWith("@")) {// skill card
		static QRegExp pattern("@(\\w+)=([^:]+)(:.+)?");
		static QRegExp ex_pattern("@(\\w*)\\[(\\w+):(.+)\\]=([^:]+)(:.+)?");
		QString card_name, card_suit, card_number;
		QString subcard_str, user_string;
		if (pattern.exactMatch(copy)) {
			QStringList texts = pattern.capturedTexts();
			card_name = texts.at(1);
			subcard_str = texts.at(2);
			user_string = texts.at(3);
		} else if (ex_pattern.exactMatch(copy)) {
			QStringList texts = ex_pattern.capturedTexts();
			card_name = texts.at(1);
			card_suit = texts.at(2);
			card_number = texts.at(3);
			subcard_str = texts.at(4);
			user_string = texts.at(5);
		}
		SkillCard*card = Sanguosha->cloneSkillCard(card_name);
		if (card){
			// skill name
			// @todo: This is extremely dirty and would cause endless troubles.
			if (card->getSkillName().isEmpty()) card->setSkillName(card_name.remove("Card").toLower());
			if (card_suit!="") card->setSuit(suit_map.value(card_suit, NoSuit));
			if (subcard_str!=".") card->addSubcards(ListS2I(subcard_str.split("+")));
			if (card_number!="") {
				if (card_number == "A") card->setNumber(1);
				else if (card_number == "J") card->setNumber(11);
				else if (card_number == "Q") card->setNumber(12);
				else if (card_number == "K") card->setNumber(13);
				else card->setNumber(card_number.toInt());
			}
			if (user_string!="") {
				user_string.remove(0, 1);
				card->setUserString(user_string);
			}
			card->deleteLater();
			return card;
		}
	} else if (str.startsWith("$")) {
		DummyCard*dummy = new DummyCard(ListS2I(copy.mid(1).split("+")));
		dummy->deleteLater();
		return dummy;
	} else if (str.startsWith("#")) {
		LuaSkillCard*new_card = LuaSkillCard::Parse(copy);
		new_card->deleteLater();
		return new_card;
	} else if (str.contains("=")) {
		static QRegExp pattern("(\\w+):(\\w*)\\[(\\w+):(.+)\\]=(.+)");
		if (pattern.exactMatch(copy)){
			QStringList subcard_ids, texts = pattern.capturedTexts();
			QString card_name = texts.at(1);
			QString m_skillName = texts.at(2);
			QString suit_string = texts.at(3);
			QString number_string = texts.at(4);
			if (texts.at(5) != ".") subcard_ids = texts.at(5).split("+");
			int number = 0;
			if (number_string == "A") number = 1;
			else if (number_string == "J") number = 11;
			else if (number_string == "Q") number = 12;
			else if (number_string == "K") number = 13;
			else number = number_string.toInt();
			Card*card = Sanguosha->cloneCard(card_name, suit_map.value(suit_string, NoSuit), number);
			if (card){
				card->addSubcards(ListS2I(subcard_ids));
				card->setSkillName(m_skillName);
				card->deleteLater();
				return card;
			}
		}
	} else if(copy != "."){
		bool ok = false;
		int card_id = copy.toInt(&ok);
		if (ok) return Sanguosha->getCard(card_id);//->getRealCard();
	}
	return nullptr;
}

Card*Card::Clone(const Card*card)
{
	Card*card_obj = nullptr;
	if (card->inherits("LuaBasicCard")) card_obj = qobject_cast<const LuaBasicCard*>(card)->clone();
	else if (card->inherits("LuaTrickCard")) card_obj = qobject_cast<const LuaTrickCard*>(card)->clone();
	else if (card->inherits("LuaWeapon")) card_obj = qobject_cast<const LuaWeapon*>(card)->clone();
	else if (card->inherits("LuaArmor")) card_obj = qobject_cast<const LuaArmor*>(card)->clone();
	else if (card->inherits("LuaOffensiveHorse")) card_obj = qobject_cast<const LuaOffensiveHorse*>(card)->clone();
	else if (card->inherits("LuaDefensiveHorse")) card_obj = qobject_cast<const LuaDefensiveHorse*>(card)->clone();
	else if (card->inherits("LuaTreasure")) card_obj = qobject_cast<const LuaTreasure*>(card)->clone();
	else card_obj = qobject_cast<Card*>(card->metaObject()->newInstance(Q_ARG(Card::Suit, card->getSuit()), Q_ARG(int, card->getNumber())));
	if (card_obj) {
		card_obj->setObjectName(card->objectName());
		card_obj->setId(card->getId());
	}
	return card_obj;
}

bool Card::targetFixed() const
{
	return target_fixed;
}

bool Card::targetsFeasible(const QList<const Player*> &targets, const Player*) const
{
	return target_fixed || !targets.isEmpty();
}

bool Card::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && (target_fixed || to_select != Self)
		&& !Self->isProhibited(to_select, this, targets);
}

bool Card::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self, int &maxVotes) const
{
	bool canSelect = targetFilter(targets, to_select, Self);
	maxVotes = canSelect ? 1 : 0;
	return canSelect;
}

void Card::doPreAction(Room*, const CardUseStruct &) const
{
}

void Card::onUse(Room*room, CardUseStruct &card_use) const
{
	room->sortByActionOrder(card_use.to);

	if (room->getMode() == "06_3v3" && (card_use.card->isKindOf("AOE") || card_use.card->isKindOf("GlobalEffect")))
		room->reverseFor3v3(card_use.card, card_use.from, card_use.to);

	QVariant data = QVariant::fromValue(card_use);
	room->getThread()->trigger(PreCardUsed, room, card_use.from, data);
	card_use = data.value<CardUseStruct>();

	LogMessage log;
	log.from = card_use.from;
	if (!card_use.card->targetFixed()||card_use.to.length()>1||!card_use.to.contains(card_use.from))
		log.to = card_use.to;
	log.type = "#UseCard";
	foreach (const Card*c, card_use.card->change_cards) {
		if(c->isVirtualCard(true)){
			log.card_str = c->toString(c->getTypeId()<1&&!c->willThrow());
			room->sendLog(log);
		}
	}
	log.card_str = card_use.card->toString(card_use.card->getTypeId()<1&&!card_use.card->willThrow());
	room->sendLog(log);

	CardMoveReason reason(CardMoveReason::S_REASON_USE, card_use.from->objectName(), card_use.card->getSkillName(), "");
	if (card_use.to.size()==1&&card_use.to.first()!=card_use.from) reason.m_targetId = card_use.to.first()->objectName();
	if (card_use.card->subcardsLength()>0){
		reason.m_useStruct = card_use;
		reason.m_extraData = QVariant::fromValue(card_use.card);
		if (card_use.card->getTypeId()!=TypeSkill) {
			room->moveCardsAtomic(CardsMoveStruct(card_use.card->getSubcards(), nullptr, Player::PlaceTable, reason), true);
		} else if (card_use.card->willThrow()){
			reason.m_reason = CardMoveReason::S_REASON_THROW;
			room->moveCardsAtomic(CardsMoveStruct(card_use.card->getSubcards(), nullptr, Player::DiscardPile, reason), true);
		}
	}

	room->getThread()->trigger(CardUsed, room, card_use.from, data);
	card_use = data.value<CardUseStruct>();

	QList<int> used_cards = card_use.card->getSubcards();
	if (used_cards.length()>0&&reason.m_reason!=CardMoveReason::S_REASON_THROW){
		foreach (int id, used_cards) {
			if (room->getCardPlace(id)!=Player::PlaceTable)
				used_cards.removeAll(id);
		}
		reason.m_useStruct = card_use;
		reason.m_extraData = QVariant::fromValue(card_use.card);
		room->moveCardsAtomic(CardsMoveStruct(used_cards, card_use.from, nullptr, Player::PlaceTable, Player::DiscardPile, reason), true);
	}
	room->getThread()->trigger(CardFinished, room, card_use.from, data);
	card_use = data.value<CardUseStruct>();
}

void Card::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	CardUseStruct cardUse = room->getTag("UseHistory"+toString()).value<CardUseStruct>();
	foreach (ServerPlayer*target, targets) {
		CardEffectStruct effect;
		effect.nullified = cardUse.nullified_list.contains("_ALL_TARGETS")
			||cardUse.nullified_list.contains(target->objectName());
		if (effect.nullified){
			room->setEmotion(target,"skill_nullify");
			continue;
		}
		effect.no_respond = cardUse.no_respond_list.contains("_ALL_TARGETS")
			||cardUse.no_respond_list.contains(target->objectName());
		effect.no_offset = cardUse.no_offset_list.contains("_ALL_TARGETS")
			||cardUse.no_offset_list.contains(target->objectName());
		effect.multiple = targets.length() > 1;
		effect.from = source;
		effect.to = target;
		effect.card = this;
		room->cardEffect(effect);
	}
}

void Card::onEffect(CardEffectStruct &) const
{
}

bool Card::isCancelable(const CardEffectStruct &) const
{
	return false;
}

void Card::addSubcard(int card_id)
{
	if (card_id < 0)
		qWarning("%s", qPrintable(tr("Subcard must not be virtual card!")));
	else
		subcards << card_id;
}

void Card::addSubcard(const Card*card)
{
	if (card->isVirtualCard())
		addSubcards(card->getSubcards());
	else
		addSubcard(card->getId());
}

QList<int> Card::getSubcards() const
{
	return subcards;
}

void Card::clearSubcards()
{
	subcards.clear();
}

bool Card::isAvailable(const Player*player) const
{
	return (can_recast&&!player->isCardLimited(this,MethodRecast))||!player->isCardLimited(this,handling_method);
}

const Card*Card::validate(CardUseStruct &) const
{
	return this;
}

const Card*Card::validateInResponse(ServerPlayer*) const
{
	return this;
}

bool Card::isMute() const
{
	return mute;
}

void Card::setMute(bool flag)
{
	if (mute != flag)
		this->mute = flag;
}

bool Card::willThrow() const
{
	return will_throw;
}

bool Card::canRecast() const
{
	return can_recast;
}

void Card::setCanRecast(bool can)
{
	can_recast = can;
}

bool Card::hasPreAction() const
{
	return has_preact;
}

Card::HandlingMethod Card::getHandlingMethod() const
{
	return handling_method;
}

void Card::addMark(const QString &mark, int add_num) const
{
	setMark(mark, getMark(mark)+add_num);
}

void Card::removeMark(const QString &mark, int remove_num) const
{
	setMark(mark, qMax(getMark(mark)-remove_num, 0));
}

void Card::setMark(const QString &mark, int value) const
{
	QString m = "cardMark:"+mark+":";
	foreach (QString flag, flags) {
		if (flag.contains(m)){
			flags.removeAll(flag);
			if(value==0) return;
			QStringList ms = flag.split(":");
			ms.takeLast();
			ms << QString::number(value);
			flags << ms.join(":");
			return;
		}
	}
	flags << m+QString::number(value);
}

int Card::getMark(const QString &mark) const
{
	QString m = "cardMark:"+mark+":";
	foreach (QString flag, flags) {
		if (flag.contains(m))
			return flag.split(":").last().toInt();
	}
	return 0;
}

QStringList Card::getMarkNames() const
{
	QStringList ms;
	foreach (QString flag, flags) {
		if (flag.contains("cardMark:"))
			ms << flag.split(":")[1];
	}
	return ms;
}

void Card::addChange(const Card*card)
{
	change_cards << card;
}

QStringList Card::getSkillNames(bool removePrefix) const
{
	QStringList names;
	foreach (const Card*c, change_cards)
		names << c->getSkillName(removePrefix);
	names << getSkillName(removePrefix);
	return names;
}

void Card::setFlags(const QString &flag) const
{
	if(flag == ".") flags.clear();
	else{
		if(flag.startsWith("-")){
			flags.removeAll(flag.mid(1));
		} else if(!flags.contains(flag))
			flags << flag;
	}
}

bool Card::hasFlag(const QString &flag) const
{
	return flags.contains(flag);
}

void Card::clearFlags() const
{
	flags.clear();
}

QStringList Card::getTips(bool split) const
{
	QStringList tips;
	foreach (QString flag, flags) {
		if (flag.contains("cardTip:")){
			flag = flag.split(":").last();
			if(split){
				if (flag.endsWith("Clear"))
					flag = flag.split("-").first();
				else if (flag.endsWith("_lun"))
					flag.remove("_lun");
			}
			tips << flag;
		}
	}
	return tips;
}

bool Card::hasTip(const QString &tip, bool split) const
{
	return getTips(split).contains(tip);
}

void Card::setTag(const QString &key, const QVariant &data) const
{
	tag[key] = data;
}

void Card::removeTag(const QString &key) const
{
	tag.remove(key);
}

bool Card::sameNameWith(const Card*card, bool different_slash) const
{
	if (!different_slash && isKindOf("Slash"))
		return card->isKindOf("Slash");
	return objectName() == card->objectName();
}

bool Card::sameNameWith(const QString &card_name, bool different_slash) const
{
	if (!different_slash && isKindOf("Slash"))
		return card_name.endsWith("slash");
	return objectName() == card_name;
}

// --------- Skill card ------------------

SkillCard::SkillCard() : Card(NoSuit, 0)
{
	handling_method = MethodDiscard;
}

void SkillCard::setUserString(const QString &user_string)
{
	this->user_string = user_string;
}

QString SkillCard::getUserString() const
{
	return user_string;
}

QString SkillCard::getType() const
{
	return "skill_card";
}

QString SkillCard::getSubtype() const
{
	return "skill_card";
}

Card::CardType SkillCard::getTypeId() const
{
	return TypeSkill;
}

QString SkillCard::toString(bool hidden) const
{
	QString str;
	if (hidden) str = QString("@%1[no_suit:0]=.").arg(metaObject()->className());
	else str = QString("@%1[%2:%3]=%4").arg(metaObject()->className()).arg(getSuitString()).arg(getNumberString()).arg(subcardString());

	if (hidden||user_string.isEmpty()) return str;

	return QString("%1:%2").arg(str).arg(user_string);
}

// ---------- Dummy card -------------------

DummyCard::DummyCard(const QList<int> &subcards)
 : SkillCard()
{
	target_fixed = true;
	handling_method = MethodNone;
	this->subcards = subcards;
	setObjectName("dummy");
}

QString DummyCard::getType() const
{
	return "dummy_card";
}

QString DummyCard::getSubtype() const
{
	return getType();
}

QString DummyCard::toString(bool) const
{
	return "$" + subcardString();
}

void DummyCard::onUse(Room*, CardUseStruct &) const
{
}

