# 技能說明實例驗證 — 2026-09-20

[呈現、metadata 與更新契約](../skill-description-instances.md)。結果對應 `builds/skill-tooltip-validation/` 保存的來源、執行檔與外部 Lua 映射快照。

## 第一批驗證

| 檢查 | 目前狀態 |
|---|---|
| 定向靜態審查與 `git diff --check` | 通過；編譯與執行期結果分列如下 |
| `qsanguosha_core_tests --suite skill-description` | 2026-09-20 通過，退出碼 0、38.9 秒、未超時且程序已退出；涵蓋優先序、正文分組、持有者隔離、私有遮蔽、HTML 跳脫、來源、重設、移除、快照、不可見實例與死亡說明 |
| configure／core／GUI 編譯 | 2026-09-20 通過；CMake 4.3.1、VS 2026 v145、Qt 6.11.1，Debug 的 `qsanguosha_core_tests` 與 `QSanguosha` 均成功 |
| GUI hover、換將、斷線重連與清潔退出 | 未執行 |

本次 focused executable 設 60 秒硬上限並配置同版本 Qt Debug PATH，使用隔離的 user-data 目錄；未執行本地 CTest。記錄位於 `builds/skill-tooltip-validation/`：`configure.log`、`build.log`、`gui-build.log` 與 `focused-result.json`。

獨立 worktree 的執行期內容由乾淨、與 upstream 一致的外部 extensions `main` 複製 280 份受追蹤 Lua 腳本，逐檔 SHA-256 核對，來源版本及映射清單保留在上述記錄目錄。原 L／H 環境未修改。

GUI 首次建置因 `home_assets.qrc` 引用六個被 Git ignore 的 SVG 缺件而失敗；從原 L 工作區補入 qrc 明列的 sun／moon／home／generals／cards／replays 圖示，逐檔核對後只重建 GUI target，成功產出 `debug/QSanguosha.exe`。這些本地素材仍未納入 Git；乾淨 worktree 要重現建置須提供同等素材。編譯保留既有程式警告及 FreeType 缺少 PDB 的連結警告，沒有修改第三方庫。

## 第二批驗證

| 第二批驗證 | 結果 |
|---|---|
| `git diff --check` | 通過 |
| core／GUI 增量建置 | 通過；SWIG 綁定已重新產生並編譯。記錄：`second-final-build.log`、測試 fixture 修正後的 `second-fixture-build.log` |
| `qsanguosha_core_tests --suite skill-description` | 通過，退出碼 0、33.756 秒、未超時，程序已退出。記錄：`second-focused-retry1-result.json` 與對應 stdout／stderr |
| GUI | 新版已啟動供使用者人工驗收；不宣稱已完成 hover／對局驗收 |
| CTest／完整對局／跨平台 | 未執行 |

首次新增 server 情境測試缺少 AI 事件接收器，在既有 `RoomThread::dispatchTrigger` 的 `getSmartAI()->filterEvent` 發生空指標存取；只修正測試 fixture 的必要初始化，再重建 core target、重跑同一個 60 秒內 focused suite。沒有修改該引擎事件邏輯，亦没有把 BasicAI 測試接收器當作 SmartAI 對局驗收。首次失敗記錄、dump 與修正前本地 PDB 位址對應保留在 `second-focused-result.json`、`second-focused-crash.dmp`、`second-focused-symbol.txt`。

以上記錄均在 `builds/skill-tooltip-validation/`。


## GUI 回饋修訂（2026-09-20）

- 省略有效／失效狀態列，沿用灰字；相同正文有不同有效性時，僅將失效實例資料灰化。
- 單將省略主將／副將標籤；無實際 usage 計數與無 state 記錄時，不產生空白摘要。None usage 亦不列技術資料。
- 固定介面文案使用英文 Qt 翻譯鍵，中文置於 `builds/sanguosha.ts`；技能作者的可讀 state／效果 metadata 沿用技能翻譯表。
- 他人修改技能時，修改與已有來源列在該技能下；角色承受效果另列底部「目前受到的效果」，只列已記錄的來源、對象及結束條件，不推測舊技能未登記的來源。
- GUI hover 與互動採人工驗收。

修訂驗證：GUI／core 增量建置及 Qt 翻譯編譯通過；skill-description 專項測試退出碼 0，34.3 秒。TS XML／翻譯覆蓋／佔位符與 diff check 通過。未執行 CTest／完整對局，GUI 人工驗收待使用者確認。記錄見 builds/skill-tooltip-validation/gui-feedback-*。
