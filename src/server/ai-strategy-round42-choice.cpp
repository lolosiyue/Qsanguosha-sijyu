#include "ai-strategy-round42-choice.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"

int aiRound42ChoicePrototype(const AIRequest &r,const ServerPlayer *p,
                            const QString &skill,const QString &prototype)
{
    if (skill != "jiqiao" || prototype != "JiqiaoCard" || !p || !p->getRoom()
        || r.decisionId == 0 || r.viewerObjectName != p->objectName()
        || r.worldView.self.objectName != p->objectName()
        || r.stateRevision != p->getRoom()->roomRuntime()->stateRevision()
        || r.kind != AIRequest::UseCard || r.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        || r.pattern != "@@jiqiao" || r.handlingMethod != Card::MethodNone)
        return -1;
    QList<AiSkillActionContext> actions = r.skillActions;
    if (r.hasSkillActionContext) actions.prepend(r.skillActionContext);
    for (const auto &action : actions) {
        const auto &activation = action.activationRef;
        if (activation.ownerObjectName != p->objectName() || activation.key.skillName != skill
            || !action.isValid() || !action.activationQuotaAvailable || !action.sourceQuotaAvailable
            || !p->hasSkillInstance(skill, activation.key.instanceID)
            || p->isSkillInvalid(skill, activation.key.instanceID))
            continue;
        // Native Jiqiao::cost establishes this on its genuine BorrowedSkillScope.
        // Merely having the skill outside that selection window grants nothing.
        const QVariant state = p->getSkillInstanceStateValue(skill, activation.key.instanceID,
                                                            "select_equipment");
        if (state.type() == QVariant::Bool && state.toBool()) return 1;
    }
    return -1;
}
