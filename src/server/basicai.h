#ifndef _BASICAI_H
#define _BASICAI_H

#include "ai.h"

class BasicAI : public TrustAI
{
    Q_OBJECT

public:
    BasicAI(ServerPlayer *player);

    virtual void activate(CardUseStruct &card_use) override;
    virtual const Card *askForNullification(const Card *trick, ServerPlayer *from, 
                                            ServerPlayer *to, bool positive) override;
};

#endif
