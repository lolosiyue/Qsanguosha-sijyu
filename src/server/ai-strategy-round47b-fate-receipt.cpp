#include "ai-strategy-round47b-fate-receipt.h"
#include "ai-strategy-preview-authority.h"
#include "ai.h"
#include "ai-runtime.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "room-state.h"
#include "serverplayer.h"
#include "skill.h"
#include <QPointer>

class AIRound47FateInquiryState final {
public:
    QPointer<Room> room;
    QPointer<ServerPlayer> viewer;
    QString pattern;
    bool active=true;
    quint64 entryEpoch=0;
};
thread_local std::shared_ptr<AIRound47FateInquiryState> AIRound47FateInquiry::s_current;
AIRound47FateInquiry::AIRound47FateInquiry(Room &room,ServerPlayer *viewer,const QString &pattern)
    :m_state(std::make_shared<AIRound47FateInquiryState>()),m_previous(s_current)
{
    // Entering any nested inquiry permanently retires earlier selections from
    // the suspended frame; restoring the frame cannot revive an old receipt.
    if(m_previous) ++m_previous->entryEpoch;
    m_state->room=&room;m_state->viewer=viewer;m_state->pattern=pattern;s_current=m_state;
}
AIRound47FateInquiry::~AIRound47FateInquiry(){m_state->active=false;s_current=m_previous;}
class AIRound47FateUseProof final {
public:
    std::shared_ptr<AIRound47FateInquiryState> inquiry;
    AIRequest request;
    AICardConversionView ticket;
    QPointer<const Card> submitted;
    QString submittedWire,submittedRawSkill;
    quint64 gameGeneration=0,aiGeneration=0,inquiryEpoch=0;
    mutable bool consumed=false;
};
namespace {
bool shape(const Card *card,const AICardConversionView &ticket)
{
    const auto *skillCard=card?dynamic_cast<const SkillCard *>(card):nullptr;
    return skillCard && card->isVirtualCard() && card->getTypeId()==Card::TypeSkill
        && card->objectName()==ticket.name && card->getClassName()==ticket.className
        && int(card->getSuit())==ticket.suit && card->getNumber()==ticket.number
        && card->getSubcards()==ticket.subcardIds && skillCard->getUserString()==ticket.declaredName;
}
bool requestShape(const ActiveSkillRequest &r,const AIRound47FateUseProof &p)
{
    return r.initiator==p.inquiry->viewer && r.reason==p.request.reason && r.pattern==p.request.pattern
        && r.activationRef==p.ticket.activationRef && r.sourceRef==p.ticket.sourceRef
        && r.selectedCardIds==p.ticket.subcardIds && r.selectedTargetNames.isEmpty()
        && r.userString==p.ticket.declaredName;
}
}
bool AIRound47FateUseAccess::current(const Room &room,const std::shared_ptr<const AIRound47FateUseProof> &p)
{
    Room &mutableRoom=const_cast<Room &>(room);
    const auto stable=[&]() {
        return p && !p->consumed && p->inquiry && p->inquiry->active
            && p->inquiry==AIRound47FateInquiry::s_current
            && p->inquiry->entryEpoch==p->inquiryEpoch
            && p->inquiry->room==&room && p->inquiry->viewer && p->inquiry->pattern=="@fatewangzhe"
            && mutableRoom.roomRuntime() && !mutableRoom.roomRuntime()->isClosing()
            && mutableRoom.roomRuntime()->stateRevision()==p->request.stateRevision
            && mutableRoom.roomRuntime()->lua().generation()==p->gameGeneration
            && mutableRoom.roomRuntime()->ai().lua().generation()==p->aiGeneration
            && !p->submitted.isNull() && shape(p->submitted,p->ticket)
            && p->submitted->toString()==p->submittedWire
            && p->submitted->getSkillName(false)==p->submittedRawSkill
            && p->submitted->getActivationSkillName()==p->ticket.activationRef.key.skillName
            && p->submitted->getActivationSkillInstanceId()==p->ticket.activationRef.key.instanceID
            && p->submitted->getSourceSkillName()==p->ticket.sourceRef.key.skillName
            && p->submitted->getSourceSkillInstanceId()==p->ticket.sourceRef.key.instanceID
            && mutableRoom.roomState()->getCurrentCardUseReason()==p->request.reason
            && mutableRoom.roomState()->getCurrentCardUsePattern()==p->request.pattern;
    };
    if(!stable())return false;
    const bool authorized=authorizeAIStrategyPreviewPayment(p->request,mutableRoom,p->inquiry->viewer,
        p->ticket.activationRef,p->ticket.sourceRef,p->ticket.subcardIds);
    // The authority's native invalidity/root queries can enter Lua. Never rely
    // on the pre-call stamp, weak object pointer or enclosing inquiry afterward.
    return authorized && stable();
}
bool AIRound47FateUseAccess::attach(const AIRequest &r,Room &room,ServerPlayer *viewer,
    const AICardConversionView &ticket,CardUseStruct &use)
{
    if(r.kind!=AIRequest::UseCard || r.pattern!="@fatewangzhe"
        || r.reason!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE || r.handlingMethod!=Card::MethodUse
        || ticket.activationRef.key.skillName!="fatewangzhe" || ticket.producerVersion!=2
        || ticket.producerReason!=r.reason || ticket.producerPattern!=r.pattern
        || ticket.subcardIds.size()!=1 || !ticket.declaredName.isEmpty()
        || use.from!=viewer || !use.to.isEmpty() || !use.hasSkillActivationRequest
        || use.activationRef!=ticket.activationRef || use.sourceRef!=ticket.sourceRef
        || use.bypass_cost || use.skipSkillEffect || use.skillExecutionID!=0 || !shape(use.card,ticket)
        || !AIRound47FateInquiry::s_current || AIRound47FateInquiry::s_current->viewer!=viewer)
        return false;
    auto p=std::make_shared<AIRound47FateUseProof>();
    p->inquiry=AIRound47FateInquiry::s_current;p->inquiryEpoch=p->inquiry->entryEpoch;p->request=r;p->ticket=ticket;p->submitted=use.card;
    p->submittedWire=use.card->toString();p->submittedRawSkill=use.card->getSkillName(false);
    p->gameGeneration=room.roomRuntime()->lua().generation();p->aiGeneration=room.roomRuntime()->ai().lua().generation();
    if(!current(room,p))return false;
    use.m_aiRound47FateUse=p;return true;
}
bool AIRound47FateUseAccess::bind(Room &room,const CardUseStruct &use,ActiveSkillRequest &r)
{
    const auto p=use.m_aiRound47FateUse;if(!p)return true;
    if(!current(room,p) || use.card!=p->submitted || use.from!=p->inquiry->viewer
        || !use.to.isEmpty() || !use.hasSkillActivationRequest || use.bypass_cost || use.skipSkillEffect
        || use.skillExecutionID!=0 || use.activationRef!=p->ticket.activationRef || use.sourceRef!=p->ticket.sourceRef
        || !requestShape(r,*p))return false;
    r.m_aiRound47FateUse=p;return true;
}
bool AIRound47FateUseAccess::extend(const Room &room,const ServerPlayer *viewer,const ViewAsSkillV2 *skill,
    const ActiveSkillRequest &r,QSet<int> &selectable)
{
    const auto p=r.m_aiRound47FateUse;if(!p)return true;
    if(!current(room,p) || viewer!=p->inquiry->viewer || !skill
        || skill!=Sanguosha->getViewAsSkill("fatewangzhe") || !requestShape(r,*p))return false;
    selectable.insert(p->ticket.subcardIds.first());return true;
}
bool AIRound47FateUseAccess::finish(const Room &room,const ActiveSkillRequest &r,const Card *card)
{
    const auto p=r.m_aiRound47FateUse;if(!p)return true;
    if(!current(room,p) || !requestShape(r,*p) || !shape(card,p->ticket)
        || card->getActivationSkillName()!=p->ticket.activationRef.key.skillName
        || card->getActivationSkillInstanceId()!=p->ticket.activationRef.key.instanceID
        || card->getSourceSkillName()!=p->ticket.sourceRef.key.skillName
        || card->getSourceSkillInstanceId()!=p->ticket.sourceRef.key.instanceID)return false;
    p->consumed=true;return true;
}
