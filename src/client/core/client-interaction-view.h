#ifndef CLIENT_INTERACTION_VIEW_H
#define CLIENT_INTERACTION_VIEW_H

// The single contract between ClientCore and any UI.
//
// Implementors:
//   DesktopInteractionView  the existing RoomScene/Dashboard (this PR)
//   future TextClient / Android / WASM Lite
//
// A view does only two things: present requests and collect answers. It must not send
// replies itself, nor judge whether an answer is legal — everything goes back through
// ClientCore::submitResponse().
//
// This header has no Q_OBJECT: implementors are usually QObject already, and multiple
// inheritance from a pure interface does not trip up moc.

#include "interaction-model.h"

class IClientInteractionView
{
public:
    virtual ~IClientInteractionView() = default;

    // A new request needs an answer. The view builds its UI here.
    virtual void presentRequest(const InteractionRequest &request) = 0;

    // The request has been completed by an accepted answer. The view tears its UI down here.
    virtual void finishRequest(const InteractionRequest &request,
        const InteractionResponse &response) = 0;

    // The request ended without an answer (next request arrived, expired, abandoned
    // locally, disconnected).
    virtual void cancelRequest(const InteractionRequest &request,
        InteractionCancelReason reason) = 0;

    // The answer was rejected. The request is still waiting; the view may hint the user but
    // must not bypass the core to send a reply.
    virtual void rejectResponse(const InteractionRequest &request,
        const InteractionResponse &response, const InteractionValidation &validation) = 0;
};

#endif
