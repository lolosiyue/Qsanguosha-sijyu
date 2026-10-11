#include "ai-strategy-round43-choice.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
namespace {
bool scope(const AIRequest &r,const ServerPlayer *p) {
    if (!p || !p->getRoom() || !p->isAlive() || r.decisionId==0
        || r.viewerObjectName!=p->objectName() || r.worldView.self.objectName!=p->objectName()
        || r.stateRevision!=p->getRoom()->roomRuntime()->stateRevision()
        || r.kind!=AIRequest::Activate || r.reason!=CardUseStruct::CARD_USE_REASON_PLAY
        || p->getPhase()!=Player::Play || !p->hasSkill("mtjieli")) return false;
    // The shared seed/issuer independently validates the chosen exact scope.
    // This finite catalogue never creates an activation or payment permission.
    QList<AiSkillActionContext> actions=r.skillActions;
    if(r.hasSkillActionContext)actions.prepend(r.skillActionContext);
    for(const auto &a:actions)
        if(a.isValid() && a.activationRef.ownerObjectName==p->objectName()
            && a.activationRef.key.skillName=="mtjieli"
            && p->hasSkillInstance("mtjieli",a.activationRef.key.instanceID)
            && !p->isSkillInvalid("mtjieli",a.activationRef.key.instanceID)) return true;
    return false;
}
}
// maotu506-519: zero selected costs, literal colour on an ActiveSkillCard.
// Bit 1 permits a declaration; bit 0 stays clear: no paid source material.
int aiRound43ChoicePrototype(const AIRequest &r,const ServerPlayer *p,
                            const QString &skill,const QString &prototype) {
    return skill=="mtjieli" && prototype=="ActiveSkillCard" && scope(r,p) ? 2 : -1;
}
bool aiRound43ChoiceDeclaration(const AIRequest &r,const ServerPlayer *p,const QJsonObject &spec) {
    return scope(r,p) && spec.value("subcards").isArray() && spec.value("subcards").toArray().isEmpty()
        && spec.value("user_string").isString()
        && (spec.value("user_string")==QJsonValue("red") || spec.value("user_string")==QJsonValue("black"));
}
// maotu521-537: dummyCard("duel") => NoSuit/0, setSkillName, then every
// hand card of the declared colour in native hand order. No availability,
// jilei, limit or payment filtering is inserted into this hypothesis.
bool aiRound43ChoiceConstruction(const AIRequest &r,const ServerPlayer *p,const QJsonObject &spec) {
    if (!scope(r,p) || spec.size()!=6 || spec.value("source_key")!=QJsonValue("mtjieli")
        || spec.value("skill")!=QJsonValue("mtjieli") || spec.value("name")!=QJsonValue("duel")
        || spec.value("suit")!=QJsonValue(int(Card::NoSuit)) || spec.value("number")!=QJsonValue(0)
        || !spec.value("subcards").isArray()) return false;
    const auto hand=p->getHandcards();
    if (hand.size()!=r.worldView.handCards.size()) return false;
    QJsonArray red,black;
    for(int i=0;i<hand.size();++i) {
        const Card *c=hand[i];const AICardView &face=r.worldView.handCards[i];
        if(!c || face.effectiveId!=c->getEffectiveId() || face.objectName!=c->objectName()
            || face.className!=c->getClassName() || face.suit!=int(c->getSuit()) || face.number!=c->getNumber()
            || p->getRoom()->getCardOwner(c->getEffectiveId())!=p
            || p->getRoom()->getCardPlace(c->getEffectiveId())!=Player::PlaceHand) return false;
        if(c->isRed())red.append(c->getEffectiveId());
        if(c->isBlack())black.append(c->getEffectiveId());
    }
    // Empty is a legitimate pure source construction for a colour with no
    // current material; native selected activation remains separately checked.
    const QJsonArray ids=spec.value("subcards").toArray();
    return ids==red || ids==black;
}
