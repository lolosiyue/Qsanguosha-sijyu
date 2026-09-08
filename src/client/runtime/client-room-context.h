#ifndef CLIENT_ROOM_CONTEXT_H
#define CLIENT_ROOM_CONTEXT_H

#include "card.h"
#include "client-game-state.h"
#include "engine-runtime-context.h"
#include "engine.h"
#include "protocol.h"
#include "protocol/protocol-message.h"
#include "room-state.h"
#include "wrapped-card.h"

#include <QObject>
#include <QVariantMap>

#include <functional>
#include <utility>

// Frontend-neutral client-side EngineRuntimeContext. TUI uses this directly and
// the future WASM runtime can reuse the same room/card projection without
// depending on terminal presentation code.
class ClientRoomContext final : public QObject, public EngineRuntimeContext
{
public:
    explicit ClientRoomContext(const ClientGameState *state, QObject *parent = nullptr)
        : QObject(parent), m_state(state), m_roomState(true)
    {
    }

    ~ClientRoomContext() override
    {
        leaveGame();
    }

    void enterGame()
    {
        if (Sanguosha == nullptr)
            return;
        Sanguosha->registerRoom(this);
        m_roomState.reset();
        m_active = true;
    }

    void leaveGame()
    {
        if (!m_active)
            return;
        m_active = false;
        if (Sanguosha != nullptr)
            Sanguosha->unregisterRoom();
        m_roomState.clear();
    }

    bool isActive() const { return m_active; }

    void applyMessage(const QSanProtocol::ProtocolMessage &message)
    {
        using namespace QSanProtocol;
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

    using OwnerResolver = std::function<const Player *(int cardId)>;

    void setOwnerResolver(OwnerResolver resolver)
    {
        m_ownerResolver = std::move(resolver);
    }

    void setCardUseContext(CardUseStruct::CardUseReason reason, const QString &pattern)
    {
        m_roomState.setCurrentCardUseReason(reason);
        m_roomState.setCurrentCardUsePattern(pattern);
    }

    QObject *runtimeObject() override { return this; }
    RoomState *roomState() override { return &m_roomState; }

    const Player *cardOwner(int cardId) const override
    {
        return m_ownerResolver ? m_ownerResolver(cardId) : nullptr;
    }

    Player::Place cardPlace(int cardId) const override
    {
        if (m_state == nullptr)
            return Player::PlaceUnknown;
        const QVariantMap cardState = m_state->card(cardId);
        if (!cardState.contains(QStringLiteral("place")))
            return Player::PlaceUnknown;
        return static_cast<Player::Place>(cardState.value(QStringLiteral("place")).toInt());
    }

    Card *card(int cardId) const override
    {
        return m_roomState.getCard(cardId);
    }

    bool projectStateCards()
    {
        if (!m_active || Sanguosha == nullptr || m_state == nullptr)
            return false;
        for (int id : m_state->cardIds()) {
            const QVariantMap data = m_state->card(id);
            if (data.value(QStringLiteral("modified")).toBool()) {
                QVariantMap payload = data;
                payload.insert(QStringLiteral("card_id"), id);
                if (!applyCardUpdate(payload))
                    return false;
            }
            Card *card = m_roomState.getCard(id);
            if (card == nullptr)
                continue;
            if (data.contains(QStringLiteral("flags"))) {
                card->setFlags(QStringLiteral("."));
                for (const QString &flag : data.value(QStringLiteral("flags")).toStringList())
                    card->setFlags(flag);
            }
            const QVariantMap marks = data.value(QStringLiteral("marks")).toMap();
            for (auto it = marks.constBegin(); it != marks.constEnd(); ++it)
                card->setMark(it.key(), it.value().toInt());
        }
        return true;
    }

private:
    bool applyCardUpdate(const QVariantMap &payload)
    {
        if (!m_active || Sanguosha == nullptr)
            return false;
        const int cardId = payload.value(QStringLiteral("card_id")).toInt();
        if (payload.value(QStringLiteral("action")).toString() == QLatin1String("reset")) {
            m_roomState.resetCard(cardId);
            return true;
        }

        WrappedCard *wrapped = qobject_cast<WrappedCard *>(m_roomState.getCard(cardId));
        if (wrapped == nullptr)
            return false;
        Card *updated = Sanguosha->cloneCard(
            payload.value(QStringLiteral("card_name")).toString(),
            static_cast<Card::Suit>(payload.value(QStringLiteral("suit")).toInt()),
            payload.value(QStringLiteral("number")).toInt(),
            payload.value(QStringLiteral("flags")).toStringList());
        if (updated == nullptr)
            return false;
        updated->setId(cardId);
        updated->setSkillName(payload.value(QStringLiteral("skill_name")).toString());
        const QString objectName = payload.value(QStringLiteral("object_name")).toString();
        if (!objectName.isEmpty())
            updated->setObjectName(objectName);
        wrapped->copyEverythingFrom(updated);
        return wrapped->getRealCard() == updated;
    }

    const ClientGameState *m_state;
    OwnerResolver m_ownerResolver;
    RoomState m_roomState;
    bool m_active = false;
};

#endif
