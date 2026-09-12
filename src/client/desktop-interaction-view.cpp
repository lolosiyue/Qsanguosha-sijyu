#include "desktop-interaction-view.h"

#include "client.h"
#include "client-core.h"
#include "interaction-descriptor-registry.h"

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

    const ClientInteractionDescriptor *descriptor
        = InteractionDescriptorRegistry::find(request.type);
    if (descriptor != nullptr && descriptor->presenter != nullptr) {
        (m_client->*(descriptor->presenter))(request);
        return;
    }
    qCWarning(qsanDesktopInteraction) << "cannot present interaction type"
        << interactionTypeName(request.type);
}

void DesktopInteractionView::finishRequest(const InteractionRequest &, const InteractionResponse &)
{
    // The desktop's cleanup (prompt box disappears, stopPending, unselectAll, setStatus) has
    // always happened before RoomScene submits the answer, so there is nothing extra to do
    // here. This override is kept because it is part of the contract: a second view
    // (text/Android) would do its cleanup here.
}

void DesktopInteractionView::cancelRequest(const InteractionRequest &request,
    InteractionCancelReason reason)
{
    // Same as above: a desktop request cancellation is always either "the next server
    // request arrived" or "abandoned locally", and both already went through setStatus().
    // All that is left is one log line.
    qCDebug(qsanDesktopInteraction) << "request" << request.requestId
        << interactionTypeName(request.type) << "cancelled:"
        << interactionCancelReasonName(reason);
}

void DesktopInteractionView::rejectResponse(const InteractionRequest &request,
    const InteractionResponse &response, const InteractionValidation &validation)
{
    // A rejected answer never reaches the wire and the request keeps waiting for a good
    // one. The desktop does not pop up a dialog: the player clicking an illegal choice
    // means the UI enable logic has a bug — catch it in the log instead of startling the
    // player mid-game.
    qCWarning(qsanDesktopInteraction).noquote()
        << "rejected desktop reply to request" << request.requestId
        << interactionTypeName(request.type) << ":" << validation.reasonName()
        << validation.detail << QString::fromUtf8(response.toSnapshot());
}
