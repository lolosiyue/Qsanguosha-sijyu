/********************************************************************
    Copyright (c) 2013-2014 - QSanguosha-Rara

    This file is part of QSanguosha-Hegemony.

    This game is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 3.0
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    See the LICENSE file for more details.

    QSanguosha-Rara
    *********************************************************************/

// Original HEG content: see docs/hegemony-original-names.json for the import namespace.
#include "h-standard-wu-generals.h"
#include "engine.h"
#include "general.h"
#include "player.h"
#include "serverplayer.h"
#include "skill.h"
#include "h-standard-tricks.h"

class HZhiheng : public ViewAsSkillV2
{
public:
    HZhiheng() : ViewAsSkillV2("heg_zhiheng") {}

    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "HZhihengCard"; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        return player && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && player->getMaxHp() > 0 && player->canDiscard(player, "he");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        const Player *player = request.initiator;
        if (!player || !candidate || candidate->hasFlag("using")) return false;
        const int id = candidate->getEffectiveId();
        const Card *pearl = player->getTreasure();
        const bool overMaxHp = request.selectedCardIds.size() >= player->getMaxHp();
        // Donor rule: once at max HP, LuminousPearl permits unlimited further
        // non-pearl costs while the pearl remains outside the selected set.
        if (overMaxHp && (!pearl || !pearl->isKindOf("LuminousPearl")
            || request.selectedCardIds.contains(pearl->getEffectiveId())
            || id == pearl->getEffectiveId())) return false;
        return id >= 0 && !request.selectedCardIds.contains(id)
            && !player->isJilei(candidate)
            && (player->handCards().contains(id) || player->hasEquip(candidate))
            && player->canDiscard(player, id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.isEmpty()) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }

    bool cost(Room *, SkillContext &context, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return false;
        // Capture the selected count before payment; V2 amount interceptors may adjust the draw.
        context.amount = request.selectedCardIds.size();
        return true;
    }

    bool pay(Room *room, SkillContext &context, const ActiveSkillRequest &request) const override
    {
        // Recheck the entire cost before the shared atomic discard, never pay a partial selection.
        return cardSelectionFeasible(request) && ViewAsSkillV2::pay(room, context, request);
    }

    EffectFlow effect(SkillContext &context) const override
    {
        const int count = getEffectiveAmount(context);
        if (context.invoker && context.invoker->isAlive() && count > 0)
            context.invoker->drawCards(count, objectName());
        return ContinueEffects;
    }
};

class HDuoshi : public ViewAsSkillV2
{
public:
    HDuoshi() : ViewAsSkillV2("heg_duoshi", 1)
    {
        response_or_use = true;
    }

    // Each activation instance owns its four uses in the current play phase.
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 4; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && ViewAsSkillV2::canSelectCard(request, candidate)
            && !candidate->hasFlag("using")
            && Sanguosha->matchExpPattern(".|red|.|hand", request.initiator, candidate);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        if (!canSelectCard(selection, material)) return nullptr;

        // The ordinary trick pipeline owns targets and material payment.
        auto *card = new HAwaitExhausted(material->getSuit(), material->getNumber());
        card->addSubcard(material->getEffectiveId());
        card->setSkillName(objectName());
        card->setShowSkill(objectName());
        return card;
    }

    // Preserve the historical AI/log key; V2 alone adds history and usage.
    QString historyKey(const ActiveSkillRequest &) const override { return "DuoshiAE"; }
};

void HStandardPackage::addWuGenerals()
{
    // Keep the maximum-HP Zhiheng variant and Duoshi in V2; reuse other registered skills.
    General *sunquan = new General(this, "heg_sunquan", "wu"); // WU 001
    sunquan->addCompanion("heg_zhoutai");
    sunquan->addSkill(new HZhiheng);

    General *ganning = new General(this, "heg_ganning", "wu"); // WU 002
    ganning->addSkill("qixi");

    General *lvmeng = new General(this, "heg_lvmeng", "wu"); // WU 003
    lvmeng->addSkill("keji");

    General *huanggai = new General(this, "heg_huanggai", "wu"); // WU 004
    huanggai->addSkill("kurou");

    General *zhouyu = new General(this, "heg_zhouyu", "wu", 3); // WU 005
    zhouyu->addCompanion("heg_huanggai");
    zhouyu->addCompanion("heg_xiaoqiao");
    zhouyu->addSkill("yingzi");
    zhouyu->addSkill("fanjian");

    General *daqiao = new General(this, "heg_daqiao", "wu", 3, false); // WU 006
    daqiao->addCompanion("heg_xiaoqiao");
    daqiao->addSkill("guose");
    daqiao->addSkill("liuli");

    General *luxun = new General(this, "heg_luxun", "wu", 3); // WU 007
    luxun->addSkill("qianxun");
    luxun->addSkill(new HDuoshi);

    General *sunshangxiang = new General(this, "heg_sunshangxiang", "wu", 3, false); // WU 008
    sunshangxiang->addSkill("jieyin");
    sunshangxiang->addSkill("xiaoji");

    General *sunjian = new General(this, "heg_sunjian", "wu", 5); // WU 009
    sunjian->addSkill("yinghun");

    General *xiaoqiao = new General(this, "heg_xiaoqiao", "wu", 3, false); // WU 011
    xiaoqiao->addSkill("tenyeartianxiang");
    xiaoqiao->addSkill("hongyan");

    General *taishici = new General(this, "heg_taishici", "wu"); // WU 012
    taishici->addSkill("tianyi");

    General *zhoutai = new General(this, "heg_zhoutai", "wu");
    zhoutai->addSkill("buqu");
    zhoutai->addSkill("mobilefenji");

    General *lusu = new General(this, "heg_lusu", "wu", 3); // WU 014
    lusu->addSkill("haoshi");
    lusu->addSkill("dimeng");

    General *erzhang = new General(this, "heg_erzhang", "wu", 3); // WU 015
    erzhang->addSkill("zhijian");
    erzhang->addSkill("guzheng");

    General *dingfeng = new General(this, "heg_dingfeng", "wu"); // WU 016
    dingfeng->addSkill("duanbing");
    dingfeng->addSkill("fenxun");
}
