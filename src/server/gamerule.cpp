#include "gamerule.h"
//#include "serverplayer.h"
#include "room.h"
//#include "standard.h"
//#include "maneuvering.h"
#include "engine.h"
#include "settings.h"
//#include "json.h"
#include "roomthread.h"
#include "wrapped-card.h"

GameRule::GameRule(QObject *)
    : TriggerSkill("game_rule")
{
    //@todo: this setParent is illegitimate in QT and is equivalent to calling
    // setParent(nullptr). So taking it off at the moment until we figure out
    // a way to do it.
    //setParent(parent);

    events << GameReady
		<< TurnStart << EventPhaseStart << EventPhaseProceeding << EventPhaseChanging
        << ChangeSlash
        << PreCardUsed << CardUsed << TargetSpecifying << CardFinished << CardEffected
        << PreCardResponded << CardResponded
        << TrickCardCanceling
        << HpChanged
        << EventLoseSkill << EventAcquireSkill
        << AskForPeaches << AskForPeachesDone << BuryVictim << GameOverJudge
        << SlashHit << SlashEffected << SlashProceed
        << ConfirmDamage << DamageCaused << DamageDone << DamageComplete
        << StartJudge << FinishRetrial << FinishJudge
        << ChoiceMade
        << CardsMoveOneTime;
}

bool GameRule::triggerable(const ServerPlayer *) const
{
    return true;
}

int GameRule::getPriority(TriggerEvent) const
{
    return -1;
}

void GameRule::onPhaseProceed(ServerPlayer *player,Room *room) const
{
    switch (player->getPhase()) {
    case Player::PhaseNone: {
        //Q_ASSERT(false);
    }
    case Player::RoundStart:{
        break;
    }
    case Player::Start: {
        break;
    }
    case Player::Judge: {
		QList<const Card *> tricks = player->getJudgingArea();
        while (tricks.length()>0&&player->isAlive()) {
            const Card *trick = tricks.takeLast();
			if(room->getCardPlace(trick->getId())!=Player::PlaceDelayedTrick) continue;
            CardMoveReason reason(CardMoveReason::S_MASK_BASIC_REASON,player->objectName(),trick->getSkillName(),"delayed_effect");
			reason.m_extraData = QVariant::fromValue(trick);
            room->moveCardTo(trick,nullptr,Player::PlaceTable,reason,true);
            room->getThread()->delay(Config.S_JUDGE_LONG_DELAY);
            if(room->cardEffect(trick,nullptr,player)) continue;
			trick->onNullified(player);
        }
        break;
    }
    case Player::Draw: {
        int num = 2;
        if(player->getMark("@extra_turn")<1) {
            if(room->getMode()=="02_1v1"||room->getMode()=="04_2v2"){
				if(!room->getTag("Global_FirstRound").toBool()) num--;
				room->setTag("Global_FirstRound",true);
			}
        }
		player->drawCards(num,"draw_phase");
		/*QVariant data = num;
        room->getThread()->trigger(DrawNCards,room,player,data);
        if(data.toInt()>0) player->drawCards(data.toInt(),"draw_phase");
        room->getThread()->trigger(AfterDrawNCards,room,player,data);*/
        break;
    }
    case Player::Play: {
		for (int i = 0; i < 9; i++){//防止无限询问
            if(!player->isAlive()) break;
			CardUseStruct card_use;
            room->activate(player,card_use);
            if(card_use.card==nullptr) break;
			if(room->useCard(card_use,true)) i = 0;
		}
        break;
    }
    case Player::Discard: {
        int num = player->getHandcardNum()-player->getMaxCards();
		foreach (const Card *c,player->getHandcards()) {
			if(num>0&&player->isCardLimited(c,Card::MethodIgnore))
				num--;
		}
        if(num>0) room->askForDiscard(player,"gamerule",num,num);
        break;
    }
    case Player::Finish: {
        break;
    }
    case Player::NotActive:{
        break;
    }
    }
}

bool GameRule::trigger(TriggerEvent triggerEvent,Room *room,ServerPlayer *player,QVariant &data) const
{
    if(room->getTag("SkipGameRule").toInt()==(int)triggerEvent) {
        room->removeTag("SkipGameRule");
        return false;
    }
    switch (triggerEvent) {
    case GameReady: {// Handle global events
		if(player) break;
		ServerPlayer *lord = room->getLord();
		if(room->getMode()=="04_boss") {
			int difficulty = Config.value("BossModeDifficulty").toInt();
			if((difficulty & (1 << GameRule::BMDIncMaxHp))>0) {
				foreach (ServerPlayer *p,room->getPlayers()) {
					if(p==lord) continue;
					room->safeSetPlayerProperty(p,"maxhp",p->getMaxHp()+2);
					room->safeSetPlayerProperty(p,"hp",p->getHp()+2);
					room->broadcastProperty(p,"maxhp");
					room->broadcastProperty(p,"hp");
				}
			}
		}else if(room->getMode()=="03_1v2"&&lord)
			room->handleAcquireDetachSkills(lord,"feiyang|bahu");
		foreach (ServerPlayer *p,room->getAlivePlayers()) {
			const General *general = p->getGeneral();
			//Q_ASSERT(general != nullptr);
			if(general->getKingdoms().contains("+"))
				room->setPlayerProperty(p,"kingdom",room->askForKingdom(p,general->objectName() + "_ChooseKingdom")); 
			if(p->getKingdom()=="demon")
				room->setPlayerProperty(p,"kingdom",room->askForKingdom(p,"gamerule_demon"));
			else if(Sanguosha->getBanPackages().contains("Godlailailai")){
				if(p->getKingdom()=="god"&&p->getGeneralName() != "anjiang"
					&& !p->getGeneralName().startsWith("boss_")&&!room->getScenario())
					room->setPlayerProperty(p,"kingdom",room->askForKingdom(p,"gamerule_god"));
			}
			foreach (const Skill *skill,p->getVisibleSkillList()) {
				if(skill->isLimitedSkill()&&(!skill->isLordSkill()||p->hasLordSkill(skill,true)))
					room->setPlayerMark(p,skill->getLimitMark(),1);
			}
		}
		if(room->getMode()=="06_ol") {
			foreach (ServerPlayer *p,room->getAlivePlayers()) {
				foreach (int id,room->getDrawPile()) {
					const Card *c = Sanguosha->getCard(id);
					if((c->objectName()=="god_horse"&&p->getGeneralName().endsWith("shencaocao"))
						|| (c->objectName()=="god_sword"&&p->getGeneralName().endsWith("shenzhaoyun"))
						|| (c->objectName()=="god_hat"&&p->getGeneralName().endsWith("shensimayi"))
						|| (c->objectName()=="god_diagram"&&p->getGeneralName().endsWith("shenzhugeliang"))
						|| (c->objectName()=="god_qin"&&p->getGeneralName().endsWith("shenzhouyu"))
						|| (c->objectName()=="god_pao"&&p->getGeneralName().endsWith("shenlvmeng"))
						|| (c->objectName()=="god_halberd"&&p->getGeneralName().endsWith("shenlvbu"))
						|| (c->objectName()=="god_double_sword"&&p->getGeneralName().endsWith("shenliubei"))
						|| (c->objectName()=="god_deer"&&p->getGeneralName().endsWith("shenluxun"))
						|| (c->objectName()=="god_bow"&&p->getGeneralName().endsWith("shenganning"))
						|| (c->objectName()=="god_axe"&&p->getGeneralName().endsWith("shenzhangliao"))
						|| (c->objectName()=="god_edict"&&p->getGeneralName().endsWith("shencaopi"))
						|| (c->objectName()=="god_ship"&&p->getGeneralName().endsWith("shensunquan"))
						|| (c->objectName()=="god_headdress"&&p->getGeneralName().endsWith("shenzhenji"))
						|| (c->objectName()=="god_blade"&&p->getGeneralName().endsWith("shenguanyu"))) {
						room->moveCardTo(c,p,Player::PlaceEquip,CardMoveReason(CardMoveReason::S_REASON_PUT,p->objectName(),"gamerule",""));
					}
				}
			}
			/*int n = 1;
			foreach (ServerPlayer *p,room->getPlayers()) {
				Player *peo = (Player *)p;
				if(n>1) {
					peo->setSeat(n);
					++n;
				} else if(p->getRole()=="rebel") {
					peo->setSeat(n);
					++n;
				} else
					room->addPlayerMark(p,"stop");
			}
			foreach (ServerPlayer *p,room->getPlayers()) {
				Player *peo = (Player *)p;
				if(n==7) {
					break;
				} else if(n>1) {
					peo->setSeat(n);
					++n;
				}
			}*/
		}
		/*if(room->getMode()=="05_ol") {
			int n = 1;
			foreach (ServerPlayer *p,room->getPlayers()) {
				Player *peo = (Player *)p;
				if(n>1) {
					peo->setSeat(n);
					++n;
				} else if(p->getRole()=="rebel") {
					peo->setSeat(n);
					++n;
				} else
					room->addPlayerMark(p,"stop");
			}
			foreach (ServerPlayer *p,room->getPlayers()) {
				Player *peo = (Player *)p;
				if(n==6) {
					break;
				} else if(n>1) {
					peo->setSeat(n);
					++n;
				}
			}
		}*/
		QList<int> n_list;
		room->setTag("FirstRound",true);
		bool kof_mode = room->getMode()=="02_1v1"&&Config.value("1v1/Rule","2013").toString() != "Classical";
		foreach (ServerPlayer *p,room->getAlivePlayers()) {
			QVariant draw_data = kof_mode ? p->getMaxHp() : 4;
			if(room->getMode()=="06_ol"&&p->isLord())
				draw_data = 8;
			else if((room->getMode()=="05_ol"&&p->getRole()=="rebel")||(room->getMode()=="04_2v2"&&p==room->getAlivePlayers().last()))
				draw_data = 5;
			//room->getThread()->trigger(DrawInitialCards,room,p,draw_data);
			n_list << draw_data.toInt();
		}
		room->drawCards(room->getAlivePlayers(),n_list,"InitialHandCards");

		kof_mode = Config.value("EnableSPConvert",true).toBool()&&isNormalGameMode(Config.GameMode);
		foreach (ServerPlayer *p,room->getAlivePlayers()){
			//p->setProperty("InitialHandCards",ListI2V(p->handCards()));
			if(kof_mode&&Sanguosha->spConvertPairs().contains(p->getGeneralName())){
				QStringList choicelist;
				foreach (QString to_gen,Sanguosha->spConvertPairs().values(p->getGeneralName())) {
					if(Config.value("Banlist/Roles").toStringList().contains(to_gen)) continue;
					const General *gen = Sanguosha->getGeneral(to_gen);
					if(gen&&!Sanguosha->getBanPackages().contains(gen->getPackage()))
						choicelist << to_gen;
				}
				if(choicelist.isEmpty()) continue;/*
				QString choice = choicelist.join("\\,\\");
				if(choicelist.length() >= 2)
					choice.replace("\\,\\" + choicelist.last(),"\\or\\" + choicelist.last());*/
				if(p->askForSkillInvoke("sp_convert","sp:"+choicelist.last(),false)) {
					QString to_cv = room->askForGeneral(p,choicelist);
					room->changeHero(p,to_cv,true,false,false);
				}
			}
		}

		/*int i = 0;
		foreach (ServerPlayer *p,room->getAlivePlayers()) {
			QVariant _nlistati = n_list.at(i);
			room->getThread()->trigger(AfterDrawInitialCards,room,p,_nlistati);
			i++;
		}*/
		room->setTag("FirstRound",false);
		foreach(ServerPlayer *p,room->getAllPlayers()){
			room->getThread()->trigger(GameStart,room,p);
		}
		//room->getThread()->trigger(GameStart,room,room->getCurrent());
		foreach (int id,room->getDrawPile())
			room->setCardFlag(id,"-visible");
        break;
    }
    case TurnStart: {
        LogMessage log;
        log.type = "$AppendSeparator";
		room->sendLog(log);
        room->addPlayerMark(player,"Global_TurnCount");
        room->setPlayerMark(player,"damage_point_round",0);
        if(room->getTag("Global_ExtraTurn" + player->objectName()).toBool())
            room->setPlayerMark(player,"@extra_turn",1);
		else if(player==room->getAlivePlayers().first()) {
			QVariant rsdata = room->getTag("TurnLengthCount").toInt()+1;
			if(player->getMark("TurnLengthCount")<rsdata.toInt()){
				room->setTag("TurnLengthCount",rsdata);
				room->doBroadcastNotify(QSanProtocol::S_COMMAND_ADD_ROUND,rsdata);
				foreach (ServerPlayer *p,room->getAllPlayers()){
					p->setMark("TurnLengthCount",rsdata.toInt());
					room->getThread()->trigger(RoundStart,room,p,rsdata);
				}
				//room->getThread()->trigger(RoundStart,room,room->getCurrent(),rsdata);
			}
			if(room->getMode()=="04_boss"&&player->isLord()) {
				int rs = rsdata.toInt(),level = room->getTag("BossModeLevel").toInt();
				if(level+rs==1) room->doLightbox("BossLevelA\\ 1 \\BossLevelB",2000,100);
				room->addPlayerMark(player,"BossTurnCount");
				rs = player->getMark("BossTurnCount");
				log.type = "#BossTurnCount";
				log.from = player;
				log.arg = QString::number(rs);
				room->sendLog(log);
				if(level < Config.BossLevel&&rs>Config.value("BossModeTurnLimit",70).toInt())
					room->gameOver("lord");
			}
        }
        if(player->faceUp()) {
            room->addPlayerMark(player,"Global_TurnCount2");  //这个标记是真正进行的回合数，被翻面了不增加
			QVariant rsdata = room->getTag("TurnLengthCount");
            room->getThread()->trigger(TurnStarted,room,player,rsdata);
            player->play();
        } else
            player->turnOver();

        if(room->getTag("Global_ExtraTurn"+player->objectName()).toBool())
            room->removePlayerMark(player,"@extra_turn");
		else if(player->getNextAlive()==room->getAlivePlayers().first()) {
			QVariant rsdata = room->getTag("TurnLengthCount");
			if(player->getMark("TurnLengthCount")==rsdata.toInt()){
				foreach (ServerPlayer *p,room->getAllPlayers())
					room->getThread()->trigger(RoundEnd,room,p,rsdata);
				//room->getThread()->trigger(RoundEnd,room,room->getCurrent(),rsdata);
				foreach (ServerPlayer *p,room->getAlivePlayers()) {
					foreach (QString mark,p->getMarkNames()) {
						if(mark.endsWith("_lun"))
							room->setPlayerMark(p,mark,0);
					}
					foreach (const Card *c,p->getHandcards() + p->getEquips()) {
						foreach (QString flag,c->getFlags()) {
							if(flag.endsWith("_lun"))
								room->setCardTip(c->getId(),"-" + flag);
						}
					}
				}
			}
		}
        break;
    }
    case EventPhaseStart: {
        if(player->getPhase()==Player::RoundStart)
            player->breakYinniState();
		else if(player->getPhase()==Player::NotActive){
			
		}
        break;
    }
    case EventPhaseProceeding: {
        onPhaseProceed(player,room);
        break;
    }
    case EventPhaseChanging: {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		//ServerPlayer *current = room->getCurrent();
        if(change.to==Player::RoundStart) {
            QStringList lose;
			foreach (QString str,player->tag["NextTurnSkill"].toStringList()) {
				foreach (QString sk,player->tag[str].toStringList()) {
					if(player->hasSkill(sk,true)) lose << "-" + sk;
				}
				player->tag.remove(str);
			}
			player->tag.remove("NextTurnSkill");
            room->handleAcquireDetachSkills(player,lose);
		}else if(change.to==Player::NotActive) {
            room->setPlayerFlag(player,".");
            room->clearPlayerCardLimitation(player,true);
			LogMessage log;
			foreach (ServerPlayer *p,room->getAllPlayers()) {
				room->addPlayerHistory(p,".");
				if(p->getMark("drank")>0) {
					room->setPlayerMark(p,"drank",0);
					if(p->getMark("drank")<1)
						log.to << p;
				}
                foreach (QString mark,p->getMarkNames()) {
                    if(mark.endsWith("-Clear")||mark.endsWith("-PlayClear")
						||(player==p&&(mark.endsWith("-SelfClear")||mark.endsWith("-SelfPlayClear"))))
                        room->setPlayerMark(p,mark,0);
                }
				if(p->property("Suijiyingbian").toString()!="")
					room->setPlayerProperty(p,"Suijiyingbian","");
                foreach (const Card *c,p->getHandcards() + p->getEquips()) {
                    foreach (QString flag,c->getFlags()) {
                        if(flag.endsWith("-Clear")||(player==p&&flag.endsWith("-SelfClear")))
                            room->setCardFlag(c,"-"+flag);
                    }
                    if(c->getSkillName()=="suijiyingbian"||(p==player&&c->getSkillName()=="wangmeizhike"))
						room->filterCards(p,QList<const Card*>() << c,true);
                }
            }
            if(log.to.length()>0){
				log.from = player;
				log.type = "#UnsetDrankEndOfTurn";
				room->sendLog(log);
			}
			foreach (QString sk,player->tag["god_speelSkills"].toStringList()){
				room->removePlayerMark(player,"&god_speel+:+"+sk);
				room->removePlayerMark(player,"Qingcheng"+sk);
			}
			player->tag.remove("god_speelSkills");

			QStringList lose;
			foreach (QString str,player->tag["OneTurnSkill"].toStringList()) {
				foreach (QString sk,player->tag[str].toStringList())
					if(player->hasSkill(sk,true)) lose << "-" + sk;
				player->tag.remove(str);
			}
			player->tag.remove("OneTurnSkill");
			room->handleAcquireDetachSkills(player,lose);
        } else if(change.from==Player::Play||change.to==Player::Play) {
            int ana = player->usedTimes("Analeptic");
            room->addPlayerHistory(player,".");
            if(ana>0) room->addPlayerHistory(player,"Analeptic",ana);
            if(change.to==Player::Play)
				room->setPlayerMark(player,"damage_point_play_phase",0);
			else{
				foreach (ServerPlayer *p,room->getAlivePlayers()) {
					foreach (QString mark,p->getMarkNames()) {
						if(mark.endsWith("-PlayClear")||(player==p&&mark.endsWith("-SelfPlayClear")))
							room->setPlayerMark(p,mark,0);
					}
				}
			}
        }
        QString phase = QString("%1Clear").arg(change.from);
        foreach (ServerPlayer *p,room->getAlivePlayers()) {
            foreach (QString mark,p->getMarkNames()) {
                if(mark.endsWith("-"+phase)||(player==p&&mark.endsWith("-Self"+phase)))
                    room->setPlayerMark(p,mark,0);
            }
        }
        break;
    }
    case ChangeSlash: {
		CardUseStruct card_use = data.value<CardUseStruct>();
		if(card_use.from->getKingdom()=="god"&&card_use.card->objectName()=="slash"&&!Sanguosha->getBanPackages().contains("Godlailailai")){
			Card *gs = Sanguosha->cloneCard("_god_slash");
			gs->setSkillName("_god_effect");
			gs->addSubcard(card_use.card);
			gs->deleteLater();
			card_use.changeCard(gs);
			data.setValue(card_use);
		}/*
		int n = card_use.from->getMark("drank");
		if(n>0) {
			card_use.card->setTag("drank",n);
			room->setCardFlag(card_use.card,"drank");
			room->setPlayerMark(card_use.from,"drank",0);
		}*/
        break;
    }
    case PreCardUsed: {
		CardUseStruct card_use = data.value<CardUseStruct>();
		if(card_use.from->hasFlag("Global_ForbidSurrender")) {
			card_use.from->setFlags("-Global_ForbidSurrender");
			room->doNotify(card_use.from,QSanProtocol::S_COMMAND_ENABLE_SURRENDER,true);
		}
		QString cardName = card_use.card->objectName();
		
		if(card_use.card->hasFlag("JINGYIN")){}
		else if(card_use.card->hasFlag("YUANBEN")) card_use.from->broadcastSkillInvoke(cardName);
		else card_use.from->broadcastSkillInvoke(card_use.card);
		
		if(player->hasFlag("CurrentPlayer")&&(card_use.card->isKindOf("BasicCard")||card_use.card->isNDTrick())){
			foreach (int id,((Player*)player)->getHandPile()+player->handCards()) {
				if(card_use.card->getSubcards().contains(id)) continue;
				const Card *c = Sanguosha->getCard(id);
				if(c->isKindOf("Suijiyingbian")||c->getSkillName()=="suijiyingbian"){
					Card *clone = Sanguosha->cloneCard(cardName,c->getSuit(),c->getNumber());
					clone->setSkillName("suijiyingbian");
					WrappedCard *w_card = Sanguosha->getWrappedCard(id);
					w_card->takeOver(clone);
					room->notifyUpdateCard(player,id,w_card);
				}
			}
			room->setPlayerProperty(player,"Suijiyingbian",cardName);
		}
		
		if(cardName=="slash"){
			if(card_use.card->isRed()) cardName = "slash_red";
			else if(card_use.card->isBlack()) cardName = "slash_black";
		}
		if(QFile::exists("image/system/emotion/"+cardName+"/0.png"))
			room->setEmotion(player,cardName);
		else if(cardName.contains("slash"))
			room->setEmotion(player,"slash");
        break;
    }
    case CardUsed: {
		CardUseStruct card_use = data.value<CardUseStruct>();
		if(card_use.m_isOwnerUse&&card_use.from->hasSkill(card_use.card->getSkillName(false),true))
			room->notifySkillInvoked(card_use.from,card_use.card->getSkillName(false));

		if(card_use.card->hasPreAction())
			card_use.card->doPreAction(room,card_use);

		if(card_use.card->getTypeId()>0){
			if(player->tag.contains("ComboMovesCard")){
				delete player->tag["ComboMovesCard"].value<const Card*>();
				player->tag.remove("ComboMovesCard");
			}
			if(!card_use.card->hasFlag("ComboMoves"))
				player->tag["ComboMovesCard"] = QVariant::fromValue((const Card*)Sanguosha->cloneCard(card_use.card));
		}

		const Card *ec = Sanguosha->getEngineCard(card_use.card->getId());
		if(ec&&card_use.card->objectName()==ec->objectName()){
			QString yingbian = ec->property("YingBianEffects").toString();
			bool directly = player->getMark("YingBianDirectlyEffective")>0;
			if(yingbian.contains("yb_fujia")) {
				bool fujia = true;
				foreach (ServerPlayer *p,room->getAlivePlayers()) {
					if(p->getHandcardNum()>player->getHandcardNum()) {
						fujia = false;
						break;
					}
				}
				if(fujia||directly) {
					if(yingbian=="yb_fujia1") {
						LogMessage log;
						log.type = "#FuJia1NoRespond";
						log.from = player;
						log.card_str = card_use.card->toString();
						log.arg = "yb_fujia1";
						room->sendLog(log);
						card_use.no_respond_list << "_ALL_TARGETS";
					} else if(yingbian=="yb_fujia2") {
						if(card_use.to.length()>1) {
							player->tag["yb_fujia2_data"] = data;
							ServerPlayer *reduce = room->askForPlayerChosen(player,card_use.to,"yb_fujia2","yb_fujia2-reduce:" + card_use.card->objectName(),true);
							if(reduce) {
								LogMessage log;
								log.type = "#QiaoshuiRemove";
								log.from = player;
								log.to << reduce;
								log.card_str = card_use.card->toString();
								log.arg = "yb_fujia2";
								room->sendLog(log);
								room->doAnimate(1,player->objectName(),reduce->objectName());
								card_use.to.removeOne(reduce);
							}
						}
					}
				}
			} else if(yingbian.contains("yb_kongchao")&&(directly||player->isKongcheng())){
				if(yingbian=="yb_kongchao1")
					player->drawCards(1,"yb_kongchao1");
				else if(yingbian=="yb_kongchao2") {
					if(card_use.whocard&&card_use.whocard->getTypeId()>0) {
						if(room->CardInPlace(card_use.whocard,Player::PlaceTable)) {
							if(!card_use.whocard->isKindOf("DelayedTrick"))
								room->obtainCard(player,card_use.whocard);
						}
					}
				} else if(yingbian=="yb_kongchao3") {
					ServerPlayer *add = nullptr;
					player->tag["yb_kongchao3_data"] = data;
					QList<ServerPlayer *> adds = room->getCardTargets(card_use.from,card_use.card,card_use.to);
					if(adds.length()>0){
						if(card_use.card->isKindOf("Collateral")){
							QStringList tos;
							tos.append(card_use.card->toString());
							foreach(ServerPlayer *t,card_use.to)
								tos.append(t->objectName());
							room->setPlayerProperty(card_use.from,"extra_collateral",tos.join("+"));
							room->askForUseCard(card_use.from,"@@extra_collateral","@qiaoshui-add:::collateral");
							add = card_use.from->tag["ExtraCollateralTarget"].value<ServerPlayer *>();
							card_use.from->tag.remove("ExtraCollateralTarget");
						}else
							add = room->askForPlayerChosen(player,adds,"yb_kongchao3","yb_kongchao3-add:" + card_use.card->objectName(),true);
					}
					if(add) {
						LogMessage log;
						log.type = "#QiaoshuiAdd";
						log.from = player;
						log.to << add;
						log.card_str = card_use.card->toString();
						log.arg = "yb_kongchao3";
						room->sendLog(log);
						room->doAnimate(1,player->objectName(),add->objectName());
						card_use.to.append(add);
						room->sortByActionOrder(card_use.to);
					}
				}
			} else if(yingbian.contains("yb_canqu")&&(directly||player->getHp()==1)) {
				if(yingbian=="yb_canqu1")
					room->setCardFlag(card_use.card,"yb_canqu1_add_damage");
				else if(yingbian=="yb_canqu2") {
					ServerPlayer *add = nullptr;
					player->tag["yb_canqu2_data"] = data;
					QList<ServerPlayer *> adds = room->getCardTargets(card_use.from,card_use.card,card_use.to);
					if(adds.length()>0){
						if(card_use.card->isKindOf("Collateral")) {
							QStringList tos;
							tos.append(card_use.card->toString());
							foreach(ServerPlayer *t,card_use.to)
								tos.append(t->objectName());
							room->setPlayerProperty(card_use.from,"extra_collateral",tos.join("+"));
							room->askForUseCard(card_use.from,"@@extra_collateral","@qiaoshui-add:::collateral");
							add = card_use.from->tag["ExtraCollateralTarget"].value<ServerPlayer *>();
							card_use.from->tag.remove("ExtraCollateralTarget");
						} else
							add = room->askForPlayerChosen(player,adds,"yb_canqu2","yb_canqu2-add:" + card_use.card->objectName(),true);
					}
					if(add) {
						LogMessage log;
						log.type = "#QiaoshuiAdd";
						log.from = player;
						log.to << add;
						log.card_str = card_use.card->toString();
						log.arg = "yb_canqu2";
						room->sendLog(log);
						room->doAnimate(1,player->objectName(),add->objectName());
						card_use.to.append(add);
						room->sortByActionOrder(card_use.to);
					}
				}
			}
		}
		data.setValue(card_use);
		if(card_use.to.length()>0) {
			room->getThread()->trigger(TargetSpecifying,room,card_use.from,data);
			QList<ServerPlayer *> triggers,aps = room->getAllPlayers();
			card_use = data.value<CardUseStruct>();
			for (int i=0;i<aps.length();i++){
				if(card_use.to.contains(aps[i])&&!triggers.contains(aps[i])) {
					room->getThread()->trigger(TargetConfirming,room,aps[i],data);
					card_use = data.value<CardUseStruct>();
					triggers << aps[i];
					i = 0;
				}
			}
		}
		if(card_use.to.length()>0) {
			if(card_use.card->isKindOf("Slash")) {
				QVariantList jink_list = card_use.from->tag["Jink_"+card_use.card->toString()].toList();
				for (int i=jink_list.length();i<card_use.to.length();i++) jink_list << 1;
				card_use.from->tag["Jink_"+card_use.card->toString()] = jink_list;
			}
			room->getThread()->trigger(TargetSpecified,room,card_use.from,data);
			foreach(ServerPlayer *p,room->getAllPlayers())
				room->getThread()->trigger(TargetConfirmed,room,p,data);/*
			foreach(ServerPlayer *p,card_use.to)
				room->getThread()->trigger(TargetConfirmed,room,p,data);*/
			card_use = data.value<CardUseStruct>();
		}
		room->setTag("UseHistory"+card_use.card->toString(),data);
		for (int i=0;i<=card_use.extra_use;i++){
			card_use.card->use(room,card_use.from,card_use.to);
			foreach(ServerPlayer *p,card_use.to){
				if(p->isAlive()) continue;
				card_use.to.removeOne(p);
			}
			if(card_use.to.isEmpty()) break;
		}
        break;
    }
    case TargetSpecifying: {
        CardUseStruct use = data.value<CardUseStruct>();
		const Card *ec = Sanguosha->getEngineCard(use.card->getId());
		if(ec&&ec->objectName()==use.card->objectName()) {
			QString yingbian = ec->property("YingBianEffects").toString();
			if(yingbian.contains("yb_zhuzhan")) {
				bool buff = use.from->getMark("YingBianDirectlyEffective")>0;
				if(!buff) {
					QString pattern = "BasicCard";
					if(use.card->isKindOf("TrickCard")) pattern = "TrickCard";
					else if(use.card->isKindOf("EquipCard")) pattern = "EquipCard";
					foreach (ServerPlayer *p,room->getOtherPlayers(use.from)) {
						if(use.to.contains(p)||!p->canDiscard(p,"h")) continue;
						p->tag["yb_zhuzhan_data"] = data;
						if(room->askForDiscard(p,yingbian,1,1,true,false,"yb_zhuzhan-discard:" + use.card->objectName(),pattern)) {
							buff = true;
							break;
						}
					}
				}
				if(buff) {
					if(yingbian=="yb_zhuzhan1")
						room->setCardFlag(use.card,"yb_zhuzhan1_buff");
					else if(yingbian=="yb_zhuzhan2"&&use.from->isAlive()) {
						QList<ServerPlayer *> adds = room->getCardTargets(use.from,use.card,use.to);
						if(adds.length()>0) {
							ServerPlayer *add = nullptr;
							player->tag["yb_zhuzhan2_data"] = data;
							if(use.card->isKindOf("Collateral")) {
								QStringList tos;
								tos.append(use.card->toString());
								foreach(ServerPlayer *t,use.to)
									tos.append(t->objectName());
								room->setPlayerProperty(use.from,"extra_collateral",tos.join("+"));
								room->askForUseCard(use.from,"@@extra_collateral","@qiaoshui-add:::collateral");
								add = use.from->tag["ExtraCollateralTarget"].value<ServerPlayer *>();
								use.from->tag.remove("ExtraCollateralTarget");
							} else
								add = room->askForPlayerChosen(player,adds,"yb_zhuzhan2","yb_zhuzhan2-add:" + use.card->objectName(),true);
							if(add) {
								LogMessage log;
								log.type = "#QiaoshuiAdd";
								log.from = player;
								log.to << add;
								log.card_str = use.card->toString();
								log.arg = "yb_zhuzhan2";
								room->sendLog(log);
								room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE,player->objectName(),add->objectName());
								use.to.append(add);
								room->sortByActionOrder(use.to);
								data.setValue(use);
							}
						}
					}
                }
            }
        }
        if(use.card->isKindOf("Slash")) {
            if(use.card->hasFlag("SlashIgnoreArmor")) {
                foreach (ServerPlayer *p,use.to)
                    p->addQinggangTag(use.card);
            }
            if(use.card->hasFlag("SlashNoRespond")) {
                use.no_respond_list << "_ALL_TARGETS";
                data.setValue(use);
            }
            if(use.card->hasFlag("SlashNoOffset")) {
                use.no_offset_list << "_ALL_TARGETS";
                data.setValue(use);
            }
        }
        break;
    }
    case CardFinished: {
        CardUseStruct use = data.value<CardUseStruct>();
        room->clearCardFlag(use.card);
		
        if(use.card->isKindOf("TrickCard")&&use.to.length()>1) {
            foreach(ServerPlayer *p,room->getPlayers())
                room->doNotify(p,QSanProtocol::S_COMMAND_NULLIFICATION_ASKED,QVariant("."));
        }
        foreach (ServerPlayer *p,room->getAlivePlayers()) {
            foreach (QString flag,p->getFlagList()) {
                if(flag.endsWith("Failed"))
                    room->setPlayerFlag(p,"-" + flag);
            }
        }
        break;
    }
    case PreCardResponded: {
        CardResponseStruct resp = data.value<CardResponseStruct>();
		QString cardName = resp.m_card->objectName();
		if(player->hasFlag("CurrentPlayer")&&(resp.m_card->isKindOf("BasicCard")||resp.m_card->isNDTrick())){
			foreach (int id,((Player*)player)->getHandPile()+player->handCards()) {
				if(resp.m_card->getSubcards().contains(id)) continue;
				const Card *c = Sanguosha->getCard(id);
				if(c->isKindOf("Suijiyingbian")||c->getSkillName()=="suijiyingbian"){
					Card *clone = Sanguosha->cloneCard(cardName,c->getSuit(),c->getNumber());
					clone->setSkillName("suijiyingbian");
					WrappedCard *w_card = Sanguosha->getWrappedCard(id);
					w_card->takeOver(clone);
					room->notifyUpdateCard(player,id,w_card);
				}
			}
			room->setPlayerProperty(player,"Suijiyingbian",cardName);
		}
		if(resp.m_isRetrial) break;
		if(resp.m_card->hasFlag("JINGYIN")){}
		else if(resp.m_card->hasFlag("YUANBEN")) player->broadcastSkillInvoke(cardName);
		else player->broadcastSkillInvoke(resp.m_card);
		
		if(cardName=="slash"){
			if(resp.m_card->isRed()) cardName = "slash_red";
			else if(resp.m_card->isBlack()) cardName = "slash_black";
		}
		if(QFile::exists("image/system/emotion/"+cardName+"/0.png"))
			room->setEmotion(player,cardName);
		else if(cardName.contains("slash"))
			room->setEmotion(player,"slash");
		
		cardName = resp.m_card->getSkillName(false);
		if(!cardName.isEmpty()&&player->hasSkill(cardName,true))
			room->notifySkillInvoked(player,cardName);
        break;
    }
    case CardResponded: {
        CardResponseStruct resp = data.value<CardResponseStruct>();
        if(resp.m_isUse){
			const Card *ec = Sanguosha->getEngineCard(resp.m_card->getId());
			if(ec&&ec->objectName()==resp.m_card->objectName()){
				QString yingbian = ec->property("YingBianEffects").toString();
				if(yingbian.contains("yb_kongchao")&&(player->isKongcheng()||player->getMark("YingBianDirectlyEffective")>0)){
					if(yingbian=="yb_kongchao1")
						player->drawCards(1,"yb_kongchao1");
					else if(yingbian=="yb_kongchao2"){
						if(resp.m_toCard&&resp.m_toCard->getTypeId()>0){
							if(room->CardInPlace(resp.m_toCard,Player::PlaceTable)){
								if(!resp.m_toCard->isKindOf("DelayedTrick"))
									room->obtainCard(player,resp.m_toCard);
							}
						}
                    }
                }
            }
        }
        foreach (ServerPlayer *p,room->getAlivePlayers()) {
            foreach (QString flag,p->getFlagList()) {
                if(flag.endsWith("Failed"))
                    room->setPlayerFlag(p,"-" + flag);
            }
        }
        break;
    }
    case TrickCardCanceling: {
        CardEffectStruct effect = data.value<CardEffectStruct>();
        if (effect.no_respond || effect.no_offset || (effect.card && effect.card->hasFlag("TrickCardCanceling")))
            return true;

        break;
    }
    case EventAcquireSkill:{
		room->filterCards(player,player->getCards("he"),false);
        break;
	}
    case EventLoseSkill: {
        QString skill_name = data.toString();
		foreach (const Card*c,player->getCards("he")) {
			if(c->getSkillName()==skill_name)
				room->filterCards(player,QList<const Card*>() << c,true);
		}
		QString st = Sanguosha->getSkill(skill_name)->getLimitMark();
		if(st!="") room->setPlayerMark(player,st,0);
		QStringList lose,skilllist = player->tag["OneTurnSkill"].toStringList();
		st = "OneTurnSkill_"+skill_name;
		if(skilllist.contains(st)) {
			foreach (QString str,player->tag[st].toStringList()) {
				if(player->hasSkill(str,true)) lose << "-"+str;
			}
			player->tag.remove(st);
			skilllist.removeOne(st);
			player->tag["OneTurnSkill"] = skilllist;
		}
		skilllist = player->tag["NextTurnSkill"].toStringList();
		st = "NextTurnSkill_"+skill_name;
		if(skilllist.contains(st)) {
			foreach (QString str,player->tag[st].toStringList()) {
				if(player->hasSkill(str,true)) lose << "-"+str;
			}
			player->tag.remove(st);
			skilllist.removeOne(st);
			player->tag["NextTurnSkill"] = skilllist;
		}
		room->handleAcquireDetachSkills(player,lose);
        break;
    }
    case HpChanged: {
        player->breakYinniState();
		if(room->getMode()=="06_ol"&&player->isLord()&&player->getHp()<player->getMaxHp()/2&&!player->tag["BossWake"].toBool()){
			player->tag["BossWake"] = true;
            foreach (QString skillName,player->getGeneral()->getRelatedSkillNames()) {
                const Skill *skill = Sanguosha->getSkill(skillName);
                if(skill&&skill->isVisible())
                    room->acquireSkill(player,skill,true,true,false);
            }
		}
        if(player->getHp()>0||data.canConvert<RecoverStruct>())
            break;
        if(data.canConvert<DamageStruct>()){
			DamageStruct damage = data.value<DamageStruct>();
            room->enterDying(player,&damage);
		}else if(data.canConvert<HpLostStruct>()){
			HpLostStruct hplost = data.value<HpLostStruct>();
            room->enterDying(player,nullptr,&hplost);
		}
        break;
    }
    case AskForPeaches: {
        DyingStruct dying = data.value<DyingStruct>();
        while (dying.who->hasFlag("Global_Dying")) {
            //room->getThread()->trigger(PreventPeach,room,player,data);
			const Card *peach = room->askForSinglePeach(player,dying.who);
            //room->getThread()->trigger(AfterPreventPeach,room,player,data);
            if(peach) room->useCard(CardUseStruct(peach,player,dying.who));
			else break;
        }
        break;
    }
    case AskForPeachesDone: {
        if(player->hasFlag("Global_Dying")) {
            DyingStruct dying = data.value<DyingStruct>();
            room->killPlayer(player,dying.damage,dying.hplost);
        }
        break;
    }
    case ConfirmDamage: {
        DamageStruct damage = data.value<DamageStruct>();
        if(damage.card) {
			if(damage.card->hasFlag("drank")){
				LogMessage log;
				log.type = "#AnalepticBuff";
				log.from = damage.from;
				log.to << damage.to;
				log.card_str = damage.card->toString();
				log.arg = "+"+damage.card->tag["drank"].toString();
				room->sendLog(log);
				damage.damage += damage.card->tag["drank"].toInt();
				data.setValue(damage);
			}
			if(damage.card->hasFlag("yb_canqu1_add_damage")){
				LogMessage log;
				log.type = "#CanQu1AddDamage";
				log.from = damage.from;
				log.to << damage.to;
				log.arg = "yb_canqu1";
				room->sendLog(log);
				damage.damage++;
				data.setValue(damage);
			}
        }
        break;
    }
    case DamageCaused: {
        DamageStruct damage = data.value<DamageStruct>();
        if(damage.from&&!damage.chain){
			if(damage.nature==DamageStruct::Ice) {
				if(damage.from->canDiscard(damage.to,"he")){
					damage.from->tag["IceDamageData"] = data;
					LogMessage log;
					log.arg2 = QString("info:%1::%2").arg(damage.to->objectName()).arg(damage.damage);
					if(damage.from->askForSkillInvoke("IceDamagePrevent",log.arg2,false)) {
						log.type = "#IceDamageEffect";
						log.from = damage.from;
						log.to << damage.to;
						log.arg = "ice_nature";
						room->sendLog(log);
						for (int i=0;i<2;i++){
							if(i==0||damage.from->canDiscard(damage.to,"he")){
								int id = room->askForCardChosen(damage.from,damage.to,"he","IceDamagePrevent",false,Card::MethodDiscard);
								room->throwCard(id,"IceDamagePrevent",damage.to,damage.from);
							}
						}
						return true;
					}
				}
			} else if(damage.nature==DamageStruct::God){
				if(damage.from->isAlive()&&damage.to->isAlive()){
					LogMessage log;
					log.arg2 = QString("info:%1::%2").arg(damage.to->objectName()).arg(damage.damage);
					if(damage.from->askForSkillInvoke("GodDamage",log.arg2,false)){
						log.type = "#GodDamage";
						log.from = damage.from;
						log.to << damage.to;
						log.arg = "god_nature";
						log.arg2 = QString::number(damage.damage);
						room->sendLog(log);
						room->loseMaxHp(damage.to,damage.damage,"GodDamage");
						return true;
					}
				}
			}
		}
        break;
    }
    case DamageDone: {
        DamageStruct damage = data.value<DamageStruct>();

        if(damage.damage<1||!damage.to->isAlive()) break;
		else if(damage.card) {
            room->setCardFlag(damage.card,"DamageDone");
            room->setCardFlag(damage.card,"DamageDone_" + damage.to->objectName());
			damage.to->removeQinggangTag(damage.card);
        }

        LogMessage log;
		log.type = "#DamageNoSource";
        if(damage.from) {
            log.from = damage.from;
            if(damage.from->isAlive())
				log.type = "#Damage";
			else{
                damage.tips << "DamageFrom:"+damage.from->objectName();
				damage.from = nullptr;
				data.setValue(damage);
			}
        }
		if(damage.card){
			log.type += "Card";
			log.card_str = damage.card->toString();
		}
        log.to << damage.to;
        log.arg = QString::number(damage.damage);
		log.arg2 = "normal_nature";
        switch (damage.nature) {
        case DamageStruct::Fire: log.arg2 = "fire_nature"; break;
        case DamageStruct::Thunder: log.arg2 = "thunder_nature"; break;
        case DamageStruct::Ice: log.arg2 = "ice_nature"; break;
        case DamageStruct::God: log.arg2 = "god_nature"; break;
        case DamageStruct::Poison: log.arg2 = "poison_nature"; break;
        default: break;
        }
        room->sendLog(log);

        if(damage.nature != DamageStruct::Normal&&player->isChained()) {
            room->setPlayerChained(player);
            if(!damage.chain) {
                damage.tips << "need_chain_damage";
                data.setValue(damage);
            }
        }

        int hujia = damage.ignore_hujia?0:damage.to->getHujia();
		if(hujia>0){
			hujia = qMin(hujia,damage.damage);
			damage.to->loseHujia(hujia);
		}

        JsonArray arg;
        arg << damage.to->objectName() << -damage.damage << damage.nature << hujia;
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_CHANGE_HP,arg);

        if(damage.damage>hujia){
            room->setTag("HpChangedData",data);
            room->setPlayerProperty(damage.to,"hp",damage.to->getHp()+hujia-damage.damage);
		}
        room->getThread()->delay(Config.AIDelay/3);
        break;
    }
    case DamageComplete: {
        DamageStruct damage = data.value<DamageStruct>();
        if(damage.prevented) break;
        if(damage.tips.contains("need_chain_damage")) {
			QList<ServerPlayer *> chains;// iron chain effect
			foreach (ServerPlayer *p,room->getOtherPlayers(player)) {
				if(p->isChained()) chains << p;
			}
			LogMessage log;
			log.type = "#IronChainDamage";
			log.arg = QString::number(damage.damage);
			damage.chain = true;
			damage.transfer = false;
			damage.transfer_reason = "";
			damage.tips.removeAll("need_chain_damage");
			foreach (ServerPlayer *p,chains) {
				if(p->isAlive()&&p->isChained()) {
					log.from = p;
					room->sendLog(log);
					room->getThread()->delay();
					damage.to = p;
					room->damage(damage);
				}
			}
        }
        if(room->getMode()=="02_1v1"||room->getMode()=="06_XMode") {
            foreach (ServerPlayer *p,room->getAllPlayers()) {
                if(p->hasFlag("Global_DebutFlag")) {
                    p->setFlags("-Global_DebutFlag");
                    if(room->getMode()=="02_1v1")
                        room->getThread()->trigger(Debut,room,p);
                }
            }
        }
        break;
    }
    case CardEffected: {
		CardEffectStruct effect = data.value<CardEffectStruct>();
		if(effect.nullified) {
			LogMessage log;
			log.type = "#CardNullified";
			log.from = effect.to;
			log.card_str = effect.card->toString();
			room->sendLog(log);
			return true;
		}else if(effect.card->getTypeId()>0){
			if(effect.offset_card==nullptr){
				effect.offset_card = room->isCanceled(effect);
				data.setValue(effect);
			}
			if(effect.offset_card) {
				if(!room->getThread()->trigger(CardOffset,room,effect.from,data)){
					effect.to->setFlags("Global_NonSkillNullify");
					return true;
				}
			}
			room->getThread()->trigger(CardOnEffect,room,effect.to,data);
		}
		for (int i=0;i<=effect.extra_effect;i++){
			if(effect.to->isAlive())
				effect.card->onEffect(effect);
			else
				break;
		}
        break;
    }
    case SlashEffected: {
        SlashEffectStruct effect = data.value<SlashEffectStruct>();
        if(effect.nullified) {
            LogMessage log;
            log.type = "#CardNullified";
            log.from = effect.to;
            log.arg = effect.slash->objectName();
            room->sendLog(log);
            if(effect.from&&effect.to)
                room->setCardFlag(effect.slash,QString("NonSkillNullify_%1").arg(effect.to->objectName()));
            return true;
        }
        if(effect.no_respond||effect.no_offset) {
            room->slashResult(effect,nullptr);
        } else {
            if(effect.jink_num>0)
                room->getThread()->trigger(SlashProceed,room,effect.from,data);
            else
                room->slashResult(effect,nullptr);
        }
        break;
    }
    case SlashProceed: {
        SlashEffectStruct effect = data.value<SlashEffectStruct>();
        QString slasher = effect.from->objectName();
        if(!effect.to->isAlive())
            break;
        if(effect.jink_num==1) {
            const Card *jink = room->askForCard(effect.to,"jink","slash-jink:" + slasher,data,Card::MethodUse,effect.from,false,"",false,effect.slash);
            room->slashResult(effect,room->isJinkEffected(effect.to,jink) ? jink : nullptr);
        } else {
            DummyCard *jink = new DummyCard;
            for (int i = effect.jink_num; i>0; i--) {
                QString prompt = QString("@multi-jink%1:%2::%3").arg(i==effect.jink_num ? "-start" : "").arg(slasher).arg(i);
				const Card *asked_jink = room->askForCard(effect.to,"jink",prompt,data,Card::MethodUse,effect.from,false,"",false,effect.slash);
                if(!room->isJinkEffected(effect.to,asked_jink)) {
                    delete jink;
                    room->slashResult(effect,nullptr);
                    return false;
                } else {
                    jink->addSubcard(asked_jink->getEffectiveId());
                }
            }
            room->slashResult(effect,jink);
        }
        break;
    }
    case SlashHit: {
        SlashEffectStruct effect = data.value<SlashEffectStruct>();
        if(effect.drank>0) effect.to->setMark("SlashIsDrank",effect.drank);
        room->damage(DamageStruct(effect.slash,effect.from,effect.to,1,effect.nature));
        break;
    }
    case GameOverJudge: {
        if(room->getMode()=="04_boss"&&player->isLord()&&(Config.value("BossModeEndless").toBool()||room->getTag("BossModeLevel").toInt() < Config.BossLevel - 1))
            break;
        else if(room->getMode()=="02_1v1") {
            QStringList list = player->tag["1v1Arrange"].toStringList();
            QString rule = Config.value("1v1/Rule","2013").toString();
            if(list.length()>((rule=="2013") ? 3 : 0)) break;
        }
        QString winner = getWinner(player,room);
        if(winner.isEmpty()) break;
		room->gameOver(winner);
		return true;
    }
    case BuryVictim: {
        DeathStruct death = data.value<DeathStruct>();
        player->bury();
        if(room->getTag("SkipNormalDeathProcess").toBool())
            return false;
        ServerPlayer *killer = death.damage ? death.damage->from : nullptr;
        //if(killer)
            rewardAndPunish(killer,player);
		if(room->getMode()=="06_ol"){
			if(player->getGeneralName()=="zhuyin"&&killer&&killer->getRole()=="rebel"){
				killer->drawCards(3,"gamerule");
				room->recover(killer,RecoverStruct(killer));
			}
		}else if(room->getMode()=="05_ol"){
			if(player->getRole()=="rebel"){
				foreach (ServerPlayer *p,room->getAlivePlayers()) {
					if(p->getRole()=="rebel")
						p->drawCards(3,"gamerule");
				}
			}
		}else if(room->getMode()=="02_1v1") {
            QStringList list = player->tag["1v1Arrange"].toStringList();
            QString rule = Config.value("1v1/Rule","2013").toString();
            if(list.length() <= ((rule=="2013") ? 3 : 0)) break;
            if(rule=="Classical") {
                player->tag["1v1ChangeGeneral"] = list.takeFirst();
                player->tag["1v1Arrange"] = list;
            } else
                player->tag["1v1ChangeGeneral"] = list.first();
            changeGeneral1v1(player);
            if(death.damage)
                player->setFlags("Global_DebutFlag");
            else
                room->getThread()->trigger(Debut,room,player);
        } else if(room->getMode()=="06_XMode") {
            changeGeneralXMode(player);
            if(death.damage) player->setFlags("Global_DebutFlag");
        } else if(room->getMode()=="04_boss"&&player->isLord()) {
            int level = room->getTag("BossModeLevel").toInt()+1;
            room->setTag("BossModeLevel",level);
            doBossModeDifficultySettings(player);
            changeGeneralBossMode(player,room);
            if(death.damage) player->setFlags("Global_DebutFlag");
            room->doLightbox(QString("BossLevelA\\ %1 \\BossLevelB").arg(level + 1),2000,100);
        }
        break;
    }
    case StartJudge: {
        JudgeStruct *judge = data.value<JudgeStruct *>();
		if(judge->card==nullptr)
			judge->card = Sanguosha->getCard(room->drawCard());
        LogMessage log;
        log.type = "$InitialJudge";
        log.from = player;
        log.card_str = QString::number(judge->card->getEffectiveId());
        room->sendLog(log);
        room->moveCardTo(judge->card,judge->who,Player::PlaceJudge,CardMoveReason(CardMoveReason::S_REASON_JUDGE,judge->who->objectName(),judge->reason,""),true);
        judge->updateResult();
		data.setValue(judge);
        break;
    }
    case FinishRetrial: {
        JudgeStruct *judge = data.value<JudgeStruct *>();
        LogMessage log;
        log.type = "$JudgeResult";
        log.from = player;
        log.card_str = QString::number(judge->card->getEffectiveId());
        room->sendLog(log);
        int delay = Config.AIDelay;
        if(judge->time_consuming) delay /= 1.25;
        room->getThread()->delay(delay);
        if(judge->play_animation) {
            room->sendJudgeResult(judge);
            room->getThread()->delay(Config.S_JUDGE_LONG_DELAY);
        }
		judge->card = Sanguosha->cloneCard(judge->card);
		data.setValue(judge);//克隆判定牌以达到保留判定牌信息
        break;
    }
    case FinishJudge: {
        JudgeStruct *judge = data.value<JudgeStruct *>();
        if(judge->throw_card&&room->getCardPlace(judge->card->getEffectiveId())==Player::PlaceJudge) {
            CardMoveReason reason(CardMoveReason::S_REASON_JUDGEDONE,judge->who->objectName(),judge->reason,"");
            if(judge->retrial_by_response) reason.m_extraData = QVariant::fromValue(judge->retrial_by_response);
            reason.m_useStruct.card = judge->card;//这样可以知道这是生效后的判定牌
            room->moveCardTo(judge->card,nullptr,Player::DiscardPile,reason,true);
        }
		if(judge->card->parent()) break;
		((Card*)judge->card)->deleteLater();
        break;
    }
    case ChoiceMade: {
        foreach (ServerPlayer *p,room->getAlivePlayers()) {
            foreach (QString flag,p->getFlagList()) {
                if(flag.endsWith("Failed"))
                    room->setPlayerFlag(p,"-" + flag);
            }
        }
        break;
    }
    case CardsMoveOneTime: {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();/*
        if((!move.from&&move.from_places.contains(Player::DrawPile))||(!move.to&&move.to_place==Player::DrawPile)) {
            foreach (ServerPlayer *p,room->getAllPlayers(true))
                room->setPlayerProperty(p,"PlayerWantToGetDrawPile",ListI2S(room->getDrawPile()).join("+"));
        }
        if((!move.from&&move.from_places.contains(Player::DiscardPile))||(!move.to&&move.to_place==Player::DiscardPile)) {
            foreach (ServerPlayer *p,room->getAllPlayers(true))
                room->setPlayerProperty(p,"PlayerWantToGetDiscardPile",ListI2S(room->getDiscardPile()).join("+"));
        }
        if((move.from==player&&move.from_places.contains(Player::PlaceHand))||(move.to==player&&move.to_place==Player::PlaceHand)){
            room->setPlayerProperty(player,"My_Visible_HandCards",ListI2S(player->handCards()).join("+"));
		}*/
		if(move.to==player&&move.to_place==Player::PlaceHand){
			QString name = player->property("Suijiyingbian").toString();
			if(name.isEmpty()) break;
			foreach (const Card *h,player->getHandcards()) {
				if(!move.card_ids.contains(h->getId())) continue;
				if(h->isKindOf("Suijiyingbian")||h->getSkillName()=="suijiyingbian"){
					Card *clone = Sanguosha->cloneCard(name,h->getSuit(),h->getNumber());
					clone->setSkillName("suijiyingbian");
					WrappedCard *w_card = Sanguosha->getWrappedCard(h->getId());
					w_card->takeOver(clone);
					room->notifyUpdateCard(player,h->getId(),w_card);
				}
			}
		}else if(move.from==player&&move.from_places.contains(Player::PlaceHand)) {
            QVariantList hands = player->property("InitialHandCards").toList();
			if(hands.isEmpty()) break;
            foreach (int id,move.card_ids) {
                if(hands.contains(QVariant(id)))
					hands.removeOne(id);
			}
            room->safeSetPlayerProperty(player,"InitialHandCards",hands);
        }
        break;
    }
    default:
        break;
    }
    return false;
}

void GameRule::changeGeneral1v1(ServerPlayer *player) const
{
    Config.AIDelay = Config.OriginAIDelay;

    Room *room = player->getRoom();
    bool classical = (Config.value("1v1/Rule","2013").toString()=="Classical");
    QString new_general;
    if(classical) {
        new_general = player->tag["1v1ChangeGeneral"].toString();
        player->tag.remove("1v1ChangeGeneral");
    } else {
        QStringList list = player->tag["1v1Arrange"].toStringList();
        if(player->getAI()) new_general = list.first();
        else new_general = room->askForGeneral(player,list);
        list.removeOne(new_general);
        player->tag["1v1Arrange"] = list;
    }

    if(player->getPhase() != Player::NotActive)
        player->changePhase(player->getPhase(),Player::NotActive);

    room->revivePlayer(player);
    room->changeHero(player,new_general,true,true);

    room->addPlayerHistory(player,".");

    //if(player->getKingdom() != player->getGeneral()->getKingdom())
        //room->setPlayerProperty(player,"kingdom",player->getGeneral()->getKingdom());

    QList<ServerPlayer *> notified = classical ? room->getOtherPlayers(player,true) : room->getPlayers();
    room->doBroadcastNotify(notified,QSanProtocol::S_COMMAND_REVEAL_GENERAL,JsonArray() << player->objectName() << new_general);

    if(!player->faceUp())
        player->turnOver();

    if(player->isChained())
        room->setPlayerChained(player);

    player->obtainEquipArea();
    player->obtainJudgeArea();

    room->setTag("FirstRound",true); //For Manjuan
    int draw_num = classical ? 4 : player->getMaxHp();/*
    QVariant data = draw_num;
    room->getThread()->trigger(DrawInitialCards,room,player,data);
    draw_num = data.toInt();*/
    try {
        player->drawCards(draw_num,"InitialHandCards");
        room->setTag("FirstRound",false);
        //player->setProperty("InitialHandCards",ListI2V(player->handCards()));
    }catch (TriggerEvent triggerEvent) {
        if(triggerEvent==TurnBroken||triggerEvent==StageChange)
            room->setTag("FirstRound",false);
        throw triggerEvent;
    }/*
    QVariant _drawnum = draw_num;
    room->getThread()->trigger(AfterDrawInitialCards,room,player,_drawnum);*/
}

void GameRule::changeGeneralXMode(ServerPlayer *player) const
{
    Config.AIDelay = Config.OriginAIDelay;

    Room *room = player->getRoom();
    ServerPlayer *leader = player->tag["XModeLeader"].value<ServerPlayer *>();
    //Q_ASSERT(leader);
    QStringList backup = leader->tag["XModeBackup"].toStringList();
    QString general_name = room->askForGeneral(leader,backup);
    if(backup.contains(general_name)) backup.removeOne(general_name);
    else backup.takeFirst();
    leader->tag["XModeBackup"] = backup;

    if(player->getPhase() != Player::NotActive)
        player->changePhase(player->getPhase(),Player::NotActive);

    room->revivePlayer(player);
    room->changeHero(player,general_name,true,true);

    room->addPlayerHistory(player,".");

    //if(player->getKingdom() != player->getGeneral()->getKingdom())
        //room->setPlayerProperty(player,"kingdom",player->getGeneral()->getKingdom());

    if(!player->faceUp())
        player->turnOver();

    if(player->isChained())
        room->setPlayerChained(player);

    player->obtainEquipArea();
    player->obtainJudgeArea();

    room->setTag("FirstRound",true); //For Manjuan
    /*QVariant data(4);
    room->getThread()->trigger(DrawInitialCards,room,player,data);
    int num = data.toInt();*/
    try {
        player->drawCards(4,"InitialHandCards");
        room->setTag("FirstRound",false);
        //player->setProperty("InitialHandCards",ListI2V(player->handCards()));
    }catch (TriggerEvent triggerEvent) {
        if(triggerEvent==TurnBroken||triggerEvent==StageChange)
            room->setTag("FirstRound",false);
        throw triggerEvent;
    }/*

    QVariant _num = num;
    room->getThread()->trigger(AfterDrawInitialCards,room,player,_num);*/
}

void GameRule::changeGeneralBossMode(ServerPlayer *player,Room *room) const
{
	Config.AIDelay = Config.OriginAIDelay;

	QString general;
	int level = room->getTag("BossModeLevel").toInt();
	room->doBroadcastNotify(QSanProtocol::S_COMMAND_UPDATE_BOSS_LEVEL,QVariant(level));
	if(level <= Config.BossLevel - 1) {
		QStringList boss_generals = Config.BossGenerals.at(level).split("+");
		if(Config.value("BossYanluo").toBool()){
			boss_generals.clear();
			if(level==1)
				boss_generals << "yl_chujiang" << "yl_songdi" << "yl_wuguan" << "yl_yanluo";
			else if(level==2)
				boss_generals << "yl_biancheng" << "yl_taishan" << "yl_dushi" << "yl_pingdeng";
			else if(level==3){
				boss_generals << "yl_zhuanlun";
			}
		}
		if(Config.value("OptionalBoss").toBool())
			general = room->askForGeneral(player,boss_generals);
		else
			general = boss_generals.at(qrand() % boss_generals.length());
	} else
		general = (qrand() % 2==0) ? "sujiang" : "sujiangf";

	if(player->getPhase() != Player::NotActive)
		player->changePhase(player->getPhase(),Player::NotActive);

	room->revivePlayer(player);
	room->changeHero(player,general,true,true);
	room->setPlayerMark(player,"BossMode_Boss",1);
	int actualmaxhp = player->getMaxHp();
	if(level >= Config.BossLevel)
		actualmaxhp = level * 5 + 5;

	if(!Config.value("BossYanluo").toBool()){
		int difficulty = Config.value("BossModeDifficulty").toInt();
		if((difficulty & (1 << BMDDecMaxHp))>0) {
			if(level==1) actualmaxhp -= 2;
			else if(level==2) actualmaxhp -= 4;
			else if(level>0) actualmaxhp -= 5;
		}
	}

	if(actualmaxhp != player->getMaxHp()) {
		room->safeSetPlayerProperty(player,"maxhp",actualmaxhp);
		room->safeSetPlayerProperty(player,"hp",actualmaxhp);
		room->broadcastProperty(player,"maxhp");
		room->broadcastProperty(player,"hp");
	}
	if(level >= Config.BossLevel)
		acquireBossSkills(player,level);
	room->addPlayerHistory(player,".");

	if(player->getKingdom() != player->getGeneral()->getKingdom())
		room->setPlayerProperty(player,"kingdom",player->getGeneral()->getKingdom());

	if(!player->faceUp()) player->turnOver();
	if(player->isChained()) room->setPlayerChained(player);
	
	room->setTag("FirstRound",true); //For Manjuan
	/*QVariant data(4);
	room->getThread()->trigger(DrawInitialCards,room,player,data);
	int num = data.toInt();*/
	try {
		player->drawCards(4,"InitialHandCards");
		room->setTag("FirstRound",false);
		//player->setProperty("InitialHandCards",ListI2V(player->handCards()));
	}catch (TriggerEvent triggerEvent) {
		if(triggerEvent==TurnBroken||triggerEvent==StageChange)
			room->setTag("FirstRound",false);
		throw triggerEvent;
	}
	//room->getThread()->trigger(AfterDrawInitialCards,room,player,data);
	if(Config.value("BossYanluo").toBool()){
		foreach (ServerPlayer *p,room->getAlivePlayers())
			room->getThread()->trigger(GameStart,room,p);
		//room->getThread()->trigger(GameStart,room,room->getCurrent());
	}
}

void GameRule::acquireBossSkills(ServerPlayer *player,int level) const
{
    QStringList skills = Config.BossEndlessSkills;
    int num = qBound(qMin(5,skills.length()),5 + level - Config.BossLevel,qMin(10,skills.length()));
    for (int i = 0; i < num; i++) {
        QString skill = skills.at(qrand() % skills.length());
        skills.removeOne(skill);
        if(skill.contains("+")) {
            QStringList subskills = skill.split("+");
            skill = subskills.at(qrand() % subskills.length());
        }
        player->getRoom()->acquireSkill(player,skill);
    }
}

void GameRule::doBossModeDifficultySettings(ServerPlayer *lord) const
{
    Room *room = lord->getRoom();
    QList<ServerPlayer *> unions = room->getOtherPlayers(lord,true);
    int difficulty = Config.value("BossModeDifficulty").toInt();
    if((difficulty & (1 << BMDRevive))>0) {
        foreach (ServerPlayer *p,unions) {
            if(p->isDead()&&p->getMaxHp()>0) {
                room->revivePlayer(p,true);
                room->addPlayerHistory(p,".");
                if(!p->faceUp())
                    p->turnOver();
                if(p->isChained())
                    room->setPlayerChained(p);
                room->safeSetPlayerProperty(p,"hp",qMin(p->getMaxHp(),4));
                room->broadcastProperty(p,"hp");
                QStringList acquired = p->tag["BossModeAcquiredSkills"].toStringList();
                foreach (QString skillname,acquired) {
                    if(p->hasSkill(skillname,true))
                        acquired.removeOne(skillname);
                }
                p->tag["BossModeAcquiredSkills"] = QVariant::fromValue(acquired);
                room->handleAcquireDetachSkills(p,acquired,true);
                foreach (const Skill *skill,p->getSkillList()) {
                    if(!skill->getLimitMark().isEmpty())
                        room->setPlayerMark(p,skill->getLimitMark(),1);
                }
            }
        }
    }
    if((difficulty & (1 << BMDRecover))>0) {
        foreach (ServerPlayer *p,unions) {
            if(p->isAlive()&&p->isWounded()) {
                room->safeSetPlayerProperty(p,"hp",p->getMaxHp());
                room->broadcastProperty(p,"hp");
            }
        }
    }
    if((difficulty & (1 << BMDDraw))>0) {
        foreach (ServerPlayer *p,unions) {
            if(p->isAlive()&&p->getHandcardNum() < 4) {
                room->setTag("FirstRound",true); //For Manjuan
                try {
                    p->drawCards(4 - p->getHandcardNum());
                    room->setTag("FirstRound",false);
                }catch (TriggerEvent triggerEvent) {
                    if(triggerEvent==TurnBroken||triggerEvent==StageChange)
                        room->setTag("FirstRound",false);
                    throw triggerEvent;
                }
            }
        }
    }
    if((difficulty & (1 << BMDReward))>0) {
        foreach (ServerPlayer *p,unions) {
            if(p->isAlive()) {
                room->setTag("FirstRound",true); //For Manjuan
                try {
                    p->drawCards(2);
                    room->setTag("FirstRound",false);
                }catch (TriggerEvent triggerEvent) {
                    if(triggerEvent==TurnBroken||triggerEvent==StageChange)
                        room->setTag("FirstRound",false);
                    throw triggerEvent;
                }
            }
        }
    }
    if(Config.value("BossModeExp").toBool()) {
        foreach (ServerPlayer *p,room->getAlivePlayers()) {
            if(p->isLord()||p->isDead()) continue;

            QMap<QString,int> exp_map;
            while (true) {
                QStringList choices,allchoices;
                int exp = p->getMark("@bossExp");
                int level = room->getTag("BossModeLevel").toInt();
                exp_map["drawcard"] = 20 * level;
                exp_map["recover"] = 30 * level;
                exp_map["maxhp"] = p->getMaxHp() * 10 * level;
                exp_map["recovermaxhp"] = (20 + p->getMaxHp() * 10) * level;
                foreach (QString c,exp_map.keys()) {
                    allchoices << QString("[%1]|%2").arg(exp_map[c]).arg(c);
                    if(exp >= exp_map[c]&&(c != "recover"||p->isWounded()))
                        choices << QString("[%1]|%2").arg(exp_map[c]).arg(c);
                }

                QStringList acquired = p->tag["BossModeAcquiredSkills"].toStringList();
                foreach (QString a,acquired) {
                    if(!p->getAcquiredSkills().contains(a))
                        acquired.removeOne(a);
                }
                int len = qMin(4,acquired.length() + 1);
                foreach (QString skillname,Config.BossExpSkills.keys()) {
                    if(!Sanguosha->getSkill(skillname)) continue;
                    int cost = Config.BossExpSkills[skillname] * len;
                    allchoices << QString("[%1]||%2").arg(cost).arg(skillname);
                    if(p->hasSkill(skillname,true)) continue;
                    if(exp >= cost)
                        choices << QString("[%1]||%2").arg(cost).arg(skillname);
                }
                if(choices.isEmpty()) break;
                allchoices << "cancel";
                choices << "cancel";
                ServerPlayer *choiceplayer = p;
                if(!p->isOnline()) {
                    foreach (ServerPlayer *cp,room->getPlayers()) {
                        if(!cp->isLord()&&cp->isOnline()) {
                            choiceplayer = cp;
                            break;
                        }
                    }
                }
                room->setPlayerProperty(choiceplayer,"bossmodeexp",p->objectName());
                room->setPlayerProperty(choiceplayer,"bossmodeacquiredskills",acquired.join("+"));
                room->setPlayerProperty(choiceplayer,"bossmodeexpallchoices",allchoices.join("+"));
                QString choice = room->askForChoice(choiceplayer,"BossModeExpStore",choices.join("+"));
				LogMessage log;
				log.from = p;
                if(choice=="cancel") break;
                else if(choice.contains("||")) { // skill
                    QStringList skilllist;
                    QString skillattach = choice.split("|").last();
                    if(acquired.length()==4) {
                        QString skilldetach = room->askForChoice(choiceplayer,"BossModeExpStoreSkillDetach",acquired.join("+"));
                        skilllist << "-" + skilldetach;
                        acquired.removeOne(skilldetach);
                    }
                    acquired.append(skillattach);
                    p->tag["BossModeAcquiredSkills"] = QVariant::fromValue(acquired);
                    int cost = choice.split("]").first().mid(1).toInt();
                    skilllist.append(skillattach);

                    log.type = "#UseExpPoint";
                    log.arg = QString::number(cost);
                    log.arg2 = "BossModeExpStore:acquireskill";
                    room->sendLog(log);

                    room->removePlayerMark(p,"@bossExp",cost);
                    room->handleAcquireDetachSkills(p,skilllist,true);
                } else {
                    QString type = choice.split("|").last();
                    int cost = choice.split("]").first().mid(1).toInt();
                    room->removePlayerMark(p,"@bossExp",cost);

                    log.type = "#UseExpPoint";
                    log.arg = QString::number(cost);
                    log.arg2 = "BossModeExpStore:" + type;
                    room->sendLog(log);

                    if(type=="drawcard") {
                        room->setTag("FirstRound",true); //For Manjuan
                        try {
                            p->drawCards(1);
                            room->setTag("FirstRound",false);
                        }catch (TriggerEvent triggerEvent) {
                            if(triggerEvent==TurnBroken||triggerEvent==StageChange)
                                room->setTag("FirstRound",false);
                            throw triggerEvent;
                        }
                    } else {
                        int hp = p->getHp();
                        int maxhp = p->getMaxHp();
                        if(type.contains("maxhp")) {
                            room->safeSetPlayerProperty(p,"maxhp",maxhp + 1);
                            room->broadcastProperty(p,"maxhp");
                        }
                        if(type.contains("recover")) {
                            room->safeSetPlayerProperty(p,"hp",hp + 1);
                            room->broadcastProperty(p,"hp");
                        }

                        log.type = "#GetHp";
                        log.arg = QString::number(p->getHp());
                        log.arg2 = QString::number(p->getMaxHp());
                        room->sendLog(log);
                    }
                }
            }
        }
    }
}

void GameRule::rewardAndPunish(ServerPlayer *killer,ServerPlayer *victim) const
{
    Room *room = victim->getRoom();

    if(room->getMode()=="03_1v2") {
        if(victim->getRole()=="rebel") {
            foreach (ServerPlayer *p,room->getOtherPlayers(victim)) {
                if(p->isAlive()&&p->getRole()=="rebel") {
                    QString choices = "draw+cancel";
                    if(p->isWounded()) choices = "draw+recover+cancel";
                    QString choice = room->askForChoice(p,"doudizhu",choices);
                    if(choice=="cancel") continue;
                    if(choice=="draw")
                        p->drawCards(2,"doudizhu");
                    else
                       room->recover(p,RecoverStruct("doudizhu"));
                }
            }
        }
    } else if(room->getMode()=="04_2v2") {
        foreach (ServerPlayer *p,room->getOtherPlayers(victim)) {
            if(p->getRole()==victim->getRole()) {
                p->drawCards(1,"04_2v2");
                break;
            }
        }
    }else if(killer&&killer->isAlive()&&victim->getMark("wujieNoRewardAndPunish-Keep")<1){
		if(room->getMode()=="06_XMode"||room->getMode()=="04_boss"||room->getMode()=="06_ol"
			|| room->getMode()=="05_ol"||room->getMode()=="08_defense")
			return;
		if(room->getMode()=="06_3v3") {
			if(Config.value("3v3/OfficialRule","2013").toString().startsWith("201"))
				killer->drawCards(2,"kill");
			else
				killer->drawCards(3,"kill");
		} else {
			if(victim->getRole()=="rebel"&&killer != victim)
				killer->drawCards(3,"kill");
			else if(victim->getRole()=="loyalist"&&killer->getRole()=="lord")
				killer->throwAllHandCardsAndEquips("kill");
		}
	}
}

QString GameRule::getWinner(ServerPlayer *victim,Room *room) const
{
    QString winner;

    if(room->getMode()=="06_3v3") {
        switch (victim->getRoleEnum()) {
        case Player::Lord: winner = "renegade+rebel"; break;
        case Player::Renegade: winner = "lord+loyalist"; break;
        default:
            break;
        }
    } else if(room->getMode()=="06_XMode") {
        QString role = victim->getRole();
        ServerPlayer *leader = victim->tag["XModeLeader"].value<ServerPlayer *>();
        if(leader->tag["XModeBackup"].toStringList().isEmpty()) {
            if(role.startsWith('r'))
                winner = "lord+loyalist";
            else
                winner = "renegade+rebel";
        }
    } else if(room->getMode()=="08_defense"||room->getMode()=="04_2v2") {
        QStringList alive_roles = room->aliveRoles(victim);
        if(!alive_roles.contains("loyalist"))
            winner = "rebel";
        else if(!alive_roles.contains("rebel"))
            winner = "loyalist";
    } else if(Config.EnableHegemony) {
        QString init_kingdom;
        foreach (ServerPlayer *p,room->getAlivePlayers()) {
            if(!p->property("basara_generals").toString().isEmpty())
                return winner;
            if(init_kingdom.isEmpty())
                init_kingdom = p->getKingdom();
            else if(init_kingdom != p->getKingdom())
                return winner;
        }

		QStringList winners;
		foreach (ServerPlayer *p,room->getPlayers()) {
			if(p->isAlive()) winners << p->objectName();
			else if(p->getKingdom()==init_kingdom) {
				QStringList generals = p->property("basara_generals").toString().split("+");
				if(generals.size()==1&&!Config.Enable2ndGeneral) continue;
				if(generals.size() >= 2) continue;

				//if someone showed his kingdom before death,
				//he should be considered victorious as well if his kingdom survives
				winners << p->objectName();
			}
		}
        if(!winners.isEmpty()) {
            foreach (ServerPlayer *player,room->getAllPlayers()) {
				QStringList generals = player->property("basara_generals").toString().split("+");
				if(generals.isEmpty()) continue;
                if(player->getGeneralName()=="anjiang") {
                    room->changePlayerGeneral(player,generals.takeFirst());
                    //room->setPlayerProperty(player,"kingdom",player->getGeneral()->getKingdom());
                    room->setPlayerProperty(player,"role",BasaraMode::getMappedRole(player->getKingdom()));
                }
                if(Config.Enable2ndGeneral&&player->getGeneral2Name()=="anjiang")
                    room->changePlayerGeneral2(player,generals.takeLast());
				room->safeSetPlayerProperty(player,"basara_generals",generals.join("+"));
				room->notifyProperty(player,player,"basara_generals");
            }
			winner = winners.join("+");
        }
    } else {
        QStringList alive_roles = room->aliveRoles(victim);
        switch (victim->getRoleEnum()) {
        case Player::Lord: {
            if(alive_roles.length()==1&&alive_roles.first()=="renegade")
                winner = room->getAlivePlayers().first()->objectName();
            else
                winner = "rebel";
            break;
        }case Player::Loyalist: {
            if(alive_roles.length()==1&&alive_roles.first()=="renegade")
                winner = room->getAlivePlayers().first()->objectName();
            else {
                if(!alive_roles.contains("lord")&&!alive_roles.contains("loyalist")&&!alive_roles.contains("renegade"))
                    winner = "rebel";
            }
            break;
        }case Player::Rebel: {
            if(alive_roles.length()==1&&alive_roles.first()=="renegade")
                winner = room->getAlivePlayers().first()->objectName();
            else if(!alive_roles.contains("rebel")&&!alive_roles.contains("renegade"))
                winner = "lord+loyalist";
            break;
        }case Player::Renegade: {
            if(!alive_roles.contains("rebel")&&!alive_roles.contains("renegade"))
                winner = "lord+loyalist";
            break;
        }default:
            break;
        }
    }
    return winner;
}

HulaoPassMode::HulaoPassMode(QObject *parent)
    : GameRule(parent)
{
    setObjectName("hulaopass_mode");
    events << StageChange;
}

bool HulaoPassMode::trigger(TriggerEvent triggerEvent,Room *room,ServerPlayer *player,QVariant &data) const
{
    switch (triggerEvent) {
    case StageChange: {
        ServerPlayer *lord = room->getLord();
        room->setPlayerMark(lord,"secondMode",1);
        QString lvbu = room->askForChoice(lord,"hulaopass_shenlvbu","bnzs+sgwq");
        if(lvbu=="bnzs")
            room->changeHero(lord,"shenlvbu2",true,true,false,false);
        else
            room->changeHero(lord,"shenlvbu3",true,true,false,false);

        LogMessage log;
        log.type = "$AppendSeparator";
        room->sendLog(log);

        log.type = "#HulaoTransfigure";
        log.arg = "#shenlvbu1";
        log.arg2 = lvbu=="bnzs" ? "#shenlvbu2" : "#shenlvbu3";
        room->sendLog(log);

		JsonArray arg;
		arg << QSanProtocol::S_GAME_EVENT_CHANGE_BGM;
		arg << "audio/system/music_danji.ogg";
		arg << true;
		room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT,arg);

        //room->doLightbox("$StageChange",5000);
        if(lvbu=="bnzs")
            room->doSuperLightbox("shenlvbu2","StageChange");
        else
            room->doSuperLightbox("shenlvbu3","StageChange");

		room->throwCard(lord->getJudgingAreaID(),CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,""),nullptr);
        if(!lord->faceUp())
            lord->turnOver();
        if(lord->isChained())
            room->setPlayerChained(lord);
        break;
    }
    case GameReady: {
        // Handle global events
        if(!player) {
			QList<int> n_list;
            room->setTag("FirstRound",true);
            foreach (ServerPlayer *p,room->getAlivePlayers()) {
                if(p->isLord()) n_list << 8;
				else n_list << p->getSeat()+1;
            }
			room->drawCards(room->getAlivePlayers(),n_list,"InitialHandCards");/*
			foreach(ServerPlayer *p,room->getAlivePlayers())
				p->setProperty("InitialHandCards",ListI2V(p->handCards()));*/

			room->setTag("FirstRound",false);
			foreach(ServerPlayer *p,room->getAllPlayers()){
				room->getThread()->trigger(GameStart,room,p);
			}
			//room->getThread()->trigger(GameStart,room,room->getCurrent());
			foreach (int id,room->getDrawPile())
				Sanguosha->getCard(id)->setFlags("-visible");
            return false;
        }
        break;
    }
    case HpChanged: {
        if(player->isLord()&&player->getHp() <= 4&&player->getMark("secondMode") < 1&&player->getGeneralName()=="shenlvbu1")
            throw StageChange;
        break;
    }
    case GameOverJudge: {
        if(player->isLord())
            room->gameOver("rebel");
        else
            if(room->aliveRoles(player).length()==1)
                room->gameOver("lord");

        return false;
    }
    case BuryVictim: {
        if(player->hasFlag("actioned")) room->setPlayerFlag(player,"-actioned");

        LogMessage log;
        log.type = "#Reforming";
        log.from = player;
        room->sendLog(log);

        player->bury();
        room->setPlayerProperty(player,"hp",0);

        foreach (ServerPlayer *p,room->getOtherPlayers(room->getLord())) {
            if(p->isAlive()&&p->askForSkillInvoke("draw_1v3",data,false))
                p->drawCards(1,"draw_1v3");
        }

        return false;
    }
    case TurnStart: {
        if(player->isDead()) {
            JsonArray arg;
            arg << QSanProtocol::S_GAME_EVENT_PLAYER_REFORM << player->objectName();
            room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT,arg);

            QString choice = player->isWounded() ? "recover" : "draw";
            if(player->isWounded()&&player->getHp()>0)
                choice = room->askForChoice(player,"Hulaopass","recover+draw");

			LogMessage log;
			log.from = player;
			log.arg = "1";
            if(choice=="draw") {
                log.type = "#ReformingDraw";
                room->sendLog(log);
                player->drawCards(1,"reform");
            } else {
                log.type = "#ReformingRecover";
                room->sendLog(log);
                room->setPlayerProperty(player,"hp",player->getHp() + 1);
            }

            if(player->getHp() + player->getHandcardNum() >= 6) {
                log.type = "#ReformingRevive";
                room->sendLog(log);
                room->revivePlayer(player);
            }
			return false;
		}
    }
    default:
        break;
    }
    return GameRule::trigger(triggerEvent,room,player,data);
}

BasaraMode::BasaraMode(QObject *parent)
    : GameRule(parent)
{
    setObjectName("basara_mode");
    events << DamageInflicted << BeforeGameOverJudge;
}

QString BasaraMode::getMappedRole(const QString &role)
{
    static QMap<QString,QString> roles;
    if(roles.isEmpty()) {
        roles["wei"] = "lord";
        roles["shu"] = "loyalist";
        roles["wu"] = "rebel";
        roles["qun"] = "renegade";
    }
    return roles[role];
}

int BasaraMode::getPriority(TriggerEvent) const
{
    return 15;
}

void BasaraMode::playerShowed(ServerPlayer *player) const
{
    QString name = player->property("basara_generals").toString();
    if(name.isEmpty()) return;
    QStringList names = name.split("+");

    Room *room = player->getRoom();
    if(Config.EnableHegemony) {
        QMap<QString,int> kingdom_roles;
        foreach(ServerPlayer *p,room->getOtherPlayers(player))
            kingdom_roles[p->getKingdom()]++;

        if(kingdom_roles[Sanguosha->getGeneral(names.first())->getKingdom()] >= Config.value("HegemonyMaxShown",2).toInt()
           &&player->getGeneralName()=="anjiang")
            return;
    }

    if(room->askForChoice(player,"RevealGeneral","yes+no")=="yes") {
        QString general_name = room->askForGeneral(player,names);

        generalShowed(player,general_name);
        if(Config.EnableHegemony) room->getThread()->trigger(GameOverJudge,room,player);
        playerShowed(player);
    }
}

void BasaraMode::generalShowed(ServerPlayer *player,QString general_name) const
{
    QString name = player->property("basara_generals").toString();
    if(name.isEmpty()) return;
    Room *room = player->getRoom();
    QStringList names = name.split("+");
    names.removeOne(general_name);
    room->safeSetPlayerProperty(player,"basara_generals",names.join("+"));
    room->notifyProperty(player,player,"basara_generals");
    if(player->getGeneralName()=="anjiang") {
        room->changeHero(player,general_name,false,false,false,false);
        if(Config.EnableHegemony)
            room->setPlayerProperty(player,"role",getMappedRole(player->getKingdom()));
    } else
        room->changeHero(player,general_name,false,false,true,false);
    LogMessage log;
    log.type = "#BasaraReveal";
    log.from = player;
    log.arg = player->getGeneralName();
    if(player->getGeneral2()) {
        log.type = "#BasaraRevealDual";
        log.arg2 = player->getGeneral2Name();
    }
    room->sendLog(log);
}

bool BasaraMode::trigger(TriggerEvent triggerEvent,Room *room,ServerPlayer *player,QVariant &data) const
{
    // Handle global events
	if(player){
		player->tag["triggerEvent"] = triggerEvent;
		player->tag["triggerEventData"] = data; // For AI
	}
    switch (triggerEvent) {
    case GameReady: {
		if(player) break;
		if(Config.EnableHegemony)
			room->setTag("SkipNormalDeathProcess",true);
		foreach (ServerPlayer *sp,room->getAlivePlayers()) {
			room->setPlayerProperty(sp,"general","anjiang");
			sp->setGender(General::Sexless);
			room->setPlayerProperty(sp,"kingdom","god");

			LogMessage log;
			log.type = "#BasaraGeneralChosen";
			log.arg = sp->property("basara_generals").toString().split("+").first();

			if(Config.Enable2ndGeneral) {
				room->setPlayerProperty(sp,"general2","anjiang");
				log.type = "#BasaraGeneralChosenDual";
				log.arg2 = sp->property("basara_generals").toString().split("+").last();
			}

			room->sendLog(log,sp);
		}
        break;
    }case CardEffected: {
        if(player->getPhase()==Player::NotActive) {
            CardEffectStruct ces = data.value<CardEffectStruct>();
			if(ces.card&&(ces.card->isKindOf("TrickCard")||ces.card->isKindOf("Slash")))
				playerShowed(player);

            const ProhibitSkill *prohibit = room->isProhibited(ces.from,ces.to,ces.card);
            if(prohibit) {
                if(prohibit->isVisible()&&ces.to->hasSkill(prohibit)) {
                    LogMessage log;
                    log.type = "#SkillAvoid";
                    log.from = ces.to;
                    log.arg = prohibit->objectName();
					if(ces.card)
						log.arg2 = ces.card->objectName();
                    room->sendLog(log);
                    room->broadcastSkillInvoke(prohibit->objectName());
                    room->notifySkillInvoked(ces.to,prohibit->objectName());
                } else {
                    const Skill *skill = Sanguosha->getMainSkill(prohibit->objectName());
                    if(skill&&skill->isVisible()&&ces.to->hasSkill(skill)) {
                        LogMessage log;
                        log.type = "#SkillAvoid";
                        log.from = ces.to;
                        log.arg = skill->objectName();
						if(ces.card)
							log.arg2 = ces.card->objectName();
                        room->sendLog(log);
                        room->broadcastSkillInvoke(skill->objectName());
                        room->notifySkillInvoked(ces.to,skill->objectName());
                    }
                }
                return true;
            }
        }
        break;
    }case EventPhaseStart: {
        if(player->getPhase()==Player::RoundStart)
            playerShowed(player);
        break;
    }case DamageInflicted: {
        playerShowed(player);
        break;
    }case BeforeGameOverJudge: {
        if(player->getGeneralName()=="anjiang") {
            QStringList generals = player->property("basara_generals").toString().split("+");
            room->changePlayerGeneral(player,generals.takeFirst());

            room->setPlayerProperty(player,"kingdom",player->getGeneral()->getKingdom());
            if(Config.EnableHegemony)
                room->setPlayerProperty(player,"role",getMappedRole(player->getKingdom()));

            room->safeSetPlayerProperty(player,"basara_generals",generals.join("+"));
            room->notifyProperty(player,player,"basara_generals");
        }
        if(Config.Enable2ndGeneral&&player->getGeneral2Name()=="anjiang") {
            QStringList generals = player->property("basara_generals").toString().split("+");
            room->changePlayerGeneral2(player,generals.last());
            room->safeSetPlayerProperty(player,"basara_generals",QString(""));
            room->notifyProperty(player,player,"basara_generals");
        }
        break;
    }case BuryVictim: {
        DeathStruct death = data.value<DeathStruct>();
        player->bury();
        if(Config.EnableHegemony) {
            ServerPlayer *killer = death.damage ? death.damage->from : nullptr;
            if(killer&&killer->getKingdom() != "god") {
                if(killer->getKingdom()==player->getKingdom())
                    killer->throwAllHandCardsAndEquips();
                else if(killer->isAlive())
                    killer->drawCards(3,"kill");
            }
            return true;
        }
        break;
    }default:
        break;
    }
    return false;
}
