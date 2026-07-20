# 現代化改造與重構路線圖（Modernization Roadmap）

> 版本：2026-07-20 ｜ 狀態：規劃文件（不含代碼變更）
> 資料來源：2026-07 全倉靜態審計（實測統計，非估算）

---

## 1. 目的與範圍

本文件定義 `qsanguosha-sijyu` 的現代化改造順序、各階段邊界與驗證方式。

**硬性約束（不可違反）**：

| 項目 | 現況 | 說明 |
|------|------|------|
| C++ 標準 | C++17 | 禁用 C++20 語法 |
| Qt 版本 | Qt 5.x（README 記載 5.14.2，MSVC 2019 x64） | Qt 6 為長期方向，非近期目標 |
| Lua 版本 | 5.2.4（`src/lua/lua.h`） | 5.4 升級為長期方向 |
| 建置系統 | qmake（`QSanguosha.pro`） | CMake 為長期方向 |
| 架構原則 | 漸進式重構（Strangler Fig），禁止一次性重寫 | 以功能邊界分割，每批可獨立驗證 |

**校準方向**：所有近期決策必須朝「可支援多 UI、Linux Server、擴展管理、新協議」的架構邊界收斂，而非就地美化。

---

## 2. 技術債量化盤點

### 2.1 語法債（Qt 5 相容，可漸進清除）

| 模式 | 總次數 | 主要分布 Top 5 | 備註 |
|------|--------|----------------|------|
| `SIGNAL(` 宏 | 612 | `roomscene.cpp` 188、`customassigndialog.cpp` 76、`server.cpp` 58、`dashboard.cpp` 36、`generic-cardcontainer-ui.cpp` 32 | 與 `SLOT(` 合計 1,200 處舊式字串連接 |
| `SLOT(` 宏 | 588 | `roomscene.cpp` 183、`customassigndialog.cpp` 76、`server.cpp` 60、`generic-cardcontainer-ui.cpp` 32、`dashboard.cpp` 31 | 新式函數指標 connect 僅約 75 處（舊:新 ≈ 16:1） |
| `qrand(` | 449 | `tenyear2.cpp` 77、`mobile.cpp` 72、`ol.cpp` 53、`standard-generals.cpp` 27、`room.cpp` 20 | **Qt 6 已移除，硬性阻斷點** |
| `qsrand(` | 10 | 散布 8 檔（`engine.cpp`、`main.cpp`、`room.cpp` 等） | 多為時間種子 |
| `QRegExp` | 32 | `oracle_helper.cpp` 4、`client.cpp` 3、`generaloverview.cpp` 3、`roomscene.cpp` 3、`card.cpp` 3 | 對比 `QRegularExpression` 僅 1 處；Qt 6 阻斷點 |
| `foreach(` | 5,752 | `tenyear2.cpp` 782、`ol.cpp` 773、`mobile.cpp` 629、`room.cpp` 348、`tenyear.cpp` 271 | **已決議暫緩**（見 §10）；`Q_FOREACH` 為 0 |
| `NULL`（大寫） | 69 | `maotu.cpp` 30、`playercardbox.cpp` 8、`room.cpp` 8 | `nullptr` 已有 3,458 處；部分為 `time(NULL)` 非指標用法 |
| 舊式 include guard | 33/33 標頭 | `src/core`、`src/server` 全部 | 前導底線命名（如 `_ROOM_H`）屬保留識別字，UB 風險；`#pragma once` 0 處 |
| `qDebug(` | 42 | `server.cpp` 9、`roomscene.cpp` 9 | 規模小 |
| `toAscii`/`fromAscii` | 0 | — | 已無殘留 |

### 2.2 記憶體管理債

| 項目 | 數量 | 說明 |
|------|------|------|
| 原始 `new` | 8,738 處 | 全專案手工生命週期管理 |
| 智慧指標 | `std::unique_ptr` 12 處（全在 `SpineGlItem.h`/`SpineEffectWidget.h`）；`std::shared_ptr`/`QSharedPointer` 為 0 | 僅新 Spine 模組使用 |

> 結論：不適合全面導入智慧指標（QObject 父子樹已是主要生命週期機制），僅對「無 parent 的堆物件」個案處理。

### 2.3 架構債

| 障礙 | 數據 | 位置 |
|------|------|------|
| Room 上帝物件（God Object） | 約 380 行 public 方法宣告（askFor* 36、do*/request 27、notify/broadcast 23）、65–70 個成員變數、13 個職責群組 | `src/server/room.h`（834 行）、`room.cpp`（8,665 行） |
| RoomScene 熔合體 | 5,852 行、180+ 方法：view + 客戶端互動狀態機 + 協議字串解析 + 動畫播放器 | `src/ui/roomscene.h`（600 行）/`roomscene.cpp` |
| 全域單例呼叫點 | `Sanguosha->` 3,616 次、`Self` 3,257 次、`ClientInstance` 307 次、`RoomSceneInstance` 49 次 | `Self` 定義於 `src/client/clientplayer.h:62`；呼叫點深入 package 層 |
| 協議滲透層 | 109 個 `S_COMMAND_*`（`protocol.h:81-189`）+ QVariant body；Client 回調表硬綁約 70 個成員函式（`client.cpp:62-171`） | `socket.h` 僅抽象位元組傳輸，語義層無介面 |
| 伺服器端無全域單例 | `ServerInstance` 0 次；`Server` 為區域物件（`server.h:214`） | 好消息：Server 側解耦阻力較小 |

### 2.4 規模債（.cpp 行數 Top 10）

| # | 檔案 | 行數 | 類型 |
|---|------|------|------|
| 1 | `src/package/tenyear2.cpp` | 27,983 | 武將包 |
| 2 | `src/package/ol.cpp` | 27,684 | 武將包 |
| 3 | `src/package/mobile.cpp` | 19,547 | 武將包 |
| 4 | `src/server/room.cpp` | 8,665 | 伺服器核心 |
| 5 | `src/package/tenyear.cpp` | 8,045 | 武將包 |
| 6 | `src/package/tenyear-strengthen.cpp` | 6,937 | 武將包 |
| 7 | `src/ui/roomscene.cpp` | 5,852 | UI 核心 |
| 8 | `src/package/ol-strengthen.cpp` | 5,593 | 武將包 |
| 9 | `src/package/mobile-strengthen.cpp` | 4,487 | 武將包 |
| 10 | `src/package/maotu.cpp` | 4,428 | 武將包 |

> 規模熱點在武將包（前 3 名合計約 7.5 萬行）；核心邏輯檔規模中等但耦合密度最高。

### 2.5 基礎設施缺口

| 項目 | 現況 |
|------|------|
| 單元測試 | 無 QTest；僅 Lua Test Runner（`lua/test/`：runner + 10 模板 + 3 範例，見 `docs/lua-test-system.md`） |
| CI | 無 `.github/`、無任何 CI 設定 |
| QML 化 | `qml/` 目錄為空；`ui-script/` 35 個 QML 僅作特效浮層（`EmbeddedQmlLoader`，`roomscene.cpp:3781/5424/5489`），主 UI 為 QGraphicsView |
| Lua 規模 | `extensions/` 106 檔、`lua/` 180 檔（含 `lua/ai/` 156 檔） |

---

## 3. 既有重構範式（後續重構必須沿用）

| 範式 | 來源文件 | 手法 | 適用場景 |
|------|----------|------|----------|
| 值物件（Value Object）+ 註冊表（Registry）+ Lua DSL | `docs/GameModeStruct_重構說明.md` | `QString` → struct、Engine 註冊 API、`createMode{}` | 協議資料型別化 |
| 薄 Presenter + 協議隔離 + 三階段漸進 | `docs/guhuo-dialog-refactor.md`、`docs/guhuo-juguan-presenter-impact.md` | ①抽離邏輯為可重用 API → ②薄 presenter → ③穩定後才換 UI 載體；AI 依賴 room tag 協議故不受 UI 重構波及 | 一切 UI 層重構 |
| Headless 測試鉤子 | `docs/lua-test-system.md` | `--lua-test` + `Room::registerTestOverride()` 攔截 6 個 askFor* | 重構期回歸保護 |

---

## 4. Phase 0：回歸保護網（先行）

| 項目 | 內容 | 驗證方式 |
|------|------|----------|
| 擴大 Lua Test Runner 覆蓋 | 以 `lua/test/templates/` 10 個事件模板，為高風險技能鏈（ViewAs、傷害結算、翻面/座次）補測試 | `QSanguosha.exe --lua-test <script>` 全綠 |
| Lua 語法檢查 | `luac -p` 批次掃 `extensions/`（106 檔）與 `lua/`（180 檔），產出錯誤清單 | 錯誤清單歸零或列為已知 |
| 靜態分析基線 | clang-tidy `modernize-*` 唯讀掃描出報告（不強制修），建立債務基線供後續階段對照 | 報告存檔 |

> 執行前提：回家後有工具鏈的環境（工作機無 Qt/編譯器/Lua CLI，見 `memory.md [State]`）。

---

## 5. Phase 1：語法層現代化

> 原則：Qt 5.14 + C++17 全相容、語義等價、每項獨立批次提交。

| 順序 | 項目 | 量 | 手法 | 風險與注意 |
|------|------|-----|------|------------|
| 1 | `qrand` → `QRandomGenerator` | 449 + `qsrand` 10 | `qrand() % n` → `QRandomGenerator::global()->bounded(n)`；種子處改 `seed()` 或刪除（依賴全域隨機即可） | 低；package 層隨機分佈行為需保持一致，分批後跑 Lua Test |
| 2 | `QRegExp` → `QRegularExpression` | 32 | 逐處改寫（API 非完全對應：`indexIn`→`match().capturedStart()` 等） | 低；逐檔處理 |
| 3 | `SIGNAL`/`SLOT` → 函數指標 connect | 1,200 | 分批順序：`server.cpp`（118）→ dialog 層（`customassigndialog.cpp` 152 等）→ UI 層（`dashboard.cpp`）→ `roomscene.cpp`（371）最後；overload 用 `QOverload<>::of()` | 中；字串連接具執行期動態性，個別依賴字串處需人工判斷；每批編譯驗證 |
| 4 | `NULL` → `nullptr` | 69 | 機械替換；`time(NULL)` 等非指標語境保留或改 `nullptr` 均可 | 極低 |
| 5 | include guard 去前導底線 | 33 標頭 | `_ROOM_H` → `ROOM_H` 式改名，或換 `#pragma once`（MSVC/MinGW 皆支援） | 極低；需全檔搜尋 guard 名無其他引用 |
| 6 | `qDebug(` 清理 | 42 | 除錯殘留移除或改條件式日誌 | 極低 |
| — | `foreach` → range-for | 5,752 | **暫緩**：Qt 5 下純風格變更、量大且容器分離（detach）語義差異易引入 bug；留待 Qt 6 前置批次處理 | 見 §10 決策記錄 |

**完成標誌**：Qt 6 遷移硬性阻斷點（`qrand`/`QRegExp`）清除；信號槽連接具編譯期型別檢查。

---

## 6. Phase 2：架構邊界建立

> 四步有依賴順序，不可顛倒。每步完成後 Room/RoomScene 應「變薄」而非「換皮」。

### 步驟 ①：協議語義介面層（Protocol Semantic Interface）

| 項目 | 內容 |
|------|------|
| 現況 | Room 直接以 `doNotify`/`doRequest`/broadcast 家族（約 50 方法）編碼 `CommandType`+QVariant 封包；Client 以 `m_callbacks` 回調表硬綁約 70 成員函式（`client.cpp:62-171`）；`socket.h` 只抽象位元組傳輸 |
| 動作 | 抽出 `IRoomChannel` 介面承接 Room 的通訊面；封包編碼集中為 `ProtocolCodec`；Client 回調表改為介面註冊 |
| 收益 | 換 WebSocket/Protobuf 只動 codec；Linux headless server 不拖 UI 型別；為 §8 長期協議鋪路 |
| 風險 | 109 個命令的 body 仍是 QVariant，本步驟不做型別化（見 §10 取捨） |

### 步驟 ②：Client 狀態層（View-Model 抽離）

| 項目 | 內容 |
|------|------|
| 現況 | `updateStatus`、目標選擇、ViewAs 啟動、對話框流程全長在 `RoomScene`（QGraphicsScene 子類）內，直接讀寫 `Self`/`ClientPlayer` |
| 動作 | 仿 Juguan Presenter 範式：拆出 `ClientSession` 承接互動狀態機；RoomScene 退化為純 view（只收狀態、發意圖） |
| 收益 | Qt Quick / 第二套 UI 的前提；`Self` 依賴收斂至 session 層 |
| 前置 | 步驟 ① 完成（session 層消費的是介面而非 Room 直接呼叫） |

### 步驟 ③：Room 職責拆分

| 項目 | 內容 |
|------|------|
| 動作 | 13 個職責群組抽 collaborator：`CardMoveService`（moveCardsAtomic/drawCards 等）、`SkillManager`（acquireSkill/detach 等）、`GameFlowEngine`（useCard/damage/judge 等）；Room 保留協調與狀態持有 |
| 收益 | `room.cpp`（8,665 行）自然縮小；測試可針對 collaborator 單獨做 |
| 注意 | SWIG 綁定（`sanguosha.i` 85KB + `luaskills.i` 102KB）與 Room 公開 API 強耦合，拆方法時須保持 Lua 可見面不變或同步更新 .i 檔 |

### 步驟 ④：package 層全域依賴收斂

| 項目 | 內容 |
|------|------|
| 現況 | `Sanguosha->` 3,616 次、`Self` 3,257 次，呼叫點深入 7.5 萬行武將包 |
| 動作 | 提供 compat shim 存取器；新碼禁止新增 `Sanguosha->`/`Self` 直用（以 lint 規則/審查收斂）；舊碼漸進遷移 |
| 原則 | 7,000 呼叫點不可一次遷移，只收斂增量、不強追存量 |

---

## 7. Phase 3：巨型檔案分割

> 純移動、不改邏輯；同 namespace、同註冊流程；每檔分割為獨立批次。

| 檔案 | 行數 | 分割策略 |
|------|------|----------|
| `src/package/tenyear2.cpp` | 27,983 | 按武將群/技能系別切 4–6 檔 |
| `src/package/ol.cpp` | 27,684 | 同上 |
| `src/package/mobile.cpp` | 19,547 | 同上 |
| `src/server/room.cpp` | 8,665 | 不單獨切，隨 Phase 2 抽離自然縮小 |
| `src/ui/roomscene.cpp` | 5,852 | 隨 Phase 2-② 退化後，殘餘按對話框/動畫/回放切 |

**分割檢查清單**：標頭依賴閉合 → `QSanguosha.pro` 的 `SOURCES` 同步 → 全編譯 → Lua Test 回歸。

---

## 8. Phase 4：長期方向（僅錨定，不展開）

| 方向 | 與近期階段的銜接點 |
|------|--------------------|
| Qt 6 / Lua 5.4 / CMake | Phase 1 清阻斷點；foreach 批次為 Qt 6 前置 |
| Linux headless Server | Phase 2-① 協議介面層 |
| Protobuf / WebSocket / WASM | Phase 2-① codec 集中後僅換實作 |
| FMOD → Qt Multimedia | 獨立項目；`CONFIG(audio)` 已隔離於 `QSanguosha.pro` |
| Lua Hot Reload | 需先穩定 `lua-wrapper` 生命週期 |
| LLM AI / Skill、擴展市集、大廳/登入 | 依賴 Phase 2 全部邊界 |

---

## 9. 執行規範

| 規則 | 內容 |
|------|------|
| 批次原則 | **小批次獨立提交**：每個子項（如「qrand 替換-tenyear2」）一批，人工審查後再下一批（無 CI 環境下的安全策略） |
| 分支策略 | 每子項一 feature 分支，自 `main` 切出 |
| Commit 風格 | 英文訊息，`refactor:`/`chore:` 前綴（對齊專案慣例） |
| 回歸要求 | 涉及遊戲邏輯的批次，提交前跑對應 Lua Test；純語法批次至少全編譯通過 |
| SWIG 同步 | 凡動到 `src/core`、`src/server` 公開 API，同批更新 `swig/*.i` 並重新生成 `sanguosha_wrap.cxx` |
| 文件同步 | 動到 `docs/` 已記錄的系統時，同批更新對應文件 |
| 記憶同步 | 每批完成後更新 `memory.md`（觸發器見 `AGENTS.md` §5） |

---

## 10. 取捨記錄（Decision Log）

| 日期 | 決策 | 選項 | 結論與理由 |
|------|------|------|------------|
| 2026-07-20 | `foreach` 5,752 處處理時機 | A. 現在全改 / B. 暫緩至 Qt 6 前置 | **B**：Qt 5 下 foreach 可用；全改為一次巨量 diff，且 Qt 容器分離（detach）語義差異有迭代器失效風險；留待 Qt 6 前置批次一次清 |
| 2026-07-20 | 協議 body 型別化程度 | A. 維持 QVariant body + codec 層 / B. 直接引入型別化訊息 struct | **A**：109 個命令全動的風險不可控；先集中編碼面，型別化留作後續獨立評估 |
| 2026-07-20 | 工作節奏 | A. 小批次獨立提交 / B. 一次做完一個 Phase | **A**：專案無 CI，批次越小越容易人工審查與回溯 |
| 2026-07-20 | 智慧指標導入 | 全面導入 / 個案處理 | **個案**：QObject 父子樹已是主要生命週期機制，僅對無 parent 堆物件處理 |
