#include "ai-strategy-round29-response.h"
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
#include <QJsonArray>
#include <cmath>
#include <limits>
#include <QPointer>
#include <atomic>
namespace {
thread_local QHash<Room *, QList<const AIRound29ZhifengQuestion *>> questions;
std::atomic<quint64> nextQuestion{0};
}
struct AIRound29ZhifengQuestion::Data {
    Room *room = nullptr;
    QPointer<ServerPlayer> viewer;
    quint64 serial = 0, frame = 0;
    bool valid = false;
};
struct AIRound29ZhifengAccess {
    static const AIRound29ZhifengQuestion::Data *current(Room &room) {
        const auto it=questions.constFind(&room);
        return it==questions.cend() || it->isEmpty() ? nullptr : it->last()->d.get();
    }
    static bool valid(const AIRequest &r, Room &room, ServerPlayer *p, quint64 serial) {
        const auto *d=current(room); quint64 frame=0;
        return d && d->valid && p && d->viewer==p && serial && d->serial==serial
            && r.zhifengQuestionSerial==serial && r.viewerObjectName==p->objectName()
            && r.stateRevision==room.roomRuntime()->stateRevision()
            && r.kind==AIRequest::UseCard && r.reason==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && r.pattern=="@@zhifeng" && r.prompt=="zhifeng0" && r.handlingMethod==Card::MethodUse
            && p->isAlive() && room.getCurrent()==p && p->hasFlag("CurrentPlayer")
            && room.roomState()->getCurrentCardUseReason()==r.reason
            && room.roomState()->getCurrentCardUsePattern()==r.pattern
            && room.getThread() && room.getThread()->currentThWeijingQuestion(p,frame) && frame==d->frame;
    }
};
AIRound29ZhifengQuestion::AIRound29ZhifengQuestion(Room &room, ServerPlayer *p,
    const QString &pattern, const QString &prompt, int method) : d(new Data) {
    d->room=&room; d->viewer=p; d->serial=++nextQuestion;
    questions[&room].append(this);
    d->valid=p && p->isAlive() && pattern=="@@zhifeng" && prompt=="zhifeng0"
        && method==int(Card::MethodUse) && room.getCurrent()==p && p->hasFlag("CurrentPlayer")
        && room.getThread() && room.getThread()->currentThWeijingQuestion(p,d->frame);
}
AIRound29ZhifengQuestion::~AIRound29ZhifengQuestion() {
    auto it=questions.find(d->room);if(it==questions.end())return;
    it->removeAll(this);if(it->isEmpty())questions.erase(it);
}
void projectAIRound29ZhifengQuestion(AIRequest &r, Room &room, ServerPlayer *p) {
    const auto *d=AIRound29ZhifengAccess::current(room);if(!d||!d->valid)return;
    r.zhifengQuestionSerial=d->serial;
    if(!AIRound29ZhifengAccess::valid(r,room,p,d->serial)){r.zhifengQuestionSerial=0;return;}
    const QJsonObject row{{"known",true},{"kind","zhifeng_native_question"}};
    r.choiceOptions.context.insert("zhifeng_question",row);
    r.worldView.decisionContext.insert("zhifeng_question",row);
}
Card *createAIRound29ZhifengProduct(const AIRequest &r, Room &room, ServerPlayer *p,
    quint64 serial, const QList<int> &costs, QString *error) {
    const auto fail=[&](const char *message)->Card*{if(error)*error=QString::fromLatin1(message);return nullptr;};
    if(!AIRound29ZhifengAccess::valid(r,room,p,serial))return fail("ThWeijing recipient question is not active");
    // Fixed @@ source chooses exactly one he material, even where the V1 rule
    // could accept more. A question is not permission to submit another recipe.
    if(costs.size()!=1 || costs.first()<0)return fail("Zhifeng source requires one selected material");
    const int id=costs.first();
    if(room.getCardOwner(id)!=p || (room.getCardPlace(id)!=Player::PlaceHand && room.getCardPlace(id)!=Player::PlaceEquip))
        return fail("Zhifeng material is not the current recipient's hand or equipment");
    const Card *material=Sanguosha->getCard(id);
    const ViewAsSkill *vs=Sanguosha->getViewAsSkill("zhifeng");
    if(!material || material->hasFlag("using") || !vs || dynamic_cast<const ViewAsSkillV2*>(vs)
        || !vs->isEnabledAtResponse(p,"@@zhifeng"))return fail("Zhifeng native producer or material changed");
    // Caller supplies the normal native Self/room-state query scope; no fake
    // skill instance, activation ref, ownership, or pattern is installed here.
    if(!vs->viewFilter({},material))return fail("Zhifeng native material prefix rejected");
    Card *card=const_cast<Card*>(vs->viewAs({material}));
    if(!card)return fail("Zhifeng native producer declined");
    if(!card->isVirtualCard() || card->getSkillName(false)!="zhifeng" || card->getSubcards()!=costs
        || (card->objectName()!="slash" && card->objectName()!="analeptic" && card->objectName()!="duel")
        || p->isLocked(card) || p->isCardLimited(card,Card::MethodUse)
        || !AIRound29ZhifengAccess::valid(r,room,p,serial)) {
        card->deleteLater();return fail("Zhifeng native product or current question is invalid");
    }
    if(error)error->clear();return card;
}
Card *rebuildAIRound29ZhifengProduct(const AIRequest &r, Room &room, ServerPlayer *p,
    const AICardConversionView &t) {
    if(!t.zhifengQuestion || t.producerVersion!=1 || t.policyClass!="@zhifeng_question"
        || t.producerReason!=r.reason || t.producerPattern!=r.pattern || t.costCount!=1
        || !t.declaredName.isEmpty() || t.activationRef.isValid() || t.sourceRef.isValid())return nullptr;
    Card *card=createAIRound29ZhifengProduct(r,room,p,t.zhifengQuestionSerial,t.subcardIds);
    if(!card)return nullptr;
    if(card->objectName()!=t.name || card->getClassName()!=t.className || int(card->getSuit())!=t.suit
        || card->getNumber()!=t.number || card->getSkillName(false)!=t.skillName || card->toString()!=t.legacyStringKey){
        card->deleteLater();return nullptr;
    }
    return card;
}


bool aiRound29ResponseConstruction(const AIRequest &r, const ServerPlayer *p, const QJsonObject &spec)
{
    if (!p || !p->getRoom() || r.viewerObjectName != p->objectName()
        || r.worldView.self.objectName != p->objectName()
        || r.stateRevision != p->getRoom()->roomRuntime()->stateRevision()
        || r.kind != AIRequest::Activate || r.reason != CardUseStruct::CARD_USE_REASON_PLAY
        || !p->hasSkill("godlongnu") || spec.value("source_key") != QStringLiteral("godlongnu")
        || spec.value("skill") != QStringLiteral("godlongnu") || !spec.value("subcards").isArray()) return false;
    const auto exactInt=[](const QJsonValue &v,int &out) {
        const double n=v.toDouble();
        if(!v.isDouble() || !std::isfinite(n) || std::floor(n)!=n
            || n<std::numeric_limits<int>::min() || n>std::numeric_limits<int>::max()) return false;
        out=int(n);return true;
    };
    int id=-1,suit=-2,number=-2;const auto costs=spec.value("subcards").toArray();
    if(costs.size()!=1 || !exactInt(costs.first(),id) || id<0
        || !exactInt(spec.value("suit"),suit) || !exactInt(spec.value("number"),number)) return false;
    bool disclosed=false;
    for(const auto &c:r.worldView.handCards) if(c.effectiveId==id) disclosed=true;
    if(!disclosed || p->getRoom()->getCardOwner(id)!=p
        || p->getRoom()->getCardPlace(id)!=Player::PlaceHand) return false;
    const Card *material=Sanguosha->getCard(id);
    if(!material || material->isVirtualCard()) return false;
    const QString name=spec.value("name").toString();
    const bool all=p->getMark("&godlongnuall-PlayClear")>0;
    // Source tests these independent OR predicates. Native can_select_card has
    // mark precedence; selection (not enumeration) retains that distinction.
    const bool sourceBranch=(name=="fire_slash" && material->isRed()
        && (all || p->getMark("&godlongnu+:+fire_slash-PlayClear")>0))
        || (name=="thunder_slash" && material->isBlack()
        && (all || p->getMark("&godlongnu+:+thunder_slash-PlayClear")>0));
    if(!sourceBranch) return false;
    // Availability clone uses ToBeDecided/-1. Final CardParse carries the
    // single material's effective face (including JOKER inheritance).
    return (suit==int(Card::SuitToBeDecided) && number==-1)
        || (suit==int(material->getSuit()) && number==material->getNumber());
}
