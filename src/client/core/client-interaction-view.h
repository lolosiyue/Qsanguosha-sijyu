#ifndef CLIENT_INTERACTION_VIEW_H
#define CLIENT_INTERACTION_VIEW_H

// ClientCore 同任何一個 UI 之間嘅唯一契約。
//
// 實作者:
//   DesktopInteractionView  現有 RoomScene／Dashboard（本 PR）
//   未來 TextClient / Android / WASM Lite
//
// View 只做兩件事:呈現 request、收集答案。佢唔可以自己送 reply,亦唔可以
// 自己判斷答案合唔合法 —— 全部要行返 ClientCore::submitResponse()。
//
// 呢個 header 冇 Q_OBJECT:implementor 通常本身已經係 QObject,多重繼承一個
// 純介面唔會令 moc 撞板。

#include "interaction-model.h"

class IClientInteractionView
{
public:
    virtual ~IClientInteractionView() = default;

    // 有新 request 要答。view 喺呢度砌 UI。
    virtual void presentRequest(const InteractionRequest &request) = 0;

    // request 已經由一個被接納嘅答案完成。view 喺呢度收拾 UI。
    virtual void finishRequest(const InteractionRequest &request,
        const InteractionResponse &response) = 0;

    // request 冇經答案就完咗(下一個 request 到、過期、本機放棄、斷線)。
    virtual void cancelRequest(const InteractionRequest &request,
        InteractionCancelReason reason) = 0;

    // 答案被拒。request 仲喺度等答,view 可以提示使用者,但唔准繞過去送 reply。
    virtual void rejectResponse(const InteractionRequest &request,
        const InteractionResponse &response, const InteractionValidation &validation) = 0;
};

#endif
