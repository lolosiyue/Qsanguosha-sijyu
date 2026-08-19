#ifndef _ENGINE_RUNTIME_CONTEXT_H
#define _ENGINE_RUNTIME_CONTEXT_H

#include "player.h"

class Card;
class RoomState;
class RoomRuntime;
class QObject;

class EngineRuntimeContext
{
public:
    virtual ~EngineRuntimeContext() = default;
    virtual QObject *runtimeObject() = 0;
    virtual RoomState *roomState() = 0;
    virtual const Player *cardOwner(int cardId) const = 0;
    virtual Player::Place cardPlace(int cardId) const = 0;
    virtual Card *card(int cardId) const = 0;
    virtual RoomRuntime *roomRuntime() { return nullptr; }
};

#endif
