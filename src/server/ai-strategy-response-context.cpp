#include "ai-strategy-round49-extra-response.h"
#include "ai-strategy-round26-response.h"
#include "ai-strategy-response-context.h"
#include "ai-strategy-judge-context.h"
#include "ai.h"
#include "room.h"
#include "serverplayer.h"
#include "lua-wrapper.h"
#include <QJsonArray>
#include <memory>

namespace {
using PublicCard = std::function<QJsonObject(const Card *)>;
QJsonValue name(const ServerPlayer *p) { return p ? QJsonValue(p->objectName()) : QJsonValue(QJsonValue::Null); }
QJsonObject useRow(const CardUseStruct &use, const PublicCard &card)
{
    QJsonArray tos;
    for (const ServerPlayer *p : use.to) if (p) tos.append(p->objectName());
    QJsonObject row{{"from", name(use.from)}, {"to", tos}, {"who", name(use.who)}};
    if (use.card) row.insert("card", card(use.card));
    if (use.whocard) row.insert("whocard", card(use.whocard));
    return row;
}
QJsonObject damageRow(const DamageStruct &d, const PublicCard &card)
{
    QJsonObject row{{"from", name(d.from)}, {"to", name(d.to)}, {"damage", d.damage},
        {"nature", int(d.nature)}, {"chain", d.chain}, {"transfer", d.transfer},
        {"by_user", d.by_user}, {"reason", d.reason}};
    if (d.card) row.insert("card", card(d.card));
    return row;
}
QJsonArray ids(const QVariant &value)
{
    QJsonArray result;
    // toIntList in the donor converts a QVariantList, preserving order/duplicates.
    for (const QVariant &id : value.toList()) result.append(id.toInt());
    return result;
}
bool question(const AIRequest &r, AIRequest::DecisionKind kind, const char *key)
{
    return r.kind == kind && r.choiceOptions.reason == QLatin1String(key);
}
bool response(const AIRequest &r, const char *pattern)
{
    return r.kind == AIRequest::UseCard && r.pattern == QLatin1String(pattern);
}
void insertTag(QJsonObject &tags, const char *key, const char *kind, const QVariant &v,
               const PublicCard &card, bool roomTag = false)
{
    QJsonObject row{{"known", true}, {"present", v.isValid()},
        {"owner", roomTag ? "room" : "owner"}, {"kind", kind}};
    const QString type = QLatin1String(kind);
    if (type == "CardUse") row.insert("value", useRow(v.value<CardUseStruct>(), card));
    else if (type == "Damage") row.insert("value", damageRow(v.value<DamageStruct>(), card));
    else if (type == "Player") row.insert("value", name(v.value<ServerPlayer *>()));
    else if (type == "IntList") row.insert("value", ids(v));
    else if (type == "String") row.insert("value", v.toString());
    tags.insert(QLatin1String(key), row);
}
}


namespace {
void projectResponseProperties(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    auto properties = [&](const char *package, std::initializer_list<const char *> keys) {
        QJsonObject facts = request.worldView.strategyFacts.value(package).toObject();
        QJsonObject values = facts.value("properties").toObject();
        for (const char *key : keys) values.insert(key, viewer->property(key).toString());
        facts.insert("properties", values); request.worldView.strategyFacts.insert(package, facts);
    };
    // Finite owner scalars, preserving QVariant::toString (including unset/false).
    // Writers: NyarzFirst.lua5642/7108, NyarzSecond.lua3477,
    // tenyear.cpp3825/6170; OL package scoped declaration properties.
    properties("NyarzFirst", {"dwzhizheshow", "jfjifengsuit"});
    properties("NyarzSecond", {"ny_second_juanxia_card"});
    properties("tenyear_xd", {"yitongSuit", "shimouPN"});
    properties("ol_qifu", {"allowed_guhuo_dialog_buttons", "mozhi", "olrende"});
    auto scalar = [&](const char *package, const char *key, const QJsonValue &value) {
        auto facts = request.worldView.strategyFacts.value(package).toObject();
        facts.insert(key, value); request.worldView.strategyFacts.insert(package, facts);
    };
    scalar("Loong", "rende_property", viewer->property("lxtx_rende").toString());
    scalar("rushB", "diy_zuguangu", viewer->property("diy_zuguangu").toString());
    scalar("sk", "diezhang_point", viewer->getTag("diezhang_point").toInt());
    scalar("NyarzFirst", "swap_pile", room.getTag("SwapPile").toInt());
    scalar("rushB", "draw_count", room.getDrawPile().size());
    scalar("NyarzSecond", "draw_count", room.getDrawPile().size());
    auto loong = request.worldView.strategyFacts.value("Loong").toObject();
    QJsonObject publicFlags;
    // Both literals are broadcast through setPlayerFlag in Loong.lua608/623/2011.
    for (const char *key : {"LxtxFeiLongDuoFeng", "xianzhenTarget"}) {
        QJsonObject flags;
        for (ServerPlayer *p : room.getAllPlayers(true)) {
            if (QString::fromLatin1(key) == "LxtxFeiLongDuoFeng") {
                const auto observed=readAIStrategyRound26PublicFlag(room,viewer,p,QString::fromLatin1(key));
                if(observed.value("known").toBool()) flags.insert(p->objectName(), observed.value("value"));
            } else flags.insert(p->objectName(), p->hasFlag(key));
        }
        publicFlags.insert(key, flags);
    }
    loong.insert("public_flags", publicFlags); request.worldView.strategyFacts.insert("Loong", loong);
    // Modern OL Anxu uses a temporary private flag, but the exact recipient
    // is also explicitly named to this giver in the actual question prompt.
    if (question(request, AIRequest::Discard, "olanxu")) {
        const auto parts = request.prompt.split(":");
        if (parts.size() >= 3 && parts.first() == "@olanxu") {
            const QString recipient = parts.at(2);
            if (room.findPlayerByObjectName(recipient, true)) {
                QJsonObject flags;
                for (const ServerPlayer *p : room.getAllPlayers(true)) flags.insert(p->objectName(), p->objectName() == recipient);
                scalar("ol_qifu", "olanxu_target", flags);
            }
        }
    }
    // Exact fixed literals whose writes/removals are broadcast setPlayerFlag.
    // Source paths and every fixed writer are inventoried in Round19 evidence.
    auto publicFlagsFor = [&](const char *package, std::initializer_list<const char *> keys) {
        auto facts = request.worldView.strategyFacts.value(package).toObject();
        auto all = facts.value("public_flags").toObject();
        for (const char *key : keys) {
            QJsonObject values;
            for (const ServerPlayer *p : room.getAllPlayers(true)) values.insert(p->objectName(), p->hasFlag(key));
            all.insert(key, values);
        }
        facts.insert("public_flags", all); request.worldView.strategyFacts.insert(package, facts);
    };
    publicFlagsFor("ol_sp", {"xiongmang_target", "olqingshiTo"});
    publicFlagsFor("sk", {"sk_pudu_target", "kuangao_target", "lirang_Target"});
    publicFlagsFor("NyarzFirst", {"jitongxindraw", "jitongxinda", "spjinjintarget", "hflanggutarget", "drangjiejud", "dwqingshidrawall", "dwqingshiupcan", "dmzhanmenggood", "old_yifuta", "shejifrom", "shejito", "xrxiaoruitarget"});
    publicFlagsFor("NyarzSecond", {"jxjuxiangtarget", "basuran", "mjtongguantarget", "gsgushetarget", "ny_second_juanxia_target", "nyarz_wangxi_target", "nyarz_liuzhuan_target", "nyarz_pojun_target", "nyarz_juece_target"});
    auto ol = request.worldView.strategyFacts.value("ol_sp").toObject();
    auto rows = ol.value("properties").toObject();
    auto own = rows.value(viewer->objectName()).toObject();
    for (const char *key : {"xiongmang", "KouchaoCNS", "xianyingName", "quanyuBf"})
        own.insert(key, viewer->property(key).toString());
    rows.insert(viewer->objectName(), own);
    // Both Caiyi histories are broadcast by room.setPlayerProperty immediately
    // on each choice (tenyear2.cpp28870/28926); no hidden recipient fields.
    for (const ServerPlayer *p : room.getAllPlayers(true)) {
        auto row = rows.value(p->objectName()).toObject();
        for (const char *key : {"SkillDescriptionRecord_caiyi", "SkillDescriptionChoiceRecord1_caiyi"})
            row.insert(key, p->property(key).toString());
        rows.insert(p->objectName(), row);
    }
    ol.insert("properties", rows); request.worldView.strategyFacts.insert("ol_sp", ol);
}

void projectAdditionalResponseTags(AIRequest &request, Room &room, ServerPlayer *viewer,
                                  QJsonObject &context, const PublicCard &card)
{
    // Each row is tied to the actual writer's immediate question, not merely
    // possession of a skill. A tag pointer never confers foreign hand access.
    struct Row { const char *package; AIRequest::DecisionKind kind; const char *question;
                 const char *key; const char *type; bool roomTag; const char *pile; };
    const Row rows[] = {
        {"NyarzFirst", AIRequest::SkillInvoke, "Godjieying", "jieyingtarget", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::UseCard, "@@Godpoxi", "godpoxi", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::SkillInvoke, "exrenxin", "renxintarget", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::SkillInvoke, "exjueqing", "exjueqingta", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::SkillInvoke, "dxibin", "dxibinta", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::SkillInvoke, "spduorui", "duoruita", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::SkillInvoke, "Gposhi", "Gposhi", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::UseCard, "@@godgongxin", "godgongxin", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::UseCard, "@@spyuqi", "spyuqita", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::Choice, "spyinghun", "spyinghun", "Player", false, nullptr},
        {"NyarzFirst", AIRequest::UseCard, "@@exchengxiang", "chengxiangcards", "IntList", false, "#exchengxiang"},
        {"NyarzFirst", AIRequest::UseCard, "@@spyuqi", "spyuqicc", "IntList", false, "#spyuqi"},
        {"NyarzFirst", AIRequest::UseCard, "@@spjiufa", "spjiufalist", "IntList", false, "#spjiufa"},
        {"NyarzFirst", AIRequest::UseCard, "@@drangjie", "drangjiecards", "IntList", false, "#drangjie"},
        {"NyarzFirst", AIRequest::UseCard, "@@jfjifeng", "jfjifengai", "IntList", false, "#jfjifeng"},
        {"NyarzFirst", AIRequest::Choice, "dwqingshi", "dwqingshiuseai", "CardUse", false, nullptr},
        {"NyarzSecond", AIRequest::Choice, "jxyangjie", "jxyangjieto", "Player", false, nullptr},
        {"NyarzSecond", AIRequest::Discard, "ny_qingwang_diy", "ny_qingwang_target", "Player", true, nullptr},
        {"NyarzSecond", AIRequest::SkillInvoke, "nyarz_diaodu", "nyarz_diaodu", "Player", true, nullptr},
        {"NyarzSecond", AIRequest::SkillInvoke, "nyarz_shangyu", "nyarz_shangyu", "Player", false, nullptr},
        {"NyarzSecond", AIRequest::PlayerChosen, "nyarz_shangyu", "nyarz_shangyu", "Player", false, nullptr},
        {"NyarzSecond", AIRequest::UseCard, "@@mjtongguan", "mjtongguancards", "IntList", false, "#mjtongguan"},
        {"NyarzSecond", AIRequest::UseCard, "@@nyarz_yingmou_mou", "nyarz_yingmou_mou_card_ids", "IntList", false, "#nyarz_yingmou_mou"},
        {"NyarzSecond", AIRequest::SkillInvoke, "nyarz_renwang", "nyarz_renwang", "Damage", true, nullptr},
        {"NyarzSecond", AIRequest::SkillInvoke, "nyarz_yijve", "nyarz_yijve", "Dying", true, nullptr},
        {"NyarzSecond", AIRequest::Discard, "nyarz_luanzheng_put", "nyarz_luanzheng_cards", "String", true, nullptr},
        {"NyarzSecond", AIRequest::Discard, "nyarz_luanzheng", "nyarz_luanzheng", "CardUse", true, nullptr},
        {"NyarzSecond", AIRequest::SkillInvoke, "nyarz_caixia", "nyarz_caixia", "Card", false, nullptr},
        {"tenyear_xd", AIRequest::UseCard, "@@qiaodui", "qiaodui_data", "CardUse", false, nullptr},
        {"tenyear_xd", AIRequest::PlayerChosen, "chiguo0", "chiguo_data", "CardUse", false, nullptr},
        {"tenyear_xd", AIRequest::PlayerChosen, "juce", "juce_data", "CardUse", false, nullptr},
        {"Loong", AIRequest::SkillInvoke, "lxtx_taoxi", "taoxi_carduse", "CardUse", false, nullptr},
        {"Loong", AIRequest::UseCard, "@@lxtx_shichou", "lxtx_shichou_data", "CardUse", false, nullptr},
        {"rushB", AIRequest::PlayersChosen, "diy_k_fenwei", "fenwei", "CardUse", false, nullptr},
        {"sk", AIRequest::PlayerChosen, "yaoming_second", "yaomingTarget", "Player", true, nullptr},
    };
    for (const auto &row : rows) {
        if (!(row.kind == AIRequest::UseCard ? response(request, row.question)
                                             : question(request, row.kind, row.question))) continue;
        const QVariant value = row.roomTag ? room.getTag(row.key) : viewer->getTag(row.key);
        const QString type(row.type), package(row.package);
        QJsonObject out = context.value(package).toObject();
        QJsonValue result;
        if (type == "Player") result = name(value.value<ServerPlayer *>());
        else if (type == "CardUse") result = useRow(value.value<CardUseStruct>(), card);
        else if (type == "Damage") result = damageRow(value.value<DamageStruct>(), card);
        else if (type == "Dying") {
            const auto dying = value.value<DyingStruct>();
            QJsonObject d{{"who", name(dying.who)}};
            if (dying.damage) d.insert("damage", damageRow(*dying.damage, card));
            result = d;
        } else if (type == "String") result = value.toString();
        else if (type == "Card") {
            const Card *c = value.value<const Card *>();
            result = c ? QJsonValue(card(c)) : QJsonValue(QJsonValue::Null);
        } else if (type == "IntList") {
            const auto list = ids(value);
            const auto piles = request.worldView.strategyFacts.value("notified_piles").toObject();
            const auto disclosed = piles.value(row.pile).toObject().value("card_ids").toArray();
            bool visible = true;
            for (const auto &id : list) if (!disclosed.contains(id)) visible = false;
            if (!visible) continue; // stale/unobserved owner tag cannot disclose hidden IDs
            result = list;
        }
        if (package == "NyarzFirst" || package == "NyarzSecond") {
            if (type == "Player") {
                auto tags = out.value("player_tags").toObject();
                tags.insert(row.key, QJsonObject{{"known", true}, {"present", !result.isNull()},
                    {"owner", row.roomTag ? QString("@room") : viewer->objectName()}, {"value", result}});
                out.insert("player_tags", tags);
            } else if (type == "IntList") {
                auto tags = out.value("id_tags").toObject(); tags.insert(row.key, result); out.insert("id_tags", tags);
            } else if (package == "NyarzSecond") {
                auto tags = out.value("values").toObject();
                tags.insert(row.key, QJsonObject{{"known", true}, {"present", value.isValid()}, {"kind", type}, {"value", result}});
                out.insert("values", tags);
            } else out.insert(row.key, result);
        } else if (package == "sk") { context.insert("sk_yaoming_target", result); context.insert("sk_yaoming_known", true); }
        else out.insert(row.key, result);
        context.insert(package, out);
    }
    // Reuse the exact normal judge producer, including owner filters and
    // honest foreign completeness. Each source writer immediately precedes its question.
    const bool firstJudge = question(request, AIRequest::SkillInvoke, "spjilve")
        || question(request, AIRequest::Choice, "spjilve");
    const bool secondJudge = question(request, AIRequest::Discard, "nyarz_huanshi")
        || question(request, AIRequest::AmazingGrace, "nyarz_huanshi");
    if (firstJudge || secondJudge || response(request, "@@midao")) {
        const QVariant raw = firstJudge ? viewer->getTag("spjilvejudge")
            : secondJudge ? room.getTag("nyarz_huanshi") : viewer->getTag("judgeData");
        const auto *judgePointer = raw.value<JudgeStruct *>();
        const JudgeStruct judge = judgePointer ? *judgePointer : raw.value<JudgeStruct>();
        QJsonObject row = projectAIStrategyJudgeContext(request, room, viewer, &judge, card);
        if (row.isEmpty()) row = QJsonObject{{"reason", judge.reason}, {"pattern", judge.pattern},
            {"who", name(judge.who)}, {"predicate_good", judge.good}, {"outcomes_complete", false}};
        if (firstJudge) {
            auto out = context.value("NyarzFirst").toObject(); out.insert("spjilvejudge", row); context.insert("NyarzFirst", out);
        } else if (secondJudge) {
            auto out = context.value("NyarzSecond").toObject(); auto values = out.value("values").toObject();
            values.insert("nyarz_huanshi", QJsonObject{{"known", true}, {"present", raw.isValid()}, {"kind", "Judge"}, {"value", row}});
            out.insert("values", values);
            // Native public-expression guard controls availability of this map.
            const auto outcomes = row.value("unfiltered_outcome_by_id").toObject();
            if (row.value("predicate_good").isBool() && !outcomes.isEmpty()) {
                QJsonObject matches;
                for (auto it = outcomes.begin(); it != outcomes.end(); ++it)
                    if (it.value().isBool()) matches.insert(it.key(), it.value().toBool() == row.value("predicate_good").toBool());
                out.insert("judge_nil_matches", QJsonObject{{judge.pattern, matches}});
            }
            context.insert("NyarzSecond", out);
        } else context.insert("judge", row);
    }
    if (question(request, AIRequest::Discard, "olqingjian")) {
        auto facts = request.worldView.strategyFacts.value("ol_qifu").toObject();
        auto tags = facts.value("owner_tag_ids").toObject();
        tags.insert("olqingjian", ids(viewer->getTag("olqingjian")));
        facts.insert("owner_tag_ids", tags); request.worldView.strategyFacts.insert("ol_qifu", facts);
    }
    if (request.kind == AIRequest::RespondCard && request.prompt.section(':', 0, 0) == "@rushB_wushuang-jink") {
        // This donor reads the prior public SlashData offset, not current use;
        // only offset_num is consumed. Do not substitute the current CardUse.
        const auto effect = room.getTag("SlashData").value<CardEffectStruct>();
        auto out = context.value("rushB").toObject();
        out.insert("SlashData", QJsonObject{{"offset_num", effect.offset_num}}); context.insert("rushB", out);
    }
    // Owner bookkeeping used by a turn-use revise, not a foreign observation.
    auto second = context.value("NyarzSecond").toObject();
    auto values = second.value("values").toObject();
    values.insert("JilveWansha", QJsonObject{{"known", true}, {"present", true}, {"kind", "Bool"},
        {"value", viewer->getTag("JilveWansha").toBool()}});
    second.insert("values", values); context.insert("NyarzSecond", second);
}
}

void projectAIStrategyResponseContext(AIRequest &request, Room &room, ServerPlayer *viewer,
    const QVariant &data, QJsonObject &context, const PublicCard &publicCard)
{
    if (!viewer || viewer->getRoom() != &room || !publicCard
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()) return;

    projectAIRound49HTMS(request, room, viewer);
    projectResponseProperties(request, room, viewer);
    projectAdditionalResponseTags(request, room, viewer, context, publicCard);

    QJsonObject ol = context.value("ol_sp").toObject();
    QJsonObject tags = ol.value("tags").toObject();
    // These tags are written by each trigger immediately before the named
    // question. Only the current viewer's tag is read, never another AI's tag.
    struct OwnerTag { AIRequest::DecisionKind kind; const char *reason; const char *key; const char *type; };
    const OwnerTag ownerTags[] = {
        {AIRequest::SkillInvoke, "jinjian", "JinjianDamage", "Damage"},
        {AIRequest::PlayersChosen, "hunjiang", "hunjiangUse", "CardUse"},
        {AIRequest::PlayersChosen, "jingzhou", "jingzhouDamage", "Damage"},
        {AIRequest::PlayersChosen, "pijing", "pijingUse", "CardUse"},
        {AIRequest::AmazingGrace, "changxin", "changxinDamage", "Damage"},
        {AIRequest::PlayerChosen, "xiaoshi", "xiaoshiUse", "CardUse"},
        {AIRequest::SkillInvoke, "dongxin", "dongxinData", "Damage"},
        // The effective donor sibing discard callback is the later dongxinData
        // body (ol_sp-ai.lua); do not revive the overwritten sibingData body.
        {AIRequest::Discard, "sibing", "dongxinData", "Damage"},
        {AIRequest::PlayerChosen, "quanyu2", "quanyu2_data", "CardUse"},
    };
    for (const auto &row : ownerTags)
        if (question(request, row.kind, row.reason))
            insertTag(tags, row.key, row.type, viewer->getTag(row.key), publicCard);
    if (response(request, "@@zhishi"))
        insertTag(tags, "ZhishiTarget", "Player", viewer->getTag("ZhishiTarget"), publicCard);
    if (response(request, "@@olqingshi"))
        insertTag(tags, "olqingshiUse", "CardUse", viewer->getTag("olqingshiUse"), publicCard);
    // Fixed native PanqinRecord stores QVariantList<int>. Preserve the actual
    // toIntList conversion, order and duplicates; unset QVariant yields {}.
    if (question(request, AIRequest::SkillInvoke, "panqin"))
        insertTag(tags, "PanqinRecord", "IntList", viewer->getTag("PanqinRecord"), publicCard);
    if (question(request, AIRequest::SkillInvoke, "SkillEffect")
        && (data.toString() == "s29" || data.toString() == "s30"))
        insertTag(tags, "SkillEffect", "Damage", viewer->getTag("SkillEffect"), publicCard);
    // The generated skill's description is explicitly delivered to this owner
    // before acquisition (newgenerals.lua26375-26379). No arbitrary tag keys.
    if (request.kind == AIRequest::PlayerChosen || request.kind == AIRequest::PlayersChosen
        || request.kind == AIRequest::Discard) {
        for (int i = 0; i < 100; ++i) {
            const QString skill = "qingshu_tianshu" + QString::number(i);
            if (request.choiceOptions.reason != skill || !viewer->hasSkill(skill, true)) continue;
            const QByteArray key = (skill + "SkillEffect").toUtf8();
            insertTag(tags, key.constData(), "String", room.getTag(QString::fromUtf8(key)), publicCard, true);
        }
    }
    if (!tags.isEmpty()) ol.insert("tags", tags);
    if (response(request, "@@guowu2")) {
        QJsonObject flags;
        // Native tenyear2.cpp:9047 discloses the eligible target set through
        // public player flags before this exact askForUseCard call.
        for (const ServerPlayer *p : room.getAllPlayers(true))
            flags.insert(p->objectName(), p->hasFlag("guowu_canchoose"));
        ol.insert("guowu_canchoose", flags);
    }
    if (question(request, AIRequest::Choice, "fuchao")) {
        const Card *reference = nullptr;
        QJsonObject f{{"known", false}};
        // Only the actual question's reference is queried. No caller-supplied
        // wire, prior question cache or arbitrary native card lookup is used.
        if (data.canConvert<CardUseStruct>()) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card) { reference = use.whocard; f.insert("mode", "use"); f.insert("input", useRow(use, publicCard)); }
        }
        if (!reference && data.canConvert<CardResponseStruct>()) {
            const CardResponseStruct resp = data.value<CardResponseStruct>();
            reference = resp.m_toCard;
            QJsonObject row{{"m_who", name(resp.m_who)}};
            if (resp.m_card) row.insert("m_card", publicCard(resp.m_card));
            if (reference) row.insert("m_toCard", publicCard(reference));
            f.insert("mode", "response"); f.insert("input", row);
        }
        if (reference) {
            f.insert("known", true);
            // Room::getUseStruct uses tag["UseHistory" + card->toString()].
            // getTag preserves its value/default semantics without inserting a
            // missing key during readonly projection. The key is never exposed.
            const QVariant history = room.getTag("UseHistory" + reference->toString());
            f.insert("use", useRow(history.value<CardUseStruct>(), publicCard));
        }
        ol.insert("fuchao", f);
    }
    if (!ol.isEmpty()) context.insert("ol_sp", ol);

    QJsonObject ny = context.value("nybeauty").toObject();
    QJsonObject known = ny.value("known_tags").toObject();
    QJsonObject kinds = ny.value("tag_kinds").toObject();
    auto nyTag = [&](const char *key, const char *kind, const QVariant &value) {
        known.insert(key, true); kinds.insert(key, kind);
        if (QLatin1String(kind) == "IntList") ny.insert(key, ids(value));
        else if (QLatin1String(kind) == "Player") ny.insert(key, name(value.value<ServerPlayer *>()));
        else if (QLatin1String(kind) == "CardUse") ny.insert(key, useRow(value.value<CardUseStruct>(), publicCard));
        else if (QLatin1String(kind) == "Damage") ny.insert(key, damageRow(value.value<DamageStruct>(), publicCard));
        else if (QLatin1String(kind) == "Card") {
            const Card *card = value.value<const Card *>();
            ny.insert(key, card ? QJsonValue(publicCard(card)) : QJsonValue(QJsonValue::Null));
        }
    };
    // Source nybeauty.lua sends the ID list to THIS viewer (private log and
    // notified pile) before @@nyyuqi; never expose it to another observer.
    if (response(request, "@@nyyuqi") && viewer->hasFlag("nyyuqiview"))
        nyTag("nyyuqiuse", "IntList", viewer->getTag("nyyuqiuse"));
    if (response(request, "@@nytongli")) nyTag("nytongliorigin", "Card", viewer->getTag("nytongliorigin"));
    if (question(request, AIRequest::PlayerChosen, "nyzhennan")) nyTag("nyzhennan_card", "Card", viewer->getTag("nyzhennan_card"));
    if (question(request, AIRequest::SkillInvoke, "ny_qiaoshi")) nyTag("ny_qiaoshi", "Damage", viewer->getTag("ny_qiaoshi"));
    if (response(request, "@@ny_qiaoshi")) nyTag("ny_qiaoshi_cards", "IntList", viewer->getTag("ny_qiaoshi_cards"));
    if (question(request, AIRequest::Choice, "nyarz_guangu") || response(request, "@@nyarz_guangu"))
        nyTag("nyarz_guangu", "Player", viewer->getTag("nyarz_guangu"));
    if (question(request, AIRequest::SkillInvoke, "nyarz_lingxi")) nyTag("nyarz_lingxi_use", "CardUse", viewer->getTag("nyarz_lingxi_use"));
    // nyarz_anzhi is an ID-only historical discard ledger, populated only by
    // public discard moves (nybeauty.lua:4286). No current face is re-read.
    if (response(request, "@@nyarz_anzhi")) nyTag("nyarz_anzhi", "IntList", room.getTag("nyarz_anzhi"));
    if (!known.isEmpty()) { ny.insert("known_tags", known); ny.insert("tag_kinds", kinds); context.insert("nybeauty", ny); }

    // The seven removed legacy Lua prototypes affect early policy declines.
    // Card_Parse only constructs a registered prototype; no policy is called.
    if (viewer->hasSkills("lualveji|luawu|luamouce|luahuntian|luafentian|luajice|luajiejiang")) {
        QJsonObject facts = request.worldView.strategyFacts.value("new").toObject(), prototypes;
        for (const QString &selector : {"#lualveji", "#luawu", "#luamouce", "#luahuntian", "#luafentian", "#luajice", "#luajiejiang"}) {
            const std::unique_ptr<SkillCard> card(LuaSkillCard::Parse(selector + ":.:"));
            prototypes.insert(selector, bool(card));
        }
        facts.insert("legacy_prototypes", prototypes);
        request.worldView.strategyFacts.insert("new", facts);
    }
}

void projectAIStrategyResponseEventContext(AIWorldView &world, Room &room, ServerPlayer *observer,
    const AIEventView &event, const PublicCard &publicCard)
{
    Q_UNUSED(publicCard);
    if (!observer || observer->getRoom() != &room || world.self.objectName != observer->objectName()
        || event.triggerEvent != ChoiceMade || event.from != observer->objectName()
        || (!event.privateViewer.isEmpty() && event.privateViewer != observer->objectName())) return;
    const auto prompt = event.details.value("prompt_list").toArray();
    if (prompt.size() < 3 || prompt.first().toString() != "skillInvoke") return;
    const QString skill = prompt.at(1).toString();
    QString package, key; QVariant value; QString owner;
    if (skill == "spduorui") {
        package = "NyarzFirst"; key = "duoruita"; value = observer->getTag(key); owner = observer->objectName();
    } else if (skill == "nyarz_diaodu") {
        package = "NyarzSecond"; key = "nyarz_diaodu"; value = room.getTag(key); owner = "@room";
    } else return;
    const auto target = value.value<ServerPlayer *>();
    auto context = world.decisionContext.value(package).toObject();
    auto tags = context.value("player_tags").toObject();
    tags.insert(key, QJsonObject{{"known", true}, {"present", target != nullptr},
        {"owner", owner}, {"value", name(target)}});
    context.insert("player_tags", tags); world.decisionContext.insert(package, context);
}
