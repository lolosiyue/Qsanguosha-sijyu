#include "ai-strategy-range-query.h"
#include "ai-strategy-round27-choice.h"
#include "ai.h"
#include "ai-strategy-round44-certificate.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"

#include <QSet>

bool validateAIStrategyOwnerRange(Room &room, ServerPlayer *viewer, const AIRequest &request,
                                 const QString &flag, const QString &sourceKey,
                                 AIOwnerRangeQueryScope::Flag &kind)
{
    if (!viewer || viewer->getRoom() != &room || request.viewerObjectName != viewer->objectName()
        || !aiRound44RangeRequestCurrent(room,request,viewer)) return false;
    if (sourceKey == "ov_xiafeng") {
        if (flag != "InfinityAttackRange" || !aiRound27ChoicePlanningQuestion(request, room, viewer)) return false;
        kind = AIOwnerRangeQueryScope::InfinityAttackRange;
        return true;
    }
    // Fixed Original heg-standard-wei-ai.lua762-764, called by
    // useCardThreatenEmperor(1174). Owner-only
    // readonly Slash planning; no receipt, native skill or final-use authority.
    if (sourceKey == "original:shensu3") {
        if (flag != "slashNoDistanceLimit" || !request.worldView.enableHegemony
            || !request.worldView.strategyFacts.value("hegemony").toObject()
                    .value("original_overrides_loaded").toBool()
            || !viewer->hasSkill("heg_shensu")) return false;
        const bool play = request.kind == AIRequest::Activate
            && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
        if (!play) return false;
        kind = AIOwnerRangeQueryScope::SlashNoDistanceLimit;
        return true;
    }
    // Fixed strategic_advantage-ai.lua638-642. The readonly scope changes only
    // the current owner query; native action/cost/target acceptance is separate.
    if (sourceKey == "original:useCardLureTiger") {
        if (flag != "slashNoDistanceLimit" || !request.worldView.enableHegemony
            || request.kind != AIRequest::Activate
            || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        bool source = viewer->hasSkill("heg_diaogui");
        auto disclosedLure = [&](const AICardView &card) {
            return card.effectiveId >= 0 && card.objectName == "lure_tiger"
                && room.getCardOwner(card.effectiveId) == viewer;
        };
        for (const AICardView &card : request.worldView.handCards) source |= disclosedLure(card);
        for (const AICardPileView &pile : request.worldView.self.piles)
            for (const AICardView &card : pile.cards) source |= disclosedLure(card);
        if (!source) return false;
        kind = AIOwnerRangeQueryScope::SlashNoDistanceLimit;
        return true;
    }
    // Fixed b6025 source callback identities. These permit owner-only planning,
    // not activation, materials or foreign native queries. No wildcard aliases.
    if (sourceKey == "bu_s2_benxi" && (request.kind != AIRequest::Activate
        || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || !viewer->hasSkill(sourceKey))) return false;
    static const QSet<QString> infinite{
        "bu_s2_benxi",
        "GodShijun", "kejieshengzhuihun", "mouwusheng", "@mouxianchou-slash",
        "@@f_yonghunSlash", "BearingDownBorder", "@sfofl_bearing_down_border",
        "sfofl_jubing", "@@sfofl_qifeng"};
    static const QSet<QString> noDistance{
        "shenghua", "shijun", "@@luazhuixi", "@@luayonglue", "@PlusZhongyong",
        "SixQiaoxie", "#sfofl_shuangren", "sfofl_sitong", "sfofl_jiaozheng",
        "sfofl_qingjin", "#sfofl_zainei", "#sfofl_sixi", "sfofl_juedou"};
    if (flag == QLatin1String("InfinityAttackRange") && infinite.contains(sourceKey)) {
        kind = AIOwnerRangeQueryScope::InfinityAttackRange;
        return true;
    }
    if (flag == QLatin1String("slashNoDistanceLimit") && noDistance.contains(sourceKey)) {
        kind = AIOwnerRangeQueryScope::SlashNoDistanceLimit;
        return true;
    }
    return false;
}

QJsonObject projectAIStrategyOwnerRange(Room &room, ServerPlayer *viewer,
                                      const QString &flag, const QString &sourceKey)
{
    QJsonObject reach;
    for (ServerPlayer *target : room.getAllPlayers(true))
        reach.insert(target->objectName(), viewer->inMyAttackRange(target));
    return {{"kind", "owner_range_hypothesis"}, {"flag", flag}, {"source_key", sourceKey},
        {"viewer", viewer->objectName()},
        {"InfinityAttackRange", viewer->hasFlag("InfinityAttackRange")},
        {"slashNoDistanceLimit", viewer->hasFlag("slashNoDistanceLimit")},
        {"attack_range", viewer->getAttackRange()},
        {"attack_range_without_weapon", viewer->getAttackRange(false)},
        {"in_attack_range", reach}};
}
