#include "tui-room-context.h"

#include "card.h"
#include "client-game-state.h"
#include "engine.h"
#include "protocol.h"
#include "protocol/protocol-message.h"
#include "wrapped-card.h"

using namespace QSanProtocol;

TuiRoomContext::TuiRoomContext(const ClientGameState *state, QObject *parent)
    : QObject(parent), m_state(state), m_roomState(true)
{
}

TuiRoomContext::~TuiRoomContext()
{
    leaveGame();
}

void TuiRoomContext::enterGame()
{
    if (Sanguosha == nullptr)
        return;
    // reset() clones the whole engine card table into WrappedCards owned by
    // this thread, so it has to run after the registration that makes those
    // cards reachable.
    Sanguosha->registerRoom(this);
    m_roomState.reset();
    m_active = true;
}

void TuiRoomContext::leaveGame()
{
    if (!m_active)
        return;
    m_active = false;
    if (Sanguosha != nullptr)
        Sanguosha->unregisterRoom();
    m_roomState.clear();
}

bool TuiRoomContext::isActive() const
{
    return m_active;
}

void TuiRoomContext::applyMessage(const ProtocolMessage &message)
{
    switch (message.command) {
    case S_COMMAND_GAME_START:
        enterGame();
        break;
    case S_COMMAND_GAME_OVER:
        leaveGame();
        break;
    case S_COMMAND_UPDATE_CARD:
        applyCardUpdate(message.payload.toMap());
        break;
    default:
        break;
    }
}

void TuiRoomContext::applyCardUpdate(const QVariantMap &payload)
{
    if (!m_active || Sanguosha == nullptr)
        return;
    const int cardId = payload.value(QStringLiteral("card_id")).toInt();
    if (payload.value(QStringLiteral("action")).toString() == QLatin1String("reset")) {
        m_roomState.resetCard(cardId);
        return;
    }

    WrappedCard *wrapped = qobject_cast<WrappedCard *>(m_roomState.getCard(cardId));
    if (wrapped == nullptr)
        return;
    // Same shape as Client::updateCard(): build the card the server describes
    // and copy it over the room's own instance. The clone is left to the card
    // lifetime manager exactly as the desktop and RoomState::reset() leave it.
    Card *updated = Sanguosha->cloneCard(
        payload.value(QStringLiteral("card_name")).toString(),
        static_cast<Card::Suit>(payload.value(QStringLiteral("suit")).toInt()),
        payload.value(QStringLiteral("number")).toInt(),
        payload.value(QStringLiteral("flags")).toStringList());
    if (updated == nullptr)
        return;
    updated->setId(cardId);
    updated->setSkillName(payload.value(QStringLiteral("skill_name")).toString());
    updated->setObjectName(payload.value(QStringLiteral("object_name")).toString());
    wrapped->copyEverythingFrom(updated);
}

QObject *TuiRoomContext::runtimeObject()
{
    return this;
}

RoomState *TuiRoomContext::roomState()
{
    return &m_roomState;
}

const Player *TuiRoomContext::cardOwner(int) const
{
    // The text client has no client-side Player objects yet, so it cannot name
    // an owner. Returning null keeps every engine query that asks for one at
    // exactly the answer it already got before this context existed.
    return nullptr;
}

Player::Place TuiRoomContext::cardPlace(int cardId) const
{
    if (m_state == nullptr)
        return Player::PlaceUnknown;
    const QVariantMap card = m_state->card(cardId);
    if (!card.contains(QStringLiteral("place")))
        return Player::PlaceUnknown;
    return static_cast<Player::Place>(card.value(QStringLiteral("place")).toInt());
}

Card *TuiRoomContext::card(int cardId) const
{
    return m_roomState.getCard(cardId);
}
