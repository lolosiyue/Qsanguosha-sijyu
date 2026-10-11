#include "ai-strategy-round27-jieyingh.h"
#include "ai-strategy-readonly-card-store.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "room-state.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "skill.h"
#include <QHash>
#include <QPointer>
#include <atomic>

namespace {
thread_local QHash<Room *, QList<const AIRound27JieyinghQuestion *>> questions;
std::atomic<quint64> nextQuestion{0};
bool sameUse(const CardUseStruct &a, const CardUseStruct &b) {
    return a.card == b.card && a.from == b.from && a.to == b.to;
}
}
struct AIRound27JieyinghQuestion::Data {
    Room *room = nullptr;
    QPointer<ServerPlayer> viewer;
    quint64 serial = 0, frame = 0;
    QString prompt;
    int mark = 0;
    AIReadonlyCardLease observed;
    CardUseStruct use;
    bool valid = false;
};
struct AIRound27JieyinghAccess {
    static const AIRound27JieyinghQuestion::Data *current(Room &room) {
        const auto it = questions.constFind(&room);
        return it == questions.cend() || it->isEmpty() ? nullptr : it->last()->d.get();
    }
    static bool valid(const AIRequest &r, Room &room, ServerPlayer *p, quint64 serial) {
        const auto *d = current(room);
        if (!d || !d->valid || d->viewer != p || !p || d->serial != serial
            || r.jieyinghQuestionSerial != serial || !serial
            || r.viewerObjectName != p->objectName() || r.stateRevision != room.roomRuntime()->stateRevision()
            || r.kind != AIRequest::UseCard || r.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            || r.pattern != "@@jieyingh" || r.prompt != d->prompt || r.handlingMethod != Card::MethodUse
            || !p->isAlive() || !p->hasFlag("CurrentPlayer") || room.getCurrent() != p
            || p->getMark("&jieyingh-SelfClear") != d->mark
            || !p->getSkillInstanceIds("jieyingh").isEmpty()
            || room.roomState()->getCurrentCardUseReason() != r.reason
            || room.roomState()->getCurrentCardUsePattern() != r.pattern) return false;
        const Card *live = d->observed ? d->observed() : nullptr;
        if (!live || live != d->use.card) return false;
        CardUseStruct active; quint64 frame = 0;
        if (!room.getThread() || !room.getThread()->currentJieyinghQuestionUse(p, active, frame)
            || frame != d->frame || !sameUse(active, d->use)) return false;
        const auto tag = p->getTag("jieyinghData");
        if (!tag.canConvert<CardUseStruct>() || !sameUse(tag.value<CardUseStruct>(), d->use)) return false;
        const auto disclosed = r.choiceOptions.context.value("nested_delegate").toObject();
        return disclosed.value("source_key") == "@@jieyingh" && disclosed.value("known") == true
            && disclosed.value("use").isObject();
    }
};
AIRound27JieyinghQuestion::AIRound27JieyinghQuestion(Room &room, ServerPlayer *p,
    const QString &pattern, const QString &prompt, int method) : d(new Data) {
    d->room = &room; d->viewer = p; d->serial = ++nextQuestion; d->prompt = prompt;
    questions[&room].append(this); // A nested unrelated question masks the outer capability.
    if (!p || pattern != "@@jieyingh" || method != int(Card::MethodUse)
        || !p->isAlive() || room.getCurrent() != p || !p->hasFlag("CurrentPlayer")
        || !p->getSkillInstanceIds("jieyingh").isEmpty()
        || (d->mark = p->getMark("&jieyingh-SelfClear")) <= 0 || !room.getThread()) return;
    if (!room.getThread()->currentJieyinghQuestionUse(p, d->use, d->frame)
        || d->use.from != p || !d->use.card || d->use.to.isEmpty()) return;
    d->observed = AIOwnerReadonlyCardStore::observe(d->use.card);
    const Card *live = d->observed ? d->observed() : nullptr;
    if (!live || (!live->isKindOf("Slash") && !live->isNDTrick()) || live->isKindOf("Collateral")
        || prompt != "@jieyingh:" + live->objectName()) return;
    const auto tag = p->getTag("jieyinghData");
    d->valid = tag.canConvert<CardUseStruct>() && sameUse(tag.value<CardUseStruct>(), d->use);
}
AIRound27JieyinghQuestion::~AIRound27JieyinghQuestion() {
    auto it = questions.find(d->room);
    if (it == questions.end()) return;
    it->removeAll(this);
    if (it->isEmpty()) questions.erase(it);
}
void projectAIRound27JieyinghQuestion(AIRequest &r, Room &room, ServerPlayer *p) {
    const auto *d = AIRound27JieyinghAccess::current(room);
    if (!d || !d->valid) return;
    r.jieyinghQuestionSerial = d->serial;
    if (!AIRound27JieyinghAccess::valid(r, room, p, d->serial)) { r.jieyinghQuestionSerial = 0; return; }
    r.choiceOptions.context.insert("jieyingh_question", QJsonObject{{"known", true},
        {"kind", "jieyingh_native_question"}, {"prototype", "JieyinghCard"}});
    r.worldView.decisionContext.insert("jieyingh_question", r.choiceOptions.context.value("jieyingh_question"));
}
Card *createAIRound27JieyinghProduct(const AIRequest &r, Room &room, ServerPlayer *p,
                                    quint64 serial, QString *error) {
    const auto fail = [&](const char *reason)->Card* { if (error) *error=QString::fromLatin1(reason); return nullptr; };
    if (!AIRound27JieyinghAccess::valid(r, room, p, serial)) return fail("Jieyingh native recipient question is not active");
    const ViewAsSkill *vs = Sanguosha->getViewAsSkill("jieyingh");
    if (!vs || dynamic_cast<const ViewAsSkillV2 *>(vs) || !vs->isEnabledAtResponse(p, "@@jieyingh"))
        return fail("Jieyingh native V1 response producer changed");
    Card *card = const_cast<Card *>(vs->viewAs(QList<const Card *>()));
    if (!card) return fail("Jieyingh native product unavailable");
    const auto *skillCard = dynamic_cast<const SkillCard *>(card);
    if (!card->isVirtualCard() || card->getClassName() != "JieyinghCard" || !card->getSubcards().isEmpty()
        || !skillCard || !skillCard->getUserString().isEmpty()
        || !AIRound27JieyinghAccess::valid(r, room, p, serial)) {
        card->deleteLater(); return fail("Jieyingh native product or live question changed");
    }
    // Exact donor response literal. Native Parse attributes an otherwise
    // unnamed C++ SkillCard; copy only that verified finite skill name, never
    // manufacture an activation/source instance or reinterpret caller input.
    std::unique_ptr<const Card> parsed(Card::Parse(QStringLiteral("@JieyinghCard=.")));
    if (!parsed || parsed->getClassName() != card->getClassName()
        || parsed->objectName() != card->objectName() || parsed->getSuit() != card->getSuit()
        || parsed->getNumber() != card->getNumber() || parsed->getSubcards() != card->getSubcards()
        || parsed->getSkillName(false) != "jieyingh") {
        card->deleteLater(); return fail("fixed Jieyingh source Parse attribution changed");
    }
    card->setSkillName(parsed->getSkillName(false));
    if (p->isLocked(card) || p->isCardLimited(card, Card::MethodUse)
        || !AIRound27JieyinghAccess::valid(r, room, p, serial)) {
        card->deleteLater(); return fail("source-attributed Jieyingh question became invalid");
    }
    if (error) error->clear();
    return card;
}
Card *rebuildAIRound27JieyinghProduct(const AIRequest &r, Room &room, ServerPlayer *p,
                                     const AICardConversionView &ticket) {
    if (!ticket.jieyinghQuestion || ticket.producerVersion != 1 || ticket.policyClass != "@jieyingh_question"
        || ticket.producerReason != r.reason || ticket.producerPattern != r.pattern
        || ticket.costCount || !ticket.subcardIds.isEmpty() || !ticket.declaredName.isEmpty()) return nullptr;
    Card *card = createAIRound27JieyinghProduct(r, room, p, ticket.jieyinghQuestionSerial);
    if (!card) return nullptr;
    if (card->objectName() != ticket.name || card->getClassName() != ticket.className
        || card->getSuit() != ticket.suit || card->getNumber() != ticket.number
        || card->getSkillName(false) != ticket.skillName || card->toString() != ticket.legacyStringKey) {
        card->deleteLater(); return nullptr;
    }
    return card; // Exact fixed Parse attribution, no fabricated activation/source IDs.
}
