# 跨平台現代化與功能移植計劃 (Cross-Platform Modernization Plan)

- Status: Approved Plan
- Implementation: M1 Complete（2026-08-09 確認）、M2 工具鏈進行中（2026-08-18：VS 2026 v145 + Qt 6.11.1）
- Last Updated: 2026-08-21
- **規範性**：本文件是跨平台現代化與另一分支通用功能移植的唯一權威執行計劃；與既有 roadmap 或審計結論衝突時，以本文件為準。

## 1. 來源、目的與範圍

候選來源為 `H:\Program file\Game\sgs\Qsgs\gitee\QSanguosha-v2` 的 `doom` 分支。該分支以提交 `c7848dc19287bc0c8f31e1aafe807dcd63e12597` 為共同基線，審查時的分支頂端為 `42167c54b6ec698d5732681c5fcac2d0d98c4400`。本項目已在技能實例、CorrectSkillV2、GameModeStruct、重播、Spine/GIF 及 UI 呈現路徑上形成不同架構，因此不得直接合併或批量 cherry-pick。

移植時必須為每項候選功能建立來源紀錄，至少包含來源提交、原始症狀、相關函式、目前是否仍存在、重現方式及與本項目架構的衝突。所有功能以重新適配現有 API 為原則。

| 分類 | 內容 |
|---|---|
| 必須移植 | 引擎與 GUI 解耦、無頭伺服器、確定性隨機數、自動對戰、CMake、Qt 6、Lua 5.4、測試與 CI |
| 優先移植 | 結構化日誌、崩潰／卡死報告、選包白名單、武將版本去重、通用安全與正確性修復 |
| 完整移植 | 4K／高 DPI (High DPI)、向量文字與元件、主題切換、自適應介面、FPS／繪製性能面板 |
| 後期移植 | LuaAI 觀測式除錯器、無效詢問跳過、單機立即投降、Android 客戶端 |
| 只建立差異帳本 | 另一分支的 AI 行為及策略修復；本計劃不改變目前 AI 決策 |
| 不移植 | 武將／卡牌／擴充包內容、Trainer、LLM 伴侶、武將立繪編輯器、舊 MinGW 工作樹部署、未實作的遊戲記錄草案 |

## 2. 交付平台與技術基線

| 平台 | 交付範圍 | 音訊後端 |
|---|---|---|
| Windows x64 | 完整 GUI、內嵌／獨立伺服器、崩潰報告、開發者符號包 | FMOD |
| Ubuntu 24.04 x64 | 無頭伺服器、安裝目錄、啟動腳本、systemd unit 範例 | Null |
| Android | 後期正式客戶端、單機內嵌房間、Google Play AAB＋Play Asset Delivery | Qt Multimedia |

| 項目 | 固定基線 |
|---|---|
| C++ | C++17 |
| Windows 編譯器 | Visual Studio 2026 v145 x64（官方 Qt kit 為 `msvc2022_64`） |
| Qt | Qt 6.11.1，一次性切換，不維護 Qt 5 相容層 |
| Lua | Lua 5.4.8，提供專案實際需要的 Lua 5.2 相容層 |
| 建置系統 | CMake 4.2+、`CMakePresets.json`、Visual Studio 2026 Open Folder |
| Android | min API 28、target/compile API 36、NDK r27c、JDK 21 |
| Android ABI | Google Play 正式版僅 `arm64-v8a`；`x86_64` 僅供 CI／模擬器 |

官方基線資料：

- [Qt 6.11.1 release](https://www.qt.io/blog/qt-6.11.1-released)
- [Qt 6.11 supported platforms](https://doc.qt.io/qt-6/supported-platforms.html)
- [Qt for Android](https://doc.qt.io/qt-6/android.html)
- [Lua releases](https://www.lua.org/ftp/)
- [Google Play target API requirement](https://developer.android.com/google/play/requirements/target-sdk)
- [Google Play 64-bit requirement](https://developer.android.com/google/play/requirements/64-bit)

## 3. 建置目標與架構邊界

| 目標 | 責任與依賴限制 |
|---|---|
| `qsanguosha_engine` | 共用規則、資料及伺服器核心；只依賴必要的 Qt Core／Network，不得依賴 Widgets、Quick、Multimedia 或顯示伺服器 |
| `QSanguosha` | Windows GUI 客戶端及本地遊戲入口 |
| `qsanguosha_server` | Windows／Linux 無頭伺服器，使用 `QCoreApplication` |
| Android app target | Qt GUI／Quick／Widgets／Multimedia 客戶端；不提供公開專用伺服器，但單機可建立內嵌房間 |
| `crashreporter` | Windows 純 Win32／DbgHelp 診斷工具 |
| CTest targets | 單元、整合、Lua、自動對戰及性能測試 |

CMake 必須啟用 AUTOMOC、AUTOUIC、AUTORCC，管理資源、翻譯、安裝規則與平台條件來源。2026-07-30 已先完成 Windows x64 過渡建置：CMake 3.28+、Qt 6.5.3、MSVC 2019、單一 `QSanguosha` target，並移除 qmake、舊 `.sln`／`.vcxproj` 及舊 Makefile 入口。此過渡建置已通過 Debug／Release，保留既有 FMOD、Breakpad、SWIG、翻譯與輸出路徑；尚未完成 `qsanguosha_engine` 邊界，也不取代最終 Qt 6.11.1／MSVC 2022→2026／Lua 5.4.8 基線。（該邊界已於 2026-08-09 完成見 §10 M1；Windows 工具鏈已於 2026-08-18 升級至 VS 2026 v145 + Qt 6.11.1 `msvc2022_64`。）

### 3.1 引擎與 GUI 解耦

- 本階段的規範性細節、批次及驗收以 [`engine-gui-decoupling-implementation-plan.md`](engine-gui-decoupling-implementation-plan.md) 為準。
- 將 `Skill::getDialog()` 形式的 GUI 物件依賴改為純資料 `SkillDialogInfo { type, objectName, parameters }`。
- UI 透過 `SkillDialogRegistry` 根據描述建立實際對話框；Lua 技能同樣只傳遞描述資料。
- 將伺服器資料結構與 QWidget 對話框拆分。
- core/server 中的 QMessageBox 改為結構化日誌及可傳回錯誤，不得由無頭程序建立 GUI。
- M1 的完成標準固定為：`qsanguosha_engine` 可獨立編譯，Qt module 只連結 Core／Network；Windows 必要網路 system library 不受此 Qt allowlist 限制。

### 3.2 確定性隨機數

- 新增每局 `GameRng` 與獨立 `UiRng`。
- 提供 `qsanRandomBounded()` 及 `qsanShuffle()`，以固定拒絕取樣及 Fisher–Yates 實作遊戲亂數。
- 每局保存 seed；UI 動畫、外觀或繪製不得消耗遊戲 RNG。
- 相同版本、設定、牌堆與 AI 在 Windows／Ubuntu 必須產生相同規範化事件雜湊。

2026-08-30 實作狀態：PCH 的 `qrand`／`qsrand` shim 與自訂 `qShuffle` 已移除，遊戲與 UI RNG 呼叫邊界已分離；相同 seed 保證新實作內可重現，不維持舊 shim 的逐項序列。`QRegExp`／`Core5Compat` 與 `Q_ENUMS` 亦在同一 Qt6 API 債批次清除。

Spine 的 GLSL 120 shader 與 `beginNativePainting()` 不屬於本批 API 相容債；現行 QPainter／原生 OpenGL 混合路徑仍保留 native-painting 邊界。core-profile 或 QRhi 遷移須以獨立 renderer 里程碑處理。

### 3.3 Lua 與 SWIG

- 內建直譯器升級至 Lua 5.4.8，保留專案既有 `continue` 語法、固定 seed 的 table hash 行為，以及 SWIG 整數轉字串相容行為。
- 相容層只保留大量重複使用的 `bit32.band` 與 `module(..., package.seeall)`；低頻舊 API 直接修改 Lua 檔案，不建立完整 Lua 5.2 模擬層。
- `extensions/main` 維持追蹤遠端最新版，不固定 commit；升級驗證應記錄當次檢查到的 commit。
- CMake 在建置目錄的 `generated/sanguosha_wrap.cxx` 自動執行 SWIG。
- wrapper 不存在或任一 `swig/*.i` 較新時會自動重新生成；來源樹的 wrapper 不參與建置，也不在工作區之間同步。
- 自動生成檔留在 CMake build tree，正常建置不會改動來源樹。

2026-08-30 實作狀態：Lua 5.4.8 核心、最小相容層、seeded state API、SWIG API 調整及 focused `lua-compat` 測試已落地。檢查 `extensions/main@4dd30101310f9c2cb7cca2de3bd4d40ac77e8736` 時，以下低頻問題只列入人工修改清單，本次不修改外部倉庫：

| 檔案 | 位置 | 待修改內容 |
|---|---:|---|
| `ai/yjcm2014-ai.lua` | 27 | `bit32.lshift(1, card:getTypeId())` 改為 `(1 << card:getTypeId())`；第 19 行另有相同註解文字 |
| `ai/ai-debug-logger.lua` | 331 | `unpack(results)` 改為 `table.unpack(results)` |
| `ai/NyarzThird-ai.lua` | 110 | `unpack(num)` 改為 `table.unpack(num)` |
| `extensions/hunlie.lua` | 1298 | `0then` 改為 `0 then` |

在上述四個外部檔案修正前，完整 AI／extensions 載入 gate 仍為待完成；這不擴張成通用相容層。

## 4. 三後端音訊架構

保留遊戲層既有 `Audio` facade，內部增加 `IAudioBackend`。同一執行檔只編譯及連結一個後端。

```text
Audio
 └─ IAudioBackend
     ├─ FmodAudioBackend      Windows／桌面 GUI
     ├─ QtMediaAudioBackend   Linux GUI、Android
     └─ NullAudioBackend      無頭伺服器
```

> **已實作（2026-08-28，Linux GUI M2B-A）**：本節的三後端抽象已落地於
> `src/ui/audio/`，`Audio` facade 維持不變。Linux GUI 使用 `QtMediaAudioBackend`，
> Windows GUI Release 維持 `FmodAudioBackend`（行為未變），dedicated server 與
> Windows Debug 使用 `NullAudioBackend`。Android 尚未接上，屬 M7。
> 契約與驗證方式見 [`linux-development-environment.md`](linux-development-environment.md) §4.7。

| 行為 | Windows FMOD | Linux／Android Qt Multimedia | Null |
|---|---|---|---|
| 短 UI 音效 | FMOD 快取 | `QSoundEffect` 預載少量音效；解不到時退到獨立小型播放器池 | no-op |
| OGG 語音 | FMOD sound/cache | `QMediaPlayer + QAudioOutput` 播放器池（上限 8） | no-op |
| OGG BGM | FMOD stream | 獨立 `QMediaPlayer` 循環播放 | no-op |
| `superpose=false` | 同檔播放中則抑制 | 播放器池檢查同檔活動實例 | no-op |
| 錯誤 | 記錄警告，不崩潰 | 記錄警告，不崩潰 | 不產生日誌噪音 |
| 無輸出裝置 | 靜音執行 | 靜音執行，`hasOutputDevice()=false` | n/a |

Android 固定只為 `button-down`、`button-hover`、`choose-item`、`pop-up` 等觸控回饋提供 WAV 衍生資產，以 `QSoundEffect` 預載。約 15,357 個原有 OGG 語音與 BGM 不整批轉 WAV；語音使用可重用播放器池，BGM 使用獨立播放器。App 進入背景時暫停音訊，返回前景後按原狀態恢復。Android 不打包任何 FMOD `.so`、Java 元件或標頭。

CMake 提供 `QSAN_AUDIO_BACKEND=FMOD|QT|NULL` 並按平台設定固定預設（Windows→`FMOD`、Linux GUI→`QT`、server-only→`NULL`）。後端選擇只發生在 CMake 與 `src/ui/audio/audio-backend-factory.cpp`，呼叫端沒有平台 `#ifdef`。`Audio::backendName()` 回報實際生效的後端，`Audio::getVersion()` 回報該後端的版本，不再假設一定是 FMOD；About 對話框顯示兩者。

M1 引擎解耦先以 `Engine::audioEffectRequested` signal 建立不含 FMOD 的純 Core port；本節的完整 `IAudioBackend` 三後端仍在 M6 實作，兩者不是互斥方案。

Windows 先驗證現有 FMOD Ex 4.44 與 MSVC 2022 x64 的連結、啟動及壓力測試。若失敗，預定退路是由外部 `FMOD_SDK_ROOT` 提供 FMOD Core 2.03；不得修改 `include/` 或 `lib/`。發布前必須確認 [FMOD 授權與 attribution](https://www.fmod.com/legal)。

「全面移除 FMOD」不是本計劃的一部分，已由「Windows FMOD／Android Qt Multimedia／無頭 Null」方案取代。

## 5. Android 資產與發布架構

| 類型 | 檔案數 | 未壓縮體積 |
|---|---:|---:|
| 音訊 | 約 15,363 | 約 1.09 GiB |
| 圖片 | 約 17,954 | 約 1.27 GiB |

約 1.09 GiB 音訊與 1.27 GiB 圖片不得直接放入 Android base module。正式渠道固定為 Google Play Android App Bundle (AAB)＋Play Asset Delivery (PAD)，不建立自有 CDN 或獨立資產下載器。

新增統一 `AssetLocator`：

```text
logical path
 ├─ DesktopAssetLocator  → 安裝目錄
 ├─ AndroidBaseLocator   → base assets
 └─ AndroidPadLocator    → fast-follow／on-demand asset pack
```

| 資產層 | 交付方式 | 內容 |
|---|---|---|
| Base module | install-time | Qt/runtime、原生程式、QML/UI、翻譯、必要字型、短 UI WAV、下載介面 |
| `core_visual` | fast-follow | 共用桌面、卡牌框、UI 圖片及預設包必要圖片 |
| `core_audio` | fast-follow | 系統音效、預設 BGM、預設十包必要語音 |
| Package-family packs | on-demand | 其餘武將圖片、語音及包專用資產 |

建置時產生包含邏輯路徑、pack ID、SHA-256、大小與內容版本的資產索引。選包頁顯示未下載包的大小及狀態；加入需要缺失資產的伺服器前先下載。離線時只允許已安裝資產。PAD pack 位置每次由 API 重新取得，不保存可能失效的絕對路徑。

CI 使用 `bundletool` 檢查 base 及 pack 大小：[Google Play app size limits](https://support.google.com/googleplay/android-developer/answer/9859372)、[Play Asset Delivery](https://developer.android.com/guide/playcore/asset-delivery)。

## 6. 選包、協定與通用體驗

### 6.1 選包白名單

- 持久化 `EnabledPackages`，並以 `EnabledPackagesMigrationVersion=1` 控制一次性遷移。
- 首次讀到舊 `BanPackages` 時，依當時完整普通包集合計算補集，保留完全相同的實際啟用結果；遷移完成後不再持久化舊鍵。
- 現有依賴 `BanPackages` 的呼叫點暫時使用執行期衍生值。
- 玩法及劇本專用包不出現在普通選包頁，由 GameModeStruct 按模式啟用。

新安裝預設啟用：

`standard`、`wind`、`fire`、`thicket`、`mountain`、`YJCM`、`YJCM2012`、`standard_cards`、`standard_ex_cards`、`maneuvering`。

### 6.2 武將版本去重

- 預設關閉。
- 使用純函式處理候選池，固定優先序為 `tenyear > new > mobile > ol > nos > base`。
- 只影響同名候選的顯示與抽取，不改包載入、技能註冊或武將內容。

### 6.3 協定與重播

- 2026-08-30 已完成 Protocol V1 codec boundary、V1-compatible capability
  negotiation、codec-neutral `ProtocolMessage`、Protocol V2 envelope／codec contract，
  以及 V1 OFFER／ACK／COMMIT runtime activation。`Packet` 保留 compatibility facade；
  production connection 可逐連線啟用 V2，replay 仍正規化為 V1 logical stream。
- Qt6 切換時網路協定與重播格式各提升一版。
- 舊客戶端及舊重播不相容；必須顯示「版本過舊」後停止解析，不得崩潰或誤讀。
- 新格式保留本項目現有重播時間軸、快照與接管功能。
- 新增伺服器能力宣告、無效詢問資訊、AI 除錯狀態及控制命令，使用明確型別及欄位驗證。

### 6.4 通用體驗

- 無效詢問跳過預設關閉；是否可回答由伺服器權威判定，客戶端不得自行執行 Lua 技能可用性判斷。
- 無懈可擊偏好保存在客戶端，只有伺服器宣告支援時啟用；摸牌或自己的回合開始時重置。
- 單機或只有一名真人、其餘皆 AI 的房間允許立即投降；多人房間保留原投票及限速規則。
- 移植長技能提示、手牌數提示、化身池查看、古惑宣告、帶標題選牌容器及唯一外部目標自動選擇，並重用本項目既有 presenter／PileContainer。

## 7. 通用缺陷修復

| 問題 | 計劃修正 |
|---|---|
| 變形裝備保存內部 `EquipCard *` | 保存穩定 WrappedCard／card ID，讀取時解析目前真實裝備；兼容多裝備槽 |
| 對方暗置手牌可被精確指定 | 伺服器從合法且不可見的候選中盲選；已知牌、自身牌及可見路徑保持精確選擇 |
| `CardsMoveOneTimeStruct` 平行陣列失配 | 在操作前正規化及驗證不變量，拒絕無法安全恢復的資料 |
| 有限武將池少於請求數 | 回傳實際可用數量並審核所有消費者，不以 `count - 1` 等可疑修補掩蓋問題 |
| 唯一目標自動選擇包含自己 | 優先唯一外部目標；只有沒有外部候選時才考慮自己 |
| 特殊玩法包滲入普通候選 | 由白名單遷移及 GameModeStruct 專用包規則統一處理 |

## 8. 完整 HiDPI 與 UI 批次

1. Qt 6 原生每螢幕 DPI、基礎幾何與多螢幕切換。
2. 文字超採樣／向量文字 helper 與快取失效規則。
3. `QSanTextButton`、角色印章、統一邊框、階段指示器及必要高解析度資產。
4. 自適應對話框、布局、主題及縮放設定。
5. 卡牌懸浮、容器、日誌、右側資訊欄及剩餘對齊問題。
6. 繪製快取、FPS 分項面板及性能回歸。

Qt 6 使用原生 DPI；應用程式 `UIScale` 範圍 1.0–2.0、步進 0.05、預設 1.0。主題可選系統／淺色／深色，新安裝預設跟隨系統。必須保留 Spine、GIF、重播、轉換卡牌與多技能 UI 行為。

Android 固定橫向顯示，處理安全區、Android 返回鍵、虛擬鍵盤及至少 48dp 的觸控目標；手機版不提供直向房間介面。

## 9. 診斷、日誌與 LuaAI 除錯

### 9.1 日誌與崩潰報告

M1 先完成 GUI／CMD 共用的 `Server::logMessage()` 與純文字 console 格式；以下 JSON Lines、輪替及保留策略屬後續診斷里程碑。

- 伺服器使用 JSON Lines 結構化日誌，包含 build/session/game ID。
- 日誌每日及 20 MiB 輪替，保留 14 日；密碼及權杖永不寫入。
- Windows 使用 DbgHelp 產生 minidump，涵蓋未處理例外、terminate、abort 及手動「產生卡死報告」。
- 報告只在本機產生，不自動上傳。預設只包含 dump、版本及去識別化摘要；日誌與增量重播必須由使用者主動勾選。
- PDB 以 build ID 建立獨立開發者符號包，不放入玩家包。
- Linux 無頭伺服器提供異常退出標記與 systemd/coredump 指引。

### 9.2 LuaAI 觀測式除錯器

- 提供 AI 手牌透視、身份傾向標籤、傾向差值浮字、斷點、單步、繼續、執行至回合結束及停用。
- 只有開啟作弊且房內沒有其他真人時才允許註冊及使用。
- 關閉時不得發送除錯封包、改變 AI 決策或寫入正常重播。
- AI 行為修復不在此階段實作，只寫入差異帳本。

## 10. 里程碑與合併門檻

M1 已完成（2026-08-09 對照 CMakeLists.txt 確認）：`qsanguosha_engine` STATIC、Qt module allowlist gate（非 Core/Network 立即 FATAL_ERROR）、`engine-smoke-test`、`deploy-server`、WHOLE_ARCHIVE 連結與 `qsanguosha_server.exe` 獨立入口均已落地。其餘里程碑仍為 **Not Started**。

| 里程碑 | 狀態 | 主要交付 | 合併門檻 |
|---|---|---|---|
| M0 | Not Started | 建立可重現的現況基線、測試清單與資產盤點 | 現有 Windows 行為、協定與重播樣本可重現；無功能性改動 |
| M1 | **Complete**（2026-08-09） | Windows CMake 過渡建置、STATIC engine（僅 Qt Core／Network）、引擎／GUI 解耦契約（SkillDialogInfo／EngineRuntimeContext／audioEffectRequested／EngineBootstrap）、allowlist gate、`deploy-server`、engine smoke test | Windows GUI 與既有建置結果可對照；STATIC engine 僅連結 Qt Core／Network；GUI／CMD server 驗收完成 |
| M2 | In Progress（2026-08-30：Lua 5.4.8 本地實作） | Qt 6.11.1、VS 2026 v145 + `msvc2022_64` kit；Lua 5.4.8、最小相容層、seeded state API 與 focused 測試已落地 | 外部四個低頻 Lua 問題修正；Windows GUI、server、Lua/SWIG 及遠端完整測試通過 |
| M3 | Not Started | `SkillDialogInfo`、選包白名單、確定性 RNG | 相同種子、輸入與包集合產生相同結果；白名單不可由客戶端繞過 |
| M4 | In Progress（V2 codec、negotiation、runtime activation complete；typed gameplay payload／replay migration pending） | 協定與重播版本化、相容性拒絕路徑 | 新舊版本差異可診斷；不支援版本被明確拒絕而非靜默誤讀 |
| M5 | Not Started | Ubuntu 無頭伺服器與 Null 音訊 | 無 X11/Wayland、FMOD 或 GUI 依賴仍可啟動及完成整局測試 |
| M6 | In Progress（2026-08-28：Linux GUI M2B-A 交付 `IAudioBackend`／FMOD／Qt／Null 三後端、`QSAN_AUDIO_BACKEND`、結構化診斷與 `--multimedia-smoke`） | 桌面 FMOD 後端抽象化及診斷 | Windows 音效行為無回歸；音訊失敗不影響遊戲狀態 |
| M7 | Not Started | Android Qt Multimedia、WAV/OGG 播放器池與觸控 UI | API 28 真機及 API 36 目標建置通過；前後景切換與音訊生命週期穩定 |
| M8 | Not Started | Android AAB、PAD 與 `AssetLocator` | `arm64-v8a` AAB 可安裝；fast-follow/on-demand 缺包、下載、重試路徑可驗證 |
| M9A | Not Started | 第一批 HiDPI、視窗縮放及安全區修復 | Windows 與 Android 代表解析度無截斷、重疊或不可操作控制項 |
| M9B | Not Started | 第二批完整 UI 適配、輸入與可及性 | 鍵鼠、觸控、字體縮放及方向切換矩陣通過 |
| M9C | Not Started | 診斷、崩潰報告及 LuaAI 除錯 | Release 可關閉敏感診斷；錯誤可由關聯 ID 定位 |
| M9D | Not Started | 通用缺陷修復、長時間穩定性及發佈候選 | 測試矩陣全綠、無阻斷級缺陷、回滾及版本說明齊備 |

每一里程碑合併前必須同時滿足：變更範圍可審查、相關 CTest 通過、協定／重播影響有明確判定、平台特有程式碼不滲入 `qsanguosha_engine`、文件與測試同步更新。不得以「後續補測」繞過門檻。

## 11. 測試矩陣與發佈驗收

| 類別 | Windows GUI | Ubuntu 無頭伺服器 | Android 客戶端 |
|---|---|---|---|
| 建置 | VS 2026 v145、Qt 6.11.1 `msvc2022_64`、Release/Debug | CMake、Qt 6.11.1、無 GUI／FMOD | API 28 裝置、target API 36、`arm64-v8a` AAB |
| 單元測試 | engine、RNG、序列化、資產定位 | engine、RNG、協定、房間生命週期 | engine 可移植子集、`AssetLocator`、播放器池 |
| 整合測試 | GUI 對局、重連、重播、FMOD | 建房、完整對局、斷線重連、Null 音訊 | 登入、對局、Qt Multimedia、PAD 下載與缺包處理 |
| 相容性 | 協定版本、重播版本、選包白名單 | 不相容客戶端拒絕與診斷 | 不相容伺服器提示、資產版本驗證 |
| UI／輸入 | 100%–300% DPI、多螢幕、鍵鼠 | 不適用 | 多密度、瀏海／安全區、直橫向、觸控返回鍵 |
| 穩定性 | 長局、反覆進出房間、音訊裝置切換 | 多房間、長時間運行、資源釋放 | 前後景切換、音訊焦點、低記憶體、網路切換 |

固定發佈門檻：所有阻斷級與高嚴重度缺陷關閉；代表性重播可重現；相同 RNG 種子結果一致；資產缺失有可理解提示；日誌不得洩漏密碼、權杖或私人資料；Windows、Ubuntu、Android 三條發佈管線均可由乾淨環境重建。

## 12. 固定假設與不在本計劃範圍

- 正式桌面客戶端先以 Windows 為唯一 GUI 交付平台；Ubuntu 本階段只交付無頭伺服器。
- Android 正式渠道固定為 Google Play AAB＋Play Asset Delivery，正式 ABI 固定為 `arm64-v8a`；其他商店、側載完整資產包及 32 位 ABI 不屬本階段。
- Android `minSdk` 固定為 API 28，`targetSdk` 固定為 API 36；若 Google Play 政策在實作時提高要求，只允許向上調整 target，不降低 minSdk。
- 不承諾舊客戶端與新協定永久互通；以明確版本協商、拒絕訊息及受控遷移為準。
- 不全面重寫引擎、技能或 LuaAI，不在現代化過程中順便更改卡牌平衡。
- 不修改 `include/`、`lib/` 內第三方庫；依賴升級須以外部套件、可重現建置或獨立導入流程處理。
- 本文件只定義總計畫；截至 2026-08-09，M1 已完成程式實作（STATIC engine、allowlist gate、deploy-server、smoke test），其餘里程碑狀態以上表為準。

## 13. 決策紀錄與官方參考

### 13.1 已鎖定決策

| 日期 | 決策 | 狀態 |
|---|---|---|
| 2026-07-25 | CMake、Qt 6.11.1、Lua 5.4.8、MSVC 2022 為現代化基線 | Superseded（編譯器見 2026-08-18） |
| 2026-08-18 | Windows 建置改 Visual Studio 2026 v145 + Qt 6.11.1 `msvc2022_64`（官方無 msvc2026 kit） | Approved |
| 2026-07-25 | Windows GUI、Ubuntu 無頭伺服器、Android 客戶端為三項正式交付 | Approved |
| 2026-07-25 | Android 使用 API 28/36、Google Play AAB＋PAD、正式 ABI `arm64-v8a` | Approved |
| 2026-07-25 | 音訊採 Windows FMOD、Android Qt Multimedia、無頭 Null 三後端 | Approved |
| 2026-07-25 | 「全面移除 FMOD」方案由上述雙實際後端加 Null 後端方案取代 | Superseded |
| 2026-07-25 | 約 1.09 GiB 音訊與約 1.27 GiB 圖片不得直接置入 Android base module | Approved |
| 2026-08-02 | M1 以 `qsanguosha_engine` 可獨立編譯且 Qt module 只有 Core／Network 為完成標準 | Complete（2026-08-09 落地） |
| 2026-08-02 | server 設定保留 `config.ini`／QSettings，不導入 YAML | Approved |
| 2026-08-02 | GUI `PC Console Start` 保留內嵌 server；另建可獨立部署的 `qsanguosha_server.exe` | Approved |
| 2026-08-02 | M1 採四個可建置批次，完整細節由引擎／GUI 解耦實作計畫規範 | Approved |

「全面移除 FMOD」不再是現行方向。FMOD 保留於 Windows 桌面；Android 不連結或載入 FMOD，改用 Qt Multimedia；Ubuntu 無頭伺服器使用 Null 後端。三者共用同一音訊介面，但各自封裝平台依賴。

### 13.2 官方文件

- [Qt 6.11 支援平台與組態](https://doc.qt.io/qt-6/supported-platforms.html)
- [Qt for Android](https://doc.qt.io/qt-6/android.html)
- [Qt Multimedia](https://doc.qt.io/qt-6/qtmultimedia-index.html)
- [Qt CMake 手冊](https://doc.qt.io/qt-6/cmake-manual.html)
- [Lua 5.4 參考手冊](https://www.lua.org/manual/5.4/)
- [FMOD Engine 文件](https://www.fmod.com/docs/)
- [Android API Level](https://developer.android.com/guide/topics/manifest/uses-sdk-element)
- [Android App Bundle](https://developer.android.com/guide/app-bundle)
- [Play Asset Delivery](https://developer.android.com/guide/playcore/asset-delivery)
- [Google Play 目標 API 要求](https://developer.android.com/google/play/requirements/target-sdk)
