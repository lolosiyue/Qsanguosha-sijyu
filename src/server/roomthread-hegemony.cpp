#include "roomthread-hegemony.h"

#include "banpair.h"
#include "engine.h"
#include "engine-runtime-context.h"
#include "game-session-controller.h"
#include "gamerule.h"
#include "generalselector.h"
#include "lua-runtime.h"
#include "protocol/gameplay/simple-choice-payloads.h"
#include "qt-collection-utils.h"
#include "room.h"
#include "server.h"
#include "server-info.h"
#include "serverplayer.h"
#include "settings.h"
#include "util.h"

#include <QSet>

using namespace QSanProtocol;

RoomThreadHegemony::RoomThreadHegemony(Room *room)
    : room(room)
{
}

void RoomThreadHegemony::run()
{
    LuaRuntime::Binding luaBinding(room->roomRuntime()->lua());
    GameRng::Binding rngBinding(room->roomRuntime()->rng());
    EngineRuntimeContextScope contextScope(*Sanguosha, room);
    chooseGenerals(room);
}

void RoomThreadHegemony::chooseGenerals(Room *room)
{
    GameSessionController *session = room->m_gameSession.get();
    Config.Enable2ndGeneral = true;
    const QList<ServerPlayer *> players = room->getPlayers();
    if (players.isEmpty()) {
        session->abort(GameSessionController::TerminationCause::InitializationFailure);
        return;
    }

    QStringList banned = ServerInfo.BanPackages;
    banned << Config.value("Banlist/Hegemony").toStringList();
    if (isNormalGameMode(room->getMode()))
        banned << Config.value("Banlist/Roles").toStringList();
    QStringList available;
    QSet<QString> admitted;
    foreach (const QString &name, Sanguosha->getLimitedGeneralNames()) {
        const General *general = Sanguosha->getGeneral(name);
        // Recheck packages: getLimitedGeneralNames has a legacy standard-package fallback.
        if (!general || admitted.contains(name) || banned.contains(name)
            || banned.contains(general->getPackage()) || BanPair::isBanned(name)
            || name.startsWith("heg_lord_") || name.startsWith("lord_")
            || general->isTotallyHidden() || general->getKingdom().isEmpty()
            || general->getKingdom() == "god" || general->getKingdom() == "ye")
            continue;
        admitted.insert(name);
        available << name;
    }
    qsanShuffle(available);
    const auto legalPair = [&admitted](const QString &head, const QString &deputy) {
        if (head == deputy || !admitted.contains(head) || !admitted.contains(deputy))
            return false;
        const General *first = Sanguosha->getGeneral(head);
        return first && first->canPairForHegemony(Sanguosha->getGeneral(deputy));
    };

    const QString forcedHead = Server::isHeadlessMode ? Server::forcedHeadlessGeneral : QString();
    const QString forcedDeputy = Server::isHeadlessMode ? Server::forcedHeadlessGeneral2 : QString();
    QList<QStringList> pools;
    const int choiceCount = qMin(qBound(2, Config.value("HegemonyMaxChoice", 7).toInt(), 21),
                                int(available.size() / players.size()));
    // Reserve a legal pair for EVERY seat before distributing optional choices.
    // Disjoint pools keep restricted replies and their fallbacks unique across the room.
    for (int seat = 0; seat < players.size(); ++seat) {
        QStringList pair;
        foreach (const QString &head, available) {
            if (seat == 0 && !forcedHead.isEmpty() && head != forcedHead) continue;
            foreach (const QString &deputy, available) {
                if (seat == 0 && !forcedDeputy.isEmpty() && deputy != forcedDeputy) continue;
                if (legalPair(head, deputy)) {
                    pair << head << deputy;
                    break;
                }
            }
            if (!pair.isEmpty()) break;
        }
        if (pair.isEmpty()) {
            qWarning("HEG preparation cannot reserve a legal distinct pair for every seat.");
            session->abort(GameSessionController::TerminationCause::InitializationFailure);
            return;
        }
        available.removeOne(pair.first());
        available.removeOne(pair.last());
        pools << pair;
    }
    for (int choice = 2; choice < choiceCount && !available.isEmpty(); ++choice) {
        for (QStringList &pool : pools) {
            if (available.isEmpty()) break;
            pool << available.takeFirst();
        }
    }

    QList<QStringList> legalPairs;
    for (int seat = 0; seat < players.size(); ++seat) {
        ServerPlayer *player = players.at(seat);
        player->clearSelected();
        ChooseGeneralRequestPayload request;
        request.candidates = pools.at(seat);
        foreach (const QString &name, request.candidates)
            player->addToSelected(name);
        foreach (const QString &head, pools.at(seat)) {
            if (seat == 0 && !forcedHead.isEmpty() && head != forcedHead) continue;
            foreach (const QString &deputy, pools.at(seat)) {
                if (seat == 0 && !forcedDeputy.isEmpty() && deputy != forcedDeputy) continue;
                if (legalPair(head, deputy)) {
                    request.hegemonyPairs << head + "+" + deputy;
                }
            }
        }
        // One request commits both slots. The server owns pair legality for every UI.
        if (request.hegemonyPairs.isEmpty()) {
            session->abort(GameSessionController::TerminationCause::InitializationFailure);
            return;
        }
        const QString defaultPair = GeneralSelector::getInstance()->selectHegemonyPair(request.hegemonyPairs);
        request.hegemonyPairs.removeOne(defaultPair);
        request.hegemonyPairs.prepend(defaultPair);
        legalPairs << request.hegemonyPairs;
        player->m_commandArgs = request.toDomainVariant();
    }
    room->doBroadcastRequest(players, S_COMMAND_CHOOSE_GENERAL);
    if (session->isTerminal()) return;
    QStringList heads;
    QStringList deputies;
    QSet<QString> chosen;
    for (int seat = 0; seat < players.size(); ++seat) {
        ServerPlayer *player = players.at(seat);
        QString reply = player->m_isClientResponseReady ? player->getClientReply().toString() : QString();
        const QStringList proposed = reply.split('+');
        const General *freeHead = proposed.size() == 2 ? Sanguosha->getGeneral(proposed.first()) : nullptr;
        // FreeChoose admits registered generals outside this seat's dealt pool.
        // Keep the ordered-pair rules and explicit headless fixture constraints.
        const bool freePair = Config.FreeChoose && freeHead
            && freeHead->canPairForHegemony(Sanguosha->getGeneral(proposed.last()))
            && (seat != 0 || forcedHead.isEmpty() || proposed.first() == forcedHead)
            && (seat != 0 || forcedDeputy.isEmpty() || proposed.last() == forcedDeputy);
        if (!legalPairs.at(seat).contains(reply) && !freePair)
            reply = legalPairs.at(seat).first();
        const QStringList names = reply.split('+');
        if (names.size() != 2) {
            session->abort(GameSessionController::TerminationCause::InitializationFailure);
            return;
        }
        const QString head = names.first(), deputy = names.last();
        // Validate the complete roster before publishing any authoritative identities.
        // Like ordinary FreeChoose, different seats may deliberately use the same
        // general. Within one pair the two names must still be distinct.
        if ((!freePair && !legalPair(head, deputy))
            || (!Config.FreeChoose && (chosen.contains(head) || chosen.contains(deputy)))) {
            session->abort(GameSessionController::TerminationCause::InitializationFailure);
            return;
        }
        chosen << head << deputy;
        heads << head;
        deputies << deputy;
    }

    QList<ServerPlayer *> kingdomPlayers;
    QList<QStringList> kingdomChoices;
    for (int seat = 0; seat < players.size(); ++seat) {
        ServerPlayer *player = players.at(seat);
        const General *head = Sanguosha->getGeneral(heads.at(seat));
        const QStringList names{heads.at(seat), deputies.at(seat)};
        player->setActualGeneral1Name(names.first());
        player->setActualGeneral2Name(names.last());
        player->setGeneralName("anjiang");
        player->setGeneral2Name("anjiang");
        player->setGeneralShowed(false);
        player->setGeneral2Showed(false);
        player->setShownRole(false);
        room->setTag(player->objectName(), names);
        room->safeSetPlayerProperty(player, "hegemony_generals", names.join("+"));
        room->setPlayerProperty(player, "kingdom", "god");
        room->broadcastProperty(player, "general");
        room->broadcastProperty(player, "general2");
        room->broadcastProperty(player, "role_shown");
        // Owners see their accepted/fallback pair before choosing a private faction.
        room->notifyProperty(player, player, "actual_general1");
        room->notifyProperty(player, player, "actual_general2");
        room->notifyProperty(player, player, "general", names.first());
        room->notifyProperty(player, player, "general2", names.last());
        room->notifyProperty(player, player, "hegemony_generals");
        const QStringList kingdoms = head->compareKingdomsWith(Sanguosha->getGeneral(deputies.at(seat)));
        kingdomChoices << kingdoms;
        if (kingdoms.size() > 1) {
            ChooseKingdomRequestPayload request;
            request.kingdoms = kingdoms;
            player->m_commandArgs = request.toDomainVariant();
            kingdomPlayers << player;
        }
    }
    // Batch private initial-faction choices; never publish a ChooseKingdom log.
    if (!kingdomPlayers.isEmpty())
        room->doBroadcastRequest(kingdomPlayers, S_COMMAND_CHOOSE_KINGDOM);
    if (session->isTerminal()) return;

    for (int seat = 0; seat < players.size(); ++seat) {
        ServerPlayer *player = players.at(seat);
        const QStringList kingdoms = kingdomChoices.at(seat);
        const QString reply = kingdomPlayers.contains(player) && player->m_isClientResponseReady
            ? player->getClientReply().toString() : QString();
        const QString kingdom = kingdoms.contains(reply) ? reply : kingdoms.first();
        player->setHegemonyKingdom(kingdom);
        QString role = HegemonyRule::getMappedRole(kingdom);
        if (role.isEmpty()) role = player->getActualGeneral1()->getKingdom();
        player->setRole(role);
        room->notifyProperty(player, player, "role");
        room->notifyProperty(player, player, "hegemony_kingdom");
        player->clearSelected();
    }
    room->setTag("HegemonyUsedGenerals", QStringList(chosen.begin(), chosen.end()));
}
