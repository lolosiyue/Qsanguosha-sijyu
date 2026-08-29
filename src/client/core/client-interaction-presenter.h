#ifndef CLIENT_INTERACTION_PRESENTER_H
#define CLIENT_INTERACTION_PRESENTER_H

#include "interaction-model.h"

class IClientInteractionPresenter
{
public:
    virtual ~IClientInteractionPresenter() = default;

    virtual void presentGeneralChoice(const InteractionRequest &request) = 0;
    virtual void presentOptionChoice(const InteractionRequest &request) = 0;
    virtual void presentPlayerChoice(const InteractionRequest &request) = 0;
    virtual void presentSkillInvoke(const InteractionRequest &request) = 0;
    virtual void presentCardResponse(const InteractionRequest &request) = 0;
    virtual void presentRoleAssignment(const InteractionRequest &request) = 0;
    virtual void presentDirectionChoice(const InteractionRequest &request) = 0;
    virtual void presentCardExchange(const InteractionRequest &request) = 0;
    virtual void presentCardDiscard(const InteractionRequest &request) = 0;
    virtual void presentRespondingUse(const InteractionRequest &request) = 0;
    virtual void presentShowOrPindian(const InteractionRequest &request) = 0;
    virtual void presentPlayCard(const InteractionRequest &request) = 0;
    virtual void presentGuanxing(const InteractionRequest &request) = 0;
    virtual void presentGongxin(const InteractionRequest &request) = 0;
    virtual void presentYiji(const InteractionRequest &request) = 0;
    virtual void presentSuitChoice(const InteractionRequest &request) = 0;
    virtual void presentKingdomChoice(const InteractionRequest &request) = 0;
    virtual void presentTriggerOrder(const InteractionRequest &request) = 0;
    virtual void presentAmazingGrace(const InteractionRequest &request) = 0;
    virtual void presentChooseCard(const InteractionRequest &request) = 0;
    virtual void presentOrderChoice(const InteractionRequest &request) = 0;
    virtual void presentRole3v3(const InteractionRequest &request) = 0;
    virtual void presentBooleanPrompt(const InteractionRequest &request) = 0;
    virtual void presentDraftGeneral(const InteractionRequest &request) = 0;
    virtual void presentArrangeGeneral(const InteractionRequest &request) = 0;
    virtual void presentQmlInteraction(const InteractionRequest &request) = 0;
};

#endif
