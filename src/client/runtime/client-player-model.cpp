#include "client-player-model.h"

#include "client-game-state.h"
#include "engine.h"
#include "client-state-projection.h"

#include <QPair>

#include <algorithm>

ClientPlayer::ClientPlayer(const ClientGameState *state, const ClientPlayerModel *model,
                           QObject *parent)
    : Player(parent), m_state(state), m_model(model)
{
}

Player *ClientPlayer::seatStep(int step) const
{
    if (m_state == nullptr || m_model == nullptr || step == 0)
        return nullptr;

    QList<QPair<int, QString>> ring;
    for (const QString &name : m_state->playerNames()) {
        if (name != objectName() && !m_state->isPlayerAlive(name))
            continue;
        ring.append({m_state->playerValue(name, QStringLiteral("seat")).toInt(), name});
    }
    std::sort(ring.begin(), ring.end());
    if (ring.isEmpty())
        return nullptr;

    int index = -1;
    for (int i = 0; i < ring.size(); ++i) {
        if (ring.at(i).second == objectName())
            index = i;
    }
    if (index < 0)
        return nullptr;

    const int size = ring.size();
    const int target = ((index + step) % size + size) % size;
    return m_model->player(ring.at(target).second);
}

Player *ClientPlayer::getNextAlive(int n) const
{
    return seatStep(n);
}

Player *ClientPlayer::getLastAlive(int n) const
{
    return seatStep(-n);
}

int ClientPlayer::aliveCount(bool includeRemoved) const
{
    if (m_state == nullptr)
        return 1;

    int alive = 0;
    for (const QString &name : m_state->playerNames()) {
        if (!m_state->isPlayerAlive(name))
            continue;
        if (!includeRemoved
            && m_state->playerValue(name, QStringLiteral("removed")).toBool()) {
            continue;
        }
        ++alive;
    }
    return qMax(alive, 1);
}

QString ClientPlayer::getGameMode() const
{
    return m_state != nullptr
        ? m_state->setup().value(QStringLiteral("mode")).toString() : QString();
}

int ClientPlayer::getHandcardNum() const
{
    if (m_state == nullptr)
        return Player::getHandcardNum();
    const QVariant count = m_state->playerValue(objectName(), QStringLiteral("hand_count"));
    if (count.isValid())
        return count.toInt();
    return m_state->cardsForPlayer(objectName(), Player::PlaceHand).size();
}

QList<const Card *> ClientPlayer::getHandcards() const
{
    QList<const Card *> cards;
    if (m_state == nullptr || Sanguosha == nullptr)
        return cards;
    for (int cardId : m_state->cardsForPlayer(objectName(), Player::PlaceHand)) {
        if (const Card *card = Sanguosha->getCard(cardId))
            cards.append(card);
    }
    return cards;
}

int ClientPlayer::getMaxCards() const
{
    if (m_state != nullptr) {
        const QVariant handMax = m_state->playerValue(objectName(), QStringLiteral("hand_max"));
        if (handMax.isValid())
            return handMax.toInt();
    }
    return Player::getMaxCards();
}

ClientPlayerModel::ClientPlayerModel(const ClientGameState *state)
    : m_state(state)
{
}

ClientPlayerModel::~ClientPlayerModel()
{
    clear();
}

void ClientPlayerModel::clear()
{
    setEngineSelf(nullptr);
    for (const Entry &entry : m_players)
        delete entry.player;
    m_players.clear();
    m_selfName.clear();
}

ClientPlayer *ClientPlayerModel::player(const QString &objectName) const
{
    return m_players.value(objectName).player;
}

ClientPlayer *ClientPlayerModel::self() const
{
    return player(m_selfName);
}

const Player *ClientPlayerModel::cardOwner(int cardId) const
{
    if (m_state == nullptr)
        return nullptr;
    const QString owner = m_state->card(cardId).value(QStringLiteral("owner")).toString();
    return owner.isEmpty() ? nullptr : player(owner);
}

void ClientPlayerModel::sync()
{
    if (m_state == nullptr)
        return;

    const QStringList names = m_state->playerNames();
    for (auto it = m_players.begin(); it != m_players.end();) {
        if (names.contains(it.key())) {
            ++it;
            continue;
        }
        delete it.value().player;
        it = m_players.erase(it);
    }

    for (const QString &name : names) {
        Entry &entry = m_players[name];
        if (entry.player == nullptr) {
            entry.player = new ClientPlayer(m_state, this, &m_root);
            entry.player->setObjectName(name);
        }
        const QVariantMap data = m_state->player(name);
        const QList<int> equipped = m_state->cardsForPlayer(name, Player::PlaceEquip);
        if (entry.applied == data && entry.equipped == equipped)
            continue;
        syncPlayer(&entry, data, equipped);
        entry.applied = data;
        entry.equipped = equipped;
    }

    m_selfName = m_state->selfName();
    setEngineSelf(self());
}

void ClientPlayerModel::syncPlayer(Entry *entry, const QVariantMap &data,
                                   const QList<int> &equipped)
{
    ClientPlayer *projected = entry->player;
    ClientRules::applyPlayerState(projected, data, entry->applied);

    if (Sanguosha == nullptr || equipped == entry->equipped)
        return;

    for (const Card *worn : projected->getEquips()) {
        if (worn != nullptr && !equipped.contains(worn->getEffectiveId()))
            projected->removeEquip(worn);
    }
    for (int cardId : equipped) {
        if (projected->getEquipsId().contains(cardId))
            continue;
        if (const Card *equip = Sanguosha->getCard(cardId))
            projected->setEquip(equip);
    }
}
