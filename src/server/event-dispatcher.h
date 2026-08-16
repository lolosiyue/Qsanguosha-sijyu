#ifndef EVENT_DISPATCHER_H
#define EVENT_DISPATCHER_H

#include "structs.h"

class TriggerSkill;

class EventDispatcher
{
public:
    virtual ~EventDispatcher() = default;

    virtual bool dispatch(TriggerEvent event, ServerPlayer *target, QVariant &data) = 0;
    virtual void registerTriggerSkill(const TriggerSkill *skill) = 0;
};

#endif
