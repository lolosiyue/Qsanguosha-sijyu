# 引擎與 GUI 解耦實作計畫 (Engine/GUI Decoupling Implementation Plan)

- Status: **Implemented**（契約已全部落地，2026-08-09 確認；僅 `Skill::getDialog()` 舊路徑殘留，見 §1 註記）
- Parent Plan: `docs/cross-platform-modernization-plan.md`
- Milestone: M1
- Last Updated: 2026-08-09
- Scope: Windows x64、Qt 6.5.3、MSVC 2019 過渡基線

> **實作狀態（2026-08-09 對照）**：以下 §1 完成標準 1-7 與 §3 契約（SkillDialogInfo／EngineRuntimeContext／audioEffectRequested／EngineBootstrap／SkillDialogRegistry／server-main／deploy-server／allowlist gate）均已落地並通過 build。殘留項：`Skill::getDialog()`（skill.h:179）與 `class QDialog` 前向宣告（skill.h:5）仍在 engine，§3.1 的「遷移完成後搜尋結果必須為零」尚未達成；此為唯一未完成條目。

## 1. 完成標準

本階段只有在下列條件全部成立時才算完成：

| # | 完成標準 | 狀態（2026-08-09） |
|---|---|---|
| 1 | `qsanguosha_engine` 可作為 `STATIC` library 獨立編譯。 | ✅ CMakeLists.txt:76 |
| 2 | engine 的 Qt module allowlist 只有 `Qt6::Core`、`Qt6::Network`。 | ✅ CMakeLists.txt:166-169 + 175-179 gate |
| 3 | engine 不包含或傳遞 QtGui、QtWidgets、QtQml、QtQuick、QtMultimedia、Core5Compat、FMOD、FreeType 或 Spine 依賴。 | ✅ link allowlist gate 強制（`getDialog()` 殘留見下方註記） |
| 4 | Windows 網路所需的 `Ws2_32`、`IPHLPAPI` 等 system library 可用 `if(WIN32)` 連結。 | ✅ CMakeLists.txt:171-173 |
| 5 | `QSanguosha` GUI 與獨立 `qsanguosha_server` 都連結同一份 engine，不重複編譯 engine `.cpp`。 | ✅ `WHOLE_ARCHIVE:qsanguosha_engine`（CMakeLists.txt:256/344） |
| 6 | 現有 GUI 開服、PC Console Start、控制其他角色、Lua skill dialog、音效與 client replay 行為無回歸。 | ✅ 人工驗收通過 |
| 7 | 專用 server 可在 CMD 顯示啟動資訊及 runtime log，且可用 `Ctrl+C` 正常結束。 | ✅ `qsanguosha_server.exe`（server-main.cpp） |

> **唯一殘留**：`Skill::getDialog()`（skill.h:179）與 `class QDialog` 前向宣告（skill.h:5）仍在 engine 標頭；§3.1 的「遷移完成後 engine 中 `QDialog`／`getDialog()` 搜尋結果為零」尚未達成。其餘 §3 契約（SkillDialogInfo／EngineRuntimeContext／audioEffectRequested／EngineBootstrap／SkillDialogRegistry／server log API）均已落地。

本階段不升級 Qt、MSVC、Lua 或網路協定，不修改卡牌平衡，也不修改 `include/`、`lib/` 內第三方內容。

## 2. Target 與所有權

| Target | 所有權 | 依賴 |
|---|---|---|
| `qsanguosha_engine` | core 規則、package、scenario、server runtime、Lua VM、SWIG、Core/Network 工具 | Qt Core／Network；必要 OS 網路庫 |
| `QSanguosha` | `main.cpp`、Client／ClientPlayer、dialog、UI、Spine、FMOD、PNG replay codec | `WHOLE_ARCHIVE:qsanguosha_engine` 及 GUI modules |
| `qsanguosha_server` | `server-main.cpp`、console message handler、console control handler | `WHOLE_ARCHIVE:qsanguosha_engine` 及 Qt Core／Network |
| `engine-smoke-test` | bootstrap、Lua、package、dialog metadata、shutdown smoke | `WHOLE_ARCHIVE:qsanguosha_engine` 及 Qt Core／Network |

`src/client/client.cpp` 與 `ClientPlayer` 留在 GUI target。package、scenario、server 與 Lua 不得包含 `clientplayer.h`，也不得使用 `ClientInstance`。

## 3. 已鎖定契約

### 3.1 Skill dialog 純資料契約

```cpp
struct SkillDialogInfo
{
    QString type;
    QString objectName;
    QVariantMap parameters;

    bool isValid() const { return !type.isEmpty(); }
};
```

- `Skill::getDialog()` 改為回傳 `SkillDialogInfo` 的純資料 API；engine 不宣告 `QDialog`。
- 無 dialog 的 skill 回傳 invalid／空 `SkillDialogInfo`。
- `SkillDialogRegistry` 位於 GUI，只依 `type` 找 factory。
- factory 保留現有 singleton／`getInstance()` 行為；registry 不接管 ownership。
- 顯示前重新套用 `objectName`、`parameters`，並於操作當下取得 `Client::currentSelf()`，不得保存舊 `ClientPlayer *`。

穩定 registry key 共 11 種：

| Key | 主要參數 |
|---|---|
| `guhuo` | `left`、`right`、`playOnly`、`slashCombined`、`delayedTricks`、`refresh` |
| `tiansuan` | `choices` |
| `juguan` | `cardNames` |
| `taoluan` | `objectName` |
| `youlong` | `objectName` |
| `weidi` | 無額外參數 |
| `shefu` | `objectName` |
| `pingjian` | 無額外參數 |
| `mobileJianying` | `objectName` |
| `huomo` | 無額外參數 |
| `caozhao` | `objectName` |

現況基線為 59 個 active `getDialog()` override、21 個 package/scenario 檔及 11 種 dialog。遷移完成後，engine source 中 `QDialog`、`getDialog()` 與 package dialog class 定義的搜尋結果必須為零。

> **2026-08-09 狀態**：11 種 dialog 已全部改走 `SkillDialogInfo`＋`SkillDialogRegistry`（skill-dialog-registry.cpp/h 於 GUI），59 個 override 已遷移；唯一未清零者為 `Skill::getDialog()` 本身（skill.h:179）與 `class QDialog` 前向宣告（skill.h:5）——引擎中的舊路徑殘留，需日後移除。

### 3.2 Lua 與 SWIG 相容

- 保留 Lua API：`setGuhuoDialog`、`setJuguanDialog`、`setTiansuanDialog`。
- 現有 DSL 字串只在 Lua wrapper 轉成 `SkillDialogInfo.parameters`，不得建立 QWidget。
- 現況 active 呼叫基線：Guhuo 42、Juguan 10、Tiansuan 4，共 56 個。
- SWIG wrapper 與 Lua C VM 屬於 engine，因 `Engine`、`Room` 與 LuaAI 直接建立 Lua state。
- SWIG 不得暴露 `QDialog`、`QColor` 或 `ClientPlayer`；`sgs.Self` 改以 `Player *` 暴露。
- `swig/sanguosha_wrap.cxx` 仍只在 build tree 自動生成，不得手動修改或同步來源樹版本。

### 3.3 Runtime context 與 Self

```cpp
class EngineRuntimeContext
{
public:
    virtual ~EngineRuntimeContext() = default;
    virtual RoomState *roomState() = 0;
    virtual const Player *cardOwner(int cardId) const = 0;
    virtual Player::Place cardPlace(int cardId) const = 0;
    virtual Card *card(int cardId) const = 0;
};
```

- `Room` 與 GUI target 的 `Client` 分別實作此介面。
- `Engine` 不得 include 或 `qobject_cast` 成 `Client`。
- engine 全域只保留 `Player *Self`。
- `Client` 保留 `ClientPlayer *m_currentSelf`、`setSelf(ClientPlayer *)`、`currentSelf()`。
- `setSelf()` 必須同步 core `Self`、signal connection、`switch_control_context` 及可見／控制狀態。
- Client/UI 需要 `ClientPlayer` API 時使用 `Client::currentSelf()`；package/rule 只能使用 `Player *`。

### 3.4 Settings 與 ServerInfo

| 元件 | 內容 |
|---|---|
| `Settings Config` | `QSettings`、server/general persistence、Qt Core 型別 |
| `UiSettings UiConfig` | QFont、QColor、theme、scale、background、palette |
| `ServerInfoStruct` | setup parsing、timeout、純資料，全域 `ServerInfo` |
| `ServerInfoWidget` | QWidget 呈現，位於 GUI/dialog |

- server 設定保留既有 `config.ini` 與 `QSettings::IniFormat`，不導入 YAML，既有 key 不改名。
- `UiConfig` 仍透過 `Config.value()`／`Config.setValue()` 持久化，不建立第二個設定檔。
- `applyColorScheme()`、`applyVisualMode()`、font 載入與 GUI warning 移到 GUI。
- `src/client/clientstruct.*` 拆成 `src/core/server-info.*` 與 GUI `serverinfowidget.*`，遷移後移除舊檔。

### 3.5 顏色、JSON 與 Core5Compat

```cpp
QString Engine::getKingdomColor(const QString &kingdom) const;
QMap<QString, QString> Engine::getSkillTypeColorMap() const;
```

- engine 保存並回傳 CSS／hex 色碼字串；無效值 `qWarning()` 後回退 `#808080`。
- `card.cpp`、`skill.cpp`、`general.cpp` 直接用字串產生 HTML。
- GUI 使用時才轉成 `QColor`。
- `JsonUtils::tryParse(QColor &)` 移至 UI JSON helper；現有使用點全在 `skin-bank.cpp`。
- engine 的 `QRegExp` 全部改成 `QRegularExpression`。
- `QTextCodec` 的 GBK 排序改用 Qt Core `QCollator`，engine 不連結 Core5Compat。

### 3.6 音效邊界

```cpp
signals:
    void audioEffectRequested(const QString &filename, bool superpose);
```

- `Engine::playAudioEffect()` 保留設定、路徑與檔案存在檢查，成功時發出 signal，既有 Lua API 與 bool 回傳不變。
- GUI 將 signal 接到 `Audio::play()`；`Audio::init()`、`Audio::quit()` 與 `audio.cpp` 留在 GUI target。
- GUI 內嵌 server 仍可播放現有音效；專用 server 不連接 signal，因此靜默執行且不連結 FMOD。
- 此 signal 是 M1 的最小解耦 port；總計畫 M6 的 `IAudioBackend` 三後端仍保留為後續目標。

### 3.7 Replay 與錯置檔案

| 元件 | Target |
|---|---|
| `RecordBuffer` | engine，封包記錄與純文字 replay |
| `RecorderImageCodec` | GUI，`TXT2PNG`／`PNG2TXT` |
| `Recorder` | GUI/client，組合 buffer 與 image codec |
| `record-analysis` | GUI/client；從 `src/core` 移出 |

- `ServerPlayer` 使用 `RecordBuffer`，不依賴 `QImage`。
- GUI client 保留 `.txt`／`.png` replay。
- GUI 內嵌 server 可由 GUI 安裝 image encoder callback。
- 專用 server 只保證文字 replay；要求 PNG 時記錄 warning 並回報失敗或依呼叫契約保存文字，不得悄悄產生損壞 PNG。
- SWIG 暫時保留 `ServerPlayer::startRecord()`／`saveRecord()`。

### 3.8 Error 與 log 規則

| 類型 | 規則 |
|---|---|
| caller 可處理 | 回傳 `bool`，必要時填入 `QString *error` |
| 可恢復異常 | `QLoggingCategory`＋`qWarning()` |
| Lua／初始化致命錯誤 | 回傳失敗＋`qCritical()`；由入口決定 GUI dialog 或 console exit |
| Room LuaAI 失敗 | 保存狀態與錯誤文字，不建立 GUI |

本階段不新增全域 `Result<T>` 或 error bus。core/server 中不得出現 `QMessageBox` 或 `QApplication`。

## 4. Server 邊界與入口

### 4.1 Server class 拆分

`src/server/server.*` 只保留 Server、socket、room、UPnP、list server、headless runtime。下列 QWidget 類別移到 GUI：

- `src/dialog/serverdialog.*`
- `src/dialog/banlistdialog.*`
- `src/dialog/select3v3generaldialog.*`
- `src/dialog/bossmodecustomassigndialog.*`

### 4.2 共用 bootstrap

```cpp
namespace EngineBootstrap {
    bool initialize(QString *error = nullptr);
    void shutdown();
}
```

`initialize()` 負責共用亂數初始化、建立 `Sanguosha`、載入 Core `Config`、BanPair、Lua 與 package runtime。它不得建立 GUI 或音效物件。

- GUI：建立 `QApplication`、安裝翻譯、呼叫 bootstrap，再初始化 `UiConfig`、palette、font、audio。
- server：建立 `QCoreApplication`、安裝翻譯、呼叫 bootstrap，再啟動監聽。

### 4.3 Server log

```cpp
const QStringList &Server::startupMessages() const;
void Server::logMessage(const QString &message);
```

- `Server::listen()` 成功後建立原 `StartScene::printServerInfo()` 的完整訊息清單。
- `logMessage()` 同時寫入 `qCInfo(qsanServerLog)` 並 emit `server_message`。
- `StartScene` 先顯示 `startupMessages()`，再連接後續 `server_message`，不再自行組裝設定文字。
- `Room::room_message` 接至 `Server::logMessage()`，不再 signal-to-signal 直接轉送。
- console 格式為 `[時間] [INFO/WARN/ERROR] 訊息`；info 到 stdout，warning/error 到 stderr。
- JSON Lines、輪替與 retention 屬總計畫後續診斷里程碑，不在 M1 擴張。

### 4.4 三種啟動方式

| 入口 | 行為 |
|---|---|
| GUI `Start Server` | 現有內嵌 server＋`StartScene`，不改使用流程 |
| GUI `PC Console Start` | 維持同一 GUI 行程內嵌 server，並讓本機 client 自動連線；不啟動外部 exe |
| `qsanguosha_server.exe` | 獨立 console server，讀取同一 `config.ini`，GUI 關閉不影響它 |

`QSanguosha.exe -server` 暫時保留為過渡相容入口，改用同一 bootstrap/log，但文件建議專用部署使用 `qsanguosha_server.exe`。獨立 server 由 `Ctrl+C` 觸發正常關閉；不新增遠端 shutdown 命令。

## 5. CMake 設計

### 5.1 顯式來源清單

`cmake/QSanguoshaSources.cmake` 保留單一 manifest，但拆為：

```cmake
QSAN_ENGINE_SOURCES
QSAN_ENGINE_MOC_HEADERS
QSAN_LUA_SOURCES
QSAN_GUI_SOURCES
QSAN_GUI_MOC_HEADERS
QSAN_FORMS
QSAN_SPINE_SOURCES
```

禁止使用 source glob。每個 `.cpp` 只能由一個 target 編譯。

### 5.2 Engine target 與 PCH

```cmake
qt_add_library(qsanguosha_engine STATIC ...)
target_link_libraries(qsanguosha_engine
    PUBLIC Qt6::Core Qt6::Network
)
target_precompile_headers(qsanguosha_engine PRIVATE
    src/core/engine-pch.h
)
```

- `src/pch.h` 為 GUI PCH，可包含 Widgets、QML、FMOD 等。
- `src/core/engine-pch.h` 只包含 QtCore、QtNetwork 與標準函式庫。
- `qrand()`／`qsrand()` 相容 helper 移至獨立 Core header，不得依賴 PCH 才有宣告。
- configure 階段檢查 engine Qt link allowlist，出現其他 Qt target 立即 `FATAL_ERROR`。

### 5.3 Package 註冊與 SWIG

現有 `ADD_PACKAGE` 依賴 translation unit 的全域 `PackageAdder`。兩個 executable 與 integration test 必須以：

```cmake
"$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>"
```

連結 engine，避免 static archive dead stripping 使 package 消失。

### 5.4 輸出與部署

| Target | Debug | Release |
|---|---|---|
| GUI | `debug/QSanguosha.exe` | `release/QSanguosha.exe` |
| Server | `debug/qsanguosha_server.exe` | `release/qsanguosha_server.exe` |

- `deploy`：一般玩家 GUI 發行包，只部署 `QSanguosha.exe` 與 GUI runtime。
- `deploy-server`：專用 server 包，只部署 server、Qt Core／Network runtime、Lua／設定與必要遊戲資料。
- 一般玩家發行流程維持單一 GUI 主程式；兩個部署 target 分開執行。

## 6. 四個可建置批次

### Batch A：純資料契約

1. 新增 `SkillDialogInfo`、`EngineRuntimeContext`、Core `Player *Self`。
2. 拆出 `ServerInfoStruct`。
3. 顏色 API 改為 `QString`，搬移 JSON QColor parser。
4. 以 `audioEffectRequested` 取代 Engine 對 `Audio` 的直接呼叫。
5. 拆出 `RecordBuffer`，移動 `record-analysis` 所有權。
6. 移除 core/server `QMessageBox`，套用 bool/error/log 規則。
7. Lua legacy dialog API 改為產生純資料。

門檻：現有 GUI Debug build 成功；Lua adapter 測試成功；核心行為不變。

### Batch B：GUI 搬移

1. 建立 `SkillDialogRegistry`。
2. 移動 11 種 dialog class 與 GUI helper，保留 singleton 行為。
3. 將 59 個 override 改為 `SkillDialogInfo`。
4. 新增 `UiSettings UiConfig`，移動 palette/font/background。
5. 移動四個 server dialog、ServerInfoWidget、RecorderImageCodec、record-analysis。
6. 完成 `Client::currentSelf()` 與控制角色同步。

門檻：GUI Debug build；一般 skill dialog、Lua dialog、控制其他角色與 PNG replay 人工回歸。

### Batch C：Runtime 與 server

1. 新增 `EngineBootstrap` 並套用 GUI、`-server`、test 入口。
2. `Server` 建立 startup message 與統一 log API。
3. `StartScene` 只呈現 Server 提供的訊息。
4. 新增 `server-main.cpp` 與 `Ctrl+C` 正常關閉。
5. 保留 GUI `Start Server`、`PC Console Start` 原行為。

門檻：GUI 兩種開服流程及專用 CMD server 實測成功；startup/runtime log 一致。

### Batch D：Build 邊界

1. 拆分 CMake source/MOC manifest。
2. 新增 STATIC engine、engine PCH 與 `WHOLE_ARCHIVE`。
3. 移除 QRegExp/QTextCodec/Core5Compat。
4. 新增 `engine-smoke-test` 與 CMake link allowlist gate。
5. 新增 `deploy-server`，保留既有 `deploy`。
6. 執行 Debug／Release build 與完整人工矩陣。

門檻：第 1 節所有完成標準成立。

## 7. 自動化驗收

`engine-smoke-test` 必須驗證：

1. `QCoreApplication` 下 `EngineBootstrap::initialize()` 成功。
2. Lua state 存在且可載入代表 script。
3. Lua `package_names` 對應的 built-in package factory 全數存在。
4. 可取得代表 general、card、skill、scenario。
5. 11 種 `SkillDialogInfo.type` 及必要 parameters 有效。
6. 三個 legacy Lua dialog setter 可轉成純資料。
7. SWIG 生成結果不含 `QDialog`、`QColor`、`ClientPlayer` type wrapper。
8. `EngineBootstrap::shutdown()` 正常完成。

建置與測試命令使用既有增量流程：

```powershell
cmake --preset vs2026-x64
cmake --build --preset debug --target qsanguosha_engine --parallel 8
cmake --build --preset debug --parallel 8
ctest --preset debug --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-cmake.ps1 -Configuration Release
```

只有 CMake/source/PCH 變更後才重新 configure；不得使用 `--fresh` 或 `--clean-first`。

## 8. 人工驗收矩陣

| 情境 | 通過條件 |
|---|---|
| GUI `Start Server` | 正常監聽；StartScene 顯示完整 startup 與 runtime log |
| GUI `PC Console Start` | 保持單一 GUI 行程；本機 client 自動連入內嵌 server |
| 專用 server | CMD 顯示同一 startup/runtime log；`Ctrl+C` 正常結束 |
| 一般 client | 可連入兩種 server，完成選將、出牌、回應及一局 |
| 控制其他角色 | 切換後手牌、技能按鈕、選牌及 11 類 dialog 都使用新 `currentSelf()` |
| Lua 相容 | 三類 legacy dialog setter 的代表技能可開啟並回傳選擇 |
| Replay | client `.txt`／`.png` 正常；專用 server 文字 replay 正常 |
| Error path | LuaAI 失敗只寫 log／狀態，core/server 不顯示 QMessageBox |

每批執行受影響情境；Batch D 執行全矩陣。任何 GUI 行為回歸、package 遺失或 engine link allowlist 失敗都阻止該批完成。

## 9. 明確延後項目

- Qt 6.11.1、MSVC 2022、Lua 5.4.8 與 Ubuntu build。
- 確定性 `GameRng`／`UiRng`。
- 完整 `IAudioBackend` 三後端；M1 只建立 signal port。
- JSON Lines、log 輪替、session/game ID 與 crash reporter。
- 遠端 server 管理／shutdown API。
- 將 dialog singleton 改為每次建立。
- 網路協定與 replay 格式升版。

