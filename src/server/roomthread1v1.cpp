#include "roomthread1v1.h"
#include "room.h"
#include "engine.h"
#include "settings.h"
#include "generalselector.h"
//#include "json.h"
//#include "package.h"
//#include "util.h"
#include "roomthread.h"

using namespace QSanProtocol;

RoomThread1v1::RoomThread1v1(Room *room)
	: room(room)
{
	room->getRoomState()->reset();
}

void RoomThread1v1::run()
{
	// initialize the random seed for this thread
	qsrand(QTime(0, 0, 0).secsTo(QTime::currentTime()));
	QString rule = Config.value("1v1/Rule", "2013").toString();
	int total_num = rule != "Classical" ? 12 : 10;

	if (Config.value("1v1/UsingExtension", false).toBool()) {
		QStringList banset = Config.value("Banlist/1v1").toStringList();
		general_names = Sanguosha->getRandomGenerals(total_num, QSet<QString>(banset.begin(), banset.end()));
	} else {
		//QStringList candidates;
		if (rule == "Classical") {
			foreach(const General *general, Sanguosha->findChild<const Package *>("standard")->findChildren<const General *>())
				general_names << general->objectName();
			foreach(const General *general, Sanguosha->findChild<const Package *>("wind")->findChildren<const General *>())
				general_names << general->objectName();
		} else {
			general_names << "nos_caocao" << "nos_simayi" << "nos_xiahoudun" << "kof_nos_zhangliao"
				<< "kof_nos_xuchu" << "nos_guojia" << "kof_zhenji" << "kof_xiahouyuan"
				<< "nos_caoren" << "dianwei" << "kof_nos_guanyu" << "nos_zhangfei"
				<< "zhugeliang" << "nos_zhaoyun" << "nos_machao" << "kof_nos_huangyueying"
				<< "kof_huangzhong" << "kof_jiangwei" << "kof_menghuo" << "kof_zhurong"
				<< "sunquan" << "nos_ganning" << "nos_huanggai" << "nos_zhouyu"
				<< "nos_luxun" << "kof_sunshangxiang" << "sunjian" << "xiaoqiao"
				<< "nos_lvbu" << "kof_nos_diaochan" << "yanliangwenchou" << "hejin";
			if (rule == "2013") {
				general_names << "kof_nos_liubei" << "kof_weiyan" << "kof_nos_lvmeng" << "kof_nos_daqiao"
					<< "nos_zhoutai" << "kof_nos_huatuo" << "nos_zhangjiao" << "pangde"
					<< "niujin" << "hansui";
			}
		}
		qShuffle(general_names);
		general_names = general_names.mid(0, total_num);
	}

	if (rule == "Classical") {
		QStringList known_list = general_names.mid(0, 6);
		unknown_list = general_names.mid(6, 4);

		for (int i = 0; i < 4; i++)
			general_names[i + 6] = QString("x%1").arg(i);

		room->doBroadcastNotify(S_COMMAND_FILL_GENERAL, JsonUtils::toJsonArray(known_list << "x0" << "x1" << "x2" << "x3"));
	} else if (rule == "WZZZ") {
		room->doBroadcastNotify(S_COMMAND_FILL_GENERAL, JsonUtils::toJsonArray(general_names));
	} else if (rule == "2013") {
		QStringList known_list = general_names.mid(0, 6);
		unknown_list = general_names.mid(6, 6);

		for (int i = 0; i < 6; i++)
			general_names[i + 6] = QString("x%1").arg(QString::number(i));

		room->doBroadcastNotify(S_COMMAND_FILL_GENERAL, JsonUtils::toJsonArray(known_list << "x0" << "x1" << "x2"
			<< "x3" << "x4" << "x5"));
	}

	int index = qrand() % 2;
	ServerPlayer *first = room->getPlayers().at(index), *next = room->getPlayers().at(1 - index);
	QString order = room->askForOrder(first, "warm");
	if (order == "cool")
		qSwap(first, next);
	first->setRole("lord");
	next->setRole("renegade");

	room->broadcastProperty(first, "role");
	room->broadcastProperty(next, "role");
	room->adjustSeats();

	if (rule == "2013") {
		takeGeneral(first, "x0");
		takeGeneral(first, "x2");
		takeGeneral(first, "x4");
		takeGeneral(next, "x1");
		takeGeneral(next, "x3");
		takeGeneral(next, "x5");
	}

	askForTakeGeneral(first);

	while (general_names.length() > 1) {
		qSwap(first, next);

		askForTakeGeneral(first);
		askForTakeGeneral(first);
	}
	askForTakeGeneral(next);

	if (rule == "2013")
		askForFirstGeneral(QList<ServerPlayer *>() << first << next);
	else
		startArrange(QList<ServerPlayer *>() << first << next);
}

void RoomThread1v1::askForTakeGeneral(ServerPlayer *player)
{
	room->tryPause();

	QString name;
	if (general_names.length()<2)
		name = general_names.first();
	else if (player->getState() != "online")
		name = GeneralSelector::getInstance()->select1v1(general_names);

	if (name.isEmpty()) {
		if(room->doRequest(player, S_COMMAND_ASK_GENERAL, QVariant(), true)){
			QVariant clientReply = player->getClientReply();
			if(JsonUtils::isString(clientReply))
				name = clientReply.toString();
		}else{
			GeneralSelector *selector = GeneralSelector::getInstance();
			name = selector->select1v1(general_names);
		}
	} else
		msleep(Config.AIDelay);
	takeGeneral(player, name);
}

void RoomThread1v1::takeGeneral(ServerPlayer *player, const QString &name)
{
	QString rule = Config.value("1v1/Rule", "2013").toString();
	QString group = player->isLord() ? "warm" : "cool";
	room->doBroadcastNotify(room->getOtherPlayers(player, true), S_COMMAND_TAKE_GENERAL, JsonUtils::toJsonArray(QStringList() << group << name << rule));

	static QRegExp unknown_rx("x(\\d)");
	QString general_name = name;
	if (unknown_rx.exactMatch(name)) {
		int index = unknown_rx.capturedTexts().at(1).toInt();
		general_name = unknown_list.at(index);

		JsonArray arg;
		arg << index << general_name;
		room->doNotify(player, S_COMMAND_RECOVER_GENERAL, arg);
	}

	room->doNotify(player, S_COMMAND_TAKE_GENERAL, JsonUtils::toJsonArray(QStringList() << group << general_name << rule));

	QString namearg = unknown_rx.exactMatch(name) ? "anjiang" : name;
	foreach (ServerPlayer *p, room->getPlayers()) {
		LogMessage log;
		log.type = "#VsTakeGeneral";
		log.arg = group;
		log.arg2 = (p == player) ? general_name : namearg;
		room->sendLog(log, p);
	}

	general_names.removeOne(name);
	player->addToSelected(general_name);
}

void RoomThread1v1::startArrange(QList<ServerPlayer *> players)
{
	room->tryPause();
	foreach (ServerPlayer *player, players) {
		if (!player->isOnline()) {
			GeneralSelector *selector = GeneralSelector::getInstance();
			arrange(player, selector->arrange1v1(player));
			players.removeOne(player);
		}
	}
	if (players.isEmpty()) return;

	foreach(ServerPlayer *player, players)
		player->m_commandArgs = QVariant();

	room->doBroadcastRequest(players, S_COMMAND_ARRANGE_GENERAL);

	foreach (ServerPlayer *player, players) {
		JsonArray clientReply = player->getClientReply().value<JsonArray>();
		if (player->m_isClientResponseReady && clientReply.size() == 3) {
			QStringList arranged;
			JsonUtils::tryParse(clientReply, arranged);
			arrange(player, arranged);
		} else {
			GeneralSelector *selector = GeneralSelector::getInstance();
			arrange(player, selector->arrange1v1(player));
		}
	}
}

void RoomThread1v1::askForFirstGeneral(QList<ServerPlayer *> players)
{
	room->tryPause();
	foreach (ServerPlayer *player, players) {
		if (!player->isOnline()) {
			GeneralSelector *selector = GeneralSelector::getInstance();
			QStringList arranged = player->getSelected();
			QStringList selected = selector->arrange1v1(player);
			selected.append(arranged);
			selected.removeDuplicates();
			arrange(player, selected);
			players.removeOne(player);
		}
	}
	if (players.isEmpty()) return;

	foreach(ServerPlayer *player, players)
		player->m_commandArgs = JsonUtils::toJsonArray(player->getSelected());

	room->doBroadcastRequest(players, S_COMMAND_CHOOSE_GENERAL);

	foreach (ServerPlayer *player, players) {
		QVariant clientReply = player->getClientReply();
		if (player->m_isClientResponseReady && JsonUtils::isString(clientReply) && player->getSelected().contains(clientReply.toString())) {
			QStringList arranged = player->getSelected();
			QString first_gen = clientReply.toString();
			arranged.removeOne(first_gen);
			arranged.prepend(first_gen);
			arrange(player, arranged);
		} else {
			GeneralSelector *selector = GeneralSelector::getInstance();
			QStringList arranged = player->getSelected();
			QStringList selected = selector->arrange1v1(player);
			selected.append(arranged);
			selected.removeDuplicates();
			arrange(player, selected);
		}
	}
}

void RoomThread1v1::arrange(ServerPlayer *player, const QStringList &arranged)
{
	QString rule = Config.value("1v1/Rule", "2013").toString();
	Q_ASSERT(arranged.length() == ((rule == "2013") ? 6 : 3));

	QString general = arranged.first();
	QStringList left = arranged.mid(1);
	player->tag["1v1Arrange"] = QVariant::fromValue(left);
	player->setGeneralName(general);
	if (player->hasHideSkill()){
		room->setPlayerProperty(player, "yinni_general", general);
		general = "yinni_hide";
		player->setGeneralName(general);
	}

	foreach (QString general, arranged) {
		room->doNotify(player, S_COMMAND_REVEAL_GENERAL, JsonUtils::toJsonArray(QStringList() << player->objectName() << general));
		if (rule != "Classical") break;
	}
}

