#include "basicai.h"
#include "standard.h"
#include "room.h"

BasicAI::BasicAI(ServerPlayer *player)
    : TrustAI(player)
{
}

void BasicAI::activate(CardUseStruct &card_use)
{
    foreach (const Card *card, self->getHandcards()) {
        if (!card->isAvailable(self)) continue;
        
        if (card->isKindOf("Slash")) {
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(self)) {
                if (self->canSlash(p) && isEnemy(p))
                    targets << p;
            }
            if (!targets.isEmpty()) {
                card_use.card = card;
                card_use.from = self;
                card_use.to << targets.first();
                return;
            }
        }
    }
    
    TrustAI::activate(card_use);
}

const Card *BasicAI::askForNullification(const Card *trick, ServerPlayer *from, 
                                         ServerPlayer *to, bool positive)
{
    if (!trick || trick->isKindOf("Nullification")) 
        return nullptr;
    
    if (!from || !to) return nullptr;
    
    if (isEnemy(from) && (isFriend(to) || to == self)) {
        foreach (const Card *card, self->getHandcards()) {
            if (card->isKindOf("Nullification") && !self->isLocked(card))
                return card;
        }
    }
    
    return nullptr;
}
