#include "tui-client-player.h"

#include "card.h"
#include "client-game-state.h"
#include "engine.h"

#include <QPair>
#include <QSet>

#include <algorithm>

namespace {

// ClientGameStateReducer renames a handful of wire properties on the way in.
// Player still answers to the wire names through its Q_PROPERTY table, so the
// sync has to put them back.
QString wirePropertyName(const QString &stateKey)
{
    if (stateKey == QLatin1String("max_hp"))
        return QStringLiteral("maxhp");
    if (stateKey == QLatin1String("deputy_general"))
        return QStringLiteral("general2");
    return stateKey;
}

// Keys the state owns in its own shape: either handled below by a typed call or
// deliberately not part of the engine player.
const QSet<QString> &structuredKeys()
{
    static const QSet<QString> keys{
        QStringLiteral("object_name"), QStringLiteral("screen_name"),
        QStringLiteral("hand_count"), QStringLiteral("hand_max"),
        QStringLiteral("flags"), QStringLiteral("marks"),
        QStringLiteral("history"), QStringLiteral("card_limitations"),
        QStringLiteral("skills"), QStringLiteral("skill_instances"),
        QStringLiteral("piles"), QStringLiteral("general_piles"),
        QStringLiteral("tags"), QStringLiteral("ui_state"),
        QStringLiteral("equip_areas"), QStringLiteral("skill_descriptions"),
        QStringLiteral("card_descriptions"), QStringLiteral("revealed_general"),
        QStringLiteral("offensive_distance"), QStringLiteral("defensive_distance")};
    return keys;
}

} // namespace

ClientPlayer::ClientPlayer(const ClientGameState *state, const TuiPlayerModel *model,
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
    // Only the ids the server actually told us about; another player's hand is
    // a count, not a list.
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

TuiPlayerModel::TuiPlayerModel(const ClientGameState *state)
    : m_state(state)
{
}

TuiPlayerModel::~TuiPlayerModel()
{
    clear();
}

void TuiPlayerModel::clear()
{
    setEngineSelf(nullptr);
    for (const Entry &entry : m_players)
        delete entry.player;
    m_players.clear();
    m_selfName.clear();
}

ClientPlayer *TuiPlayerModel::player(const QString &objectName) const
{
    return m_players.value(objectName).player;
}

ClientPlayer *TuiPlayerModel::self() const
{
    return player(m_selfName);
}

const Player *TuiPlayerModel::cardOwner(int cardId) const
{
    if (m_state == nullptr)
        return nullptr;
    const QString owner = m_state->card(cardId).value(QStringLiteral("owner")).toString();
    return owner.isEmpty() ? nullptr : player(owner);
}

void TuiPlayerModel::sync()
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

void TuiPlayerModel::syncPlayer(Entry *entry, const QVariantMap &data,
                                const QList<int> &equipped)
{
    ClientPlayer *player = entry->player;
    const QVariantMap &applied = entry->applied;
    const auto changed = [&applied, &data](const QString &key) {
        return applied.value(key) != data.value(key);
    };

    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        if (structuredKeys().contains(it.key()) || !changed(it.key()))
            continue;
        // Everything else is a wire property the server set on the player, and
        // Player's Q_PROPERTY table converts it. Names it does not know (the
        // distanceTo_<name> matrix, View_As_Equips_List) become dynamic
        // properties, which is exactly where the engine looks for them.
        player->setProperty(wirePropertyName(it.key()).toLatin1().constData(), it.value());
    }
    if (changed(QStringLiteral("screen_name")))
        player->setScreenName(data.value(QStringLiteral("screen_name")).toString());

    if (changed(QStringLiteral("flags"))) {
        player->setFlags(QStringLiteral("."));
        for (const QString &flag : data.value(QStringLiteral("flags")).toStringList())
            player->setFlags(flag);
    }

    if (changed(QStringLiteral("marks"))) {
        const QVariantMap marks = data.value(QStringLiteral("marks")).toMap();
        for (const QString &mark : player->getMarkNames()) {
            if (!marks.contains(mark))
                player->setMark(mark, 0);
        }
        for (auto it = marks.constBegin(); it != marks.constEnd(); ++it)
            player->setMark(it.key(), it.value().toInt());
    }

    if (changed(QStringLiteral("history"))) {
        player->clearHistory();
        const QVariantMap history = data.value(QStringLiteral("history")).toMap();
        for (auto it = history.constBegin(); it != history.constEnd(); ++it)
            player->addHistory(it.key(), it.value().toInt());
    }

    if (changed(QStringLiteral("card_limitations"))) {
        player->clearCardLimitation();
        for (const QVariant &value : data.value(QStringLiteral("card_limitations")).toList()) {
            const QVariantMap limitation = value.toMap();
            // The reducer keeps the wire entry as it arrived, and the wire says
            // "methods" -- a string list, joined here the way the desktop joins
            // it (client.cpp:1506). Reading a key the server never sends handed
            // Player::setCardLimitation() an empty string, whose one empty
            // token walks into Engine::getCardHandlingMethod()'s Q_ASSERT and
            // takes the client down the first time anything is limited.
            QStringList methods;
            for (const QVariant &method :
                     limitation.value(QStringLiteral("methods")).toList()) {
                const QString name = method.toString();
                if (!name.isEmpty())
                    methods.append(name);
            }
            if (methods.isEmpty())
                continue;
            player->setCardLimitation(methods.join(QLatin1Char(',')),
                limitation.value(QStringLiteral("pattern")).toString(),
                limitation.value(QStringLiteral("reason")).toString(),
                limitation.value(QStringLiteral("single_turn")).toBool());
        }
    }

    if (changed(QStringLiteral("skills"))) {
        // Player::getSkillNames() reports instance-formatted names, which never
        // match the plain names on the wire, so the previous list is the only
        // safe thing to diff against.
        const QStringList wanted = data.value(QStringLiteral("skills")).toStringList();
        for (const QString &skill : applied.value(QStringLiteral("skills")).toStringList()) {
            if (!wanted.contains(skill))
                player->loseSkill(skill);
        }
        const QStringList had = applied.value(QStringLiteral("skills")).toStringList();
        for (const QString &skill : wanted) {
            if (!had.contains(skill))
                player->addSkill(skill);
        }
    }

    if (Sanguosha == nullptr || equipped == entry->equipped)
        return;
    // Equipment has to be the room's own cards: the engine reaches through them
    // for weapon range and the horse corrections distance falls back on.
    for (const Card *worn : player->getEquips()) {
        if (worn != nullptr && !equipped.contains(worn->getEffectiveId()))
            player->removeEquip(worn);
    }
    for (int cardId : equipped) {
        if (player->getEquipsId().contains(cardId))
            continue;
        if (const Card *equip = Sanguosha->getCard(cardId))
            player->setEquip(equip);
    }
}
