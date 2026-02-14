#include "couple-scenario.h"
//#include "skill.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
//#include "util.h"

class CoupleScenarioRule : public ScenarioRule
{
public:
    CoupleScenarioRule(Scenario *scenario)
        : ScenarioRule(scenario)
    {
        events << GameReady << GameOverJudge << BuryVictim;
		coupleScenario = qobject_cast<CoupleScenario *>(scenario);
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        switch (triggerEvent) {
        case GameReady: {
            if (player) return false;
            foreach (ServerPlayer *p, room->getPlayers()) {
                if (p->isLord()) continue;
                else {
                    QMap<QString, QString> OH_map = coupleScenario->getOriginalMap(true);
                    QMap<QString, QString> OW_map = coupleScenario->getOriginalMap(false);
                    if (OH_map.contains(p->getGeneralName())) {
						QMap<QString, QStringList> W_map = coupleScenario->getMap(false);
                        QStringList h_list = W_map.value(OH_map.value(p->getGeneralName()));
						if (h_list.length()>1&&p->askForSkillInvoke("reselect",data,false)) {
							QString general_name = room->askForGeneral(p, h_list);
							if(general_name!=p->getGeneralName())
								room->changeHero(p, general_name, true, false);
                        }
                    } else if (OW_map.contains(p->getGeneralName())) {
						QMap<QString, QStringList> H_map = coupleScenario->getMap(true);
                        QStringList w_list = H_map.value(OW_map.value(p->getGeneralName()));
						if (w_list.length()>1&&p->askForSkillInvoke("reselect",data,false)) {
							QString general_name = room->askForGeneral(p, w_list);
							if(general_name!=p->getGeneralName())
								room->changeHero(p, general_name, true, false);
                        }
                    }
                }
            }
            coupleScenario->marryAll(room);
            room->setTag("SkipNormalDeathProcess", true);
            break;
        }
        case GameOverJudge: {
			ServerPlayer *lp = room->getLord();
            if (player==lp) {
                coupleScenario->marryAll(room);
            } else if (player->getGender()==lp->getGender()) {
                ServerPlayer *loyalist = nullptr;
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    if (p->getRole() == "loyalist") {
                        loyalist = p;
                        break;
                    }
                }
                ServerPlayer *widow = coupleScenario->getSpouse(player);
                if (widow && widow->isAlive() && loyalist == nullptr)
                    coupleScenario->remarry(lp, widow);
            } else if (player->getRole() == "loyalist") {
                room->setPlayerProperty(player, "role", "renegade");
                QList<ServerPlayer *> widows;
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    if (p->getGender()!=lp->getGender()&&coupleScenario->isWidow(p))
                        widows << p;
                }
				ServerPlayer *new_wife = room->askForPlayerChosen(lp, widows, "remarry");
				if (new_wife) coupleScenario->remarry(lp, new_wife);
            }
            QList<ServerPlayer *> players = room->getAlivePlayers();
            if (players.length() == 1) {
                ServerPlayer *survivor = players.first();
                ServerPlayer *spouse = coupleScenario->getSpouse(survivor);
                if (spouse)
                    room->gameOver(QString("%1+%2").arg(survivor->objectName()).arg(spouse->objectName()));
                else
                    room->gameOver(survivor->objectName());
            } else if (players.length() == 2) {
                ServerPlayer *first = players.at(0);
                ServerPlayer *second = players.at(1);
                if (coupleScenario->getSpouse(first) == second)
                    room->gameOver(QString("%1+%2").arg(first->objectName()).arg(second->objectName()));
            }
            return true;
        }
        case BuryVictim: {
            DeathStruct death = data.value<DeathStruct>();
            player->bury();
            // reward and punishment
            if (death.damage && death.damage->from && death.damage->from != player) {
                if (coupleScenario->getSpouse(death.damage->from) == player)
                    death.damage->from->throwAllHandCardsAndEquips("kill");
                else
                    death.damage->from->drawCards(3, "kill");
            }
            break;
        }
        default:
            break;
        }
        return false;
    }
private:
    CoupleScenario *coupleScenario;
};

CoupleScenario::CoupleScenario()
    : Scenario("couple")
{
    lord = GetConfigFromLuaState(Sanguosha->getLuaState(), "couple_lord").toString();
    //loadCoupleMap();

    rule = new CoupleScenarioRule(this);
}

void CoupleScenario::loadCoupleMap()
{
	QStringList GeneralNames = Sanguosha->getLimitedGeneralNames();
    foreach (QString couple, GetConfigFromLuaState(Sanguosha->getLuaState(), "couple_couples").toStringList()) {
        QStringList husbands = couple.split("+").first().split("|");
        QStringList wifes = couple.split("+").last().split("|");
        foreach(QString wife, wifes){
            foreach(QString gn, GeneralNames){
				if(gn.endsWith(wife)&&!wifes.contains(gn)&&Sanguosha->getGeneral(gn)->isFemale())
					wifes << gn;
			}
			if(!GeneralNames.contains(wife))
				wifes.removeOne(wife);
		}
        foreach(QString husband, husbands){
            foreach(QString gn, GeneralNames){
				if(gn.endsWith(husband)&&!husbands.contains(gn)&&Sanguosha->getGeneral(gn)->isMale())
					husbands << gn;
			}
			husband_map[husband] = wifes;
			if(!GeneralNames.contains(husband))
				husbands.removeOne(husband);
		}
		if(husbands.isEmpty()||wifes.isEmpty()) continue;
        original_husband_map[husbands.first()] = wifes.first();
        foreach(QString wife, wifes)
            wife_map[wife] = husbands;
        original_wife_map[wifes.first()] = husbands.first();
    }/*
    foreach (QString couple, GetConfigFromLuaState(Sanguosha->getLuaState(), "couple_couples").toStringList()) {
        QStringList husbands = couple.split("+").first().split("|");
        QStringList wifes = couple.split("+").last().split("|");
        foreach(QString husband, husbands)
            husband_map[husband] = wifes;
        original_husband_map[husbands.first()] = wifes.first();
        foreach(QString wife, wifes)
            wife_map[wife] = husbands;
        original_wife_map[wifes.first()] = husbands.first();
    }*/
}

void CoupleScenario::marryAll(Room *room) const
{
    foreach (QString husband_name, husband_map.keys()) {
        ServerPlayer *husband = room->findPlayer(husband_name, true);
        if (husband == nullptr) continue;
		foreach (QString wife_name, husband_map[husband_name]) {
			ServerPlayer *wife = room->findPlayer(wife_name, true);
			if (wife != nullptr) {
				marry(husband, wife);
				break;
			}
		}
    }
    foreach (QString wife_name, wife_map.keys()) {
        ServerPlayer *husband = room->findPlayer(wife_name, true);
        if (husband == nullptr) continue;
		foreach (QString husband_name, wife_map[wife_name]) {
			ServerPlayer *wife = room->findPlayer(husband_name, true);
			if (wife != nullptr) {
				marry(husband, wife);
				break;
			}
		}
    }
}

void CoupleScenario::setSpouse(ServerPlayer *player, ServerPlayer *spouse) const
{
    if (spouse)
        player->tag["spouse_player"] = spouse->objectName();
    else
        player->tag.remove("spouse_player");
}

void CoupleScenario::marry(ServerPlayer *husband, ServerPlayer *wife) const
{
    if (getSpouse(husband) == wife)
        return;

    LogMessage log;
    log.type = "#Marry";
    log.from = husband;
    log.to << wife;
    husband->getRoom()->sendLog(log);

    setSpouse(husband, wife);
    setSpouse(wife, husband);
}

void CoupleScenario::remarry(ServerPlayer *enkemann, ServerPlayer *widow) const
{
    Room *room = enkemann->getRoom();

    ServerPlayer *ex_husband = getSpouse(widow);
    setSpouse(ex_husband, nullptr);
    LogMessage log;
    log.type = "#Divorse";
    log.from = widow;
    log.to << ex_husband;
    room->sendLog(log);

    ServerPlayer *ex_wife = getSpouse(enkemann);
    if (ex_wife) {
        setSpouse(ex_wife, nullptr);
        LogMessage log;
        log.type = "#Divorse";
        log.from = enkemann;
        log.to << ex_wife;
        room->sendLog(log);
    }

    marry(enkemann, widow);
	room->doAnimate(1,enkemann->objectName(),widow->objectName());
	room->doAnimate(1,widow->objectName(),enkemann->objectName());
    room->setPlayerProperty(widow, "role", "loyalist");
    room->resetAI(widow);
}

ServerPlayer *CoupleScenario::getSpouse(const ServerPlayer *player) const
{
    foreach (ServerPlayer *p, player->getRoom()->getPlayers()) {
		if(p->objectName()==player->tag["spouse_player"].toString())
			return p;
	}
	return nullptr;
}

bool CoupleScenario::isWidow(ServerPlayer *player) const
{
    ServerPlayer *spouse = getSpouse(player);
    return spouse==nullptr || spouse->isDead();
}

void CoupleScenario::assign(QStringList &generals, QStringList &roles) const
{
    generals << lord;

    ((CoupleScenario*)this)->loadCoupleMap();

    QStringList husbands = original_husband_map.keys();
    qShuffle(husbands);
    husbands = husbands.mid(0, 4);

    QStringList others;
    foreach(QString husband, husbands)
        others << husband << original_husband_map[husband];

    generals << others;
    qShuffle(generals);

    // roles
    for (int i = 0; i < 9; i++) {
        if (generals.at(i) == lord)
            roles << "lord";
        else
            roles << "renegade";
    }
}

int CoupleScenario::getPlayerCount() const
{
    return 9;
}

QString CoupleScenario::getRoles() const
{
    return "ZNNNNNNNN";
}

void CoupleScenario::onTagSet(Room *, const QString &) const
{
}

AI::Relation CoupleScenario::relationTo(const ServerPlayer *a, const ServerPlayer *b) const
{
    if (getSpouse(a) == b)
        return AI::Friend;/*
	
    if ((a->isLord() || b->isLord()) && a->isMale() != b->isMale())
        return AI::Neutrality;*/
	
	if(a->isLord()){
		if(a->getGender()!=b->getGender())
			return AI::Neutrality;
	}else if(b->isLord()){
		if(a->getHp()>=b->getHp())
			return AI::Neutrality;
	}
	
    return AI::Enemy;
}

QMap<QString, QStringList> CoupleScenario::getMap(bool isHusband) const
{
    return isHusband ? husband_map : wife_map;
}

QMap<QString, QString> CoupleScenario::getOriginalMap(bool isHusband) const
{
    return isHusband ? original_husband_map : original_wife_map;
}

