# askForQml 通用 QML 互動接口

## 概述

`askForQml` 是一條由 server 主動發起、client 以嵌入式 QML overlay 回覆的通用互動鏈。

它適合處理以下場景：
- 臨時性的 UI 問答或選單
- 需要回傳複合結果的互動面板
- 不想為單一功能再新增一組專用 Widgets 協議的需求

目前這條鏈路的核心入口如下：
- `Room::askForQml()`：server 端發起請求
- `Client::qml_interact`：client 端轉發到 UI
- `RoomScene::onQmlInteract()`：建立嵌入式 QML overlay
- `EmbeddedQmlLoader::qmlResultReady`：把 QML 結果送回 client/server

## 完整流程

```text
Server
  Room::askForQml(player, qmlPath, params, timeout)
    -> notifyMoveFocus(player, S_COMMAND_QML_INTERACT)
    -> doRequest(player, S_COMMAND_QML_INTERACT, [qmlPath, params], timeout, true)

Client
  Client::askForQml(arg)
    -> emit qml_interact(qmlPath, params)
    -> setStatus(AskForQml)

UI
  RoomScene::onQmlInteract(qmlPath, params)
    -> new EmbeddedQmlLoader(this)
    -> connect(qmlResultReady, onQmlResultReady)
    -> QTimer::singleShot(params["timeout"], loader, SLOT(timeout()))
    -> loadQmlOverlay(...)

QML
  emit finished(result)
  或 qmlLoader.closeFromQml()

UI
  EmbeddedQmlLoader::receiveQmlResult(result)
    -> emit qmlResultReady(result)
  EmbeddedQmlLoader::timeout()
    -> emit qmlResultReady(QVariant())
  RoomScene::onQmlResultReady(result)
    -> Client::replyQml(result)

Server
  Room::doRequest() 結束
    -> player->getClientReply()
```

## C++ API

### `Room::askForQml`

```cpp
QVariant askForQml(ServerPlayer *player,
                   const QString &qmlPath,
                   const QVariantMap &params,
                   int timeout = 30000);
```

| 參數 | 類型 | 說明 |
|------|------|------|
| `player` | `ServerPlayer *` | 要被請求互動的目標玩家 |
| `qmlPath` | `QString` | client 端要載入的 QML 檔案路徑 |
| `params` | `QVariantMap` | 傳給 QML 的上下文資料 |
| `timeout` | `int` | server 端等待回覆的毫秒數 |

**返回值**：
- QML 端有回傳時，拿到對應的 `QVariant`
- 請求失敗或 server 等待超時時，回傳空 `QVariant()`

### 典型用法

```cpp
QVariantMap params;
params["title"] = "請選擇一項";
params["timeout"] = 15000;
params["choices"] = QStringList() << "draw" << "recover";

QVariant result = room->askForQml(player, "ui-script/qml/ChooseOption.qml", params, 15000);
if (result.isValid()) {
    QVariantMap map = result.toMap();
    QString choice = map.value("choice").toString();
    // 根據 choice 繼續處理
}
```

## QML 端契約

`EmbeddedQmlLoader` 目前對 QML 暴露兩種互動方式：

1. root item 宣告 `finished(QVariant)`，由 loader 自動接線
2. 透過 context property `qmlLoader` 呼叫 `closeFromQml()` 關閉 overlay

另外 loader 還會額外注入：
- `fileHandler`
- `params` 中每一個 key 對應的 context property

也就是說，若 `params["title"] = "請選擇一項"`，QML 端可以直接使用 `title`，不是透過 `params.title` 取值。

### 推薦寫法

```qml
import QtQuick 2.12

Item {
    signal finished(var result)

    function submit(choice) {
        finished({ choice: choice })
    }

    function cancel() {
        finished({})
    }
}
```

這樣 `RoomScene::onQmlResultReady()` 會立即把結果送回 server。

### `closeFromQml()` 的實際語義

`qmlLoader.closeFromQml()` 目前只會：
- 關閉 QML overlay
- 析構 `EmbeddedQmlLoader`

它**不會**主動發出 `qmlResultReady(QVariant())`。

因此若 QML 端只呼叫 `closeFromQml()`：
- client overlay 會立刻消失
- 但 server 端不會立刻拿到空結果
- 最終通常要等 `Room::askForQml(..., timeout)` 的 server 端等待超時，才會回到空 `QVariant()`

若需求是「立即取消並立刻讓 server 繼續」，應改成直接 `finished({})` 或 `finished(QVariant())`，不要只呼叫 `closeFromQml()`。

## 路徑與載入限制

目前 `EmbeddedQmlLoader::loadQmlOverlay()` 直接使用：
- `QFile::exists(qmlPath)`
- `QUrl::fromLocalFile(qmlPath)`

因此 `qmlPath` 應視為**本地檔案路徑**，不是 `qrc:/` 資源路徑。

若要避免工作目錄差異造成載入失敗，建議在上層先把路徑整理成 client 可直接找到的實際檔案路徑。

## timeout 約定

目前 askForQml 有兩個 timeout 來源：

| 位置 | 作用 |
|------|------|
| `Room::askForQml(..., timeout)` 第四參數 | server 端 `doRequest()` 等待回覆的時間 |
| `params["timeout"]` | client 端 `RoomScene::onQmlInteract()` 建立 overlay 後的自動 timeout |

這兩者**不會自動同步**。

建議：
- 需要自動超時關閉時，同時設定兩者
- 兩者盡量保持一致，避免出現 client 已關閉但 server 還在等，或 server 已超時但 client overlay 尚未結束的落差

## 協議與狀態補充

- 協議命令使用 `S_COMMAND_QML_INTERACT`
- 目前不需要另外在 request/response pair 表裡補映射，`Room::doRequest()` 對未特別映射的命令，預設就會期待同命令回覆
- `Client::setStatus(AskForQml)` 會把 client 帶到 `AskForQml = 0x10`
- 由於目前 `ClientStatusBasicMask = 0x0F`，這個狀態在部分 masked 判斷下會等價於 `NotActive`，剛好能在 overlay 顯示期間壓住底層房間互動

## Lua / SWIG 暴露

`swig/sanguosha.i` 已匯出同名介面：

```cpp
QVariant askForQml(ServerPlayer *player, const char *qmlPath, const QVariantMap &params, int timeout = 30000);
```

因此 Lua 層理論上也能沿 SWIG 呼叫 `Room::askForQml()`。

但目前專案內尚未看到實際呼叫點，也沒有額外包一層高階 helper；若要從 Lua 直接使用，仍需先準備可傳入的 `QVariantMap` 參數。

## 適用邊界

適合：
- 一次性選單
- 輕量互動對話框
- 需要回傳字串、數字、布林、map 的客製流程

不適合：
- 長時間常駐 HUD
- 需要和底層房間按鈕並行操作的互動
- 只靠 `closeFromQml()` 就要求 server 立即恢復流程的設計