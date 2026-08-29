#ifndef DESKTOP_INTERACTION_VIEW_H
#define DESKTOP_INTERACTION_VIEW_H

// Desktop（RoomScene／Dashboard）嘅 IClientInteractionView 實作。
//
// 呢個 adapter 係 F1 嘅相容策略:結構化 request 入,現有 desktop 呈現出。
// 佢刻意唔 include RoomScene、Dashboard 或者任何 QWidget —— desktop 嘅呈現
// 一直都係經 Client 嘅 signal 同 status 驅動,所以 adapter 只需要 call
// Client 上面幾個 presentXxx() port,RoomScene／Dashboard 嘅 slot 一行都唔使
// 改,外觀同操作亦因此保證唔變。
//
// 將來嘅 TextClient／Android／WASM Lite 只需要寫自己嗰個
// IClientInteractionView,再 core->setView() 就得,唔使掂 Client。

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
