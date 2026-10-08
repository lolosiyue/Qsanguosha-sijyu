#ifndef IKAI_UTILS_H
#define IKAI_UTILS_H

// Helpers shared by the TouhouTripleSha 异界 ports (ikai-*.cpp).

#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include "skill.h"
#include "standard.h"

namespace IkaiUtils {

// A hand or equipped card the player may still commit.
inline bool ownsCard(const Player *self, const Card *card)
{
    if (!self || !card || card->hasFlag("using"))
        return false;
    const int id = card->getEffectiveId();
    return self->handCards().contains(id) || self->getEquipsId().contains(id);
}

// A skill-made 杀 from `from` to `to` that ignores distance; false when it cannot be used.
inline bool useSkillSlash(Room *room, const SkillContext &ctx, ServerPlayer *from, ServerPlayer *to, const QString &skill,
                          bool addHistory = false)
{
    if (!from || !to || !from->isAlive() || !to->isAlive())
        return false;
    auto *slash = new Slash(Card::NoSuit, 0);
    slash->setSkillName("_" + skill);
    if (from->isCardLimited(slash, Card::MethodUse) || !from->canSlash(to, slash, false)) {
        delete slash;
        return false;
    }
    CardUseStruct use(slash, from, to);
    use.m_addHistory = addHistory;
    use.setOwnedCard(slash);
    room->useCardFromSkillEffect(use, ctx, addHistory);
    return true;
}

// Every selected id passes canSelectCard in selection order.
template <typename SkillT>
bool selectionValid(const SkillT *skill, const ActiveSkillRequest &request)
{
    ActiveSkillRequest selection = request;
    selection.selectedCardIds.clear();
    foreach (int id, request.selectedCardIds) {
        if (!skill->canSelectCard(selection, Sanguosha->getCard(id)))
            return false;
        selection.selectedCardIds << id;
    }
    return true;
}

}

#endif
