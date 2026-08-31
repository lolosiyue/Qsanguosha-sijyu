#include "game-session-controller.h"

#include "banpair.h"
#include "card-movement-service.h"
#include "engine.h"
#include "engine-runtime-context.h"
#include "gamerule.h"
#include "lua-runtime.h"
#include "lua-wrapper.h"
#include "miniscenarios.h"
#include "qt-collection-utils.h"
#include "room.h"
#include "room-roster.h"
#include "roomthread.h"
#include "roomthread1v1.h"
#include "roomthread3v3.h"
#include "roomthreadxmode.h"
#include "scenario.h"
#include "server.h"
#include "server-info.h"
#include "serverplayer.h"
#include "settings.h"
#include "standard.h"

#include <QFile>
#include <QScopeGuard>
#include <QSet>

using namespace QSanProtocol;

GameSessionController::GameSessionController(Room &room)
    : m_room(room), m_state(State::Waiting),
      m_preparationPhase(PreparationPhase::None),
      m_terminationCause(TerminationCause::None)
{
}

GameSessionController::State GameSessionController::state() const
{
    return m_state;
}

GameSessionController::PreparationPhase GameSessionController::preparationPhase() const
{
    return m_preparationPhase;
}

GameSessionController::TerminationCause GameSessionController::terminationCause() const
{
    return m_terminationCause;
}

bool GameSessionController::requestStart()
{
    if (m_state != State::Waiting)
        return false;
    return transitionTo(State::Preparing);
}

bool GameSessionController::hasGameStarted() const
{
    return m_state == State::Initializing || m_state == State::Playing;
}

bool GameSessionController::isPlaying() const
{
    return m_state == State::Playing;
}

bool GameSessionController::isTerminal() const
{
    return m_state == State::Finished || m_state == State::Aborted;
}

void GameSessionController::abort(TerminationCause cause)
{
    if (isTerminal())
        return;
    if (transitionTo(State::Aborted))
        m_terminationCause = cause;
}

void GameSessionController::markGameReadyCompleted()
{
    transitionTo(State::Playing);
}

bool GameSessionController::transitionTo(State next)
{
    if (next == m_state)
        return true;
    if (isTerminal()) {
        qWarning("Game session transition rejected from terminal state %d to %d.",
                 int(m_state), int(next));
        return false;
    }

    const bool legal = next == State::Finished || next == State::Aborted
        || (m_state == State::Waiting && next == State::Preparing)
        || (m_state == State::Preparing && next == State::Initializing)
        || (m_state == State::Initializing && next == State::Playing);
    if (!legal) {
        qWarning("Illegal game session transition from %d to %d.",
                 int(m_state), int(next));
        return false;
    }

    m_state = next;
    if (next == State::Initializing || next == State::Playing || isTerminal())
        m_preparationPhase = PreparationPhase::None;
    return true;
}
void GameSessionController::gameOver(const QString &winner, TerminationCause cause)
{
	QVariant data = winner;

	ServerPlayer* target = nullptr;

	// 直接從全體玩家清單中尋找有效目標
	// 優先順序：存活玩家 > RestPlayer > 任何玩家
	foreach (ServerPlayer* p, m_room.getPlayers()) {
		if (!p) continue;
		// 生還玩家優先
		if (p->isAlive()) {
			target = p;
			break;
		}
	}

	// 降級策略：若無生還玩家，嘗試 RestPlayer
	if (!target) {
		foreach (ServerPlayer* p, m_room.getPlayers()) {
			if (!p) continue;
			if (p->property("RestPlayer").toBool()) {
				target = p;
				break;
			}
		}
	}

	// 最終降級：使用任何可用的玩家
	if (!target) {
		target = m_room.getPlayers().first();
	}

	m_room.thread->trigger(GameOver, &m_room, target, data);

	if (transitionTo(State::Finished))
		m_terminationCause = cause;

	emit m_room.game_over(winner);

	if (m_room.mode.contains("_mini_")){
		QStringList winners = winner.split("+");
		foreach(ServerPlayer*sp, m_room.getPlayers()){
			if (!sp) continue;
			if (sp->getState() != "robot"&&(winners.contains(sp->getRole())
				|| winners.contains(sp->objectName()))){
				QString id = Config.GameMode.mode_id;
				id.replace("_mini_", "");
				int current = id.toInt();
				if (current < Sanguosha->getMiniSceneCounts()){
					int stage = Config.value("MiniSceneStage", 1).toInt();
					if (current + 1 > stage) Config.setValue("MiniSceneStage", current + 1);
					id = QString(MiniScene::S_KEY_MINISCENE).arg(current + 1);
					Config.setValue("GameMode", id);
					Config.GameMode = Sanguosha->getGameMode(id);
				}
				break;
			}
		}
	}
	Config.AIDelay = Config.OriginAIDelay;

	QString name = m_room.getTag("NextGameMode").toString();
	if (!name.isEmpty()){
		GameModeStruct nextMode = Sanguosha->getGameMode(name);
		if (nextMode.isValid()) {
			Config.GameMode = nextMode;
			Config.setValue("GameMode", name);
		} else {
			qWarning("Next game mode '%s' is unavailable; keeping mode '%s'.",
				qPrintable(name), qPrintable(Config.GameMode.mode_id));
		}
		m_room.removeTag("NextGameMode");
	}
	data = m_room.getTag("NextGameSecondGeneral");
	if (data.canConvert<bool>()){
		Config.Enable2ndGeneral = data.toBool();
		Config.setValue("Enable2ndGeneral", data);
		m_room.removeTag("NextGameSecondGeneral");
	}

	QStringList all_roles;
	QList<ServerPlayer*> playersCopy = m_room.getPlayers();
	foreach(ServerPlayer*player, playersCopy){
		if (!player) continue;
		all_roles << player->getRole();
	}
	const QStringList winnerTokens = winner == QLatin1String(".")
		? QStringList() : winner.split(QLatin1Char('+'), Qt::SkipEmptyParts);
	QVariantMap arg{{QStringLiteral("schema_version"), 1},
		{QStringLiteral("standoff"), winner == QLatin1String(".")},
		{QStringLiteral("winner_tokens"), winnerTokens},
		{QStringLiteral("roles"), all_roles}};
	m_room.doBroadcastNotify(S_COMMAND_GAME_OVER, arg);
	throw GameFinished;
}

void GameSessionController::prepareForStart()
{
	m_preparationPhase = PreparationPhase::AssigningRoles;
	auto phaseReset = qScopeGuard([this]() { m_preparationPhase = PreparationPhase::None; });
	if (m_room.scenario){
		bool already = false;
		if (m_room.scenario->objectName() == "challengedeveloper"){
			Config.EnableCheat = false;
			Config.setValue("EnableCheat", false);

			QList<ServerPlayer*> humans;
			foreach(ServerPlayer*p, m_room.getPlayers()){
				if (p->getState() != "robot")
					humans << p;
			}
			if (!humans.isEmpty()){
				already = true;
				ServerPlayer*human = humans.at(qsanRandomBounded(humans.length()));
				human->setGeneralName("sujiang");
				m_room.broadcastProperty(human, "general");
				human->setRole("lord");
				m_room.broadcastProperty(human, "role");

				foreach(ServerPlayer*p, m_room.getPlayers()){
					if (p == human) continue;
					p->setGeneralName("sujiang");
					m_room.broadcastProperty(p, "general");
					p->setRole("rebel");
					m_room.broadcastProperty(p, "role");
				}
			}
		}
		if (!already){
			QStringList generals, roles;
			m_room.scenario->assign(generals, roles);
			const QList<ServerPlayer *> players = m_room.getPlayers();
			for (int i = 0; i < players.length(); i++){
				ServerPlayer*player = players[i];
				if (generals.length() > i){
					player->setGeneralName(generals[i]);
					m_room.broadcastProperty(player, "general");
				}
				player->setRole(roles[i]);
				if (m_room.scenario->exposeRoles()||roles[i]=="lord")
					m_room.broadcastProperty(player, "role");
				else
					m_room.notifyProperty(player, player, "role");
			}
		}
		m_room.updateStateItem();
	} else if (m_room.mode == "06_3v3" || m_room.mode == "06_XMode" || m_room.mode == "02_1v1"){
		return;
	} else {
		GameModeStruct gameMode = Sanguosha->getGameMode(m_room.mode);
		if (m_room.mode == "08_defense" || (Config.RandomSeat && gameMode.shuffle_seats)) {
			QList<ServerPlayer *> players = m_room.getPlayers();
			qsanShuffle(players);
			m_room.replacePlayerOrder(players);
		}
		if (m_room.mode!="04_2v2"&&!Config.EnableHegemony&&Config.value("FreeAssign").toBool()){
			ServerPlayer*owner = m_room.getOwner();
			if (owner&&owner->isOnline()){
				m_room.notifyMoveFocus(owner, S_COMMAND_CHOOSE_ROLE);
				if(m_room.doRequest(owner, S_COMMAND_CHOOSE_ROLE, QVariant(), true)){
					QVariant clientReply = owner->getClientReply();
					if(clientReply.canConvert<JsonArray>()){
						JsonArray replyArray = clientReply.value<JsonArray>();/*
						if(Config.FreeAssignSelf){
							QString name = replyArray.value(0).value<JsonArray>().value(0).toString();
							QString role = replyArray.value(1).value<JsonArray>().value(0).toString();
							owner = findChild<ServerPlayer*>(name);
							owner->setRole(role);
							QList<ServerPlayer*> all_players = getPlayers();
							all_players.removeOne(owner);
							QStringList roles = Sanguosha->getRoleList(mode);
							roles.removeOne(role);
							qsanShuffle(roles);
							for (int i = 0; i < all_players.count(); i++){
								all_players[i]->setRole(roles[i]);
								if (mode.contains("_")||roles[i] == "lord")
									broadcastProperty(all_players[i], "role", roles[i]);
								else
									notifyProperty(all_players[i], all_players[i], "role");
							}
						}else{*/
							QList<ServerPlayer*> all_players = m_room.getPlayers();
							QList<ServerPlayer*> players = all_players;
							QStringList roles = Sanguosha->getRoleList(m_room.mode);
							for (int i = 0; i < replyArray.value(0).value<JsonArray>().size(); i++){
								QString name = replyArray.value(0).value<JsonArray>().value(i).toString();
								QString role = replyArray.value(1).value<JsonArray>().value(i).toString();
								owner = m_room.findChild<ServerPlayer*>(name);
								players.swapItemsAt(i, players.indexOf(owner));
								all_players.removeOne(owner);
								roles.removeOne(role);
								owner->setRole(role);
							}
							m_room.replacePlayerOrder(players);
							qsanShuffle(roles);
							for (int i = 0; i < all_players.count(); i++)
								all_players[i]->setRole(roles[i]);
			for (int i = 0; i < players.count(); i++){
				if (Sanguosha->hasShowRoleMode(m_room.mode) || players[i]->getRole() == "lord")
					m_room.broadcastProperty(players[i], "role");
				else
					m_room.notifyProperty(players[i], players[i], "role");
			}
						//}
						m_room.adjustSeats();
						return;
					}
				}
			}
		}
		if (m_room.mode == "04_1v3" || m_room.mode == "04_boss"){
			const QList<ServerPlayer *> players = m_room.getPlayers();
			ServerPlayer*lord = players[qsanRandomBounded(4)];
			for (int i = 0; i < 4; i++){
				if (players[i] == lord) players[i]->setRole("lord");
				else players[i]->setRole("rebel");
				m_room.broadcastProperty(players[i], "role");
			}
			m_room.adjustSeats();
			return;
		}
		assignRoles();
	}
	m_room.adjustSeats();
}

bool GameSessionController::makeSurrender(ServerPlayer *initiator)
{
	bool loyalGiveup = true;
	int loyalAlive = 0;
	bool renegadeGiveup = true;
	int renegadeAlive = 0;
	bool rebelGiveup = true;
	int rebelAlive = 0;

	// broadcast polling request
	QList<ServerPlayer*> playersAlive;
	foreach(ServerPlayer*player, m_room.getPlayers()){
		QString playerRole = player->getRole();
		if ((playerRole == "loyalist" || playerRole == "lord")&&player->isAlive()) loyalAlive++;
		else if (playerRole == "rebel"&&player->isAlive()) rebelAlive++;
		else if (playerRole == "renegade"&&player->isAlive()) renegadeAlive++;

		if (player != initiator&&player->isAlive()&&player->isOnline()){
			player->m_commandArgs = initiator->getGeneral()->objectName();
			playersAlive << player;
		}
	}
	m_room.doBroadcastRequest(playersAlive, S_COMMAND_SURRENDER);

	// collect polls
	foreach(ServerPlayer*player, playersAlive){
		bool result = false;
		if (!player->m_isClientResponseReady || !player->getClientReply().canConvert<bool>())
			result = !player->isOnline();
		else
			result = player->getClientReply().toBool();

		QString playerRole = player->getRole();
		if (playerRole == "loyalist" || playerRole == "lord"){
			loyalGiveup&= result;
			if (player->isAlive()) loyalAlive++;
		} else if (playerRole == "rebel"){
			rebelGiveup&= result;
			if (player->isAlive()) rebelAlive++;
		} else if (playerRole == "renegade"){
			renegadeGiveup&= result;
			if (player->isAlive()) renegadeAlive++;
		}
	}

	// vote counting
	if (loyalGiveup&&renegadeGiveup&&!rebelGiveup)
		gameOver("rebel", TerminationCause::Surrender);
	else if (loyalGiveup&&!renegadeGiveup&&rebelGiveup)
		gameOver("renegade", TerminationCause::Surrender);
	else if (!loyalGiveup&&renegadeGiveup&&rebelGiveup)
		gameOver("lord+loyalist", TerminationCause::Surrender);
	else if (loyalGiveup&&renegadeGiveup&&rebelGiveup){
		// if everyone give up, then ensure that the initiator doesn't win.
		QString playerRole = initiator->getRole();
		if (playerRole == "lord" || playerRole == "loyalist")
			gameOver(renegadeAlive >= rebelAlive ? "renegade" : "rebel", TerminationCause::Surrender);
		else if (playerRole == "renegade")
			gameOver(loyalAlive >= rebelAlive ? "loyalist+lord" : "rebel", TerminationCause::Surrender);
		else if (playerRole == "rebel")
			gameOver(renegadeAlive >= loyalAlive ? "renegade" : "loyalist+lord", TerminationCause::Surrender);
	}

	m_room.m_surrenderRequestReceived = false;

	initiator->setFlags("Global_ForbidSurrender");
	m_room.doNotify(initiator, S_COMMAND_ENABLE_SURRENDER, QVariant(false));
	return true;
}
void GameSessionController::assignGeneralsForPlayers(const QList<ServerPlayer *> &toAssign)
{
	QSet<QString> existed;
	foreach(ServerPlayer*player, m_room.getPlayers()){
		QString gn = player->getGeneralName();
		if(gn.isEmpty()) continue;
		existed << gn;
		if(gn=="yinni_hide")
			existed << player->property("yinni_general").toString();
		gn = player->getGeneral2Name();
		if(gn.isEmpty()) continue;
		existed << gn;
		if(gn=="yinni_hide")
			existed << player->property("yinni_general2").toString();
	}
	if (Config.Enable2ndGeneral){
		foreach(QString name, BanPair::getAllBanSet())
			existed << name;
		if (toAssign.first()->getGeneral()){
			foreach(QString name, BanPair::getSecondBanSet())
				existed << name;
		}
	}

	const int max_choice = (Config.EnableHegemony&&Config.Enable2ndGeneral) ?
		Config.value("HegemonyMaxChoice", 7).toInt() : Config.value("MaxChoice", 5).toInt();
	const int total = Sanguosha->getGeneralCount();
	const int max_available = (total - existed.size()) / toAssign.length();
	const int choice_count = qMin(max_choice, max_available);
	QStringList choices = Sanguosha->getRandomGenerals(total - existed.size(), existed);

	if (Config.EnableHegemony){
		if (toAssign.first()->getGeneral()){
			foreach(ServerPlayer*sp, m_room.getPlayers()){
				QStringList old_list = sp->getSelected();
				sp->clearSelected();

				//keep legal generals
				foreach(QString name, old_list){
					if (Sanguosha->getGeneral(name)->getKingdom() != sp->getGeneral()->getKingdom()
						|| sp->findReasonable(old_list, true) == name){
						sp->addToSelected(name);
						old_list.removeOne(name);
					}
				}

				//drop the rest and add new generals
				while (old_list.length()){
					QString choice = sp->findReasonable(choices);
					sp->addToSelected(choice);
					old_list.pop_front();
					choices.removeOne(choice);
				}
			}
			return;
		}
	}

	foreach(ServerPlayer*player, toAssign){
		player->clearSelected();
		QStringList hidden;
		for (int i = 0; i < choice_count; i++){
			hidden << "unknown";
			QString choice = player->findReasonable(choices, true);
			if (choice.isEmpty()) break;
			player->addToSelected(choice);
			choices.removeOne(choice);
		}
		m_room.doAnimate(S_ANIMATE_HUASHEN, player->objectName(), hidden.join(":"));
	}
	if(m_room.thread) m_room.thread->delay();
}

void GameSessionController::assignGeneralsForPlayersOfJianGeDefenseMode(const QList<ServerPlayer *> &toAssign)
{
	QMap<QString, QSet<QString> > existed;
	foreach(ServerPlayer*player, m_room.getPlayers()){
		if (player->property("jiange_defense_type").toString() != "general")
			continue;
		if (player->getGeneral())
			existed[player->getGeneral()->getKingdom()] << player->getGeneralName();
		if (player->getGeneral2())
			existed[player->getGeneral2()->getKingdom()] << player->getGeneral2Name();
	}
	if (Config.Enable2ndGeneral){
		foreach(QString name, BanPair::getAllBanSet()){
			const General*gen = Sanguosha->getGeneral(name);
			if (gen) existed[gen->getKingdom()] << name;
		}
		if (toAssign.first()->getGeneral()){
			foreach(QString name, BanPair::getSecondBanSet()){
				const General*gen = Sanguosha->getGeneral(name);
				if (gen) existed[gen->getKingdom()] << name;
			}
		}
	}

	QMap<QString, QStringList> general_choices;
	foreach(QString key, Config.JianGeDefenseKingdoms.keys()){
		QString kingdom = Config.JianGeDefenseKingdoms[key];
		int total = Sanguosha->getGeneralCount(false, kingdom);
		general_choices[kingdom] = Sanguosha->getRandomGenerals(total - existed[kingdom].size(), existed[kingdom], kingdom);
	}

	const int max_choice = Config.value("MaxChoice", 5).toInt();
	foreach(ServerPlayer*player, toAssign){
		QStringList choices;
		int choice_count = 0;
		QString kingdom = Config.JianGeDefenseKingdoms[player->getRole()];
		QString jiange_defense_type = player->property("jiange_defense_type").toString();
		if (jiange_defense_type == "machine"){
			choices = Config.JianGeDefenseMachine[kingdom];
			choice_count = choices.length();
		} else if (jiange_defense_type == "soul"){
			choices = Config.JianGeDefenseSoul[kingdom];
			choice_count = choices.length();
		} else{
			int total = Sanguosha->getGeneralCount(false, kingdom);
			int max_available = (total - existed[kingdom].size()) / 2;
			choice_count = qMin(max_choice, max_available);
			choices = general_choices[kingdom];
		}

		player->clearSelected();

		for (int i = 0; i < choice_count; i++){
			QString choice = player->findReasonable(choices, true);
			if (choice.isEmpty()) break;
			player->addToSelected(choice);
			choices.removeOne(choice);
			if (jiange_defense_type == "general")
				general_choices[kingdom].removeOne(choice);
		}
	}
}

void GameSessionController::chooseGenerals(QList<ServerPlayer *> players)
{
	m_preparationPhase = PreparationPhase::ChoosingGenerals;
	auto phaseReset = qScopeGuard([this]() { m_preparationPhase = PreparationPhase::None; });
	if (Config.Enable2ndGeneral)
		Config.Enable2ndGeneral = m_room.mode!="02_1v1"&&m_room.mode!="06_3v3"&&m_room.mode!="06_XMode"&&m_room.mode!="04_1v3";
	if (players.isEmpty()) players = m_room.getPlayers();
	// for lord.
	QString general = "sujiang";
	ServerPlayer*the_lord = m_room.getLord();
	if (players.contains(the_lord)){
		// 自動化測試: headless 指定主公武將 (--test-general), 跳過隨機選將
		const QString forcedName = Server::forcedHeadlessGeneral;
		const bool forced = Server::isHeadlessMode && !forcedName.isEmpty()
			&& Sanguosha->getGeneral(forcedName) != nullptr;
		QStringList lord_list;
		if (forced){
			general = forcedName;
		}else{
			if (Config.EnableSame || m_room.mode == "03_1v2"){
				lord_list = Sanguosha->getRandomGenerals(Config.value("MaxChoice", 5).toInt());
				if(m_room.mode == "03_1v2"){
					QStringList all_generals = Sanguosha->getLimitedGeneralNames();
					qsanShuffle(all_generals);
					foreach(QString general_name, all_generals){
						if(general_name.contains("ddz_")&&!lord_list.contains(general_name)){
							lord_list.prepend(general_name);
							break;
						}
					}
				}
			}else
				lord_list = Sanguosha->getRandomLords();
			general = m_room.askForGeneral(the_lord, lord_list, QString(), "for_lord");
		}
		the_lord->setGeneralName(general);
		m_room.notifyProperty(the_lord, the_lord, "general");
		if (!Config.EnableBasara){
			if (the_lord->hasHideSkill()){
				m_room.setPlayerProperty(the_lord, "yinni_general", general);
				general = "yinni_hide";
				the_lord->setGeneralName(general);
			}
			if (m_room.mode != "03_1v2")
				m_room.broadcastProperty(the_lord, "general", general);
		}
		players.removeOne(the_lord);
		if (Config.EnableSame){
			foreach(ServerPlayer*p, players){
				p->setGeneralName(general);
				if(general=="yinni_hide")
					m_room.setPlayerProperty(p, "yinni_general", the_lord->property("yinni_general"));
			}
			Config.Enable2ndGeneral = false;
			return;
		}else if(Config.Enable2ndGeneral){
			if(general=="yinni_hide") general = the_lord->property("yinni_general").toString();
			lord_list = Sanguosha->getRandomGenerals(Config.value("MaxChoice", 5).toInt(),QSet<QString>()<<general);
			// 自動化測試: headless 指定主公副將 (--test-general2), 否則隨機
			const QString forced2Name = Server::forcedHeadlessGeneral2;
			if (Server::isHeadlessMode && !forced2Name.isEmpty()
				&& Sanguosha->getGeneral(forced2Name) != nullptr
				&& forced2Name != general){
				general = forced2Name;
			}else{
				general = m_room.askForGeneral(the_lord, lord_list);
			}
			the_lord->setGeneral2Name(general);
			m_room.notifyProperty(the_lord, the_lord, "general2");
			if (!Config.EnableBasara){
				if (the_lord->hasHideSkill()){
					m_room.setPlayerProperty(the_lord, "yinni_general2", general);
					general = "yinni_hide";
					the_lord->setGeneral2Name(general);
				}
				if (m_room.mode != "03_1v2")
					m_room.broadcastProperty(the_lord, "general2", general);
			}
		}
	}

	assignGeneralsForPlayers(players);
	foreach(ServerPlayer*player, players){
		QStringList selected = player->getSelected();
		selected = triggerPreSelectionSkills(player, selected, "for_general");
		player->clearSelected();
		foreach(const QString &gen, selected)
			player->addToSelected(gen);
		setupChooseGeneralRequestArgs(player);
	}

	m_room.doBroadcastRequest(players, S_COMMAND_CHOOSE_GENERAL);
	foreach(ServerPlayer*player, players){
		QString clientChoice;
		bool playerChose = false;
		if (player->m_isClientResponseReady){
			clientChoice = player->getClientReply().toString();
			if (player->getSelected().contains(clientChoice) || Config.FreeChoose)
				playerChose = true;
		}
		QString chosen;
		if (playerChose)
			chosen = clientChoice;
		else
			chosen = m_room._chooseDefaultGeneral(player);

		if (!playerChose)
			triggerGeneralNotChosen(player, player->getSelected(), chosen, "for_general");

		if (m_room._setPlayerGeneral(player, chosen, true)){
			if (player->hasHideSkill()){
				m_room.setPlayerProperty(player, "yinni_general", player->getGeneralName());
				player->setGeneralName("yinni_hide");
			}
			m_room.notifyProperty(player, player, "general");
		}
	}

	if (Config.Enable2ndGeneral){
		assignGeneralsForPlayers(players);
		foreach(ServerPlayer*player, players)
			setupChooseGeneralRequestArgs(player);

		m_room.doBroadcastRequest(players, S_COMMAND_CHOOSE_GENERAL);
		foreach(ServerPlayer*player, players){
			if ((player->m_isClientResponseReady&&m_room._setPlayerGeneral(player, player->getClientReply().toString(), false))
				||m_room._setPlayerGeneral(player, m_room._chooseDefaultGeneral(player), false)){
				if(player->hasHideSkill(2)){
					m_room.setPlayerProperty(player, "yinni_general2", player->getGeneral2Name());
					player->setGeneral2Name("yinni_hide");
				}
				m_room.notifyProperty(player, player, "general2");
			}
		}
	}

	if (Config.EnableBasara){
		foreach(ServerPlayer*player, m_room.getPlayers()){
			QStringList names;
			if (player->getGeneral()){
				names.append(player->getGeneralName());
				if(names.last()=="yinni_hide"){
					names.takeLast();
					names.append(player->property("yinni_general").toString());
				}
				player->setGeneralName("anjiang");
				m_room.notifyProperty(player, player, "general");
			}
			if (player->getGeneral2()){
				names.append(player->getGeneral2Name());
				if(names.last()=="yinni_hide"){
					names.takeLast();
					names.append(player->property("yinni_genera2").toString());
				}
				player->setGeneral2Name("anjiang");
				m_room.notifyProperty(player, player, "general2");
			}
			m_room.safeSetPlayerProperty(player, "basara_generals", names.join("+"));
			m_room.notifyProperty(player, player, "basara_generals");
		}
	}else{
		if (m_room.mode == "03_1v2"&&the_lord){
			m_room.broadcastProperty(the_lord, "general");
			if(Config.Enable2ndGeneral)
				m_room.broadcastProperty(the_lord, "general2");
		}
	}
	/*if (Config.value("EnableSUPERConvert", true).toBool()&&mode != "05_ol"){
		foreach(ServerPlayer*p, getPlayers()){
			QStringList choicelist;
			foreach(QString gen, Sanguosha->getLimitedGeneralNames()){
				if (p->getGeneralName().endsWith(gen.split("_").last()))
					choicelist << gen;
			}
			QString to_cv;
			if (choicelist.length() > 1){
				AI*ai = p->getAI();
				if (ai) to_cv = askForChoice(p, "gamerule", choicelist.join("+"));
				else to_cv = askForGeneral(p, choicelist);
				p->setGeneralName(to_cv);
				if (Config.EnableBasara)
					notifyProperty(p, p, "general", to_cv);
				else
					broadcastProperty(p, "general", to_cv);
				if (Config.EnableSame){
					foreach(ServerPlayer*p, players){
						if (!p->isLord())
						p->setGeneralName(to_cv);
					}
					Config.Enable2ndGeneral = false;
					return;
				}
				to_cv = Sanguosha->getGeneral(to_cv)->getKingdom();
				if (to_cv != p->getKingdom())
					setPlayerProperty(p, "kingdom", to_cv);
			}
			if (p->getGeneral2()){
				QStringList choicelis;
				foreach(QString gen, Sanguosha->getLimitedGeneralNames()){
					if (p->getGeneral2Name().endsWith(gen.split("_").last()))
						choicelis << gen;
				}
				if (choicelis.length() > 1){
					AI*ai = p->getAI();
					if (ai) to_cv = askForChoice(p, "gamerule", choicelis.join("+"));
					else to_cv = askForGeneral(p, choicelis);
					p->setGeneral2Name(to_cv);
					if (Config.EnableBasara)
						notifyProperty(p, p, "general2", to_cv);
					else
						broadcastProperty(p, "general2", to_cv);
					if (Config.EnableSame){
						foreach(ServerPlayer*p, players){
							if (!p->isLord())
							p->setGeneralName(to_cv);
						}
					}
				}
			}
		}
	}*/
}

void GameSessionController::chooseGeneralsOfJianGeDefenseMode()
{
	QList<ServerPlayer*> toAssign = m_room.getPlayers();

	assignGeneralsForPlayersOfJianGeDefenseMode(toAssign);
	foreach(ServerPlayer*player, toAssign)
		setupChooseGeneralRequestArgs(player);

	m_room.doBroadcastRequest(toAssign, S_COMMAND_CHOOSE_GENERAL);
	foreach(ServerPlayer*player, toAssign){
		if(player->m_isClientResponseReady&&m_room._setPlayerGeneral(player, player->getClientReply().toString(), true)) continue;
		QString result = m_room._chooseDefaultGeneral(player);
		if (player->property("jiange_defense_type").toString() != "general"){ // randomly chosen
			QStringList selected = player->getSelected();
			result = selected.at(qsanRandomBounded(selected.length()));
		}
		m_room._setPlayerGeneral(player, result, true);
	}

	if (Config.Enable2ndGeneral){
		QList<ServerPlayer*> toAssign;
		foreach(ServerPlayer*p, m_room.getPlayers()){
			if (p->property("jiange_defense_type").toString() == "general")
				toAssign << p;
		}
		assignGeneralsForPlayersOfJianGeDefenseMode(toAssign);
		foreach(ServerPlayer*player, toAssign)
			setupChooseGeneralRequestArgs(player);

		m_room.doBroadcastRequest(toAssign, S_COMMAND_CHOOSE_GENERAL);
		foreach(ServerPlayer*player, toAssign){
			if (player->m_isClientResponseReady&&m_room._setPlayerGeneral(player, player->getClientReply().toString(), false)) continue;
			m_room._setPlayerGeneral(player, m_room._chooseDefaultGeneral(player), false);
		}
	}
}

QStringList GameSessionController::triggerPreSelectionSkills(ServerPlayer *player, QStringList generals, const QString &reason)
{
	QSet<QString> processedSkills;
	QStringList result = generals;

	foreach (const QString &generalName, generals) {
		const General *general = Sanguosha->getGeneral(generalName);
		if (!general || !general->hasPreSelectionSkill()) continue;

		foreach (const QString &skillName, general->getPreSelectionSkills()) {
			if (processedSkills.contains(skillName)) continue;
			processedSkills.insert(skillName);

			const PreSelectionMetaSkill *skill = qobject_cast<const PreSelectionMetaSkill*>(Sanguosha->getSkill(skillName));
			if (!skill) continue;

			result = skill->onGeneralChoosing(&m_room, player, result, reason);

			QString activeSkills = skill->getActiveSkills();
			if (!activeSkills.isEmpty()) {
				QString existingActive = player->property("preselection_active_skills").toString();
				if (!existingActive.isEmpty()) existingActive += ",";
				existingActive += activeSkills;
				m_room.setPlayerProperty(player, "preselection_active_skills", existingActive);
			}
		}
	}

	return result;
}

void GameSessionController::triggerGeneralNotChosen(ServerPlayer *player, const QStringList &generals, const QString &chosen, const QString &reason)
{
	const General *general = Sanguosha->getGeneral(chosen);
	if (!general || !general->hasPreSelectionSkill()) return;

	foreach (const QString &skillName, general->getPreSelectionSkills()) {
		const PreSelectionMetaSkill *skill = qobject_cast<const PreSelectionMetaSkill*>(Sanguosha->getSkill(skillName));
		if (!skill) continue;

		skill->onGeneralNotChosen(&m_room, player, generals, chosen, reason);

		QString activeSkills = skill->getActiveSkills();
		if (!activeSkills.isEmpty()) {
			QString existingActive = player->property("preselection_active_skills").toString();
			if (!existingActive.isEmpty()) existingActive += ",";
			existingActive += activeSkills;
			m_room.setPlayerProperty(player, "preselection_active_skills", existingActive);
		}
	}
}

void GameSessionController::setupChooseGeneralRequestArgs(ServerPlayer *player)
{
	QStringList selected = player->getSelected();

	JsonArray options = JsonUtils::toJsonArray(selected).value<JsonArray>();
	if (Config.EnableBasara) options.append("anjiang(lord)");
	else if(m_room.getLord()&&m_room.mode!="03_1v2") options.append(m_room.getLord()->getGeneralName()+"(lord)");
	player->m_commandArgs = options;
}
void GameSessionController::run()
{
	if (m_state == State::Waiting && !requestStart())
		return;
	if (m_state != State::Preparing)
		return;
	LuaRuntime::Binding luaBinding(m_room.m_runtime->lua());
	GameRng::Binding rngBinding(m_room.m_runtime->rng());
	EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
	LogMessage seedLog;
	seedLog.type = "#GameSeed";
	seedLog.arg = QString::number(m_room.m_sessionConfig.seed);
	m_room.sendLog(seedLog);
	m_room.AIHumanized = Config.value("AIHumanized", true).toBool();
	Config.AIDelay = Config.OriginAIDelay;
	// Scale AIDelay down for large player counts (>8) to reduce lag in 20-player games.
	// Formula: delay * 8 / playerCount, minimum 100 ms.
	if (Config.AIDelay > 0) {
		int n = m_room.getPlayers().length();
		if (n > 8)
			Config.AIDelay = qMax(Config.AIDelay * 8 / n, 100);
	}

	foreach(ServerPlayer*player, m_room.getPlayers()){
		//Ensure that the game starts with all player's mutex locked
		player->drainAllLocks();
		player->releaseLock(ServerPlayer::SEMA_MUTEX);
	}
#ifdef AUDIO_SUPPORT
	Audio::stopBGM();
#endif

	prepareForStart();
	if (m_room.isFinished())
		return;

	bool using_countdown = !m_room._virtual&&m_room.property("to_test").toString().isEmpty();

#ifndef QT_NO_DEBUG
	using_countdown = false;
#endif

	if (using_countdown){
		for (int i = Config.CountDownSeconds; i >= 0; i--){
			m_room.doBroadcastNotify(S_COMMAND_START_IN_X_SECONDS, i);
			m_room.sleep(1);
		}
	} else
		m_room.doBroadcastNotify(S_COMMAND_START_IN_X_SECONDS, QVariant(0));

	if (m_room.isFinished())
		return;

	m_preparationPhase = PreparationPhase::ChoosingGenerals;
	if (m_room.scenario&&!m_room.scenario->generalSelection()){
	} else if (m_room.mode == "06_3v3"){
		m_preparationPhase = PreparationPhase::ModeDrafting;
		m_room.thread_3v3 = new RoomThread3v3(&m_room);
		m_room.thread_3v3->start();

		QObject::connect(m_room.thread_3v3, SIGNAL(finished()), &m_room, SLOT(startGame()));
		QObject::connect(m_room.thread_3v3, SIGNAL(finished()), m_room.thread_3v3, SLOT(deleteLater()));
		return;
	} else if (m_room.mode == "06_XMode"){
		m_preparationPhase = PreparationPhase::ModeDrafting;
		m_room.thread_xmode = new RoomThreadXMode(&m_room);
		m_room.thread_xmode->start();

		QObject::connect(m_room.thread_xmode, SIGNAL(finished()), &m_room, SLOT(startGame()));
		QObject::connect(m_room.thread_xmode, SIGNAL(finished()), m_room.thread_xmode, SLOT(deleteLater()));
		return;
	} else if (m_room.mode == "02_1v1"){
		m_preparationPhase = PreparationPhase::ModeDrafting;
		m_room.thread_1v1 = new RoomThread1v1(&m_room);
		m_room.thread_1v1->start();

		QObject::connect(m_room.thread_1v1, SIGNAL(finished()), &m_room, SLOT(startGame()));
		QObject::connect(m_room.thread_1v1, SIGNAL(finished()), m_room.thread_1v1, SLOT(deleteLater()));
		return;
	} else if (m_room.mode == "04_1v3"){
		ServerPlayer*lord = m_room.getPlayers().first();
		QStringList lords;
		lords = GetConfigFromLuaState(m_room.getLuaState(), "extra_boss").toStringList();
		if(lords.isEmpty()){
			m_room.setPlayerProperty(lord, "general", "shenlvbu1");
		}else
			m_room.setPlayerProperty(lord, "general", m_room.askForGeneral(lord, lords));

		QStringList names;
		QStringList hulao_gens;
		hulao_gens = GetConfigFromLuaState(m_room.getLuaState(), "hulao_generals").toStringList();
		foreach(QString gen_name, hulao_gens){
			if (gen_name.startsWith("-")){ // means banned generals
				names.removeOne(gen_name.mid(1));
			} else if (gen_name.startsWith("package:")){
				const Package*pack = Sanguosha->findChild<const Package*>(gen_name.mid(8));
				if (pack){
					foreach(const General*general, pack->findChildren<const General*>()){
						if(general->isTotallyHidden()||names.contains(general->objectName())) continue;
						if(!Config.AddGodGeneral&&general->getKingdoms().contains("god")) continue;
						names << general->objectName();
					}
				}
			} else if (!names.contains(gen_name))
				names << gen_name;
		}
		qsanShuffle(names);
		foreach(ServerPlayer*player, m_room.getPlayers()){
			if (player == lord) continue;
			lords.clear();
			for (int i = 0; i < 3; i++){
				lords << names.takeFirst();
				if (names.isEmpty()) break;
			}
			m_room.setPlayerProperty(player, "general", m_room.askForGeneral(player, lords));
		}
	} else if (m_room.mode == "04_boss"){
		QStringList boss_lv_1 = Config.BossGenerals.first().split("+");
		if (Config.value("BossYanluo").toBool()){
			boss_lv_1.clear();
			boss_lv_1 << "yl_qinguang";
		}
		QStringList lords;
		lords = GetConfigFromLuaState(m_room.getLuaState(), "extra_boss").toStringList();
		if(lords.length()>0) boss_lv_1 = lords;
		ServerPlayer*lord = m_room.getPlayers().first();

		if (Config.value("OptionalBoss").toBool()){
			m_room.setPlayerProperty(lord, "general", m_room.askForGeneral(lord, boss_lv_1));
		} else
			m_room.setPlayerProperty(lord, "general", boss_lv_1.at(qsanRandomBounded(boss_lv_1.length())));
		m_room.setPlayerMark(lord, "BossMode_Boss", 1);

		QList<ServerPlayer*> players = m_room.getPlayers();
		players.removeOne(lord);
		chooseGenerals(players);
	} else if (m_room.mode == "05_ol"){
		QStringList jiang_list, bing_list;
		jiang_list << "godlai_zhangji" << "godlai_fanchou" << "godlai_niufudongxie" << "godlai_dongyue" << "godlai_lijue" << "godlai_guosi";
		bing_list << "godlai_longxiang" << "godlai_huben" << "godlai_fengyao" << "godlai_baolve" << "godlai_feixiong_right" << "godlai_feixiong_right";
		QStringList lords;
		lords = GetConfigFromLuaState(m_room.getLuaState(), "extra_boss").toStringList();
		if(lords.length()>0) jiang_list = lords;
		foreach(ServerPlayer*player, m_room.getPlayers()){
			if (player->isLord()){
				QString jiang = m_room.askForGeneral(player, jiang_list);
				m_room.setPlayerProperty(player, "general", jiang);
				QString bing = bing_list[jiang_list.indexOf(jiang)];
				foreach(ServerPlayer*p, m_room.getPlayers()){
					if (p->getRole() == "loyalist"){
						m_room.setPlayerProperty(p, "general", bing);
						if (bing == "godlai_feixiong_right")
							bing = "godlai_feixiong_left";
					}
				}
			}
		}
		bing_list << jiang_list;
		jiang_list = Sanguosha->getRandomGenerals(m_room.getPlayers().length()*4, qsanToSet(bing_list));
		foreach(ServerPlayer*player, m_room.getPlayers()){
			if (player->getRole() == "rebel"){
				lords.clear();
				for (int i = 0; i < 5; i++){
					lords << jiang_list.takeFirst();
					if (jiang_list.isEmpty()) break;
				}
				m_room.setPlayerProperty(player, "general", m_room.askForGeneral(player, lords));
			}
		}
	} else if (m_room.mode == "06_ol"){
		QStringList gui_list, list, god_list;
		gui_list << "hundun" << "qiongqi" << "taowu" << "taotie" << "yingzhao" << "xiangliu" << "zhuyan" << "bifang";
		QStringList lords;
		lords = GetConfigFromLuaState(m_room.getLuaState(), "extra_boss").toStringList();
		if(lords.length()>0) gui_list = lords;
		foreach(ServerPlayer*player, m_room.getPlayers()){
			if (player->getRole() == "loyalist")
				m_room.setPlayerProperty(player, "general", "zhuyin");
			else if (player->isLord())
				m_room.setPlayerProperty(player, "general", m_room.askForGeneral(player, gui_list));
		}
		foreach(QString god, Sanguosha->getLimitedGeneralNames("god"))
			if (god.contains("shen")) list << god;
		qsanShuffle(list);
		for (int i = 0; i < Config.value("fuck_god_spinbox", 3).toInt(); ++i){
			if (list.isEmpty()) break;
			god_list << list.takeFirst();
		}
		gui_list << god_list;
		list = Sanguosha->getRandomGenerals(m_room.getPlayers().length()*4, qsanToSet(gui_list));
		foreach(ServerPlayer*player, m_room.getPlayers()){
			if (player->getRole() == "rebel"){
				lords.clear();
				for (int i = 0; i < 5; ++i)
					lords << list.takeFirst();
				QString general = m_room.askForGeneral(player, lords+god_list);
				m_room.setPlayerProperty(player, "general", general);
				god_list.removeOne(general);
			}
		}
	} else if (m_room.mode == "08_defense"){
		QStringList type_list;
		type_list << "machine" << "general" << "soul" << "general"
			<< "general" << "soul" << "general" << "machine";
		const QList<ServerPlayer *> players = m_room.getPlayers();
		for (int i = 0; i < 8; i++)
			m_room.setPlayerProperty(players[i], "jiange_defense_type", type_list[i]);
		chooseGeneralsOfJianGeDefenseMode();
	} else if (Sanguosha->hasSkipGeneralSelection(m_room.mode)) {
		foreach (ServerPlayer *player, m_room.getPlayers())
			m_room.setPlayerProperty(player, "general", "anjiang");
	} else
		chooseGenerals();
	if (m_room.isFinished())
		return;
	m_preparationPhase = PreparationPhase::None;
	startGame();

	if (m_room._m_Id<1&&QFile::exists("lua/ai/cstring")){
		QStringList pns,all_generals;
		foreach(const General*general, Sanguosha->findChildren<const General*>()){
			all_generals << general->objectName();
			if(general->isTotallyHidden()) continue;
			QString pn = general->objectName();
			if(!QFile::exists("image/fullskin/generals/full/"+pn+".jpg"))
				m_room.output(pn+"-full_jpg");
			if(!QFile::exists("image/generals/card/"+pn+".jpg"))
				m_room.output(pn+"-card_jpg");
			if(!QFile::exists("audio/death/"+pn+".ogg"))
				m_room.output(pn+"-death_ogg");
			foreach(const Skill*vs, general->getVisibleSkillList()){
				pn = vs->objectName();
				if(pns.contains(pn)) continue;
				if(vs->getSources().isEmpty())
					m_room.output(pn+"-ogg");
				if(Sanguosha->translate("$"+pn+"1").contains(pn))
					m_room.output(pn+"-translate");
				pns << pn;
			}
			foreach(QString pn, general->getRelatedSkillNames()){
				if(pn.contains("#")||pns.contains(pn)) continue;
				if(!QFile::exists("audio/skill/"+pn+".ogg")&&!QFile::exists("audio/skill/"+pn+"1.ogg"))
					m_room.output(pn+"-ogg");
				if(Sanguosha->translate("$"+pn+"1").contains(pn))
					m_room.output(pn+"-translate");
				pns << pn;
			}
		}
		m_room.tag["AllGenerals"] = all_generals;
	}
}

void GameSessionController::assignRoles()
{
	m_preparationPhase = PreparationPhase::AssigningRoles;
	auto phaseReset = qScopeGuard([this]() { m_preparationPhase = PreparationPhase::None; });
	QStringList roles = Sanguosha->getRoleList(m_room.mode);
	const bool showAllRoles = Sanguosha->hasShowRoleMode(m_room.mode)
		|| (!Sanguosha->isCustomGameMode(m_room.mode) && m_room.mode.contains("_"));
	if (m_room.mode == "04_2v2"){/*
		roles.clear();
		if (qsanRandomBounded(2)<1) roles << "loyalist" << "rebel" << "rebel" << "loyalist";
		else roles << "rebel" << "loyalist" << "loyalist" << "rebel";*/
		QList<ServerPlayer *> players = m_room.getPlayers();
		qsanShuffle(players);
		m_room.replacePlayerOrder(players);
	} else if (m_room.mode == "02_1v1"){
		roles.prepend(roles.takeLast());
	}else if (m_room.mode != "08_defense"&&m_room.mode != "05_ol"&&m_room.mode != "06_ol")
		qsanShuffle(roles);

	const QList<ServerPlayer *> players = m_room.getPlayers();
	for (int i = 0; i < players.count(); i++){
		if (i >= roles.count()) break;
		players[i]->setRole(roles[i]);
		if (showAllRoles || (roles[i] == "lord"&&!ServerInfo.EnableHegemony))
			//|| mode == "06_ol"|| mode == "05_ol" || mode == "04_1v3" || mode == "04_boss" || mode == "08_defense" || mode == "03_1v2" || mode == "04_2v2")
			m_room.broadcastProperty(players[i], "role", roles[i]);
		else
			m_room.notifyProperty(players[i], players[i], "role");
	}
}

void GameSessionController::startGame()
{
	if (m_state == State::Waiting && !requestStart())
		return;
	if (m_state != State::Preparing)
		return;
	LuaRuntime::Binding luaBinding(m_room.m_runtime->lua());
	GameRng::Binding rngBinding(m_room.m_runtime->rng());
	EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
	m_room.m_roster->resetAliveToPlayers();
	const QList<ServerPlayer *> players = m_room.getPlayers();
	m_room.m_chatHistory.clear();/*
	if (mode == "08_defense"){
		QList<int> next_list;
		next_list << 0 << 7 << 1 << 6 << 2 << 5 << 3 << 4;
		for (int i = 0; i < player_count - 1; i++)
			getPlayers()[next_list[i]]->setNext(getPlayers()[next_list[i+1]]);
		getPlayers()[4]->setNext(getPlayers().first());
	} else {*/
		for (int i = 0; i < m_room.player_count - 1; i++)
			players[i]->setNext(players[i+1]);
		players.last()->setNext(players.first());
	//}

	foreach(ServerPlayer*player, players){
		if (player->getGeneral()){
			int max_hp = player->getGeneralMaxHp();

			player->setMaxHp(max_hp);
			player->setHp(qMin(player->getGeneralStartHp(),max_hp));

			if (!Config.EnableBasara){
				m_room.broadcastProperty(player, "general");
				if(player->getGeneral2())
					m_room.broadcastProperty(player, "general2");
			}
			if (m_room.mode == "02_1v1")
				m_room.doBroadcastNotify(m_room.getOtherPlayers(player, true), S_COMMAND_REVEAL_GENERAL, JsonArray() << player->objectName() << player->getGeneralName());

			m_room.broadcastProperty(player, "hp");
			m_room.broadcastProperty(player, "maxhp");
			int hujia = player->getGeneralStartHujia();
			if (hujia > 0) m_room.addPlayerMark(player, "@HuJia", hujia);

			//if (mode == "06_3v3" || mode == "06_XMode")
				//broadcastProperty(player, "role");
		}
		// setup AI
		AI*ai = m_room.cloneAI(player);
		m_room.ais << ai;
		player->setAI(ai);
	}

	if(!m_room.thread) m_room.thread = new RoomThread(&m_room);

	m_room.preparePlayers();
	foreach (ServerPlayer *player, players)
		player->refreshUIState();
	foreach (ServerPlayer *receiver, players)
		m_room.notifySkillInstanceSnapshot(receiver);

	m_room.doBroadcastNotify(S_COMMAND_GAME_START, JsonUtils::toJsonArray(m_room.m_cardMovement->drawPile()));

	if (!transitionTo(State::Initializing))
		return;

	Server*server = qobject_cast<Server*>(m_room.parent());
	foreach(ServerPlayer*player, players){
		// A trusted player is still a human connection: registering only
		// "online" players left them out of name2objname, so reconnect could
		// never find them.
		if (!player->isOffline())
			server->signupPlayer(player);
	}

	m_room.setCurrent(m_room.getAlivePlayers().first());

	foreach(int card_id, m_room.m_cardMovement->drawPile())
		m_room.setCardMapping(card_id, nullptr, Player::DrawPile);

	m_room.doBroadcastNotify(S_COMMAND_UPDATE_PILE, m_room.m_cardMovement->drawPile().length());

	if(m_room.scenario){
		const ScenarioRule*rule = m_room.scenario->getRule();
		if (rule) m_room.thread->addTriggerSkill(rule);
	}

	if (m_room.mode != "02_1v1"&&m_room.mode != "06_3v3"&&m_room.mode != "06_XMode")
		m_room.m_runtime->state().reset();
	QObject::connect(m_room.thread, SIGNAL(started()), &m_room, SIGNAL(game_start()));

	if (!m_room._virtual) m_room.thread->start();
}
