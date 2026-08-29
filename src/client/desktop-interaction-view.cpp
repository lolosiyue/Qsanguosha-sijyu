#include "desktop-interaction-view.h"

#include "client.h"
#include "client-core.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(qsanDesktopInteraction, "qsan.client.desktop-view")

DesktopInteractionView::DesktopInteractionView(Client *client)
    : m_client(client)
{
}

DesktopInteractionView::~DesktopInteractionView()
{
    // View 死之前一定要同 core 解綁,否則 core 會 present 落一嚿死物度。
    if (m_client != nullptr && m_client->interactionCore() != nullptr
        && m_client->interactionCore()->view() == this) {
        m_client->interactionCore()->detachView();
    }
}

void DesktopInteractionView::presentRequest(const InteractionRequest &request)
{
    if (m_client == nullptr)
        return;

    switch (request.type) {
    case InteractionType::ChooseGeneral:
        m_client->presentGeneralChoice(request);
        return;
    case InteractionType::Choice:
        m_client->presentOptionChoice(request);
        return;
    case InteractionType::ChoosePlayer:
        m_client->presentPlayerChoice(request);
        return;
    case InteractionType::SkillInvoke:
        m_client->presentSkillInvoke(request);
        return;
    case InteractionType::ResponseCard:
        m_client->presentCardResponse(request);
        return;
    case InteractionType::None:
        break;
    default:
        m_client->presentStructuredInteraction(request);
        return;
    }
    qCWarning(qsanDesktopInteraction) << "cannot present interaction type"
        << interactionTypeName(request.type);
}

void DesktopInteractionView::finishRequest(const InteractionRequest &, const InteractionResponse &)
{
    // Desktop 嘅收拾（prompt box 消失、stopPending、unselectAll、setStatus）一直
    // 都喺 RoomScene 送答案之前就做咗,所以呢度冇嘢要補做。保留呢個 override
    // 係因為佢係契約嘅一部分:第二個 view（text／Android）就要喺呢度收工。
}

void DesktopInteractionView::cancelRequest(const InteractionRequest &request,
    InteractionCancelReason reason)
{
    // 同上:desktop 嘅 request 取消一定係「下一個 server request 到咗」或者
    // 「本機主動放棄」,兩者都已經 setStatus() 過。淨低只需要一行 log。
    qCDebug(qsanDesktopInteraction) << "request" << request.requestId
        << interactionTypeName(request.type) << "cancelled:"
        << interactionCancelReasonName(reason);
}

void DesktopInteractionView::rejectResponse(const InteractionRequest &request,
    const InteractionResponse &response, const InteractionValidation &validation)
{
    // 被拒嘅答案唔會上線,request 亦仲喺度等一個好答案。Desktop 唔會彈窗:
    // 玩家撳到一個唔合法嘅選擇,本身就係 UI enable 邏輯有 bug,應該喺 log
    // 度捉,而唔係喺對局中間嚇親玩家。
    qCWarning(qsanDesktopInteraction).noquote()
        << "rejected desktop reply to request" << request.requestId
        << interactionTypeName(request.type) << ":" << validation.reasonName()
        << validation.detail << QString::fromUtf8(response.toSnapshot());
}
