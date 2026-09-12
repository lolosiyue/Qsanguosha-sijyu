#ifndef DESKTOP_INTERACTION_VIEW_H
#define DESKTOP_INTERACTION_VIEW_H

// IClientInteractionView implementation for the desktop (RoomScene/Dashboard).
//
// This adapter is F1's compatibility strategy: structured requests in, existing desktop
// presentation out. It deliberately does not include RoomScene, Dashboard or any QWidget
// — desktop presentation has always been driven by Client's signals and status, so the
// adapter only needs to call Client's presentXxx() ports and none of RoomScene/Dashboard's
// slots change a line, guaranteeing the look and behavior stay identical.
//
// A future TextClient/Android/WASM Lite only needs to write its own
// IClientInteractionView and call core->setView(); Client stays untouched.

#include "client-interaction-view.h"

class Client;

class DesktopInteractionView final : public IClientInteractionView
{
public:
    explicit DesktopInteractionView(Client *client);
    ~DesktopInteractionView() override;

    void presentRequest(const InteractionRequest &request) override;
    void finishRequest(const InteractionRequest &request, const InteractionResponse &response) override;
    void cancelRequest(const InteractionRequest &request, InteractionCancelReason reason) override;
    void rejectResponse(const InteractionRequest &request, const InteractionResponse &response,
        const InteractionValidation &validation) override;

private:
    Client *m_client;
};

#endif
