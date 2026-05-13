在進行任何代碼修改前，你必須先讀取本規範並掃描相關的頭文件以確定對象關係。

# 太陽神三國殺時語版 (QSanguosha-v2 / Sijyu) AI Rules

## 1. 項目概覽 (Project Overview)
本項目是一個開源的三國殺遊戲引擎。
- **底層**: C++ 與 Qt 5.14 框架。
- **邏輯層**: 大量使用 Lua 腳本處理武將技能（動態加載）。
- **UI 層**: 使用 QML 與 Spine (skeletal animation) 處理特效與介面。
- **核心架構**: 客戶端-伺服器分離模式（即使是單機模式也會運行本地伺服器）。

## 2. 代碼規範 (Code Conventions)

### C++ 規範
- **類名**: 採用 `PascalCase`（如 `Room`, `ServerPlayer`）。
- **函數名/變數名**: 採用 `camelCase`（如 `addSocket`, `playerCount`）。
- **成員變數**: 通常帶有 `m_` 或 `_m_` 前綴（如 `m_players`, `_m_Id`）。
- **內存管理**: 嚴格遵循 Qt 的 **父子對象系統（Parent-Child System）**，優先依賴 `QObject` 的自動析構，避免手動調用 `delete` 除非明確不受 Qt 管理。

### Lua 規範
- **武將 ID**: 使用全拼或約定縮寫（如 `guojia`, `shenlvmeng`）。
- **技能命名**: 常見前綴包括 `nos` (舊版), `ol` (Online版), `tenyear` (十週年版)。
- **全局接口**: 必須通過 `sgs` 全局表與 C++ 層交互。

### Lua 與 C++ 的邊界
- **Engine (C++)**: 僅在涉及核心系統功能、全局 API 註冊、底層協議（Protocol）或渲染引擎變更時修改 `engine.cpp` 或 `src/core`。
- **Skills (Lua)**: 絕大多數武將技能、卡牌邏輯應在 Lua 的 `Package` 目錄下新增或修改。
- **Package (C++)**: 新增武將包時需在 `src/package` 下創建對應類並繼承 `Package`。

## 3. 併發與線程安全 (Concurrency & Thread Safety)
- **SafeLuaMutex**: 當 C++ 的 Room 執行線程需要訪問 Lua 狀態機時，必須持有 `SafeLuaMutex`。
- **RAII 助手**: 
    - 使用 `LuaLocker` 獲取 Lua 鎖。
    - 使用 `LuaUnlocker` 在執行可能導致阻塞的操作（如等待客戶端回應）時暫時釋放 Lua 鎖，以防止 UI 線程死鎖。

## 4. Do's & Don'ts

### Do's
- **分析依賴**: 在修改任何代碼前，先掃描相關頭文件以確定 `Player`, `Room`, `Engine` 之間的對象關係與數據流向。
- **保持風格**: 嚴格遵守現有的 Qt 對象導向風格。
- **確認上下文**: 如果無法確認本地檔案的上下文或代碼邏輯，**必須詢問使用者**，禁止憑空猜測。
- **最小化讀取**: 優先查看 `STRUCTURE.md` 以確定目標檔案。

### Don'ts
- **禁止隨意重構**: 嚴禁在未經明確授權的情況下，對現有穩定的核心邏輯（如 `RoomThread` 的事件循環）進行大規模重構。
- **禁止刪除註釋**: 嚴禁刪除現有的多語言（中/英）註釋，這些註釋對於後續開發與維護至關重要。
- **禁止盲目猜測**: 針對 AI 讀取檔案的限制，若無法定位到具體代碼，不可編造函數或類名。

---

*本規範為 AI 開發的最高準則，所有修改提議必須與此保持一致。*
