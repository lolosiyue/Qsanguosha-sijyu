#include "player.h"
#include "engine.h"
#include "room.h"
//#include "client.h"
#include "standard.h"
#include "settings.h"
#include "clientstruct.h"
#include "exppattern.h"
#include "wrapped-card.h"
#include <src/util/ThreadSafeHelper.h>

Player::Player(QObject *parent)
    : QObject(parent), owner(false), general(nullptr), general2(nullptr),
    m_gender(General::Sexless), hp(-1), max_hp(-1), role("unknown"), state("online"),
	seat(0), player_seat(0), alive(true), phase(NotActive),
	//equip_area(QList<int>()), equips(QList<const EquipCard *>()),
    //weapon(nullptr), armor(nullptr), defensive_horse(nullptr), offensive_horse(nullptr), treasure(nullptr),
    face_up(true), chained(false),
    //hasweaponarea(true), hasarmorarea(true), hasdefensivehorsearea(true), hasoffensivehorsearea(true), hastreasurearea(true),
    hasjudgearea(true),
    role_shown(false)//, pile_open(QMap<QString, QStringList>())
{
	equip_area << 0 << 1 << 2 << 3 << 4;
	static QList<const char*> areas;
	if(areas.isEmpty()) areas << "weapon_area" << "armor_area" << "defensive_horse_area" << "offensive_horse_area" << "treasure_area";
	foreach(int ea, equip_area) setProperty(areas[ea], true);
}

void Player::setScreenName(const QString &screen_name)
{
    this->screen_name = screen_name;
}

QString Player::screenName() const
{
    return screen_name;
}

bool Player::isOwner() const
{
    return owner;
}

void Player::setOwner(bool owner)
{
    if (this->owner != owner){
        this->owner = owner;
        emit owner_changed(owner);
    }
}

bool Player::hasShownRole() const
{
    return role_shown;
}

void Player::setShownRole(bool shown)
{
    this->role_shown = shown;
}

void Player::setHp(int hp)
{
    if (this->hp != hp){
        this->hp = hp;
        emit hp_changed();
    }
}

int Player::getHp() const
{
    return hp;
}

int Player::getMaxHp() const
{
    return max_hp;
}

void Player::setMaxHp(int max_hp)
{
    if (this->max_hp != max_hp){
		this->max_hp = max_hp;
		if (hp > max_hp)
			hp = max_hp;
		emit hp_changed();
	}
}

int Player::getLostHp() const
{
    return max_hp - qMax(hp, 0);
}

bool Player::isWounded() const
{
	if (getKingdom() == "wu"){
		foreach(const Player *p, getAliveSiblings()){
			if (p->hasFlag("CurrentPlayer") && p->hasLordSkill("guiming"))
				return true;
		}
	}
    return hp < 0 || hp < max_hp;
}

General::Gender Player::getGender() const
{
    return m_gender;
}

void Player::setGender(General::Gender gender)
{
    m_gender = gender;
}

bool Player::isMale() const
{
    return m_gender == General::Male;
}

bool Player::isFemale() const
{
    return m_gender == General::Female;
}

bool Player::isNeuter() const
{
    return m_gender == General::Neuter;
}

int Player::getSeat() const
{
    return seat;
}

void Player::setSeat(int seat)
{
    this->seat = seat;
}

int Player::getPlayerSeat() const
{
    return player_seat;
}

void Player::setPlayerSeat(int player_seat)
{
    this->player_seat = player_seat;
}

bool Player::isAdjacentTo(const Player *another) const
{
    if (seat<0||another->seat<0) return false;
    if (qAbs(seat-another->seat)==1) return true;
	int min = seat, max = seat;
	foreach(const Player *p, getAliveSiblings()){
		if (p->seat<0) continue;
		min = qMin(min,p->seat);
		max = qMax(max,p->seat);
	}
    return (seat == min && another->seat == max)
        || (seat == max && another->seat == min);
}

bool Player::isAlive() const
{
    return alive;
}

bool Player::isDead() const
{
    return !alive;
}

bool Player::isRest() const
{
    return property("RestPlayer").toBool();
}

void Player::setAlive(bool alive)
{
    this->alive = alive;
}

QString Player::getFlags() const
{
    return getFlagList().join("|");
}

QStringList Player::getFlagList() const
{
    return QStringList(flags.values());
}

void Player::setFlags(const QString &flag)
{
    if (flag == ".")
        clearFlags();
    else{
		if (flag.startsWith("-")){
			flags.remove(flag.mid(1));
		} else
			flags.insert(flag);
	}
}

bool Player::hasFlag(const QString &flag) const
{
    return flags.contains(flag);
}

void Player::clearFlags()
{
    flags.clear();
}

int Player::getAttackRange(bool include_weapon) const
{
    if (hasFlag("InfinityAttackRange"))
        return 999;

    int range = Sanguosha->correctAttackRange(this, include_weapon, true);
    if (range<0) range = 1;

    if (include_weapon&&hasSkill("benshi"))
        include_weapon = false;

    if (include_weapon){
		static QList<const Weapon *> weapons = Sanguosha->findChildren<const Weapon *>();
		foreach(const Weapon *w, weapons){
			if (hasWeapon(w->objectName())){
				if (range==1) range = w->getRange(this);
				else range = qMax(range,w->getRange(this));
			}
		}
    }
    range += Sanguosha->correctAttackRange(this, include_weapon, false);
    return qMax(range, 0);
}

bool Player::inMyAttackRange(const Player *other, int distance_fix, bool chengwu) const
{
    if (attack_range_pair.contains(other)) return true;
    if (this==other||property("BanAttackRange").toString().split("+").contains(other->objectName())) return false;

	if (other->property("inMyAttackRangeKingdoms").toString().contains(getKingdom()))
		return true;

	if (getKingdom() == "wu"){
		foreach(const Player *p, getAliveSiblings()){
			if (p->hasLordSkill("zhaofu") && p->distanceTo(other) == 1)
				return true;
		}
	}

    if (chengwu && hasLordSkill("jinchengwu")){
        foreach(const Player *p, getAliveSiblings()){
            if (p->getKingdom() == "jin" && p->inMyAttackRange(other, distance_fix, false))
                return true;
        }
    }

    return distanceTo(other, distance_fix) <= getAttackRange();
}

bool Player::inMyAttackRange(const Player *other, QList<int> card_ids, bool chengwu) const
{
    int distance_fix = 0;
	const EquipCard *card = getWeapon();
    if (card && card_ids.contains(card->getId())){
        const Weapon *w = qobject_cast<const Weapon *>(card);
        distance_fix += w->getRange(this) - getAttackRange(false);
    }
	card = getOffensiveHorse();
    if (card && card_ids.contains(card->getId())){
        const Horse *oh = qobject_cast<const Horse *>(card);
        distance_fix -= oh->getCorrect(this);
    }
    return inMyAttackRange(other, distance_fix, chengwu);
}

void Player::setFixedDistance(const Player *player, int distance)
{
    fixed_distance.insert(player, distance);
}

void Player::removeFixedDistance(const Player *player, int distance)
{
    fixed_distance.remove(player, distance);
}

void Player::insertAttackRangePair(const Player *player)
{
    attack_range_pair.append(player);
}

void Player::removeAttackRangePair(const Player *player)
{
    attack_range_pair.removeOne(player);
}

int Player::distanceTo(const Player *other, int distance_fix) const
{
	if (!other || this == other)
		return 0;
	if(other->seat<0)
		return 999;

	int right = Sanguosha->correctDistance(this, other, true);
	if (right>0) return right;

	if (fixed_distance.contains(other)){
		right = 999;
		foreach(int d, fixed_distance.values(other)){
			if (right > d) right = d;
		}
		return right;
	}

	right = qAbs(seat - other->seat);
	right = qMin(aliveCount() - right, right);

	right += Sanguosha->correctDistance(this, other);
	right += distance_fix;

	return qMax(right, 1);
}

void Player::setGeneral(const General *new_general)
{
    if (this->general != new_general){
        this->general = new_general;

        if (new_general && kingdom.isEmpty())
            setKingdom(new_general->getKingdom());

        emit general_changed();
    }
}

void Player::setGeneralName(const QString &general_name)
{
    const General *new_general = Sanguosha->getGeneral(general_name);
    //Q_ASSERT(general_name.isEmpty() || new_general);
    if(new_general) setGeneral(new_general);
}

QString Player::getGeneralName() const
{
    if (general)
        return general->objectName();
    return "";
}

void Player::setGeneral2Name(const QString &general_name)
{
    const General *new_general = Sanguosha->getGeneral(general_name);
    if (general2 != new_general){
        general2 = new_general;

        emit general2_changed();
    }
}

QString Player::getGeneral2Name() const
{
    if (general2)
        return general2->objectName();
    return "";
}

const General *Player::getGeneral2() const
{
    return general2;
}

QString Player::getState() const
{
    return state;
}

void Player::setState(const QString &state)
{
    if (this->state != state){
        this->state = state;
        emit state_changed();
    }
}

void Player::setRole(const QString &role)
{
    if (this->role != role){
        this->role = role;
        emit role_changed(role);
    }
}

QString Player::getRole() const
{
    return role;
}

Player::Role Player::getRoleEnum() const
{
    static QMap<QString, Role> role_map;
    if (role_map.isEmpty()){
        role_map.insert("lord", Lord);
        role_map.insert("loyalist", Loyalist);
        role_map.insert("rebel", Rebel);
        role_map.insert("renegade", Renegade);
    }
    return role_map.value(role);
}

const General *Player::getAvatarGeneral() const
{
    if (general) return general;
    return Sanguosha->getGeneral(property("avatar").toString());
}

const General *Player::getGeneral() const
{
    return general;
}

bool Player::isLord() const
{
    return getRole() == "lord";
}

bool Player::hasSkill(const QString &skill_name, bool include_lose) const
{
    if(skill_name.isEmpty()) return false;
	if(skills.contains(skill_name)||acquired_skills.contains(skill_name)
		||property("pingjian_triggerskill").toString()==skill_name){
		if(include_lose) return true;//||hasEquipSkill(skill_name)
		const Skill *skill = Sanguosha->getSkill(skill_name);
		if(skill) {
			bool valid = skill->isAttachedLordSkill()||skill->property("IgnoreInvalidity").toBool()
				||!skill->isVisible()||Sanguosha->correctSkillValidity(this,skill);
			QMutexLocker locker(&m_skillCacheMutex);
			m_skillValidityCache[skill_name] = valid;
			return valid;
		}
	}
    return false;
}

bool Player::hasSkill(const Skill *skill, bool include_lose) const
{
    //Q_ASSERT(skill != nullptr);
    return skill&&hasSkill(skill->objectName(), include_lose);
}

bool Player::hasSkills(const QString &skill_name, bool include_lose) const
{
    foreach(QString skill, skill_name.split("|")){
        bool checkpoint = true;
        foreach(QString sk, skill.split("+")){
			checkpoint = hasSkill(sk, include_lose);
            if (!checkpoint) break;
        }
        if (checkpoint) return true;
    }
    return false;
}

bool Player::hasInnateSkill(const QString &skill_name) const
{
    if (general && general->hasSkill(skill_name))
        return true;

    return general2 && general2->hasSkill(skill_name);
}

bool Player::hasInnateSkill(const Skill *skill) const
{
    return skill&&hasInnateSkill(skill->objectName());
}

bool Player::hasLordSkill(const QString &skill_name, bool include_lose) const
{
	if(isLord()){
		static QString ban_gm = QString("06_3v3|06_XMode|02_1v1|03_1v2");
		if(ServerInfo.EnableHegemony||ban_gm.contains(ServerInfo.GameMode)
		||Config.value("WithoutLordskill").toBool()) return false;
		return hasSkill(skill_name,include_lose);
	}else{
		if (hasSkill("weidi")){
			foreach(const Player *player, getAliveSiblings()){
				if (player->isLord()&&player->hasLordSkill(skill_name,true))
					return true;
			}
		}
		return acquired_skills.contains(skill_name)&&hasSkill(skill_name,include_lose);
	}
}

bool Player::hasLordSkill(const Skill *skill, bool include_lose) const
{
    return skill&&hasLordSkill(skill->objectName(), include_lose);
}

void Player::acquireSkill(const QString &skill_name)
{
    if(acquired_skills.contains(skill_name)) return;
    acquired_skills << skill_name;
}

void Player::detachSkill(const QString &skill_name)
{
    acquired_skills.removeOne(skill_name);
}

void Player::detachAllSkills()
{
    acquired_skills.clear();
}

void Player::addSkill(const QString &skill_name)
{
    if(skills.contains(skill_name)) return;
    skills << skill_name;
}

void Player::loseSkill(const QString &skill_name)
{
    skills.removeOne(skill_name);
}

QString Player::getPhaseString() const
{
    switch (phase){
    case RoundStart: return "round_start";
    case Start: return "start";
    case Judge: return "judge";
    case Draw: return "draw";
    case Play: return "play";
    case Discard: return "discard";
    case Finish: return "finish";
    case NotActive:
    default:
        return "not_active";
    }
}

void Player::setPhaseString(const QString &phase_str)
{
    static QMap<QString, Phase> phase_map;
    if (phase_map.isEmpty()){
        phase_map.insert("round_start", RoundStart);
        phase_map.insert("start", Start);
        phase_map.insert("judge", Judge);
        phase_map.insert("draw", Draw);
        phase_map.insert("play", Play);
        phase_map.insert("discard", Discard);
        phase_map.insert("finish", Finish);
        phase_map.insert("not_active", NotActive);
    }
    setPhase(phase_map.value(phase_str, NotActive));
}

static bool CompareByLocation(const EquipCard *a, const EquipCard *b)
{
    return a->location() <= b->location();
}

void Player::setEquip(const Card *equip)
{
    //const EquipCard *card = qobject_cast<const EquipCard *>(equip->getRealCard());
    //Q_ASSERT(card != nullptr);
	equips << qobject_cast<const EquipCard *>(equip->getRealCard());
	/*switch (card->location()){
    case EquipCard::WeaponLocation: weapon = equip; break;
    case EquipCard::ArmorLocation: armor = equip; break;
    case EquipCard::DefensiveHorseLocation: defensive_horse = equip; break;
    case EquipCard::OffensiveHorseLocation: offensive_horse = equip; break;
    case EquipCard::TreasureLocation: treasure = equip; break;
    }*/
	if (equips.length()>1)
		std::stable_sort(equips.begin(), equips.end(), CompareByLocation);
}

void Player::removeEquip(const Card *equip)
{
    //const EquipCard *card = qobject_cast<const EquipCard *>(Sanguosha->getEngineCard(equip->getId()));
    //const EquipCard *card = qobject_cast<const EquipCard *>(equip->getRealCard());
    //Q_ASSERT(card != nullptr);
    foreach(const EquipCard *e, equips){
        if (e->getId()==equip->getId())
			equips.removeOne(e);
    }
	/*switch (card->location()){
    case EquipCard::WeaponLocation: weapon = nullptr; break;
    case EquipCard::ArmorLocation: armor = nullptr; break;
    case EquipCard::DefensiveHorseLocation: defensive_horse = nullptr; break;
    case EquipCard::OffensiveHorseLocation: offensive_horse = nullptr; break;
    case EquipCard::TreasureLocation: treasure = nullptr; break;
    }*/
}

bool Player::hasEquip(const Card *card) const
{
    //Q_ASSERT(card != nullptr);
    QList<int> ids;
    if (card->isVirtualCard())
        ids << card->getSubcards();
    else ids << card->getId();
    if (ids.isEmpty()) return false;
    foreach(const EquipCard *e, equips){
        if (ids.contains(e->getId()))
            return true;
    }
    return false;
}

bool Player::hasEquip() const
{
    return !equips.isEmpty();//weapon != nullptr || armor != nullptr || defensive_horse != nullptr || offensive_horse != nullptr || treasure != nullptr;
}

QList<int> Player::getEquipRealSlots(int card_id) const
{
    const Card *card = Sanguosha->getCard(card_id);
    if (card) {
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        if (equip) {
            return equip->getOccupyLocations();
        }
    }
    return QList<int>();
}

QList<const EquipCard *> Player::getWeapons() const
{
    QList<const EquipCard *> _equips;
    foreach(const EquipCard *e, equips){
        if (getEquipRealSlots(e->getEffectiveId()).contains(0))
            _equips << e;
    }
    return _equips;
}

QList<const EquipCard *> Player::getArmors() const {
    QList<const EquipCard *> _equips;
    foreach(const EquipCard *e, equips){
        if (getEquipRealSlots(e->getEffectiveId()).contains(1)) _equips << e;
    }
    return _equips;
}

QList<const EquipCard *> Player::getDefensiveHorses() const {
    QList<const EquipCard *> _equips;
    foreach(const EquipCard *e, equips){
        if (getEquipRealSlots(e->getEffectiveId()).contains(2)) _equips << e;
    }
    return _equips;
}

QList<const EquipCard *> Player::getOffensiveHorses() const {
    QList<const EquipCard *> _equips;
    foreach(const EquipCard *e, equips){
        if (getEquipRealSlots(e->getEffectiveId()).contains(3)) _equips << e;
    }
    return _equips;
}

QList<const EquipCard *> Player::getTreasures() const {
    QList<const EquipCard *> _equips;
    foreach(const EquipCard *e, equips){
        if (getEquipRealSlots(e->getEffectiveId()).contains(4)) _equips << e;
    }
    return _equips;
}

bool Player::hasWeapons() const { return !getWeapons().isEmpty(); }
bool Player::hasArmors() const { return !getArmors().isEmpty(); }
bool Player::hasDefensiveHorses() const { return !getDefensiveHorses().isEmpty(); }
bool Player::hasOffensiveHorses() const { return !getOffensiveHorses().isEmpty(); }
bool Player::hasTreasures() const { return !getTreasures().isEmpty(); }

int Player::getWeaponsCount() const { return getWeapons().length(); }
int Player::getArmorsCount() const { return getArmors().length(); }
int Player::getDefensiveHorsesCount() const { return getDefensiveHorses().length(); }
int Player::getOffensiveHorsesCount() const { return getOffensiveHorses().length(); }
int Player::getTreasuresCount() const { return getTreasures().length(); }

const EquipCard *Player::getWeapon() const
{
    foreach(const EquipCard *e, equips){
        if (getEquipRealSlots(e->getEffectiveId()).contains(0)) return e;
    }
	return nullptr;
}

const EquipCard *Player::getArmor() const
{
    foreach(const EquipCard *e, equips){
        if (getEquipRealSlots(e->getEffectiveId()).contains(1)) return e;
    }
	return nullptr;
}

const EquipCard *Player::getDefensiveHorse() const
{
    foreach(const EquipCard *e, equips){
        if (getEquipRealSlots(e->getEffectiveId()).contains(2)) return e;
    }
    return nullptr;
}

const EquipCard *Player::getOffensiveHorse() const
{
    foreach(const EquipCard *e, equips){
        if (getEquipRealSlots(e->getEffectiveId()).contains(3)) return e;
    }
    return nullptr;
}

const EquipCard *Player::getTreasure() const
{
    foreach(const EquipCard *e, equips){
        if (getEquipRealSlots(e->getEffectiveId()).contains(4)) return e;
    }
    return nullptr;
}

QList<const Card *> Player::getEquips(int index) const
{
    QList<const Card *> _equips;
    foreach(const EquipCard *e, equips){
        if (index<0||e->location()==index)
			_equips << e;
    }/*
    if (weapon)
        equips << weapon;
    if (armor)
        equips << armor;
    if (defensive_horse)
        equips << defensive_horse;
    if (offensive_horse)
        equips << offensive_horse;
    if (treasure)
        equips << treasure;*/
    return _equips;
}

QList<int> Player::getEquipsId() const
{
    QList<int> _equips;
    foreach(const EquipCard *e, equips)
        _equips << e->getId();
    return _equips;
}

const EquipCard *Player::getEquip(int index) const
{
    foreach(const EquipCard *e, equips){
        if (e->location()==index) return e;
    }/*
    WrappedCard *equip;
    switch (index){
    case 0: equip = weapon; break;
    case 1: equip = armor; break;
    case 2: equip = defensive_horse; break;
    case 3: equip = offensive_horse; break;
    case 4: equip = treasure; break;
    default:
        return nullptr;
    }
    if (equip != nullptr)
        return qobject_cast<const EquipCard *>(equip->getRealCard());*/
    return nullptr;
}

bool Player::viewAsEquip(const QString &equip_name) const
{
    QString view = property("View_As_Equips_List").toString();
    if (view!=""&&view.split("+").contains(equip_name)) return true;
    foreach(QString skill_name, skills + acquired_skills){
		const ViewAsEquipSkill *vaes = Sanguosha->getViewAsEquipSkill(skill_name);
		if(vaes==nullptr) continue;
		view = vaes->viewAsEquip(this);
		if(view!=""&&view.split(",").contains(equip_name)&&hasSkill(skill_name))
			return true;
    }/*
	static QList<const EquipCard *> Equips = Sanguosha->findChildren<const EquipCard *>();
	foreach(const EquipCard *equip, Equips){
		if (equip->objectName()==equip_name||equip->getClassName()==equip_name)
			return view_as_equips.contains(equip->getClassName())||view_as_equips.contains(equip->objectName());
	}*/
    return false;
}

bool Player::isLocked(const Card *card, bool isHandcard) const
{
	if(Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return isCardLimited(card, Card::MethodResponse, isHandcard);
	return isCardLimited(card, Card::MethodUse, isHandcard);
}

void Player::addEquipsNullified(const QString &pattern, bool single_turn)
{
	setCardLimitation("effect",pattern,single_turn);
}

void Player::removeEquipsNullified(const QString &pattern, bool single_turn)
{
	QString _pattern = pattern;
	if (!_pattern.endsWith("$1") && !_pattern.endsWith("$0"))
		_pattern = single_turn?_pattern+"$1":_pattern+"$0";
	removeCardLimitation("effect",_pattern);
}

bool Player::isEquipsNullified(const Card *card) const
{
    return isCardLimited(card,Card::MethodEffect);
}

bool Player::hasWeapon(const QString &weapon_name, bool need_area) const
{
    if (!alive||(need_area&&getMark("IgnoreArea0")<1&&!hasEquipArea(0))) return false;
	static QStringList w_equips;
	if(w_equips.isEmpty()){
		foreach(const Weapon*w,Sanguosha->findChildren<const Weapon*>())
			w_equips << w->objectName();
	}
	if(w_equips.contains(weapon_name)){
		foreach(const EquipCard*w,equips){
			if(w->objectName()==weapon_name&&!isEquipsNullified(w))
				return true;
		}
		if(viewAsEquip(weapon_name)){
			Card *ec = Sanguosha->cloneCard(weapon_name);
			ec->deleteLater();
			return !isEquipsNullified(ec);
		}
	}else{
		foreach(const EquipCard*w,equips){
			if(w->inherits("Weapon")&&w->inherits(weapon_name.toStdString().c_str()))
				return !isEquipsNullified(w);
		}
	}
	return false;
}

bool Player::hasArmorEffect(const QString &armor_name, bool need_area) const
{
	if (!alive||(need_area&&getMark("IgnoreArea1")<1&&!hasEquipArea(1))||getMark("Armor_Nullified")>0)
        return false;
	static QStringList a_equips;
	if(a_equips.isEmpty()){
		foreach(const Armor*a,Sanguosha->findChildren<const Armor*>())
			a_equips << a->objectName();
	}
    if (armor_name.isEmpty()){
		foreach(const EquipCard*a,equips){
			if(a->inherits("Armor")&&!isEquipsNullified(a))
				return true;
		}
		QStringList view_as_equips = property("View_As_Equips_List").toString().split("+");
		foreach(QString skill_name,skills+acquired_skills){
			const ViewAsEquipSkill *vaes = Sanguosha->getViewAsEquipSkill(skill_name);
			if(vaes==nullptr) continue;
			QString cns = vaes->viewAsEquip(this);
			if(cns!=""&&hasSkill(skill_name))
				view_as_equips << cns.split(",");
		}
		foreach(QString an,view_as_equips){
			if(a_equips.contains(an)){
				Card *ac = Sanguosha->cloneCard(an);
				ac->deleteLater();
				if(!isEquipsNullified(ac))
					return true;
			}
		}
    } else if(a_equips.contains(armor_name)){
		foreach(const EquipCard*a,equips){
			if(a->objectName()==armor_name&&!isEquipsNullified(a))
				return true;
		}
		if(viewAsEquip(armor_name)){
			Card *ec = Sanguosha->cloneCard(armor_name);
			ec->deleteLater();
			return !isEquipsNullified(ec);
		}
    }
	return false;
}

bool Player::hasDefensiveHorse(const QString &horse_name, bool need_area) const
{
    if (!alive||(need_area&&getMark("IgnoreArea2")<1&&!hasEquipArea(2))) return false;
	static QStringList d_equips;
	if(d_equips.isEmpty()){
		foreach(const DefensiveHorse*d,Sanguosha->findChildren<const DefensiveHorse*>())
			d_equips << d->objectName();
	}
	if(d_equips.contains(horse_name)){
		foreach(const EquipCard*h,equips){
			if(h->objectName()==horse_name&&!isEquipsNullified(h))
				return true;
		}
		if(viewAsEquip(horse_name)){
			Card*ec = Sanguosha->cloneCard(horse_name);
			ec->deleteLater();
			return !isEquipsNullified(ec);
		}
	}
	return false;
}

bool Player::hasOffensiveHorse(const QString &horse_name, bool need_area) const
{
    if (!alive||(need_area&&getMark("IgnoreArea3")<1&&!hasEquipArea(3))) return false;
	static QStringList o_equips;
	if(o_equips.isEmpty()){
		foreach(const OffensiveHorse*o,Sanguosha->findChildren<const OffensiveHorse*>())
			o_equips << o->objectName();
	}
	if(o_equips.contains(horse_name)){
		foreach(const EquipCard*h,equips){
			if(h->objectName()==horse_name&&!isEquipsNullified(h))
				return true;
		}
		if(viewAsEquip(horse_name)){
			Card*ec = Sanguosha->cloneCard(horse_name);
			ec->deleteLater();
			return !isEquipsNullified(ec);
		}
	}
	return false;
}

bool Player::hasTreasure(const QString &treasure_name, bool need_area) const
{
    if (!alive||(need_area&&getMark("IgnoreArea4")<1&&!hasEquipArea(4))) return false;
	static QStringList t_equips;
	if(t_equips.isEmpty()){
		foreach(const Treasure*t,Sanguosha->findChildren<const Treasure*>())
			t_equips << t->objectName();
	}
	if(t_equips.contains(treasure_name)){
		foreach(const EquipCard*t,equips){
			if(t->objectName()==treasure_name&&!isEquipsNullified(t))
				return true;
		}
		if(viewAsEquip(treasure_name)){
			Card*ec = Sanguosha->cloneCard(treasure_name);
			ec->deleteLater();
			return !isEquipsNullified(ec);
		}
	}
	return false;
}

QList<const Card *> Player::getJudgingArea() const
{
    return judging_area;
}

QList<int> Player::getJudgingAreaID() const
{
    QList<int>ids;
    foreach(const Card *j, judging_area)
        ids << j->getId();
    return ids;
}

Player::Phase Player::getPhase() const
{
    return phase;
}

void Player::setPhase(Phase phase)
{
    this->phase = phase;
    emit phase_changed();
}

bool Player::faceUp() const
{
    return face_up;
}

void Player::setFaceUp(bool face_up)
{
    if (this->face_up != face_up){
        this->face_up = face_up;
        emit state_changed();
    }
}

int Player::getMaxCards() const
{
    int origin = Sanguosha->correctMaxCards(this, true);
    if (origin < 0) origin = qMax(hp, 0);
    if (general2&&Config.MaxHpScheme==3){
        if (getMark("AwakenLostMaxHp")<1&&(general->getMaxHp()+general2->getMaxHp())%2!=0)
            origin++;
    }
    return qMax(origin+Sanguosha->correctMaxCards(this),0);
}

QString Player::getKingdom() const
{
	if(kingdom.isEmpty()){
		if(general) return general->getKingdom();
	}else{
		if(kingdom.contains("+")){
			QStringList kins = kingdom.split("+");
			foreach(QString king, kins)
				if (king != "god") return king;
			return kins.first();
		}
	}
    return kingdom;
}

void Player::setKingdom(const QString &kingdom)
{
    QString _kingdom = kingdom.split("+").first();
    if (this->kingdom != _kingdom){
        this->kingdom = _kingdom;
        emit general_changed();
    }
}

bool Player::isKongcheng() const
{
    return getHandcardNum() == 0;
}

bool Player::isNude() const
{
    return isKongcheng() && equips.isEmpty();
}

bool Player::isAllNude() const
{
    return isNude() && judging_area.isEmpty();
}

bool Player::canDiscard(const Player *to, const QString &flags) const
{
    if (!to || isDead() || to->isDead()) return false;
    if (flags.contains("h")){
		foreach(const Card *h, to->getHandcards())
			if (canDiscard(to,h->getId())) return true;
	}
    if (flags.contains("e")){
		foreach(const Card *e, to->getEquips())
			if (canDiscard(to,e->getId())) return true;
	}
    if (flags.contains("j")){
		foreach(const Card *j, to->getJudgingArea())
			if (canDiscard(to,j->getId())) return true;
	}
    return false;
}

bool Player::canDiscard(const Player *to, int card_id) const
{
	if (this==to) return !isCardLimited(Sanguosha->getCard(card_id),Card::MethodDiscard);
	return Sanguosha->isCardLimited(this,Sanguosha->getCard(card_id),Card::MethodDiscard)==nullptr;
}

bool Player::canDiscard(const QString &flags, const Player *to) const
{
	if(to==nullptr) to = this;
	return canDiscard(to,flags);
}

bool Player::canDiscard(int card_id, const Player *to) const
{
	if(to==nullptr) to = this;
	return canDiscard(to,card_id);
}

void Player::addDelayedTrick(const Card *trick)
{
    judging_area << trick;
}

void Player::removeDelayedTrick(const Card *trick)
{
    foreach(const Card *j, judging_area){
        if (j->getId() == trick->getId())
            judging_area.removeOne(j);
    }
}

bool Player::containsTrick(const QString &trick_name) const
{
    foreach(const Card *trick, judging_area){
        if (trick->objectName() == trick_name)
            return true;
    }
    return false;
}

bool Player::isChained() const
{
    return chained;
}

void Player::setChained(bool chained)
{
    if (this->chained != chained){
        this->chained = chained;
        emit state_changed();
    }
}

void Player::addMark(const QString &mark, int add_num)
{
    setMark(mark, getMark(mark)+add_num);
}

void Player::removeMark(const QString &mark, int remove_num)
{
    setMark(mark, qMax(0, getMark(mark)-remove_num));
}

void Player::setMark(const QString &mark, int value)
{
    if(value==0) marks.remove(mark);
	else marks[mark] = value;
}

int Player::getMark(const QString &mark) const
{
    return marks.value(mark, 0);
}

int Player::getHujia() const
{
    return getMark("@HuJia");
}

QStringList Player::getMarkNames() const
{
    return marks.keys();
}

bool Player::canSlash(const Player *other, const Card *slash, bool distance_limit,
    int rangefix, const QList<const Player *> &others) const
{
	if (!other || !other->isAlive())
		return false;
	if (!slash){
		Slash *newslash = new Slash(Card::NoSuit, 0);
		newslash->deleteLater();
		slash = newslash;
	}
	if (isCardLimited(slash,Card::MethodUse) || isProhibited(other, slash, others)) return false;
	if (attack_range_pair.contains(other)) return true;
	if (distance_limit){
		rangefix -= Sanguosha->correctCardTarget(TargetModSkill::DistanceLimit, this, slash, other);
		if (rangefix>-500) return inMyAttackRange(other, rangefix);
	}
	return this != other;
}

bool Player::canSlash(const Player *other, bool distance_limit, int rangefix, const QList<const Player *> &others) const
{
    return canSlash(other, nullptr, distance_limit, rangefix, others);
}

int Player::getCardCount(bool include_equip, bool include_judging) const
{
    int count = getHandcardNum();
    if (include_equip) count += equips.length();
    if (include_judging) count += judging_area.length();
    return count;
}

QList<int> Player::getPile(const QString &pile_name) const
{
    return piles[pile_name];
}

QStringList Player::getPileNames() const
{
    /*QStringList names;
    foreach(QString pile_name, piles.keys())
        names.append(pile_name);*/
    return piles.keys();
}

QString Player::getPileName(int card_id) const
{
    foreach(QString pile_name, piles.keys()){
        if (piles[pile_name].contains(card_id))
            return pile_name;
    }
    foreach(const Card *h, handcards){
        if (h->getId() == card_id)
            return "hand_area";
    }
    foreach(const EquipCard *e, equips){
        if (e->getId() == card_id)
            return "equip_area";
    }
    foreach(const Card *j, judging_area){
        if (j->getId() == card_id)
            return "judge_area";
    }
    return "";
}

bool Player::pileOpen(const QString &pile_name, const QString &player) const
{
    return pile_open[pile_name].contains(player);
}

void Player::setPileOpen(const QString &pile_name, const QString &player)
{
    if(player==".") pile_open[pile_name].clear();
	else pile_open[pile_name].append(player);
}

QList<int> Player::getHandPile() const
{
    QList<int> result;
    foreach(QString pile, getPileNames()){
        if (pile=="wooden_ox"||pile.startsWith("&"))
			result << getPile(pile);
    }
    return result;
}

void Player::addHistory(const QString &name, int times)
{
    history[name] += times;
}

int Player::getSlashCount() const
{/*
    return history.value("Slash", 0)
        + history.value("ThunderSlash", 0)
        + history.value("FireSlash", 0)
        + history.value("IceSlash", 0);*/
    static QStringList classnames;
	if(classnames.isEmpty()){
		foreach(const Slash *slash, Sanguosha->findChildren<const Slash *>()){
			if (classnames.contains(slash->getClassName())) continue;
			classnames << slash->getClassName();
		}
	}
    int count = 0;
    foreach(QString classname, classnames)
        count += history.value(classname, 0);
    return count;
}

void Player::clearHistory(const QString &name)
{
    if (name.isEmpty()) history.clear();
    else history.remove(name);
}

bool Player::hasUsed(const QString &card_class, bool actual) const
{
    return usedTimes(card_class, actual) > 0;
}

int Player::usedTimes(const QString &card_class, bool actual) const
{
    if (!actual){
        if (property("AllSkillNoLimitingTimes").toBool()) return 0;
        if (property("SkillNoLimitingTimes").toString().split("+").contains(card_class)) return 0;
    }
    return history.value(card_class, 0);
}

bool Player::hasEquipSkill(const QString &skill_name) const
{
	static QStringList equipsName;
	if(equipsName.isEmpty()){
		foreach(const EquipCard*ec, Sanguosha->findChildren<const EquipCard*>()){
			if(equipsName.contains(ec->objectName())||Sanguosha->getSkill(ec)==nullptr) continue;
			equipsName.append(ec->objectName());
		}
	}
    return equipsName.contains(skill_name)&&(acquired_skills.contains(skill_name)||viewAsEquip(skill_name));
}

QSet<const TriggerSkill *> Player::getTriggerSkills() const
{
    QSet<const TriggerSkill *> skillList;
    foreach(QString skill_name, skills + acquired_skills){
        if(hasEquipSkill(skill_name)) continue;
		const TriggerSkill *skill = Sanguosha->getTriggerSkill(skill_name);
        if (skill) skillList << skill;
    }
    return skillList;
}

QSet<const Skill *> Player::getSkills(bool include_equip, bool visible_only) const
{
    QList<const Skill *> skills = getSkillList(include_equip, visible_only);
    return QSet<const Skill *>(skills.begin(), skills.end());
}

QList<const Skill *> Player::getSkillList(bool include_equip, bool visible_only) const
{
    QList<const Skill *> skillList;
    foreach(QString skill_name, skills + acquired_skills){
        if(!include_equip&&hasEquipSkill(skill_name)) continue;
		const Skill *skill = Sanguosha->getSkill(skill_name);
		if(skill==nullptr||(visible_only&&!skill->isVisible())) continue;
        skillList << skill;
    }
    return skillList;
}

QSet<const Skill *> Player::getVisibleSkills(bool include_equip) const
{
    QList<const Skill *> skills = getVisibleSkillList(include_equip);
    return QSet<const Skill *>(skills.begin(), skills.end());
}

QList<const Skill *> Player::getVisibleSkillList(bool include_equip) const
{
    return getSkillList(include_equip, true);
}

QStringList Player::getAcquiredSkills() const
{
    return acquired_skills;
}

QString Player::getSkillDescription() const
{
    QString description;
    QStringList relateds;
	const General *general = getGeneral();
	if (general&&general->objectName()!="anjiang")
		relateds << general->getRelatedSkillNames();
	general = getGeneral2();
	if (general&&general->objectName()!="anjiang")
		relateds << general->getRelatedSkillNames();
    QList<const Skill *> basara_list;
    if (getGeneralName() == "anjiang" || getGeneral2Name() == "anjiang"){
        foreach(QString basara_gen, property("basara_generals").toString().split("+")){
			general = Sanguosha->getGeneral(basara_gen);
			if (general) basara_list.append(general->getVisibleSkillList());
        }
    }
    foreach(const Skill *skill, getVisibleSkillList()){
        if (skill->isAttachedLordSkill() || basara_list.contains(skill))
            continue;
        QString desc = skill->getDescription(this);
		{
			// Use cached skill validity to avoid calling into Lua from UI thread
			bool skillOwned = skills.contains(skill->objectName()) || acquired_skills.contains(skill->objectName())
				|| property("pingjian_triggerskill").toString() == skill->objectName();
			bool skillValid = true;
			if (skillOwned) {
				if (!skill->isAttachedLordSkill() && !skill->property("IgnoreInvalidity").toBool() && skill->isVisible()) {
					QMutexLocker locker(&m_skillCacheMutex);
					skillValid = m_skillValidityCache.value(skill->objectName(), true);
				}
			} else {
				skillValid = false;
			}
			if (!skillValid) desc = "<font color=\"#bab8ba\">" + desc + "</font>";
		}
        description += QString("<b>%1</b>：%2<br/><br/>").arg(Sanguosha->translate(skill->objectName())).arg(desc);
		desc = skill->getWakedSkills();
		if(desc.isEmpty()) continue;
		relateds << desc.split(",");
    }
	while(relateds.length()>0){
		QString sn = relateds.first();
		relateds.removeAll(sn);
		if(hasSkill(sn, true)) continue;
		const Skill *sk = Sanguosha->getSkill(sn);
		if (sk&&sk->isVisible())
			description += QString("<font color=\"#01A5AF\"><b>%1</b>：%2</font><br/><br/>").arg(Sanguosha->translate(sn)).arg(sk->getDescription(this));
	}
    if (description.isEmpty()) description = tr("No skills");
	else description.replace("\n", "<br/>");
    return description;
}

bool Player::isProhibited(const Player *to, const Card *card, const QList<const Player *> &others) const
{
    return Sanguosha->isProhibited(this, to, card, others);
}

bool Player::isPindianProhibited(const Player *to) const
{
    return Sanguosha->isPindianProhibited(this, to);
}

bool Player::canSlashWithoutCrossbow(const Card *slash) const
{
	if(!slash){
		Slash *newslash = new Slash(Card::NoSuit, 0);
		newslash->addSubcards(getEquips(0));
		newslash->deleteLater();
		slash = newslash;
	}
    int slash_count = getSlashCount();
    foreach(const Player *p, getAliveSiblings()){
        if (slash_count<=Sanguosha->correctCardTarget(TargetModSkill::Residue,this,slash,p))
            return true;
    }
    return false;
}

void Player::setCardLimitation(const QString &limit_list, const QString &pattern, bool single_turn)
{
    QString _pattern = pattern;
    if (!pattern.contains("$"))
        _pattern.append(single_turn ? "$1" : "$0");
    foreach(QString limit, limit_list.split(","))
        card_limitation[Sanguosha->getCardHandlingMethod(limit)] << _pattern;
}

void Player::removeCardLimitation(const QString &limit_list, const QString &pattern)
{
    QString _pattern = pattern;
    if (!pattern.contains("$"))
        _pattern.append("$0");
    foreach(QString limit, limit_list.split(","))
        card_limitation[Sanguosha->getCardHandlingMethod(limit)].removeOne(_pattern);
}

void Player::clearCardLimitation(bool single_turn)
{
    /*static QList<Card::HandlingMethod> limit_type;
	if(limit_type.isEmpty())
		limit_type << Card::MethodUse << Card::MethodResponse << Card::MethodDiscard
        << Card::MethodRecast << Card::MethodPindian << Card::MethodIgnore << Card::MethodEffect;*/
    foreach(Card::HandlingMethod method, card_limitation.keys()){
        foreach(QString pattern, card_limitation[method]){
            if (!single_turn || pattern.endsWith("$1")){
                card_limitation[method].removeOne(pattern);
				if(card_limitation[method].isEmpty())
					card_limitation.remove(method);
			}
        }
    }
}

bool Player::isCardLimited(const Card *card, Card::HandlingMethod method, bool isHandcard) const
{
    if (method == Card::MethodNone) return false;
    if (card->inherits("SkillCard") && method == card->getHandlingMethod()){
        foreach(int card_id, card->getSubcards()){
            const Card *c = Sanguosha->getCard(card_id);
            foreach(QString pattern, card_limitation[method]){
				pattern.chop(2);
                if (isHandcard) pattern.replace("hand", ".");
                if (Sanguosha->matchExpPattern(pattern,this, c)) return true;
            }
        }
    } else {
        if(method==Card::MethodUse&&card->inherits("Peach")&&getMark("Global_PreventPeach")>0)
			return true;
		foreach(QString pattern, card_limitation[method]){
			pattern.chop(2);
            if (isHandcard) pattern.replace("hand", ".");
            if (Sanguosha->matchExpPattern(pattern,this, card)) return true;
        }
    }
    return Sanguosha->isCardLimited(this, card, method, isHandcard)!=nullptr;
}

void Player::addQinggangTag(const Card *card)
{
    QStringList qinggang = tag["Qinggang"].toStringList();
    qinggang.append(card->toString());
    tag["Qinggang"] = qinggang;
	addEquipsNullified("Armor");
}

void Player::removeQinggangTag(const Card *card)
{
    QStringList qinggang = tag["Qinggang"].toStringList();
	foreach(QString qg, qinggang){
		if(qg==card->toString()){
			qinggang.removeOne(qg);
			removeEquipsNullified("Armor");
			tag["Qinggang"] = qinggang;
		}
	}
}

void Player::copyFrom(Player *p)
{
    marks = QMap<QString, int>(p->marks);
    piles = QMap<QString, QList<int> >(p->piles);
    acquired_skills = QStringList(p->acquired_skills);
    flags = QSet<QString>(p->flags);
    history = QHash<QString, int>(p->history);
    m_gender = p->m_gender;

    hp = p->hp;
    max_hp = p->max_hp;
    kingdom = p->kingdom;
    role = p->role;
    seat = p->seat;
    alive = p->alive;

    phase = p->phase;/*
    weapon = p->weapon;
    armor = p->armor;
    defensive_horse = p->defensive_horse;
    offensive_horse = p->offensive_horse;
    treasure = p->treasure;*/
    equip_area = p->equip_area;
    equips = p->equips;
    face_up = p->face_up;
    chained = p->chained;
    judging_area = QList<const Card*>(p->judging_area);
    fixed_distance = QMultiHash<const Player *, int>(p->fixed_distance);
    card_limitation = QMap<Card::HandlingMethod, QStringList>(p->card_limitation);

    tag = QVariantMap(p->tag);
}

QList<const Player *> Player::getSiblings(bool include_self) const
{
    QList<const Player *> siblings;
    if (parent()){
        siblings = parent()->findChildren<const Player *>();
        if (include_self) return siblings;
		siblings.removeOne(this);
    }
    return siblings;
}

QList<const Player *> Player::getAliveSiblings(bool include_self) const
{
    QList<const Player *> siblings = getSiblings(include_self);
    foreach(const Player *p, siblings){
        if (p->isAlive()) continue;
		siblings.removeOne(p);
    }
    return siblings;
}

bool Player::isNostalGeneral(const Player *p, const QString &general_name)
{
    static QStringList nostalMark;
    if (nostalMark.isEmpty())
        nostalMark << "nos_" << "tw_";
    foreach(QString s, nostalMark){
        QString nostalName = s + general_name;
        if (p->getGeneralName().contains(nostalName) || p->getGeneral2Name().contains(nostalName))
            return true;
    }

    return false;
}

bool Player::hasEquipArea(int i) const
{
    if (i == 0) return hasWeaponArea();
    else if (i == 1) return hasArmorArea();
    else if (i == 2) return hasDefensiveHorseArea();
    else if (i == 3) return hasOffensiveHorseArea();
    else if (i == 4) return hasTreasureArea();
    return false;
}

bool Player::hasEquipArea() const
{
    foreach(int i, equip_area){
		if(hasEquipArea(i))
			return true;
	}
	return false;
}

bool Player::hasWeaponArea() const
{
    return equip_area.contains(0)&&weapon_area;
}

void Player::setWeaponArea(bool flag)
{
	setEquipArea(0,flag);
}

bool Player::hasArmorArea() const
{
    return equip_area.contains(1)&&armor_area;
}

void Player::setArmorArea(bool flag)
{
	setEquipArea(1,flag);
}

bool Player::hasDefensiveHorseArea() const
{
    return equip_area.contains(2)&&defensive_horse_area;
}

void Player::setDefensiveHorseArea(bool flag)
{
	setEquipArea(2,flag);
}

bool Player::hasOffensiveHorseArea() const
{
    return equip_area.contains(3)&&offensive_horse_area;
}

void Player::setOffensiveHorseArea(bool flag)
{
	setEquipArea(3,flag);
}

bool Player::hasTreasureArea() const
{
    return equip_area.contains(4)&&treasure_area;
}

void Player::setTreasureArea(bool flag)
{
	setEquipArea(4,flag);
}

void Player::setEquipArea(int i, bool flag)
{
    if (flag){
		if (!equip_area.contains(i)) equip_area << i;
	}else equip_area.removeOne(i);
    weapon_area = equip_area.contains(0);
    armor_area = equip_area.contains(1);
    defensive_horse_area = equip_area.contains(2);
    offensive_horse_area = equip_area.contains(3);
    treasure_area = equip_area.contains(4);
}

void Player::addEquipArea(int i)
{
    equip_area << i;
}

int Player::getEquipArea(int i)
{
    int n = 0;
	foreach(int ea, equip_area){
		if (i<0||ea==i) n++;
	}
	return n;
}

bool Player::hasJudgeArea() const
{
    return hasjudgearea;
}

void Player::setJudgeArea(bool flag)
{
    this->hasjudgearea = flag;
}

bool Player::canPindian(const Player *target, bool except_self) const
{
    if (isKongcheng()) return false;
    if(target){
		if (except_self && this == target) return false;
		if (target->isKongcheng()) return false;
		return !isPindianProhibited(target);
	}else{
		foreach(const Player *p, getAliveSiblings(!except_self)){
			if (canPindian(p))
				return true;
		}
	}
    return false;
}
/*
bool Player::canPindian(bool except_self) const
{
    if (isDead() || isKongcheng()) return false;
    foreach(const Player *p, getAliveSiblings(!except_self)){
        if (canPindian(p, except_self))
            return true;
    }
    return false;
}*/

bool Player::canBePindianed(bool except_self) const
{
    if (isKongcheng()) return false;
    foreach(const Player *p, getAliveSiblings(!except_self)){
        if (p->canPindian(this))
            return true;
    }
    return false;
}

bool Player::isYourFriend(const Player *fri) const
{
    if (role=="renegade"||fri->getRole()=="renegade") return this == fri;
    if (role.isEmpty()||fri->getRole().isEmpty()) return false;
    return fri->getRole().startsWith(role.at(0));
}

int Player::getChangeSkillState(const QString &skill_name) const
{
    QString str = "ChangeSkill_" + skill_name + "_State";
    int n = property(str.toStdString().c_str()).toInt();
    if (n <= 0) n = 1;
    return n;
}

bool Player::hasCard(const Card *card) const
{
    return card && hasCard(card->getEffectiveId());
}

bool Player::hasCard(int id) const
{
	foreach(const Card *card, getHandcards())
		if (card->getId() == id) return true;
	return getEquipsId().contains(id);
}

QList<int> Player::handCards() const
{
    QList<int> cardIds;
    foreach(const Card *card, getHandcards())
        cardIds << card->getId();
    return cardIds;
}

QList<int> Player::getdrawPile() const
{
    QObject *room = Sanguosha->currentRoomObject();
	if(room){
		Room *serverRoom = qobject_cast<Room *>(room);
		if(serverRoom) return serverRoom->getDrawPile();
	}
	return QList<int>();//ListS2I(property("PlayerWantToGetDrawPile").toString().split("+"));
}

QList<int> Player::getdiscardPile() const
{
    QObject *room = Sanguosha->currentRoomObject();
	if(room){
		Room *serverRoom = qobject_cast<Room *>(room);
		if(serverRoom) return serverRoom->getDiscardPile();
	}
	return QList<int>();// ListS2I(property("PlayerWantToGetDiscardPile").toString().split("+"));
}

QString Player::getDeathReason() const
{
    return property("My_Death_Reason").toString();
}

bool Player::isJieGeneral() const
{
    if (getGeneralName().startsWith("tenyear_") || getGeneral2Name().startsWith("tenyear_"))
        return true;
    if (getGeneralName().startsWith("ol_") || getGeneral2Name().startsWith("ol_"))
        return true;
    if (getGeneralName().startsWith("second_ol_") || getGeneral2Name().startsWith("second_ol_"))
        return true;
    if (getGeneralName().startsWith("mobile_") || getGeneral2Name().startsWith("mobile_"))
        return true;/*
    QString translate, translate2;
    if (translate.startsWith("界") || translate2.startsWith("界")) return true;
    if (translate.startsWith("OL界") || translate2.startsWith("OL界")) return true;
    if (translate.startsWith("手杀界") || translate2.startsWith("手杀界")) return true;
    if ((translate.startsWith("界") && translate.endsWith("-手杀")) ||
            (translate2.startsWith("界") && translate2.endsWith("-手杀"))) return true;*/
    return false;
}

bool Player::isJieGeneral(const QString &name, const QString &except_name) const
{
    if (!isJieGeneral()) return false;

    if (except_name != ""){
        if (getGeneralName().contains(except_name) || getGeneral2Name().contains(except_name))
            return false;
    }

    if (name == "") return true;

    if (getGeneralName().contains(name) || getGeneral2Name().contains(name)) return true;
    return false;
}

bool Player::hasHideSkill(int general) const
{
    if (general == 1 && getGeneral())
        return getGeneral()->hasHideSkill();
    else if (general == 2 && getGeneral2())
        return getGeneral2()->hasHideSkill();
    return false;
}

bool Player::inYinniState() const
{
    return !property("yinni_general").toString().isEmpty() || !property("yinni_general2").toString().isEmpty();
}

bool Player::canSeeHandcard(const Player *player) const
{
    if (this == player || (ServerInfo.GameMode == "04_2v2" && isYourFriend(player))) return true;
    foreach(QString mark, player->getMarkNames()){
        if (mark.startsWith("HandcardVisible_ALL") && player->getMark(mark) > 0)
            return true;
    }
    foreach(QString mark, getMarkNames()){
        if (mark.startsWith("HandcardVisible_" + player->objectName()) && getMark(mark) > 0)
            return true;
    }
    return false;
}

QString Player::getLogName() const
{
	QString general_name = Sanguosha->translate(getGeneralName());
	if (getGeneral2()) general_name += "/"+Sanguosha->translate(getGeneral2Name());
	if (ServerInfo.EnableSame || getGeneralName() == "anjiang")
		general_name += QString("[%1]").arg(getSeat());
	return general_name;
}

void Player::removeCard(int id, Place place)
{
    switch (place){
    case PlaceHand: {
		foreach(const Card *h, handcards){
			if(h->getId()==id)
				handcards.removeOne(h);
		}
        break;
    }case PlaceEquip: {
        removeEquip(Sanguosha->getCard(id));
        break;
    }case PlaceDelayedTrick: {
        removeDelayedTrick(Sanguosha->getCard(id));
        break;
    }case PlaceSpecial: {
		foreach(QString pile_name, piles.keys()){
			if (piles[pile_name].contains(id)){
				piles[pile_name].removeAll(id);
				if(piles[pile_name].isEmpty())
					piles.remove(pile_name);
			}
		}
        break;
    }default:
        break;
    }
}

QList<const Card *> Player::getHandcards() const
{
    return handcards;
}

void Player::addCard(int id, Place place)
{
    switch (place){
    case PlaceHand: {
		foreach(const Card *h, handcards){
			if(h->getId()==id)
				return;
		}
        handcards << Sanguosha->getCard(id);
        break;
    }case PlaceEquip: {
        setEquip(Sanguosha->getCard(id));
        break;
    }case PlaceDelayedTrick: {
        addDelayedTrick(Sanguosha->getCard(id));
        break;
    }default:
        break;
    }
}

bool Player::isLastHandCard(const Card *card, bool contain) const
{
	QList<int> hids = handCards();
	if(card->isVirtualCard()){
		QList<int> ids = card->getSubcards();
		if(ids.length()>0){
			if (contain){
				foreach(int id, hids){
					if (!ids.contains(id))
						return false;
				}
				return true;
			} else if(ids.length()>=hids.length()){
				foreach(int id, ids){
					if (!hids.contains(id))
						return false;
				}
				return true;
			}
		}
	}
    return hids.length()==1&&hids.contains(card->getEffectiveId());
}

bool Player::hasTurn() const
{
	foreach(const Player*p, getAliveSiblings(true)){
		if(p->hasFlag("CurrentPlayer")) return true;
	}
    return false;
}

int Player::getHandcardNum() const
{
    return handcards.length();
}

int Player::getRandomHandCardId() const
{
    const Card *c = getRandomHandCard();
	if (c) return c->getEffectiveId();
	return -1;
}

const Card *Player::getRandomHandCard() const
{
	if (handcards.isEmpty()) return nullptr;
    return handcards.at(qrand()%handcards.length());
}

void Player::drawCard(const Card *card)
{
    handcards << card;
}

void Player::sortHandCards(const QString &hands)
{
    sortHandCards(ListS2I(hands.split("+")));
}

void Player::sortHandCards(QList<int>hands)
{
    if(hands.length()!=handcards.length()) return;
	handcards.clear();
	foreach(int id, hands)
		handcards << Sanguosha->getCard(id);
}

void Player::setSkillDescriptionSwap(const QString &skill_name, const QString &key, const QString &value)
{
    QHash<QString, QString> swap = description_s2k2v[skill_name];
	QString _value;
	if(value.contains("+")){
		foreach (QString v, value.split("+"))
			_value.append(Sanguosha->translate(v));
	}else
		_value = Sanguosha->translate(value);
	swap[key] = _value;
	description_s2k2v[skill_name] = swap;
}

QHash<QString, QString> Player::getSkillDescriptionSwap(const QString &skill_name) const
{
    return description_s2k2v[skill_name];
}

bool Player::setProperty(const char* name, const QVariant& value) {
    return ThreadSafeHelper::setProperty(this, name, value);
}


/**
 * 檢查玩家是否符合特定的主公技勢力要求
 * @param player 玩家對象指針
 * @param targetKingdom 目標勢力代碼 (例如 "qun", "wei", "shu")
 */
bool Player::hasLordSkillKingdom(const QString& targetKingdom, const Player *player) const {
    // 1. 優先檢查：如果玩家本國勢力直接符合，無需檢查屬性
    if (this->getKingdom() == targetKingdom) {
        return true;
    }

    // 2. 獲取特殊屬性
    const QString lordskill_kingdom = this->property("lordskill_kingdom").toString();
    
    // 3. 如果屬性為空，且第1步已失敗，則返回 false
    if (lordskill_kingdom.isEmpty()) {
        return false;
    }

    // 4. 解析屬性：檢查是否包含目標勢力或 "all"
    const QStringList kingdoms = lordskill_kingdom.split('+');
    if (player) {
        // 如果提供了目標玩家，還可以檢查目標玩家是否符合要求
        if (kingdoms.contains(player->objectName())) {
            return true;
        }
    }
    return kingdoms.contains(targetKingdom) || kingdoms.contains("all");
}

