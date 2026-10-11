#include "ai-strategy-retrial-context.h"
#include "ai.h"
#include "ai-strategy-judge-context.h"
#include <QJsonArray>
#include <QObject>
#include <QSet>
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
#include "wrapped-card.h"

namespace {
using CardProjector = std::function<QJsonObject(const Card *)>;
QObject *previewLedger(Room &room, bool create)
{
    const QString name = QStringLiteral("_isolated_retrial_preview_ledger");
    auto *ledger = room.findChild<QObject *>(name, Qt::FindDirectChildrenOnly);
    if (!ledger && create) { ledger = new QObject(&room); ledger->setObjectName(name); }
    return ledger;
}
void invalidatePreviewCard(QObject *ledger, int id)
{
    auto rows = ledger->property("rows").toJsonObject();
    // Invalidate the whole small preview, including pending handoff, on face
    // replacement. A same-place wrapper mutation must not retain stale knowledge.
    for (auto it = rows.begin(); it != rows.end();) {
        const auto row = it.value().toObject();
        const auto ids = row.value(row.contains("pending") ? "pending" : "ids").toArray();
        if (ids.contains(id)) it = rows.erase(it); else ++it;
    }
    ledger->setProperty("rows", rows);
}
bool previewSkill(const QString &skill) { return skill == "PlusYiji" || skill == "fatewangzhe" || skill == "caoduo"; }
QString caoduoSourceScope(Room &room, ServerPlayer *viewer)
{
    if (!viewer || room.getCurrent() != viewer || !viewer->hasSkill("caoduo")
        || !viewer->hasFlag("Global_InTempMoving")) return {};
    // TriggerSkillV2 does not allocate SkillExecutionRegistry IDs for this
    // rule. The actual live native history scope is the authoritative lifetime.
    const auto scope = room.historyParent(room.currentHistoryEventId(), "skill", true);
    const auto data = scope.value("data").toMap();
    if (scope.value("status").toString() != "active"
        || data.value("executing_skill").toString() != "caoduo") return {};
    return scope.value("id").toString();
}
QString previewKey(const ServerPlayer *viewer, const QString &skill)
{ return viewer->objectName() + QChar(0x1f) + skill; }
QJsonObject previewFace(const QJsonObject &card)
{
    QJsonObject face;
    // Frozen public face only: never retained action rules, flags or skill chain.
    for (const char *key : {"id", "effective_id", "name", "class_name", "type_id", "suit",
        "suit_string", "number", "kind_of", "red", "black", "name_length", "is_single_target"})
        if (card.contains(key)) face.insert(key, card.value(key));
    face.insert("flags_complete", false); face.insert("skill_names_complete", false);
    return face;
}
bool inRoom(const ServerPlayer *player, const Room &room)
{
    return !player || player->getRoom() == &room;
}
QJsonValue damageValue(const QVariant &value, const Room &room, const CardProjector &publicCard,
    const ServerPlayer *requiredTarget = nullptr)
{
    if (!value.canConvert<DamageStruct>()) return QJsonValue(QJsonValue::Undefined);
    const DamageStruct damage = value.value<DamageStruct>();
    if (!inRoom(damage.from, room) || !inRoom(damage.to, room)
        || (requiredTarget && damage.to != requiredTarget))
        return QJsonValue(QJsonValue::Undefined);
    return QJsonObject{
        {"from", damage.from ? QJsonValue(damage.from->objectName()) : QJsonValue(false)},
        {"to", damage.to ? QJsonValue(damage.to->objectName()) : QJsonValue(false)},
        {"amount", damage.damage}, {"nature", int(damage.nature)}, {"reason", damage.reason},
        {"chain", damage.chain}, {"transfer", damage.transfer}, {"by_user", damage.by_user},
        {"card", damage.card ? QJsonValue(publicCard(damage.card)) : QJsonValue(false)}};
}
QJsonValue useValue(const QVariant &value, const Room &room, const CardProjector &publicCard)
{
    if (!value.canConvert<CardUseStruct>()) return QJsonValue(QJsonValue::Undefined);
    const CardUseStruct use = value.value<CardUseStruct>();
    if (!inRoom(use.from, room)) return QJsonValue(QJsonValue::Undefined);
    QJsonArray targets;
    for (const auto *target : use.to) {
        if (!target || !inRoom(target, room)) return QJsonValue(QJsonValue::Undefined);
        targets.append(target->objectName());
    }
    return QJsonObject{{"from", use.from ? QJsonValue(use.from->objectName()) : QJsonValue(false)},
        {"to", targets}, {"targets", targets},
        {"card", use.card ? QJsonValue(publicCard(use.card)) : QJsonValue(false)},
        {"m_addHistory", use.m_addHistory}};
}
QJsonValue pindianValue(const QVariant &value, const Room &room, const CardProjector &publicCard)
{
    if (!value.canConvert<PindianStruct *>()) return QJsonValue(QJsonValue::Undefined);
    const auto *p = value.value<PindianStruct *>();
    if (!p || !p->from || !p->to || !inRoom(p->from, room) || !inRoom(p->to, room)
        || !p->from_card || !p->to_card) return QJsonValue(QJsonValue::Undefined);
    // ServerPlayer::pindianSelect sends both $PindianResult logs and completes
    // a visible move before finishPindian triggers PindianVerifying.
    return QJsonObject{{"from", p->from->objectName()}, {"to", p->to->objectName()},
        {"reason", p->reason}, {"from_number", p->from_number}, {"to_number", p->to_number},
        {"from_card", publicCard(p->from_card)}, {"to_card", publicCard(p->to_card)}};
}
void scopedFlags(QJsonObject &context, const Room &room, const QStringList &names)
{
    QJsonObject rows;
    for (const auto *p : room.getAllPlayers(true)) {
        QJsonObject flags;
        for (const auto &name : names) flags.insert(name, p->hasFlag(name));
        rows.insert(p->objectName(), flags);
    }
    context.insert("biaofeng_flags", rows);
}
QJsonValue playerValue(const QVariant &value, const Room &room)
{
    if (!value.canConvert<ServerPlayer *>()) return QJsonValue(QJsonValue::Undefined);
    const ServerPlayer *target = value.value<ServerPlayer *>();
    if (!inRoom(target, room)) return QJsonValue(QJsonValue::Undefined);
    return target ? QJsonValue(target->objectName()) : QJsonValue(false);
}
QJsonValue cardValue(const QVariant &value, const CardProjector &publicCard)
{
    if (!value.canConvert<const Card *>()) return QJsonValue(QJsonValue::Undefined);
    const Card *card = value.value<const Card *>();
    return card ? QJsonValue(publicCard(card)) : QJsonValue(false);
}
void insertKnown(QJsonObject &context, const char *key, const QJsonValue &value)
{
    if (!value.isUndefined()) context.insert(QString::fromLatin1(key), value);
}
}

void recordAIStrategyRetrialPreview(Room &room, ServerPlayer *recipient,
    const QList<CardsMoveStruct> &moves, bool isLostPhase, const CardProjector &publicCard)
{
    if (!recipient || recipient->getRoom() != &room || !publicCard) return;
    auto *ledger = previewLedger(room, true);
    auto rows = ledger->property("rows").toJsonObject();
    for (const auto &move : moves) {
        // Both explicit preview cleanup and actual moves from the preview hand
        // revoke on loss, before any later callback can reuse the observation.
        if (isLostPhase && move.from == recipient && move.from_place == Player::PlaceHand) {
            for (const QString skill : {QStringLiteral("PlusYiji"), QStringLiteral("fatewangzhe"), QStringLiteral("caoduo")}) {
                const QString key = previewKey(recipient, skill);
                auto row = rows.value(key).toObject(); auto faces = row.value("faces").toObject();
                auto mappings = row.value("mappings").toObject();QJsonArray ids;
                for (const auto &value : row.value("ids").toArray()) {
                    const int id = value.toInt();
                    if (move.card_ids.contains(id)) { faces.remove(QString::number(id)); mappings.remove(QString::number(id)); }
                    else ids.append(id);
                }
                if (ids.isEmpty()) rows.remove(key);
                else { row.insert("ids", ids); row.insert("faces", faces); row.insert("mappings", mappings); rows.insert(key, row); }
            }
        }
        QString skill = move.reason.m_skillName;
        QString sourceScope;
        if (skill.isEmpty() && move.reason.m_reason == CardMoveReason::S_REASON_UNKNOWN) {
            sourceScope = caoduoSourceScope(room, recipient);
            if (!sourceScope.isEmpty()) skill = "caoduo";
        }
        if (!previewSkill(skill)
            || (skill == "caoduo" ? sourceScope.isEmpty() : move.reason.m_reason != CardMoveReason::S_REASON_PREVIEW)
            || move.reason.m_playerId != recipient->objectName()
            || move.to != recipient || move.from != nullptr
            || move.from_place != Player::PlaceTable || move.to_place != Player::PlaceHand) continue;
        const QString key = previewKey(recipient, skill);
        QJsonArray ids;QSet<int> seen;bool valid = move.open && !move.card_ids.isEmpty() && move.card_ids.size() <= (skill == "caoduo" ? 512 : 64);
        for (int id : move.card_ids) { if (id < 0 || seen.contains(id)) valid = false;seen.insert(id);ids.append(id); }
        if (skill == "caoduo" && valid) {
            auto *owner = room.getCardOwner(move.card_ids.first());
            valid = owner && owner != recipient && owner->getRoom() == &room
                && owner->handCards() == move.card_ids;
            for (int id : move.card_ids)
                valid = valid && room.getCardOwner(id) == owner && room.getCardPlace(id) == Player::PlaceHand;
        }
        if (!valid) { rows.remove(key);continue; }
        if (isLostPhase) { rows.insert(key, QJsonObject{{"pending", ids}, {"source_scope", sourceScope}});continue; }
        const auto pendingRow = rows.value(key).toObject();
        const auto pending = pendingRow.value("pending");
        if (!pending.isArray() || pending.toArray() != ids
            || pendingRow.value("source_scope").toString() != sourceScope) { rows.remove(key);continue; }
        QJsonObject faces, mappings;
        for (int id : move.card_ids) {
            const Card *card = room.getCard(id);
            if (!card || card->isVirtualCard() || card->getEffectiveId() != id) { valid = false;break; }
            const QString idKey = QString::number(id);
            auto watched = ledger->property("watched").toJsonObject();
            if (!watched.contains(idKey)) {
                if (const auto *wrapped = qobject_cast<const WrappedCard *>(card)) {
                    QObject::connect(wrapped, &WrappedCard::identityChanged, ledger,
                        [ledger, id]() { invalidatePreviewCard(ledger, id); }, Qt::DirectConnection);
                    watched.insert(idKey, true); ledger->setProperty("watched", watched);
                }
            }
            auto face = previewFace(publicCard(card));face.insert("id", id);face.insert("effective_id", id);
            if (skill == "caoduo") face.insert("known_flags", QJsonObject{{"caoduo", card->hasFlag("caoduo")}});
            faces.insert(idKey, face);
            mappings.insert(idKey, QJsonObject{{"place", int(room.getCardPlace(id))},
                {"owner", room.getCardOwner(id) ? room.getCardOwner(id)->objectName() : QString()}});
        }
        if (!valid) rows.remove(key);
        else {
            const qulonglong serial = ledger->property("next_serial").toULongLong() + 1;
            ledger->setProperty("next_serial", QVariant::fromValue(serial));
            rows.insert(key, QJsonObject{{"ids", ids}, {"faces", faces}, {"mappings", mappings},
                {"serial", QString::number(serial)}, {"source_scope", sourceScope}});
        }
    }
    ledger->setProperty("rows", rows);
}

void recordAIStrategyRetrialPreview(Room &room, ServerPlayer *recipient,
    const QList<CardsMoveStruct> &moves, bool isLostPhase)
{
    recordAIStrategyRetrialPreview(room, recipient, moves, isLostPhase, [](const Card *card) {
        // Invoked only AFTER a genuine recipient notification passed the narrow
        // PREVIEW gate. Intrinsic disclosed face, not actor/private card state.
        return QJsonObject{{"id", card->getEffectiveId()}, {"effective_id", card->getEffectiveId()},
            {"name", card->objectName()}, {"class_name", card->getClassName()},
            {"type_id", int(card->getTypeId())}, {"suit", int(card->getSuit())},
            {"suit_string", card->getSuitString()}, {"number", card->getNumber()},
            {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
            {"red", card->isRed()}, {"black", card->isBlack()}, {"name_length", card->nameLength()}};
    });
}

void invalidateAIStrategyRetrialPreview(Room &room, int cardId)
{
    if (cardId < 0) return;
    if (auto *ledger = previewLedger(room, false)) invalidatePreviewCard(ledger, cardId);
}

QJsonObject readAIStrategyRetrialPreview(Room &room, ServerPlayer *viewer, const QString &skill)
{
    const QJsonObject unknown{{"known", false}};
    if (!viewer || viewer->getRoom() != &room || !previewSkill(skill)) return unknown;
    auto *ledger = previewLedger(room, false);if (!ledger) return unknown;
    const auto row = ledger->property("rows").toJsonObject().value(previewKey(viewer, skill)).toObject();
    if (!row.value("ids").isArray() || row.contains("pending")) return unknown;
    if (skill == "caoduo" && (!viewer->hasFlag("LuaTaoxiUsed")
        || row.value("source_scope").toString().isEmpty()
        || caoduoSourceScope(room, viewer) != row.value("source_scope").toString())) return unknown;
    const QVariant tag = skill == "caoduo" ? viewer->getTag("LuaZongxuan") : room.getTag(skill);
    if (tag.metaType().id() != QMetaType::QString || tag.toString().isEmpty()) return unknown;
    QJsonArray ids, cards;QSet<int> seen;
    const auto available = row.value("ids").toArray();const auto faces=row.value("faces").toObject();
    int cursor=0;
    for (const auto &part : tag.toString().split('+')) {
        bool ok=false;const int id=part.toInt(&ok);if (!ok || id<0 || seen.contains(id)) return unknown;seen.insert(id);
        while (cursor < available.size() && available.at(cursor).toInt()!=id) ++cursor;
        if (cursor==available.size()) return unknown;++cursor;
        const QString key=QString::number(id);const auto mapping=row.value("mappings").toObject().value(key).toObject();
        const auto *owner=room.getCardOwner(id);
        if (mapping.value("place").toInt()!=int(room.getCardPlace(id))
            || mapping.value("owner").toString()!=(owner?owner->objectName():QString())) return unknown;
        // Revalidate only a previously witnessed face; no changed/new face is
        // returned. setSuit/setNumber do not emit WrappedCard::identityChanged.
        if (!faces.value(key).isObject()) return unknown;
        const auto face = faces.value(key).toObject();
        const Card *current = room.getCard(id);
        if (!current || current->isVirtualCard() || current->getEffectiveId() != id
            || current->objectName() != face.value("name").toString()
            || current->getClassName() != face.value("class_name").toString()
            || int(current->getSuit()) != face.value("suit").toInt(-1)
            || current->getNumber() != face.value("number").toInt(-1)) {
            invalidatePreviewCard(ledger, id); return unknown;
        }
        if (skill == "caoduo" && (room.getCardPlace(id) != Player::PlaceHand
            || !owner || owner == viewer
            || face.value("known_flags").toObject().value("caoduo").toBool() != current->hasFlag("caoduo"))) {
            invalidatePreviewCard(ledger, id); return unknown;
        }
        ids.append(id);cards.append(face);
    }
    return {{"known", true}, {"ids", ids}, {"cards", cards}, {"skill", skill},
        {"serial", row.value("serial")}};
}

void projectAIStrategyRetrialContext(AIRequest &request, Room &room, ServerPlayer *viewer,
    const QVariant &data, QJsonObject &context, const CardProjector &publicCard)
{
    Q_UNUSED(data);
    if (!viewer || viewer->getRoom() != &room || !publicCard
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()) return;
    const QString &reason = request.choiceOptions.reason;
    if (request.kind == AIRequest::UseCard && (request.pattern == "@PlusYiji" || request.pattern == "@fatewangzhe"
        || (request.pattern == "@@caoduo" && request.prompt == "@xinji-use"))) {
        const QString skill = request.pattern == "@@caoduo" ? "caoduo" : request.pattern.mid(1);
        const auto preview = readAIStrategyRetrialPreview(room, viewer, skill);
        if (preview.value("known").toBool()) {
            context.insert("retrial_preview", preview);
            if (skill == "PlusYiji") context.insert("biaofeng_yiji", room.getTag(skill).toString());
            else if (skill == "fatewangzhe") { QJsonArray ids;for (const auto &id : preview.value("ids").toArray()) ids.append(QString::number(id.toInt()));context.insert("fate_wangzhe_ids", ids); }
        }
    }

    // Exact public resolution windows, never general Room-tag availability.
    if (request.kind == AIRequest::UseCard && (request.pattern == "@PlusGuicai2"
        || request.pattern == "@PlusGuidao2" || request.pattern == "@@SixGuicai2")) {
        const auto value = pindianValue(room.getTag("CurrentPindianStruct"), room, publicCard);
        if (!value.isUndefined()) {
            context.insert("biaofeng_pindian", value);
            const QString stem = request.pattern == "@PlusGuicai2" ? "PlusGuicai_"
                : request.pattern == "@PlusGuidao2" ? "PlusGuidao_" : "SixGuicai";
            scopedFlags(context, room, {stem + "Source", stem + "Target"});
        }
    } else if ((request.kind == AIRequest::UseCard
        && (request.pattern == "@PlusYizhong" || request.pattern == "@@SixLongdan1"))
        || (request.kind == AIRequest::SkillInvoke && reason == "SixZhongyong")) {
        const auto value = useValue(room.getTag("CurrentUseStruct"), room, publicCard);
        if (!value.isUndefined()) {
            context.insert("biaofeng_use", value);
            if (request.pattern == "@@SixLongdan1") scopedFlags(context, room, {"SixLongdan_Target"});
            if (request.pattern == "@PlusYizhong") {
                QJsonObject marks;
                // addPlayerMark is broadcast before this public TargetConfirmed
                // response window (biaofeng.lua3623); no arbitrary mark oracle.
                for (const auto *p : room.getAllPlayers(true)) marks.insert(p->objectName(), p->getMark("PlusYizhong"));
                context.insert("biaofeng_yizhong_marks", marks);
            }
        }
    } else if (request.kind == AIRequest::SkillInvoke && reason == "SevenYueDan") {
        const QVariant value = room.getTag("SevenFengPing");
        if (value.metaType().id() == QMetaType::QString)
            context.insert("biaofeng_fengping", value.toString());
    } else if (request.kind == AIRequest::Discard && reason == "dev_caiyi") {
        // filterData[CardUsed] is updated only at the END of that event. During
        // nested TargetSpecified this is the previous completed use, not data.
        const auto *ledger = previewLedger(room, false);
        if (ledger) {
            const auto use = ledger->property("completed_card_uses").toJsonObject().value(viewer->objectName());
            if (use.isObject()) context.insert("challengedeveloper_caiyi_use", use);
        }
    } else if (request.kind == AIRequest::Discard && reason == "dev_saodong") {
        insertKnown(context, "challengedeveloper_saodong_damage",
            damageValue(viewer->getTag("dev_saodong_damage"), room, publicCard));
    } else if (request.kind == AIRequest::SkillInvoke && reason == "dev_jiaodao") {
        insertKnown(context, "challengedeveloper_jiaodao_use",
            useValue(viewer->getTag("dev_jiaodao_data"), room, publicCard));
    } else if (request.kind == AIRequest::Choice && reason == "luadushi") {
        // The only fixed writer stores an integer card ID (leo.lua2859), not
        // ServerPlayer*. QVariant::toPlayer is null in the original callback.
        context.insert("leo_luadushi", false);
    } else if (request.kind == AIRequest::AmazingGrace && reason == "newluaweiwo") {
        const QVariant value = room.getTag("newluaweiwo");
        const auto *judge = value.canConvert<JudgeStruct *>() ? value.value<JudgeStruct *>() : nullptr;
        if (judge) context.insert("leo_newluaweiwo_judge",
            projectAIStrategyJudgeContext(request, room, viewer, judge, publicCard));
        else context.insert("leo_newluaweiwo_judge", false);
    }

    // Fixed extensions/dongmanbao.lua 4995/4996, 6464/6465, 8482/8483.
    // These are the exact recipients of the current damage decision.
    if (request.kind == AIRequest::Discard && reason == "se_Fanshe")
        insertKnown(context, "dongmanbao_fanshe_damage",
            damageValue(room.getTag("FansheDamage"), room, publicCard, viewer));
    else if (request.kind == AIRequest::PlayerChosen && reason == "SE_Zhihui")
        insertKnown(context, "dongmanbao_zhihui_damage",
            damageValue(room.getTag("SE_Zhihui"), room, publicCard, viewer));
    else if (request.kind == AIRequest::SkillInvoke && reason == "SE_Zishang")
        insertKnown(context, "dongmanbao_zishang_damage",
            damageValue(room.getTag("SE_Zishang"), room, publicCard));
    // 8912/8913: the public dying target, not any hidden card/role.
    else if (request.kind == AIRequest::Discard && reason == "SE_Shouzang")
        insertKnown(context, "dongmanbao_shouzang_target",
            playerValue(room.getTag("SE_Shouzang_target"), room));
    // 13914/13915: exact type already included in this owner's prompt/pattern.
    else if (request.kind == AIRequest::Discard && reason == "SE_Lingshang") {
        const QVariant value = room.getTag("SE_Lingshang_type");
        if (value.metaType().id() == QMetaType::QString) {
            const QString type = value.toString();
            if (type == "BasicCard" || type == "TrickCard" || type == "EquipCard")
                context.insert("dongmanbao_lingshang_type", type);
        }
    // 14633/14634: previously declared public SkillCard target, same source asker.
    } else if (request.kind == AIRequest::PlayerChosen && reason == "se_biling")
        insertKnown(context, "dongmanbao_biling_target",
            playerValue(room.getTag("se_bilingvs_target"), room));
    // Fixed biaofeng.lua3983: just-inflicted public damage card.
    else if (request.kind == AIRequest::UseCard && request.pattern == "@PlusYuanmou")
        insertKnown(context, "biaofeng_yuanmou_card",
            cardValue(room.getTag("PlusYuanmou_Damage"), publicCard));
    // 6602-6610: recipient obtains and publicly shows this equipment first.
    else if (request.kind == AIRequest::SkillInvoke && reason == "PlusChenggui")
        insertKnown(context, "biaofeng_chenggui_card",
            cardValue(room.getTag("PlusChenggui_card"), publicCard));
    // 7360-7365: target is also named explicitly in the current prompt.
    else if (request.kind == AIRequest::UseCard && request.pattern == "@PlusCuorui")
        insertKnown(context, "biaofeng_cuorui_target",
            playerValue(room.getTag("PlusCuorui"), room));
    // 7929-7933: public Slash/Duel being redirected, no private hand access.
    else if (request.kind == AIRequest::UseCard && request.pattern == "@PlusLiuli")
        insertKnown(context, "biaofeng_liuli_card",
            cardValue(room.getTag("liuli-card"), publicCard));
    // 11987 broadcasts the selected public card's wire to this exact owner;
    // 11867 consumes it in the immediate attached-skill response.
    else if (request.kind == AIRequest::UseCard && request.pattern == "@@SixWulveOthers!") {
        const QVariant value = viewer->property("SixWulveCard");
        if (value.metaType().id() == QMetaType::QString)
            context.insert("biaofeng_six_wulve_card", value.toString());
    }
    else if (request.kind == AIRequest::Choice && reason == "SevenFengPing")
        insertKnown(context, "biaofeng_fengping",
            playerValue(room.getTag("SevenFengPing"), room));
}

void projectAIStrategyRetrialEventContext(AIWorldView &world, Room &room, ServerPlayer *viewer,
    const AIEventView &event, const CardProjector &publicCard)
{
    if (!viewer || viewer->getRoom() != &room || !publicCard
        || world.self.objectName != viewer->objectName()) return;
    if (event.triggerEvent == CardUsed && event.kind == "card_use" && !event.privateEvent) {
        // Called at the actual RoomThread end-of-event boundary, after the
        // per-observer projection. Preserve this immutable observed value;
        // never query a later live card/tag or another viewer's history.
        auto *ledger = previewLedger(room, true);
        auto histories = ledger->property("completed_card_uses").toJsonObject();
        const auto card = event.details.value("card");
        histories.insert(viewer->objectName(), QJsonObject{
            {"from", event.from.isEmpty() ? QJsonValue(false) : QJsonValue(event.from)},
            {"to", QJsonArray::fromStringList(event.targets)},
            {"card", card.isObject() && !card.toObject().isEmpty() ? card : QJsonValue(false)}});
        ledger->setProperty("completed_card_uses", histories);
    }
    if (event.triggerEvent != ChoiceMade || event.kind != "choice"
        || event.from != viewer->objectName()
        || !event.privateEvent || event.privateViewer != viewer->objectName()) return;
    // These tags exist during the synchronous private ChoiceMade callback,
    // before native skill execution resumes. Never export into later events.
    if (event.reason == "SE_Zishang"
        && event.details.value("choice_kind") == QJsonValue("skillInvoke"))
        insertKnown(world.decisionContext, "dongmanbao_zishang_damage",
            damageValue(room.getTag("SE_Zishang"), room, publicCard));
    else if (event.reason == "SevenFengPing"
        && event.details.value("choice_kind") == QJsonValue("skillChoice"))
        insertKnown(world.decisionContext, "biaofeng_fengping",
            playerValue(room.getTag("SevenFengPing"), room));
}
