# Isolated AI 遷移狀態表

> 2026-09-21 本輪共用層實作已補裝備／牌族策略、選擇與回應分派、索引與規劃快取、
> 身份／模式 hook 的唯讀查詢與原子更新；後續同批補入空城／血量／贈牌／拆牌／改判、
> 舊 table hook ABI 與 askFor 預設。isolated 是獨立新架構，原版 SmartAI 只作行為參考；
> 安全 fallback 只是故障保底，觸發即代表 isolated 驗收失敗。細節與尚未完成的語意見
> [共用層對照](isolated-ai-common-layer.md)。新增 `--suite ai-common` 契約來源；
> **2026-09-21 後續授權檢查點：SWIG 重新生成與 Debug engine／server／runtime runner 編譯通過；
> 完整 `--suite ai-common` exit 0（112.0 秒），`--suite card-lifetime` exit 0（407.3 秒）。**
> 命令、修正與日誌見 `builds/ai-common-completion-report.md`。未執行 CTest、GUI 或完整對局；
> 契約通過不代表任意武將 parity。以下各 PR 保留各自歷史驗證，不由本檢查點追認全部階段。

> **2026-09-22 fallback 政策變更：isolated 優先，答不出就落回 SmartAI。** `RoomRuntime::initialize()`
> 原本只在 `requiresLegacyRuntime()`（有明確 legacy 路由設定）時才載 `smart-ai.lua`，預設設定下
> 從不成立，isolated 拒答一律落到 `TrustAI`。現改為 `Config.EnableAI` 即載入，每個 Room 初始化約
> +250～570ms；路由不變，`routeFor()` 仍預設 `AiRouteIsolated`。對應測試改為
> `defaultRoomLoadsSmartAiFallbackAndStillRoutesIsolated`（ai-common case 46）。

> 下文「整題交回 legacy」「不是缺陷」等是舊階段的歷史決策，已被上述自主決策目標取代。
> 新的候選選擇可提交已知且完整授權的合法方案；其他未知選項仍記錄缺口。
> 只有全部相關選項已處理且沒有可行方案，才可回 pass，不能以保底補足新版覆蓋率。

依 `isolated-ai-migration-plan.md` 的階段劃分記錄實況。**本表只記已驗證的事實**：欄位寫「已建置／已跑測試」時，同一列必須指得出命令與結果；沒跑就寫沒跑。

- 主倉庫：本輪開始為 `debug` @ `4e6b36a`，結尾核對已由其他 session 移至 `9ac9ed6`；下列 PR 02–06 記錄保留各自當時基準。
- 記錄日期：2026-09-19（PR 02–04）、2026-09-20（PR 05–06）、2026-09-21（PR 07 停止驗證與靜態收尾）。
- 建置／測試環境：Windows x64、MSVC v145、Qt 6.11.1 `msvc2022_64`、`builds/cmake-vs2026`

> **2026-09-20 的既有阻斷已解除，`--suite room-runtime` 回 exit 0。** 見下面
> 「2026-09-20 的既有阻斷：如何解掉的」。
>
> 同時修正本文件先前的一項歸屬錯誤：那兩處改動當時被記成「工作樹裡未提交的引擎改動」，
> 實際上它們在 2026-09-20 03:26 以 `fc1c09e`（`feat(large-room): add 50-player mode and
> native resolution UI`）提交進 `debug`，`5020eba` 又跟著清理了 `setFlags` 的寫法。
> 也就是說它們不是「別人正在改的東西」，而是**已經是 `debug` 的基準**。上一輪記錄時
> `debug` 還停在 `2549934`，所以那個判斷在當下並沒有錯，但現在必須照實更新。

---

## 階段狀態

| 階段 | 狀態 | 依據 |
|---|---|---|
| P0 來源／版本／可重現建置 | **大致完成** | 清單、守門、載入政策與上游同步都已落地並實跑驗證。剩上游 ref 未釘版（政策決定）。 |
| P1 作者 API／集合／回傳語意 | **完成（PR 02）** | 未覆蓋訊號、四種回傳狀態、`aiUseCard`／`ai_card_use`／`AIUsePlan`、`self:sort`。純 Lua 契約實跑通過，破壞驗證 exit 30。 |
| P2 候選資料完整性 | **部分完成（PR 03＋06）** | Play／Response 的 `available`、`maxVotes` 照原生語意、`completeCoverage` 實測、AI 選的目標一律重驗。破壞驗證 exit 31／32。candidate_id 授權券（計畫 §5.4）已於 PR 06 補上。PR 07 已實作有界完整有序組合（§5.2 第 2 點），最終來源尚未驗收，見文末。 |
| P3 `aiUseCard` | **部分完成（PR 04＋05）** | PR 04：實體 Slash＋Peach 垂直路徑、單一目標演算法、全覆蓋才作答（破壞驗證 exit 33）。PR 05：Duel＋順手牽羊／過河拆橋，以及已知張數與估計張數的分離（破壞驗證 exit 34）。**未做**：裝備與其餘牌族、多目標牌族、每一族的技能互動邊界（見 PR 05 的能力清單）。 |
| P4 能力券／技能實例／時間推演 | **部分完成（PR 06）** | 轉化候選由權威端自己造牌後發票，`card_spec` 只能靠票授權，作者寫的每一欄都與權威端紀錄比對；instance identity、借用來源與配額在作答當下重驗。候選票（§5.4）一併補上。破壞驗證 exit 35。PR 07 已實作受限 n ≥ 2 參數票、§7.3 分支 scratch、巢狀上下文與多目標投影，最終來源尚未驗收；§7.4 跨 request 意圖未做（PR 06 舊清單是歷史範圍）。 |
| P5 代表性 extensions 遷移 | 未開始 | — |
| P6 Shadow／效能／分階啟用 | 未開始（原記「不適用」有誤） | `2549934` 移除的是 shadow auditing 的實作，不等於 §9 的驗收工作已完成：對局、效能基線、分批切換與 rollback 都還沒做，也沒有任何量測數字。 |

---

## P0 明細

| 工作 | 狀態 | 依據 |
|---|---|---|
| A. 盤點實際載入鏈 | **完成** | `docs/ai-runtime-manifest.json`：14 個檔案、4 階段載入鏈、5 條部署路徑。 |
| B. 固定來源歸屬 | **完成（歸屬）／未完成（釘版）** | 唯一上游是 `lolosiyue/extensions`，由 `tools/ci/fetch-extensions.{sh,ps1}` 取得。隔離 runtime 已同步上去（`4aeaf95`），乾淨 fetch 可開機；但 ref 仍是浮動的 `main`。 |
| C. 消除重複權威 | **完成** | 本工作樹沒有第二份 canonical source（見下「計畫前提修正」）。唯一另一份副本是已被 ignore 的測試產物，已記入 manifest。 |
| D. 執行基線驗證 | **完成** | 下節有實際命令與輸出；另對真實上游實跑過 fetcher。 |

### 已修的缺口

| 編號 | 問題 | 修法 |
|---|---|---|
| 1 | `routeFor()` 預設 `AiRouteIsolated`，但 `QSAN_ASSET_REQUIRED` 只驗 `lua/ai/smart-ai.lua`；缺 isolated 檔案的套件裝得乾淨，開房才死在 `AiLuaRuntime::initialize()` | `CMakeLists.txt` required 清單補進 bootstrap／facades／`ai_isolated_core` 三支／`mode-ai.lua` |
| 2 | Android 打包同一個洞 | `cmake/QSanguoshaAndroidAssets.cmake` 的 FATAL_ERROR 清單改為迴圈，涵蓋同一組檔案 |
| 3 | 兩支 fetcher 與其 contract test 只守 `isolated-bootstrap`／`isolated-facades`／`isolated/ask-for-use-card`，漏掉 `ask-for-choice`／`decision-core`。上游改名其中任一支，fetch 會通過，然後每個 Room 都沒有 AI | 兩支 fetcher 改成迴圈守 `ai_isolated_core` 全部；套件 handler 不守 |
| 4 | 沒有任何東西讓上述清單保持同步 | 新增 `tools/ai/check-ai-runtime-manifest.py` 與 CTest `qsanguosha_ai_runtime_manifest_check`，比對 manifest／`ai_isolated_core`／磁碟內容／兩支 fetcher／CMake required 清單，並拒絕 C++ 再度出現腳本清單與「套件 handler 被標成 required」 |

### 未修、已記錄的缺口

| 編號 | 問題 | 為何留著 |
|---|---|---|
| 5 | **上游 ref 未釘版。** `QSAN_EXTENSIONS_REF` 預設 `main`；CI 把解析出的 commit 寫進 `GITHUB_ENV` 就沒再驗證。兩次相隔一天的建置可以拿到不同 AI | 現在已有可釘的對象：`4aeaf953`（同步後，實跑 fetcher 驗證可開機）。但「誰負責升級、升級時怎麼驗」是政策決定，未經指示不改 CI 行為 |
| 6 | ~~`scarlet-ai.lua` 是死碼~~ **已修，見「載入政策改由 Lua 決定」** | — |
| 7 | **`mode_policy` 是 gameplay VM 算的，不是 isolated VM。** `ai-decision-coordinator.cpp:498` 走 `roomRuntime()->lua()`，而 `mode-ai.lua` 只由 `smart-ai.lua` 的 `dofile` 進來。缺檔時 `evaluateModePolicy` 靜默退化成「自己是自己唯一的朋友」，沒有任何錯誤 | 這是 isolated runtime 唯一的跨 VM 依賴。已把 `mode-ai.lua` 列入 required 讓缺檔會被擋下，但「靜默退化」這個行為本身要改成可觀測的失敗，屬 P2 的候選資料完整性 |
| 8 | 兩支 fetcher 的 isolated 複製深度不同：`.sh` 用 `find … --parents` 遞迴，`.ps1` 用 `Copy-Item ai\isolated\*.lua` 只一層。未來若上游新增 `ai/isolated/<subdir>/`，Windows 會靜默漏掉 | 目前上游沒有這種巢狀目錄，不是現存缺陷。已記入 manifest 的 `deployment_paths` |

### 載入政策改由 Lua 決定（不再寫在 C++）

原先 `AiLuaRuntime::loadConfiguredScripts()` 裡有一份硬編碼的 `defaultScripts`，等於由 host 決定要載哪些 AI 檔。已改成與 `smart-ai.lua` 相同的作法：

| 層 | 誰決定 | 對應 smart-ai 的哪一段 |
|---|---|---|
| 進入點 | C++ 固定載 `isolated-bootstrap.lua`、`isolated-facades.lua` | `room-runtime.cpp:511` 固定載 `smart-ai.lua` |
| 核心 dispatcher | **`isolated-bootstrap.lua` 的 `ai_isolated_core`** | `smart-ai.lua` 開頭那幾行 `dofile` |
| 套件 handler | **啟用的套件 × `lua/ai/isolated/<套件名>-ai.lua` 是否存在** | `sgs.GetFileNames("lua/ai")` 配 `getExtensions()` 找 `<package>-ai.lua` |

sandbox 拿掉了 `dofile`／`loadfile`／`sgs.Sanguosha`，所以 isolated VM 不可能自己開檔或列目錄；**開檔動作**必須由 host 代勞，但**清單的權威**已經搬回 Lua 這邊。比對規則與 smart-ai 一致：套件名與檔名大小寫不一致時以小寫比對、載入時用磁碟上的真實檔名；單一套件 handler 壞掉只讓該套件失去隔離 AI（記 `qWarning`），不會讓整個 runtime 起不來——等同 smart-ai 對每支套件 AI 的 `pcall`。

`AiIsolatedScripts` 保留為顯式覆寫（含空清單），隔離測試靠它立起只有一支或零支腳本的 runtime。

結果：`scarlet-ai.lua` 不再是死碼——只要 scarlet 套件啟用它就載入，跟 C++ 清單和本機 `config.ini` 都無關。`standard-ai.lua` 同理從「host 點名的必要腳本」變成「standard 套件的 handler」，因此從 `QSAN_ASSET_REQUIRED`、Android gate 與兩支 fetcher 的守門清單中移除：某個套件還沒有隔離 AI 是正常狀態，不該變成部署阻斷。

### 上游同步（已完成）

比對 `lolosiyue/extensions` main 與本機 `lua/ai/` 後發現：**整套隔離 AI runtime 是只存在於這台機器的未推送工作。** 同步前的上游狀態（`4fc9fe5434175ddbef9a245b0e1ce249577e9191`，與計畫 §1.1 記的 commit 相同）：

| 檔案 | 同步前上游 | 本機 | 後果 |
|---|---|---|---|
| `isolated/ask-for-choice.lua` | **沒有** | 有 | 在 `ai_isolated_core` 裡 → 乾淨環境 `initialize()` 失敗 |
| `isolated/decision-core.lua` | **沒有** | 有 | 同上 |
| `isolated/scarlet-ai.lua` | **沒有** | 有 | scarlet 套件在別處沒有隔離 AI |
| `isolated-bootstrap.lua` | 26 行 | 151 行 | 上游版沒有 `ai_memory`／`ai_coverage`／`ai_decide`／`ai_isolated_core`（139 行實質差異） |
| `isolated-facades.lua` | 322 行 | 1341 行 | 上游版沒有本機這套 facade（1051 行實質差異） |
| `isolated/ask-for-use-card.lua` | 38 行 | 105 行 | 上游版沒有雙 ABI registry（99 行實質差異） |
| `mode-ai.lua` | 426 行 | 426 行 | **內容相同**，只差行尾（上游 CRLF、本機 LF） |
| `isolated/standard-ai.lua`、`pass-ai.lua`、`instruction-limit-test.lua` | 有 | 有 | 內容相同 |

在任何其他環境跑 fetcher，拿到的是 26 行的 bootstrap 與 4 支 isolated 檔：`ai_isolated_core` 不存在、`ask-for-choice.lua` 與 `decision-core.lua` 不存在，`AiLuaRuntime::initialize()` 必然失敗，而 `routeFor()` 預設走 isolated，等於沒有 AI。

**這個坑之所以能存在這麼久，正是因為 `lua/ai/` 被 gitignore：** 本機改動不會出現在 `git status`，而 fetcher 只複製不刪除，所以做出這些檔案的那台機器一直是好的，其他環境一直是壞的，兩邊都不會報錯。

**已於 2026-09-19 同步。** 六支隔離 runtime 檔案（上表前六列）已推進 `lolosiyue/extensions` main：

```
4aeaf95 feat(ai): sync the isolated AI runtime from the engine worktree
        6 files changed, 1739 insertions(+), 33 deletions(-)
```

行尾統一為 CRLF，與 `ai/` 其餘檔案一致；`mode-ai.lua` 內容本來就相同，未動。

驗證（實跑）：

```
bash tools/ci/fetch-extensions.sh <乾淨目錄>
[fetch-extensions] ok: lua/ai=171, extensions=102, lua=1 (ref=main commit=4aeaf9530af7e5e88df2071987905973beb6e81a)
```

exit 0 —— 這支 fetcher 帶著本次新增的核心守門，對真實上游跑過，代表 CI 的 fetch 步驟現在會過。fetch 出來的 `lua/ai/isolated/` 有全部 7 支檔案，`isolated-bootstrap.lua` 也帶著 `ai_isolated_core`。

**`value-boundary.lua` 也已同步**（`724980c`）：推上 `ai/value-boundary.lua`，並在上游 `smart-ai.lua` 的 `dofile("lua/ai/mode-ai.lua")` 之後補上 `dofile("lua/ai/value-boundary.lua")`——只加這一行，`"lua/ai/"..sl` 那段維持原樣，fetcher 的大小寫修補照常生效。本機那行不會再被下次 fetch 洗掉。

**仍未同步**：`hayate-ai.lua`（本機已改）與 `lolihime-ai.lua`（上游沒有）屬未提交的擴充包工作，與隔離 AI 無關。

### 計畫前提修正

計畫 §1.2 與 §14 的兩個前提在本工作樹不成立：

1. **沒有第二個 `extensions` 工作樹。** `extensions/` 與 `lua/ai/` 都在主倉庫路徑下，且都被 `.gitignore:11-12` 排除，`git ls-files` 各為 0 檔。計畫假設的 `extensions` 倉庫 `ai/isolated/` 目錄（[S6][S7]）不是本地的一份 checkout，而是 fetcher 的遠端來源。因此 P0-B／P0-C 沒有兩份權威可以協調。
2. **`lua/ai/isolated/` 的內容比計畫描述的多。** 除計畫列出的 ask-for-use-card／standard-ai／pass-ai／instruction-limit-test 之外，還有 `ask-for-choice.lua`、`decision-core.lua`、`scarlet-ai.lua`。

另外修正計畫 §3-A 的一項歸屬：**`value-boundary.lua` 不在 isolated VM 裡**。它只由 legacy `smart-ai.lua` 的 `dofile` 與 `tests/lua/value-boundary-contract.lua` 載入。

---

## 基線驗證（實際執行）

`lua/ai/` 未納版控，所以「乾淨環境可取得同樣版本」目前的實況是：跑一次 fetcher 才有 AI，而**跑出來的東西開不了機**（見上節）。本機 `lua/ai/smart-ai.lua` 帶著 fetcher 的大小寫修補（含 2 處 `"lua/ai/"..ai_file`、0 處 `"lua/ai/"..sl`），SHA-256 `7BFB480DE8354354D00D628AE6E0CFC88E691333B56BC16BF0A0C19F1C549ED4`，與 `docs/lua-ai-spec.md` 記錄的值一致；但它**不只是** fetcher 的產物，還多一行上游沒有的 `dofile("lua/ai/value-boundary.lua")`。

### 建置

```
cmake --build builds/cmake-vs2026 --config Debug --target qsanguosha_runtime_tests --parallel 8
```

結果：exit 0。

### 測試

```
PATH=H:\Qt6111\6.11.1\msvc2022_64\bin;%PATH%
builds\cmake-vs2026\tests\Debug\qsanguosha_runtime_tests.exe --suite runtime-contract --seed 2026082201
```

結果：exit 0，`TOTAL: 11 / PASS: 11 / FAIL: 0`。逐項：

```
PASS lua-runtime          PASS room-runtime           PASS room-lua-teardown
PASS lua-exception-unwind PASS initial-room-close     PASS initialization-handoff
PASS turn-reclaim         PASS wrapped-adoption       PASS card-lifetime
PASS card-lifetime-lua    PASS synthetic-30
```

`room-runtime` 即 `tests/room-runtime-isolation-test.cpp`，涵蓋 isolated runtime 的載入、路由、額度與 Lua 契約。

### 新增的來源檢查

```
python tools/ai/check-ai-runtime-manifest.py --root .
ai-runtime-manifest: files=14 required=7 core=3 package_handlers=2; the Lua declaration, the fetch gates and the asset manifest agree
```

exit 0。負向驗證（在複製的樹上做，各自都如預期 exit 1）：

| 破壞方式 | 檢查是否擋下 |
|---|---|
| 刪掉 `lua/ai/isolated/decision-core.lua` | 是，並指出跑哪支 fetcher |
| 放一支未登記的 `lua/ai/isolated/wind-ai.lua` | 是 |
| 改掉 `ai_isolated_core` 其中一項 | 是，並印出兩邊清單 |

### fetcher contract test

```
python tools/autotest/tests/test_fetch_extensions_contract.py
FETCH_EXTENSIONS_RESULT PASS
```

exit 0。這支會建一個 fixture git 倉庫並**實跑** `tools/ci/fetch-extensions.sh`。改守門前它先紅（`lua/ai is incomplete: isolated/ask-for-choice.lua is missing after fetch`），因為舊 fixture 只有一支 isolated 腳本、不足以讓引擎開機；補齊 fixture 後轉綠。

### 載入政策的測試（實跑）

`tests/room-runtime-isolation-test.cpp` 新增兩例，都在 `room-runtime` suite 內：

- `isolatedScriptsComeFromLuaAndEnabledPackages`（失敗碼 28）：移走 `AiIsolatedScripts` 後，確認 `ai_isolated_core` 被 Lua 宣告且三支核心都載入，並確認 `ai_skill_use["@@lianying"]` 存在——`standard-ai.lua` 沒有被任何 C++ 清單點名，它是因為 standard 套件啟用、檔案在磁碟上才載入的。
- `aBrokenPackageHandlerDoesNotStopTheRuntime`（失敗碼 29）：挑一個啟用但還沒有隔離 handler 的套件，暫時寫入一支語法錯誤的 `<套件名>-ai.lua`，確認 `initialize()` 仍成功、dispatcher 仍在，跑完刪除該檔。

證明這兩例真的有跑而不是被跳過：把 `isolated-bootstrap.lua` 的 `ai_isolated_core` 改名後重跑（不需重新編譯），`--suite room-runtime` 回 **exit 28**，即第一例的失敗碼；還原後回 exit 0（`room runtime isolation passed`）。第二例的效果也直接可見於輸出：

```
isolated AI script load failed: "standard_cards-ai.lua" "lua/ai/isolated/standard_cards-ai.lua:1: syntax error near 'is'"
room runtime isolation passed
```

### 未執行

- 未跑完整 CTest（只跑 `runtime-contract` 一個 suite）。
- 未在 Linux／Docker／Android 上驗證本次的 CMake 改動；只在 Windows 重新 configure 成功。
- `tools/ci/fetch-extensions.ps1` 的改動只有靜態檢查，沒有實跑（實跑會改寫工作樹的 `lua/ai/`）。`.sh` 那支由下面的 contract test 對 fixture 倉庫實跑過。
- 未做效能量測。

---

## P1–P3 明細（PR 02／03／04／05）

記錄日期：2026-09-19（PR 02–04）、2026-09-20（PR 05）。以下每一項都指得出命令與結果；破壞驗證是指「把該項改壞後重跑，確認測試真的紅」，用來證明測試沒有被跳過。

### PR 02（P1）：作者 API 與未覆蓋傳遞

計畫 §4 的核心是「未覆蓋不能被內層吞成 declined」。舊作者寫 `if use.card then ... end`，所以不支援時若只回 `card=nil`，會被讀成「AI 決定不出牌」。

| 項目 | 落點 |
|---|---|
| 受控未覆蓋訊號 | `isolated-facades.lua` 的 `AIUnsupported`：有標記的表當 error 值，不用字串，dispatcher 才分得出「接不住」與「handler 壞了」。`ai_unsupported()` 丟出，`AIUnsupported.capture()` 接手且**只接**這一種，真正的錯誤照樣往上丟 |
| 第四種回傳狀態 | `AIResultValue.normalize` 加 `"unsupported"`，與 `unhandled`／`declined`／`pass` 並列；回傳訊號與丟出訊號走同一個出口 |
| 落地點只有一個 | `isolated-bootstrap.lua` 的 `ai_decide`：未覆蓋 → 回 nil（整個 callback 退回 legacy）並記錄原因 |
| 未覆蓋紀錄 | `ai_coverage.notCovered／uncovered／clearUncovered`，上限 64 筆、超過只累加 dropped；只存 kind／reason／key 這類字串，不倒任何觀察者的牌面 |
| 作者 API | `decision-core.lua` 的 `ai_card_use` 註冊表、`SmartAIView:aiUseCard(card, use)`（預設丟訊號）與 `tryUseCard`（回 `(plan, status)` 自己處理）、`AIUsePlan`（`use.to` 是純 Lua AIList，裡面是 PlayerView）、相容版 `SmartAIView:sort(players, key, anti)` |

`sortByKeepValue`／`sortByUseValue`／`sortByUsePriority` **沒有改**：它們本來就回副本、同分用牌 ID 決勝，與 legacy 對 QList 輸入的行為一致，改成原地排序反而會破壞相容性。改的是把這個契約寫進測試。

`self:sort` 與舊版有兩點刻意不同，都寫在程式註解裡：同分用 object name 決勝而不是 `os.time` 快取（牆鐘會讓同一局面排出不同順序）；快照答不出來的鍵（例如 `chaofeng`）回 nil 而不是靜默改排防禦。

測試：新增 `tests/lua/isolated-use-plan-contract.lua`（純 Lua，只需 bootstrap／facades／decision-core），由 `tests/room-runtime-isolation-test.cpp` 的 `useCardPlanContract()` 載入，失敗碼 **30**；`tests/lua/isolated-adapter-contract.lua` 補上訊號、normalize 與未覆蓋紀錄的案例。

破壞驗證：把 `SmartAIView:tryUseCard` 改名（不必重新編譯）後重跑 `--suite room-runtime` → **exit 30**，還原後 exit 0。

### PR 03（P2）：候選描述的是「這次問的問題」

這一階段找出並修掉三個既有缺陷，都是計畫 §5.1 點名過的型態。

| 編號 | 缺陷 | 修法 | 破壞驗證 |
|---|---|---|---|
| 9 | **`available` 對每一種 request 都用 `Card::isAvailable`。** 那是出牌階段的問題；在回應情境下它把閃標成不可用、把殺標成可用，正好相反 | `makeAICardCandidate` 依 request kind 分流：Activate 用 `isAvailable`，回應用 `Engine::matchPattern`（先去掉強制詢問的結尾 `!`） | exit **31**，輸出 `A response was judged with the Play availability rule false true` |
| 10 | **`maxTargets` 是從 `maxVotes` 推出來的。** 原生的 `maxVotes` 是「同一個人最多能被選幾次」——`fire.cpp:591` 對同一個人回 3——不是可選目標數。這正是計畫說的「只依變數名推論」 | 移除 `maxTargets`；改送原生語意的 `maxVotes`（只送大於一次的人，查不到就是一次）、`feasibleWithNoTarget`，以及**實測**出來的 `completeCoverage` | 併入 31／32 的測試 |
| 11 | **值型 `card_id` 答案完全略過目標合法性重驗。** `CardUseStruct::parse` 會把 `m_validateTargets` 設成 true，legacy 字串答案因此有守；`applyResult` 直接塞 `candidate.card` 的那條路沒有任何東西把它打開，於是超距離／被禁止的目標可以進 gameplay | `applyResult` 一律 `candidate.m_validateTargets = true`——AI 選的目標就是 AI 選的目標 | exit **32**，輸出 `An AI-selected target reached gameplay without revalidation` |

`completeCoverage` 是**實測**不是推論：固定目標牌直接成立；沒有任何合法目標也成立（「一個人都指不到」本身就是完整答案，不能跟「這個 request 描述不完整」混為一談）；其餘逐個合法目標檢查「它自己就是完整動作」且「沒有第二個目標能接上去」。`targetFilter` 可能觸發 TargetMod 之類的 Lua，所以探測有 256 次預算，用完就判定為不完整——往安全的方向倒，規劃端因此改成不規劃。

Lua 端新增 `CandidateView:getMaxVotes(name)`、`needsATarget()`、`hasCompleteCoverage()`；`getTurnUse` 碰到不完整的候選直接丟未覆蓋訊號，不截斷後假裝完整。

測試：`cardCandidatesDescribeTheQuestionAsked()`（失敗碼 **31**）用真的 Room、真的殺與閃，比對同兩張牌在 Play 與回應下的答案正好相反；`illegalAiTargetsNeverReachGameplay()`（失敗碼 **32**）讓 AI 回一個「殺自己」的答案，確認 `areCardTargetsLegal` 擋下、`Room::useCard` 回 false，且手牌數沒有變。

**未做（計畫明列，這裡沒有做）**：§5.4 的 request-local `candidate_id` 授權券；§5.2 第 2 點「權威端提供有界的完整合法組合或前綴契約」——現在多目標牌一律判為未覆蓋，不是支援。

### PR 04（P3）：Slash + Peach 垂直路徑

| 項目 | 落點 |
|---|---|
| 單一目標演算法 | `SmartAIView:rankTargets(names, stance)`：activate、`aiUseCard` 與每張牌的策略都只用這一份。`pickTargets` 變成它的薄入口。計畫 §6.1 要求的就是不要有第二套「挑敵人」 |
| 實體殺 | 只打敵人，先打虛弱、威脅大的；打不到敵人就不打，不硬找友軍 |
| 桃 | 只在受傷時用，而且留一張救命：血量 > 1 且手上只有一張時不喝 |
| 全覆蓋才作答 | `planTurnUse` 先確認**每一張打得出去的牌**都有策略、activate handler 先確認**每一個可啟動的技能**都有 handler，任何一項缺就丟未覆蓋訊號。計畫 §6.4：「即使會用 Slash，只要仍有未支援的技能／牌可能改變選擇，不能宣稱已完整覆蓋此 request」 |

**這條規則的實際後果要講清楚**：真實對局裡手牌幾乎一定有還沒有策略的牌，所以 isolated 的 activate 現在多數回合會整題退回 legacy。這是計畫 §6.4 指定的首版保守作法（「只有依賴完整的受控情境允許 isolated 正式作答」），不是缺陷；但它表示**目前 isolated AI 在真實對局的出牌覆蓋率接近零**，要靠 PR 05 之後逐族補策略才會上升。

測試：`slashAndPeachPlanThroughOneSharedPath()`（失敗碼 **33**）涵蓋五件事——規劃（殺打到比較弱的敵人，而不是清單上的第一個）、全覆蓋不足時整題回退、拒出（沒受傷的桃）、改成瀕死後同一張桃被喝掉、以及關係未知時回退且原因被記錄成 `activate|Slash|the mode policy does not describe relations`。最後再用一個直接呼叫 `tryUseCard` 的 handler 確認和 activate 路徑挑到同一個目標。

破壞驗證（都不必重新編譯，改 Lua 即可）：把殺的目標排序反轉 → exit **33**，輸出 `The Slash was not planned onto the weaker enemy true 7 QList("vertical-strong")`；把 `planTurnUse` 的全覆蓋預檢拿掉 → exit **33**，輸出 `A turn holding an unread card was answered anyway true 1 7`。

**PR 05 改到的一處**：這個案例原本用「一張過河拆橋」當「手上有沒有策略的牌」的例子。過河拆橋現在有策略了，所以同一個 fixture 會被正常規劃，案例自己先紅（實際看到 exit **33**，輸出 `A turn holding an unread card was answered anyway true 1 7`）。已把那張牌換成無中生有（`ExNihilo`，`target_fixed`＋`feasible_with_no_target`），它仍然沒有隔離策略，案例的用意不變。

### PR 05（P3 續）：Duel、順手牽羊／過河拆橋

新增三支策略，全部登記在既有的 `ai_card_use` 註冊表上，沒有第二套挑目標演算法；敵友判定與基礎排序仍然只有 `SmartAIView:rankTargets(names, stance)` 一份，牌族只在那份順序上套自己的估值。

#### 先補的共用工具：已知張數 vs 估計張數

計畫 §6.3 明寫「明確區分 known card count 與 estimate；他人未知手牌不可當成零」。原本只有 `getCardsNum(class, player)`，它數的是 `getKnownCards()`，對別人的暗牌一律回 0——直接拿它比大小，等於宣稱「敵人手上沒有殺」。新增兩支，落在 `decision-core.lua`：

| API | 語意 | 答不出來時 |
|---|---|---|
| `getCardsNum(class, player)` | **已知**張數，行為不變 | 牌區不可見回 nil |
| `getUnknownCardsNum(player)` | 這名觀察者看不見的張數；手牌全開時是 0 | 快照沒有張數回 nil |
| `estimateCardsNum(class, player)` | 已知 ＋ 看不見的張數 × 密度 | **沒有登記密度的牌族回 nil**，不回一個看起來像已知的 0 |

密度取自本倉庫 standard 牌堆的實際張數（`src/package/standard-cards.cpp` 的牌表：108 張裡殺 30、閃 15、桃 8），只登記這三種。換牌包會讓比例偏掉——這是估計不是規則，寫在程式註解與下面的能力清單裡。

#### 能力清單：決鬥（Duel）

```
入口：ai_card_use.Duel（activate 的 planTurnUse 與任何直接呼叫 aiUseCard 的 handler 共用）
所需觀察：自己的手牌（完全已知）、每個合法目標的手牌張數與已知牌、mode policy 的敵友關係
所需規則：權威端候選的 legal_targets（距離、禁止、ExtraTarget 都已算進去）
所需策略：rankTargets(enemy) 的共用排序 ＋ 殺的數量比較
完整支援的情境：
  - 單目標決鬥（ExtraTarget 修正為 0 時的一般情況），mode policy 認得出敵友
  - 指到共用排序裡第一個「我方殺數 ≥ 對方估計殺數」的敵人。被指的人先出殺，
    所以追平就贏，不需要多一張
  - 血量 ≤ 1 且手上沒有桃時要求多一張餘裕才開（輸掉就是死）
  - 三態都答得出來：planned／declined（比不過就不開）／unsupported
刻意未支援的情境：
  - 指友軍（決鬥從來不是增益牌，這裡不做）
  - 把「這張殺留著回應決鬥」與「這回合拿這張殺去打人」一起算：兩者互斥，沒有建模
  - 對方的觀星／無雙／青龍刀之類會改變殺數或傷害的技能與裝備：估計只看張數，
    不看技能。這是策略估計，不是規則宣告（與 Slash 的既有邊界一致）
  - 決鬥打起來之後的 duel-slash 回應：那是另一個 respond_card 詢問，
    `ai_skill_cardask` 沒有註冊，整題退回 legacy
缺能力時的結果：
  - 這次 request 沒有這張牌的候選 → ai_unsupported("this request carries no candidate
    for the Duel", "Duel")
  - mode policy 不管理關係 → ai_unsupported("the mode policy does not describe
    relations", "Duel")
  - 快照拿不到自己的手牌 → ai_unsupported("the snapshot does not carry the viewer's
    own hand", "Duel")
  - 估不出對方的殺數（例如換了牌包、密度沒登記）→ 不賭，換下一個敵人；全部估不出
    就 declined。「估不出來」不會被讀成「對方沒有殺」
```

#### 能力清單：順手牽羊（Snatch）／過河拆橋（Dismantlement）

兩張牌共用同一份「拆誰最划算」的估值（`strip_value`：裝備 2 分、手牌 1 張 1 分、判定區 0 分）與同一個規劃函式 `plan_strip`；差別在 use value（順手牽羊拿到手，所以值錢），不在挑誰。

```
入口：ai_card_use.Snatch / ai_card_use.Dismantlement
所需觀察：每個合法目標的裝備區（公開）、手牌張數（公開）、mode policy 的敵友關係
所需規則：權威端候選的 legal_targets——順手牽羊的一格射程、兩張牌的「對方身上要有牌」、
          禁止與 ExtraTarget 都已經在候選裡算完，策略不自己算距離
所需策略：rankTargets(enemy) 的共用排序 ＋ strip_value
完整支援的情境：
  - 單目標（ExtraTarget 修正為 0 時的一般情況），mode policy 認得出敵友
  - 在共用排序挑出來的敵人裡，指身上東西最多的那一個；同分時保留共用排序的先後
  - 身上只剩判定區的敵人是 0 分，不指他：拆掉敵人的樂不思蜀是幫他解套
  - 三態都答得出來：planned／declined（沒有值得拆的敵人）／unsupported
刻意未支援的情境：
  - **指友軍拆掉他判定區的樂不思蜀／兵糧寸斷。** 這是過河拆橋真正的另一半用法，
    但真正丟掉哪一張是後面另一個 card_chosen 詢問決定的，而這個 runtime 沒有註冊
    那一族的 handler（契約裡用 `assert(not ai_coverage.covers("card_chosen"))` 驗著）。
    指友軍等於把結果押在一個自己沒有作答的決策上，所以這一批不做
  - 拆完之後拿／丟哪一張牌：同上，是 card_chosen，退回 legacy
  - 裝備的實際威脅差異（諸葛連弩 vs 防具）：現在一律算 2 分，沒有逐件估值
  - 對方的手牌內容：只用張數，不猜內容
缺能力時的結果：
  - 沒有候選 → ai_unsupported("this request carries no candidate for the <牌名>", 牌名)
  - mode policy 不管理關係 → ai_unsupported("the mode policy does not describe
    relations", 牌名)
  - 快照描述不出目標身上有什麼 → ai_unsupported("the snapshot does not describe what
    this target holds", 牌名)
```

#### 出牌優先序

`default_use_value`／`default_keep_value` 的三個新值直接取自 legacy `lua/ai/standard_cards-ai.lua`（Snatch 9／3.46、Dismantlement 5.6／3.44、Duel 3.7／3.42），放進來之後與既有項目維持 legacy 的相對順序。`default_use_priority` 不能照搬：legacy 的 9.3／9.4 與這裡既有的 0–5 不是同一把尺，所以只搬相對順序（拆 6.2 > 順 6.1 > 決鬥 4.1 > 殺 4）。與 legacy 的一處差異：這裡的桃是 5，落在順手牽羊與決鬥之間，legacy 的桃是 0.9 排最後——桃的 5 是 PR 04 定的，本批沒有動它。

#### 覆蓋率的實際變化

PR 04 那條「全覆蓋才作答」的規則沒有放寬。實際效果是：手上只有殺／桃／決鬥／順手牽羊／過河拆橋（加上不影響判斷的技能）的回合，現在 isolated 答得出來；只要還有一張別的牌打得出去，整題照舊退回 legacy。**這一批沒有量測真實對局的覆蓋率**，沒有跑過對局，所以不宣稱數字往上走了多少，只宣稱多了三個牌族。

#### 測試

| 檔案 | 內容 |
|---|---|
| `tests/lua/isolated-trick-families-contract.lua`（新增） | 純 Lua。已知／估計的分離（含手牌全開時估計退回已知、沒登記密度的牌族回 nil）、決鬥的 planned／declined／餘裕規則／兩條 unsupported、拆牌族挑 rich 而非共用排序第一名的 poor、只剩判定區的敵人不指、空候選是 declined、兩條 unsupported、優先序排出 `Dismantlement,Snatch,Duel,Slash`、以及 `card_chosen` 沒有被覆蓋 |
| `tests/room-runtime-isolation-test.cpp` 的 `trickFamiliesPlanWithTheirOwnValuations()`（新增，失敗碼 **34**） | 同一個 Room 先載入上面那支純 Lua 契約，再走 `decideIsolated` 的端到端：順手牽羊指 `strip-rich`（不是共用排序第一名 `strip-poor`）、決鬥指 `strip-poor`（共用排序第一名，證明拆牌族的估值沒有取代那份排序）、手上沒有殺的決鬥回 Pass、mode policy 清空後整題回退且原因記成 `activate|Snatch|the mode policy does not describe relations` |

只多建一個 Room（純 Lua 契約與端到端共用同一個），所以 `room-runtime` 只多約 35 秒；`tests/runtime-tests-main.cpp` 的 600000 ms 預算沒有動。

破壞驗證（兩次都只改 Lua，不必重新編譯）：

| 破壞方式 | 結果 |
|---|---|
| `plan_strip` 改成取共用排序第一個命中的人（`value > 0 and best_value == nil`），等於丟掉牌族估值 | exit **34**，輸出 `tests/lua/isolated-trick-families-contract.lua:164: assertion failed!` |
| `ai_card_use.Duel` 改成 `use.to:append(ranked:last())`，等於丟掉共用排序 | exit **34**，輸出 `The Duel was not opened against the ranked enemy true 20 QList("strip-rich") ""`——這一條純 Lua 契約抓不到（它只驗有一個目標），是原生端到端案例抓到的，代表那個案例本身也是有效的 gate |

兩次破壞都還原了，`git diff` 與磁碟上的 `decision-core.lua` 已確認回到未破壞狀態。

### PR 06（P4）：card_spec、轉化候選與技能實例

計畫 §11 給 PR 06 的完成標準是「不能偽造轉化，來源／配額完整」。

#### 先講這一批修掉的是什麼

`buildSpecCard()` 舊的樣子讓四種偽造都過得去，而且比計畫描述的更寬：

| # | 舊行為 | 後果 |
|---|---|---|
| 1 | `!player->hasSkill(spec.skillName) && !Sanguosha->getViewAsSkill(spec.skillName)` 才拒絕 | 是 `\|\|` 的形狀：**只要引擎全域登記過這個 view-as 技能名就放行**，玩家有沒有這個技能都無所謂 |
| 2 | `Sanguosha->cloneCard(spec.name, suit, number)` | **任何一張引擎牌名都造得出來**，與那個技能實際變得出什麼無關——正是計畫 §7.1 說的「clone 一張照用」 |
| 3 | 技能只用**裸名字**定址 | 同名多實例分不出來；`instanceID` 與配額在這條路上完全沒有被檢查 |
| 4 | subcards 只驗「玩家現在持有」 | 沒有驗它是不是那個技能**合法的成本** |

#### 授權改成「這次 request 自己發的票」

新增 `AICardConversionView`：權威端**自己**用玩家真的持有的那個技能實例造一張牌，
把那張牌的身分（`objectName`／`getKindOfNames()`——受控卡牌目錄，不是作者宣稱的字串）、
完整的 instance identity（`activationRef`／`sourceRef`／兩個配額旗標）、以及造它的**那一組**
成本牌記下來，配一個 request-local 的 `conversionId`。

`applyResult` 走 `card_spec` 時：

1. `spec.conversionId` 必須指到**這一次 request** 發出的票，否則直接拒絕；
2. 作者寫的 `name`／`suit`／`number`／`skill`／`subcards` **逐欄與權威端自己的紀錄比對**，
   不一致就是偽造，不是「照作者的意思調整」；
3. instance、`canActivate`、`cardSelectionFeasible` 與**配額**在作答當下重驗一次——票是
   建 request 時發的，而 AI 在中間想過一輪；
4. `sourceRef` 必須解析回**當初發票的那個 root**，因為付錢的是它；
5. 最後由權威端**再呼叫一次 `createCard()`** 造牌，作者的描述從頭到尾只被用來挑出
   「他指的是哪一張已授權的轉化」。

舊的 `hasSkill || getViewAsSkill` 檢查整條刪掉。

#### 列舉是有界的，而且「列不完」與「沒有」分開

`buildCardConversions()` 只列舉兩種形狀：不用付成本（n = 0）與付恰好一張（n = 1）。
更寬的就不列舉——列舉手牌的所有子集正是計畫 §7.2 要避開的組合爆炸。總數上限 64 筆、
探測預算 512 次。

三態而不是兩態：

| 情況 | 處理 |
|---|---|
| n = 0 / n = 1 | 列舉 |
| n ≥ 2、技能查不到、實例已經不在身上 | **列不完** → `conversionsEnumerated = false` |
| 造出來的牌沒有名字（裸 `ActiveSkillCard` proxy） | **跳過但不算缺口**：那是 skill action，本來就由 `hasSkillActionContext` 那條路帶著 `selectedCardIds` 處理，不是轉化 |

第三條是這一批自己發現的：`ActiveSkillCard` 的 `objectName` 是空的（`card.cpp:1084`，
`SkillCard` 不設），而 `ViewAsSkillV2::createCard()` 的預設實作就是回那個 proxy。把它
記成缺口會讓每一個 proxy 技能永遠無法被覆蓋。

#### `conversionsEnumerated` 的預設值：fail-closed

第一次整跑回 **exit 26**（`decisionCorePlansATurnFromCandidates`，**既有案例**，不是新案例）。
原因不是 fixture 壞了，而是一個我沒有先決定的語意：**沒有人設這個欄位時它該是什麼意思。**

- 預設 true → 「沒有轉化，而且這是事實」。**fail-open**：任何忘記呼叫
  `buildCardConversions` 的路徑都會靜默宣稱自己完整。
- 預設 false → 「這一側沒有被告知」。**fail-closed**：這種 request 退回 legacy。

選 false，而且規劃端檢查寫成 `~= true` 而不是 `== false`，讓「Lua fixture 沒寫這個欄位」
與「C++ 送了 false」得到同一個答案。**「沒有被告知」與「被告知沒有」不可以是同一個答案**
——這正是 P2／P3 一路在守的那條線（未知手牌 vs 空手牌、沒有合法目標 vs 沒有描述組合）。

生產端成本是零：`makeRequest` 對 Activate／UseCard／RespondCard 一律呼叫
`buildCardConversions`，欄位一定被明確設定。只有手寫的 fixture 要自己說清楚，而那是對的
——一個宣稱「我是一個完整的問題」的 fixture 本來就該自己說出來，不該用繼承的。

#### 轉化走同一套策略，沒有第二套演算法

這是本批最重要的一個設計決定。轉化出來的一張殺**就是一張殺**：

- `ConversionView` 答得出策略會問的每一個方法（`objectName`／`getClassName`／
  `getEffectiveId`），也答得出候選會被問的每一個方法（`getLegalTargets`／`targetFixed`／
  `hasCompleteCoverage`／`needsATarget`／`getMaxVotes`）。
- 合成 id 用**負數**（`-conversionId`）。實體牌 id 一律 ≥ 0，所以撞不到；
  `getCardCandidate()` 看到負數就查回那一筆轉化。
- 結果是 `ai_card_use.Slash` **一行都不用改**就會規劃一張變出來的殺，而且用的是同一份
  `rankTargets()`。`getTurnUse` 也把轉化與實體牌排進**同一份清單、同一把尺**比優先序。

#### 技能覆蓋的第二條路

PR 04 的規則是「每一個可啟動技能都要有 `ai_skill_activate` handler，否則整題回退」。
P4 之後技能不只能被啟動，也能被轉化成一張牌，所以覆蓋有兩條路：掛了 handler 算；
或者**它的轉化已經列完，而且每一張列出來的牌都有 `ai_card_use` 策略**。兩條都沒有才是
未覆蓋。列不完一律不算覆蓋。

#### 新增的 fixture（`~test` 套件）

`~test` 原有的三個 V2 fixture 沒有一個同時「付一張牌」且「產出一張有名字的牌」：
`active_skill_v2_test` 是 n = 0 產出殺，`active_skill_v2_proxy_ui_test` 是 n = 2 但產出
裸 proxy。所以新增 `ViewAsSkillV2CostTest`（`active_skill_v2_cost_test`）：付一張紅牌、
產出一張殺。這是 §7 驗收清單「一個帶成本的技能動作」唯一的取得方式。
`~test` 的註解本來就寫明「for test only」，沒有動到任何正式武將。

#### 測試

| 檔案 | 內容 |
|---|---|
| `tests/lua/isolated-conversion-contract.lua`（新增） | 純 Lua。轉化的身分來自權威端、合成負數 id 查得回同一筆、轉化答得出候選那幾個問題、借用的 source 關係、走同一套 `ai_card_use.Slash` 並送出票、成本原樣回送、沒有策略的轉化是未覆蓋、`newCard` 四種找不到的情況都回 nil、「列不完」與「沒有」分開、技能覆蓋的第二條路（含反例）、以及規劃 A 不污染 B |
| `tests/room-runtime-isolation-test.cpp` 的 `conversionsAreAuthorizedNotClaimed()`（新增，失敗碼 **35**） | 真的 Room、真的技能實例。兩個 fixture 的轉化都被列出且身分正確、成本技能不列舉它拒絕的那張牌、目標描述與實體候選同一套、帶票的答案被接受且 instance identity 傳得下去、**五種偽造全部被拒**（偽造牌名／偽造成本／完全沒有票／不存在的票／指一個不擁有這個轉化的技能名）、同名兩實例各自成票、配額用盡後同一張票失效、以及實體牌的候選票 |

這個案例與純 Lua 契約共用同一支函式但各自建 Room（契約一個、端到端一個），所以
`room-runtime` 多約 70 秒。

#### 建置與測試（實跑）

```
cmake --build builds/cmake-vs2026 --config Debug --target qsanguosha_runtime_tests --parallel 4
```

exit 0。

```
builds\cmake-vs2026\tests\Debug\qsanguosha_runtime_tests.exe --suite room-runtime --seed 2026082201
```

exit **0**，`room runtime isolation passed`，耗時 **502 秒**。

```
python tools/ai/check-ai-runtime-manifest.py --root .
ai-runtime-manifest: files=14 required=7 core=3 package_handlers=2; the Lua declaration, the fetch gates and the asset manifest agree
```

exit 0（本批沒有新增 `lua/ai/` 底下的檔案——新增的契約在 `tests/lua/`——所以 manifest 不用動）。

#### 過程中被測試抓到的兩個真缺陷

這兩次都不是 fixture 壞掉，是我沒有先決定的語意，值得記下來：

| 次 | exit | 症狀 | 真正的問題與處置 |
|---|---|---|---|
| 1 | **26**（`decisionCorePlansATurnFromCandidates`，**既有案例**） | 既有案例先紅，新案例根本跑不到 | `conversionsEnumerated` 的預設值沒有被決定。選 fail-closed，並把檢查寫成 `~= true`，讓「Lua fixture 沒寫」與「C++ 送 false」得到同一個答案。生產端零成本 |
| 2 | **26** | 同上 | 手寫的候選 `candidateId` 是 -1（型別自己的「沒有票」），而 Lua 把它當票送出去，被 normalize 判成格式錯誤的票 → `AI_RUNTIME_ERROR` → 整題回退。修在來源：-1 不是票就不要宣稱有票。**normalize 的嚴格度沒有放寬**，因為「格式錯誤的票」與「沒有票」是兩件事，只有前者是錯誤 |

#### 破壞驗證（實跑）

| 破壞方式 | 需要重新編譯 | 結果 |
|---|---|---|
| `SmartAIView:newCard` 不查授權清單，直接捏一個 proposal 回去 | 否 | exit **35**，`tests/lua/isolated-conversion-contract.lua:149: assertion failed!`（該行是 `assert(ai:newCard("peach") == nil)`） |
| `ConversionView:toCardSpec()` 不送 `conversion_id`（答案不帶票） | 否 | exit **35**，`tests/lua/isolated-conversion-contract.lua:112: assertion failed!`（該行是 `assert(answer.card_spec.conversion_id == 1)`） |
| `buildSpecCard()` 不再把作者的 `name`／`suit`／`number`／`skill`／`subcards` 與權威端紀錄比對（等於回到信任作者） | 是 | exit **35**，輸出 `A forged card name was built anyway` |

三次破壞都已還原；`grep -c "PR06-BREAK\|TASKA-BREAK\|PR06-DBG"` 在
`ai-decision-coordinator.cpp`、`ai-runtime.cpp`、`isolated-facades.lua`、
`decision-core.lua`、`player.cpp` 都是 0。

#### 整組（最終狀態，實跑）

```
builds\cmake-vs2026\tests\Debug\qsanguosha_runtime_tests.exe --suite runtime-contract --seed 2026082201
```

exit **0**，`TOTAL: 11 / PASS: 11 / FAIL: 0`，耗時 **898 秒**。

耗時從 PR 05 記錄的 668 秒漲到 898 秒，原因是 `room-runtime` 本身從「在案例 17 就中斷」
變成整支跑完（502 秒）。`room-runtime` 的 600000 ms 預算沒有動也沒有超時。

**同一組指令前一次跑的是 exit 1**，唯一一筆 `FAIL initial-room-close: timed out after
60000 ms`，當時整組 970 秒。歸因：機器負載，不是本批造成的。證據有三點——
(1) 該 suite 單獨跑是 exit 0、**48 秒**（預算 60 秒），(2) `test-suite.h` 用
`QProcess::waitForFinished` 逐支序列執行，所以 suite 之間不會互相重疊，
(3) 安靜重跑整組即 exit 0。**但要記清楚：這一支的餘裕本來就只有兩成
（48／60 秒），在任何負載下都容易翻掉，而本批之前沒有量過它的基準值，所以
「PR 06 沒有讓它變慢」只有機制上的理由（`buildCardConversions` 不會在關房時執行），
沒有前後對照的量測。**

#### 診斷能力的缺口（**沒有修**，另案）

`ai-runtime.cpp` 在 `lua_pcall` 失敗時把 Lua 的錯誤字串**丟掉**：設完
`errorCode = "AI_RUNTIME_ERROR"` 就 `lua_pop`，沒有讀。結果是每一個隔離 AI 的 Lua 錯誤
對外都只表現成「安靜地退回 legacy」。上面第 2 個缺陷因此多花了一整輪建置＋整跑
（約 10 分鐘）才拿到一句 Lua 早就寫好的訊息。在 `ai-runtime.cpp:654` 加一行
`qCritical` 印 `lua_tostring` 就能永久解決。本批**沒有加**：它會改變生產環境的日誌行為，
超出 PR 06 的範圍。

---

### PR 02–04 的建置與測試（2026-09-19）

```
cmake --build builds/cmake-vs2026 --config Debug --target qsanguosha_runtime_tests --parallel 8
```

exit 0。

```
builds\cmake-vs2026	ests\Debug\qsanguosha_runtime_tests.exe --suite room-runtime --seed 2026082201
```

exit 0，`room runtime isolation passed`，耗時 **370 秒**。

改完預算後整組重跑：

```
builds\cmake-vs2026\tests\Debug\qsanguosha_runtime_tests.exe --suite runtime-contract --seed 2026082201
```

exit 0，`TOTAL: 11 / PASS: 11 / FAIL: 0`，耗時 672 秒。

耗時是這次唯一的回歸：`--suite runtime-contract` 給每個 suite 的預設上限是 300 秒，`room-runtime` 原本跑得進去，加上本次四個案例後跑不進去，於是整組先報 `FAIL room-runtime: timed out after 300000 ms`。實測歸因：把四個新案例停用後同一支 suite 是 **230 秒**，四個案例共 140 秒，也就是每個約 35 秒，成本全在各自建一個 Room。各案例自己建 Room 正是這個 suite 的用意，所以沒有把它們合併，而是照 `card-lifetime` 的先例在 `tests/runtime-tests-main.cpp` 給 `room-runtime` 明寫 600000 ms 的預算並註明原因。

```
python tools/ai/check-ai-runtime-manifest.py --root .
ai-runtime-manifest: files=14 required=7 core=3 package_handlers=2; ...
```

exit 0。

### PR 05 的建置與測試（2026-09-20）

建置（`--parallel 8` 第一次撞到 MSVC 的 `C1090 PDB API 呼叫失敗，錯誤碼 '23'`，降成 `--parallel 4` 後通過；這是 PDB 寫入的併發問題，與本批改動無關）：

```
cmake --build builds/cmake-vs2026 --config Debug --target qsanguosha_runtime_tests --parallel 4
```

exit 0。

```
python tools/ai/check-ai-runtime-manifest.py --root .
ai-runtime-manifest: files=14 required=7 core=3 package_handlers=2; the Lua declaration, the fetch gates and the asset manifest agree
```

exit 0（本批沒有新增 `lua/ai/` 檔案，只改了 `decision-core.lua`，所以 manifest 不用動）。

測試——**這裡必須把話說清楚**。本工作樹在 2026-09-20 有一批與隔離 AI 無關的未提交引擎改動，讓 `aiWorldViewIsScopedAndRevisioned`（失敗碼 17）先紅，整個 suite 停在那裡，後面的案例根本跑不到（詳見下一節）。所以驗證分兩段：

1. **最終狀態（兩個暫時措施都已還原）：**

```
builds\cmake-vs2026\tests\Debug\qsanguosha_runtime_tests.exe --suite room-runtime --seed 2026082201
```

exit **17**，輸出 `A flag set and cleared moved the state revision 23 25` ／ `AI world view scope or state revision gate failed`。**這是既有阻斷，不是 PR 05 造成的**（歸因見下一節）。該阻斷已於同日解除，那條契約也已改寫，所以這一段記的是 PR 05 當下的實況，不是現在的行為。

2. **暫時繞過那一項之後（用來實際驗證 PR 05）**：在 `runRoomRuntimeIsolationTests()` 開頭暫時設 `Config.EnableAI = true`，並暫時把 `aiWorldViewIsScopedAndRevisioned` 的 `return 17;` 註解掉，其餘一行未改：

```
builds\cmake-vs2026\tests\Debug\qsanguosha_runtime_tests.exe --suite room-runtime --seed 2026082201
```

exit **0**，`room runtime isolation passed`，耗時 **314 秒**（PR 04 記錄是 370 秒；同一台機器的負載差異，不是本批讓它變快）。這一輪裡失敗碼 30／31／32／33／34 全部通過。

兩個暫時措施都已從 `tests/room-runtime-isolation-test.cpp` 移除（`grep -n "PR05-TEMP" ` 回 0 筆）。上面第 1 段的 exit 17 就是移除之後實跑的結果。

3. **最終狀態的整組**（同樣是還原之後）：

```
builds\cmake-vs2026\tests\Debug\qsanguosha_runtime_tests.exe --suite runtime-contract --seed 2026082201
```

exit **1**，`TOTAL: 11 / PASS: 10 / FAIL: 1`，唯一一筆是 `FAIL room-runtime: exit code 17`，耗時 **668 秒**。其餘十個 suite（`lua-runtime`、`room-lua-teardown`、`lua-exception-unwind`、`initial-room-close`、`initialization-handoff`、`turn-reclaim`、`wrapped-adoption`、`card-lifetime`、`card-lifetime-lua`、`synthetic-30`）全部 PASS——也就是那批引擎改動的爆炸半徑目前只落在 `room-runtime` 這一支。`room-runtime` 的 600000 ms 預算沒有動也沒有超時。

### 2026-09-20 的既有阻斷：如何解掉的

上一輪記錄的兩處衝突，事後查明**都不是未提交的工作樹改動**，而是 `fc1c09e`
（`feat(large-room): add 50-player mode and native resolution UI`，2026-09-20 03:26）
提交進 `debug` 的內容；`5020eba` 之後又清理了 `setFlags` 的括號與 no-op 判斷，語意未變。
上一輪看到的是提交前的工作樹，所以當時記成「別人正在改的東西」；現在它們是基準。

兩項都已按下述決定處理，`--suite room-runtime` 回 exit 0。

#### 1. `makeRequest` 在 `--ai off` 時回一個只有表頭的 request——**維持原樣，測試自己開 AI**

`ai-decision-coordinator.cpp` 的 `makeRequest()` 在 `!Config.EnableAI` 時直接回傳，
不建 world view 也不建候選。先確認這不是正確性問題：三個 `decideIsolated()` 的呼叫點
（`:905`、`:1450`、`:1631`）本來就在 `Config.EnableAI` 為假時把 route 釘成
`AiRouteLegacyDirect`，`decideSkillAction()`（`:1683`）更是整個 early-return，所以
**沒有這一行也不可能有 isolated handler 被叫到**。它剩下的作用只有成本。

而那個成本是真的：`cloneAI()`（`swig/ai.i:303`）在 `!Config.EnableAI` 時回 `TrustAI`，
而 `game-session-controller.cpp:1252` 無條件給每個玩家一個 AI。所以在 `--ai off` 的
伺服器上，每個託管／斷線玩家的每一次 `activate`／`askForUseCard` 都會走到
`makeRequest`。沒有這個 gate，每一次都要跑一次 `evaluateModePolicy`（進 gameplay Lua VM）
加上 O(手牌數 × 存活玩家數²) 的 `targetFilter`／`isProhibited` 探測，而且沒有任何人讀。
50 人模式正是讓這個二次方項變得昂貴的原因，也是 `fc1c09e` 會加這一行的原因。

**決定：維持原樣。** `--suite room-runtime` 整支在 `Config.EnableAI = true` 下跑
（`runRoomRuntimeIsolationTests()` 開頭的 `ScopedAiEnabled`，結束時還原）。這支 suite 的
主題就是隔離 AI，問的每一題都要 AI 開著才問得出來。

一併澄清上一輪對選項的描述：「只跳過候選建置、不跳過 world view」**解不掉這個 suite**。
gate 在 `room-runtime` 裡的實際影響面是三個案例，而且分成兩類：

| 案例 | 需要的是 |
|---|---|
| 17 `aiWorldViewIsScopedAndRevisioned` | world view（它從頭到尾沒有檢查 `cardCandidates`） |
| 31 `cardCandidatesDescribeTheQuestionAsked` | `makeRequest` 建出來的候選 |
| 32 `illegalAiTargetsNeverReachGameplay` | 同上 |
| 26／30／33／34 | **兩者都不要**——它們自己手動組 `AIRequest` 再直接呼叫 `decideIsolated` |

26／33／34 從來沒有被這個改動弄壞，只是案例 17 先中斷整支 suite，所以跑不到。把 gate
移到 `buildWorldView` 之後只會修好 17，31 和 32 照樣紅，測試那邊還是得開 AI。

因為這樣改之後「`--ai off` 時 request 長什麼樣」就沒有任何東西在看了，所以把那個形狀
**寫成明確契約**補進案例 17：`--ai off` 下 `makeRequest` 必須仍帶 viewer 與
`stateRevision`，而 world view、候選與 skill actions 必須全空。

#### 2. `Player::setFlags()` 發 `gameplay_property_changed()`——**flag 算 gameplay 狀態，改契約**

`fc1c09e` 不只動了 `setFlags`：同一批還給 `clearFlags`、`setFixedDistance`、
`removeFixedDistance`、`addCard`、`removeCard`、`setTag`、`removeTag`、`clearTags` 與
動態 `setProperty` 都加上了同一個信號，並補了 no-op 判斷。這是一次「凡是看得見的變動都要
讓 revision 前進」的整理，不是誤加的一行。

**決定：flag 算 gameplay 狀態，把舊契約改掉。** 理由是 flag 確實餵給這份快照自己發佈的欄位：`Player::getAttackRange()` 讀 `hasFlag("InfinityAttackRange")`
（`player.cpp:247`），而 `attackRange` 是 `AIPlayerView` 的欄位；`hasFlag` 全倉有
842 處 C++ 與 1148 處 Lua 讀取點。更直接的是 `buildWorldView` 的**距離表快取是以
`stateRevision` 為鍵**（`ai-decision-coordinator.cpp:550-573`）：舊契約下，一個被 Lua
距離技能讀到的 flag 可以改變距離而不動 revision，那份快取就會送出過期的表。

沒有任何辦法靠名字分辨「scratch flag」與「規則 flag」——`AI_TestScratch` 與
`InfinityAttackRange` 對 `setFlags()` 來說是同一種東西，而且擴充包會自己加 flag。
所以收窄成名單式 allowlist 只會變成靜默漏接，唯一不靠名單的收窄方式就是整個拿掉，
那等於把上面那個快取漏洞重新打開。

案例 17 的舊斷言是「設一個 flag 再清掉，revision 不可以動」。**新契約**是：

| 動作 | 契約 |
|---|---|
| 設一個還沒設過的 flag | revision 前進 |
| 再設一次同一個 flag（沒有變化） | revision **不**前進 |
| 清掉一個設過的 flag | revision 前進 |
| 再清一次（沒有變化） | revision **不**前進 |

也就是說被放棄的是「scratch flag 不動 revision」，保留下來的是「沒有變化就不算變化」——
後者正是 `fc1c09e` 那些 no-op 判斷提供的保證，值得釘住。

代價要講清楚：**沒有量測過這是否讓 legacy-adapted 路徑多出回退。** isolated 路徑在
`makeRequest` 與 `applyResult` 之間不會有人動 Room，所以那一側不受影響；
legacy-adapted 路徑本來就會被 live Lua 推動 revision，而且 `decide()`（`:1665`）已經
為此重新蓋章。但「AI callback 裡設 `Global_*` flag」正是可能多出回退的形狀，這一輪沒有量。

#### 這兩條契約的破壞驗證（實跑）

| 破壞方式 | 結果 |
|---|---|
| `makeRequest` 的 `if (!Config.EnableAI) return request;` 改成 `if (false)` | exit **17**，輸出 `An --ai off request still built the snapshot nobody reads 29 2 1 1 1 0`（world view revision 29、playerOrder 2、handCards 1、players 1、候選 1） |
| `Player::setFlags()` 的 `emit gameplay_property_changed();` 拿掉 | exit **17**，輸出 `Setting a flag did not move the state revision 23 23` |

兩次破壞都各自重新建置並重跑（分別 175 秒與 147 秒——都在案例 17 就中斷，所以比整支快），
之後都已還原；`grep -c "TASKA-BREAK"` 在 `player.cpp` 與 `ai-decision-coordinator.cpp`
都是 0，`src/core/player.cpp` 已 `git checkout` 回 `5020eba` 的內容。

#### 保留的診斷訊息

`aiWorldViewIsScopedAndRevisioned` 原本十幾處只寫 `return false;`。上一輪為了做歸因替
每一處補的 `qCritical()`**予以保留**：它們只在失敗路徑上執行，而這是一個兩百行的案例，
少了訊息就看不出是哪一項倒了——上面兩次破壞驗證能直接指出原因，就是靠它們。

### 這一批改動又變成「只存在於這台機器」

`lua/ai/` 仍然被 gitignore（`git ls-files lua/ai` = 0）。PR 02–04 改了 `isolated-facades.lua`、`isolated-bootstrap.lua`、`isolated/decision-core.lua` 三支，PR 05 再改了 `isolated/decision-core.lua`，**它們現在只存在於這台機器**——和本文件上面記錄過的那個坑一模一樣。

而且這次比上次嚴重，因為 C++ 與 Lua 這次是綁死的：`ai-runtime.cpp` 已經不再送 `max_targets`，改送 `complete_coverage`／`feasible_with_no_target`／`max_votes`。任何環境若拿到**上游那份舊的** `decision-core.lua`，`candidate:getMaxTargets()` 會是 nil，呼叫就報錯，`decideIsolated` 記成 `AI_RUNTIME_ERROR` 後整題退回 legacy——不會崩，但那裡的 isolated AI 等於全滅。

**所以 PR 02–05 要能用，必須先把這三支推上 `lolosiyue/extensions`。** 至今沒有推（未經指示不對外部倉庫寫入）。

---

## PR 06 明確沒有做的部分

照計畫 §11，PR 06 的範圍是「`card_spec`、轉化候選與 skill instance」。範圍內做完了，
以下是**範圍內外都還沒做**、不要被上面的能力清單誤導的：

| # | 沒做的事 | 說明 |
|---|---|---|
| 1 | **n ≥ 2 的轉化** | 付兩張以上成本的 view-as 技能一律不列舉，整個 request 標成 `conversionsEnumerated = false` 然後退回 legacy。計畫 §7.2 說的「參數化的選擇契約」沒有做，只做了「先支援少量明確族群」的那一半 |
| 2 | **計畫 §7.3 的有限推演** | request-local planning branch、成本牌預留、計劃中的使用次數、遞迴追蹤、每個候選計劃自己的 scratch 子分支——**一項都沒有做**。純 Lua 契約裡只驗到「規劃 A 不會改掉 B 的計劃」這個最弱的形式，那不是分支 scratch。§11 把這些歸在 PR 07 |
| 3 | **§7.4 的跨 request 意圖** | `ai_memory` 的儲存契約（按觀察者分區、只收純值、拒收代理、VM 重建即消失）是 PR 02 就有的，本批**一行都沒有動**。PR 06 沒有讓任何 handler 跨 request 記住轉化意圖，所以也沒有「用 skill instance 當鍵、下一次 request 重新查證」這件事——沒有人寫，就沒有東西可以驗。「VM 重建後記憶遺失」若現在寫測試，驗的是 PR 02 的程式碼，不是這一批的 |
| 3a | **巢狀 `aiUseCard` 的上下文（§7.2 最後一點）** | `tryUseCard` 沒有帶 request 的 kind／reason，所以從一個回應詢問呼叫進來的巢狀規劃，套的仍然是同一套出牌階段判斷。計畫明寫要避免這件事，**本批沒有做，也沒有任何測試在擋它**。§11 把巢狀規劃歸在 PR 07 |
| 4 | **多目標的轉化** | 候選端仍把多目標判為不完整（P2 留下的 §5.2 第 2 點），轉化沿用同一套 `describeCardTargets`，所以多目標的轉化同樣連規劃都不會開始。留給 PR 07 |
| 5 | **`card_chosen` 族** | 與 PR 05 相同，仍然沒有 handler。轉化如果後面接一個選牌詢問，那一題照樣退回 legacy |
| 6 | **真正的轉化技能（正式武將）** | 只用 `~test` 套件的 fixture 驗過。沒有任何一個正式武將的轉化技能被接上，計畫 §8／PR 08 才做 |
| 7 | **真實對局的覆蓋率數字** | 與 PR 05 相同：一場都沒有跑。本批多了「轉化也能被規劃」這條路，但**沒有量過它在真實對局裡發生多少次** |
| 8 | **效能量測** | 沒有量。`buildCardConversions` 對每個 request 多做一輪 `createCard` 與目標探測，成本沒有量過 |
| 9 | **乾淨環境驗證** | 只在本機建置與執行 |
| 10 | **`ai-runtime.cpp` 的 Lua 錯誤訊息** | 見上面「診斷能力的缺口」。已定位、沒有修 |

## PR 05 明確沒有做的部分

照計畫 §11，PR 05 的範圍是「Duel、拆牌和必要估值依賴」。範圍內做完了，以下是**範圍內外都還沒做**、不要被上面的能力清單誤導的：

| # | 沒做的事 | 說明 |
|---|---|---|
| 1 | **過河拆橋指友軍** | 見上面的能力清單。卡在 `card_chosen` 這一族沒有隔離 handler；要做得先補那一族，不是補這張牌 |
| 2 | **`card_chosen` 族本身** | 順手牽羊拿哪一張、過河拆橋丟哪一張，現在全部退回 legacy。`ai_skill_cardchosen` 的 registry 在，但沒有任何 handler |
| 3 | **決鬥的 `duel-slash` 回應** | 同上，是 `respond_card` 的 `askForCard`，沒有 handler，退回 legacy |
| 4 | **計畫 §6.2 說的「裝備 → 其他牌族」** | 裝備牌一支策略都沒有；`ExNihilo`、`AmazingGrace`、`SavageAssault`、`ArcheryAttack`、`Collateral`、`Indulgence`、`Lightning`、`Nullification` 也都沒有 |
| 5 | **多目標牌族** | 候選端仍然把多目標判為不完整（P2 留下的 §5.2 第 2 點），所以 `Collateral` 這類牌連規劃都不會開始 |
| 6 | **技能互動邊界的「強制」版本** | 三支新策略都只把技能／裝備的影響寫成文件邊界，沒有讓「目標有未知的相關技能」變成 `unsupported`。這與 Slash 既有的作法一致，但兩者都還是宣告而非強制 |
| 7 | **真實對局的覆蓋率數字** | 一場都沒有跑。只能說多了三個牌族，不能說覆蓋率變成多少 |
| 8 | **效能量測** | 一個數字都沒有量，只量了 suite 的牆鐘時間 |
| 9 | **乾淨環境驗證** | 只在本機建置與執行 |

---

## 下一個 PR 的入口

1. ~~同步隔離 runtime 到 extensions 倉庫~~ **已完成（`4aeaf95`）**。
2. ~~`value-boundary.lua` 與 `smart-ai.lua` 那行 `dofile`~~ **已完成（`724980c`）**。
3. ~~計畫的 PR 05（P3 續）：Duel、順手牽羊／過河拆橋~~ **已完成**，見上面的 PR 05 明細。
4. ~~先解掉 2026-09-20 的既有阻斷~~ **已完成**，見「2026-09-20 的既有阻斷：如何解掉的」。`--suite room-runtime` 回 exit 0，回歸基準恢復。
5. **發布 PR 02–07 的三支 Lua**（`isolated-facades.lua`、`isolated-bootstrap.lua`、`isolated/decision-core.lua`）到 `lolosiyue/extensions`。2026-09-21 已反向同步至 H 端本地權威倉庫並核對 SHA-256；**commit／push 未授權、未執行**，遠端版本相容與發布阻斷仍存在。
6. 釘 `QSAN_EXTENSIONS_REF`，並填進 `docs/ai-runtime-manifest.json` 的 `ref_pinned`。要釘的對象是第 5 項推完之後的 commit，不是 `724980c`。
7. 在真正乾淨的環境（非本機）建置一次並跑 `room-runtime`——目前只驗到 fetch 層，沒有在乾淨環境建置。
8. ~~計畫的 PR 06（P4）：`card_spec` 轉化候選與技能實例~~ **已完成**，見上面的 PR 06 明細；`candidate_id` 授權券（§5.4）也一併做了。
8a. **計畫的 PR 07（P2/P4）：實作與本輪靜態收尾已落地，最終驗收未完成。** 四個目標、停止原因、靜態修正、同步及未完成 gate 見文末。下一輪先靜態複核，不啟動驗收或 PR 08。
9. 計畫的 PR 08（P5）、PR 09（P6）。**PR 09 需要固定機器與資料集的實際量測**（§9.3／§9.4 的 P50／P95／最大值、payload、5／10／20 人與 60／80 張壓力場景），至今一個數字都沒有量，只量了測試 suite 的牆鐘時間。


## PR 07（P2/P4）：有序多目標、巢狀規劃與分支 scratch

記錄日期：2026-09-21。開始 HEAD `8471f53`；工作期間另一 session 提交
`4e6b36a`，本輪靜態收尾時再移至 `9ac9ed6`。凜未執行 commit／push；保留 PR 02–06 與其他 session 的變更。
本節更新 PR 06 當時「明確沒有做」的其中幾項，歷史驗證結果不改寫。

### 能力與邊界

| 項目 | 本批落點與能力 |
|---|---|
| 有序目標 | `describeCardTargets` 以原生票數契約、禁止條件與完整前綴建立 `target_combinations`。Collateral 的 `targetFilter` 回 false 但票數非零仍可選；不能用 bool 取代票數。實體與轉化共用投影。 |
| 完整性／預算 | 每牌最多 2048 次探測、128 組、8 層；request 共用 16384 次目標／成本探測。`AiTargetProjectionBudget` 可下調，不能突破硬上限。超額清空組合且 `complete_coverage=false`。 |
| 作者選擇 API | `getTargetCombinations` 每頁最多 32 組；`getTargetSelection(prefix)` 回下一步與能否結束。資料未知回 nil，已知無組合回空頁。 |
| 共用規劃 | Slash 的多目標走原 `ai_card_use.Slash` 與 `rankTargets`；只選權威端列出的完整序列。規劃返回前再查序列，非法組合回 unsupported。 |
| 分支 scratch | 每次 tryUseCard 深複製父 scratch，保留成本／使用計數／選擇目標；正常、未覆蓋與 error 均還原父分支。回傳 plan.scratch 不與兄弟共用。沒有模擬 gameplay history、裝備、距離。 |
| 上下文 | `use.context` 與策略第 4 參數帶原始 kind/reason/pattern/handling_method。巢狀呼叫沿用；現有五種 Play 策略對 Response／ResponseUse 明確 unsupported。 |
| 規劃預算 | request 共用 128 次候選規劃／8 層遞迴；scratch 複製限 4096 值／16 層。超額傳 planning 未覆蓋訊號；instruction 繼續由 host 限額並以 AI_INSTRUCTION_LIMIT 回報。 |
| n ≥ 2 | `ViewAsSkillV2::hasIndependentAIConversion()` 預設 false。首批只支援固定 2–8 張獨立手牌成本、產物與目標規則不隨成本變化的顯式契約；一個 instance/source 發一張參數化票，不列舉子集。 |
| 多牌提交 | `cost_count / eligible_subcards`，Lua `withSubcards` 與 `newCard` 綁定副本；權威端重驗數量、重複、範圍、持有、每一步選牌、可結束、instance/source/quota 與重建產物。 |
| 測試入口 | 失敗碼 36 已由既有 Collateral 案例使用，本批接 37（投影）、38（Lua 分支／上下文／預算）、39（參數化成本）。同一組案例接入 room-runtime；ai-planning 是只跑這組的 focused 入口，沒有新增 CTest。 |

### 既有實跑記錄（已停止；本輪只讀證據）

命令皆在主工作樹執行；Qt PATH 前置 `H:\Qt6111\6.11.1\msvc2022_64\bin`。
測試 stdout/stderr 先導至 `builds/pr07/*.log`，旁邊 JSON 保存 child exit、耗時與當時 HEAD。

| 檢查 | 實際結果 |
|---|---|
| 指定 CMake runtime target，parallel 4 | 最初 exit 1：另一 session 同目錄 server_tests 建置占用 `.tlog`（MSB6003）；等待退出後重試 exit 0。後續 fixture／budget 建置均 exit 0。 |
| `python tools/ai/check-ai-runtime-manifest.py --root .` | exit 0；`files=14 required=7 core=3 package_handlers=2; the Lua declaration, the fetch gates and the asset manifest agree`。本批只新增 tests/lua 檔案，manifest 無新增 AI 檔。 |
| `--suite ai-planning --seed 2026082201`（planning-6） | exit 0，42.109 秒；`PR07 contracts passed: ordered targets, branches, costs, instruction limit`；最後 lifetime entries=0。 |
| room-runtime／runtime-contract | 最終狀態 NOT RUN；不得沿用 PR 06 或 planning-6 的通過結果。 |

前期失敗亦保留：planning-1/2 回 38（fixture 漏 player_order，目標查不到）；planning-3
回 38（漏 hujia）；planning-4 回 38（conversion fixture 漏 suit/number）。這些是修補
測試資料的失敗，不算破壞驗證。planning-5 在 135.703 秒由凜停止，child exit
4294967295：原預算 fixture 建 50 人完整快照，已改為三人＋降低探測額度的 bounded case；
該次不是 PASS，也沒有藉此展開引擎效能修復。新 fixture 使用 gameplay Lua binding，
避免原生規則探測打到錯誤 VM；planning-6 未出現前期的 rejected callback 訊息。

### 停止原因與最終狀態（2026-09-21）

**PR 07 未完整通過，也不符合「每個新增案例皆有破壞證據」。** 原生 targets
破壞驗證執行途中，使用者下令「禁止測試」，程序已停止；本輪再次明確禁止所有
建置、CTest、focused executable、smoke、完整對局及破壞驗證，後續要求
「下一步 prompt，先不驗收」。本輪只有來源／文件靜態審查與 Lua 檔案同步。

| 證據／gate | 狀態與限制 |
|---|---|
| `planning-6.json`／`.log` | 舊來源 exit 0、42.109 秒。之後的 rankTargets 缺玩家資料修正、追加案例及本輪靜態修正均沒有完成執行驗證。 |
| `break-scratch.json`／`.log` | 已完成 Lua 破壞案例：exit 38、32.219 秒，命中 root polluted。來源已還原。 |
| `break-context.json`／`.log` | 已完成 Lua 破壞案例：exit 38、31.078 秒，命中巢狀 context assertion。來源已還原。 |
| `break-candidates.json`／`.log` | 已完成 Lua 破壞案例：exit 38、33.188 秒，命中 candidate budget lost。來源已還原。 |
| `break-recursion.json`／`.log` | 已完成 Lua 破壞案例：exit 38、34.766 秒，命中 recursion limit lost。來源已還原。 |
| 原生 targets 破壞驗證 | **中止，未完成**。`break-targets.log` 只有初始化開始，無完成結果／JSON；不得算預期失敗。依上一輪交接，coordinator 暫時反轉目標改動已還原且當時 SHA 相符；本輪只核對目前有序投影來源，未重跑。 |
| 原生成本破壞驗證 | **未完成**，沒有完成證據。 |
| 最終 ai-planning／room-runtime／runtime-contract | **NOT RUN**；現有 JSON 不代表最終來源通過。 |
| 測試 executable／相關建置產物 | **仍是破壞版本，不可作正式 binary 或驗收證據**。最後成功建置是 `build-break-targets.log` 的目標反轉版本，包含 `builds/cmake-vs2026/tests/Debug/qsanguosha_runtime_tests.exe` 與相關 engine library／object。來源還原不會修復 binary；本輪沒有且不得為此重建。 |

上述證據均位於 `builds/pr07/`；本輪未改寫既有 log／JSON，也未啟動任何測試程式。

### 四個目標的靜態收尾（不是執行驗收）

| 目標 | 靜態結論／本輪最小修正 |
|---|---|
| §5.2 完整有序目標 | 實體牌與轉化共用有界投影，超額清空組合；prefix API 只查權威序列。通用規劃改以完整可行序列是否存在決定候選；`{}` 與 `{{}}` 分別是沒有可行動作及合法空目標動作。兩個 fixed-target 原生 fixture 補上空序列。 |
| §7.3 分支／三態／預算 | 找到更換根 scratch 可重置 private 候選計數的缺口，改由新 `ai_decide` 顯式重置；同 request 的新 facade 仍共用預算。完整性先於策略 declined 檢查；缺失組合／合法目標／所需敵友關係不得降成空集合，`pickTargets` 保留未知回 nil。 |
| §7.2 參數化成本／instance | n=2–8 opt-in、distinct 手牌成本、不列舉子集；原生提交沿用票、逐前綴選牌、可結束、來源根與 quota 重驗。修正 `newCard` 在第一張參數票綁定失敗就提前回 nil：繼續查後續符合條件的 instance/source，返回原票來源。 |
| §7.2 巢狀上下文 | kind/reason/pattern/handling_method 沿同 request 私有 context 傳遞；正常／unsupported／error 均還原父 scratch。五種正式 Play 策略的 Response／ResponseUse 仍 unsupported。 |

本輪只修改三支 Lua、`isolated-planning-contract.lua`、兩個原生 fixture 與兩份文件；
保留 PR 02–06、其他 session 變更及生產 Lua 錯誤日誌。新增案例涵蓋 unknown／known-empty、
scratch/facade 替換不重置預算、新 dispatch 重置，以及後續 instance 成本票選取；
**全數未執行、沒有新增破壞證據**。未替正式武將啟用 opt-in，沒有啟動 PR 08。
指定受追蹤檔案的 `git diff --check` 通過；這只是空白／差異檢查，不是編譯、Lua 語法或執行證據。

### 外部權威倉庫反向同步（僅本地，未發布）

本輪重新核對 `H:\Program file\Game\sgs\Qsgs\working\extensions`：分支 `main`，
origin 為 `https://github.com/lolosiyue/extensions.git`，HEAD 與 upstream 比較為 `0/0`；
`git ls-remote origin refs/heads/main` 與本地 HEAD 均為 `de2d4eb`。
同步前只有 `ai/scarlet-ai.lua`、`extensions/scarlet.lua` dirty，三支目標的
index／working tree 均乾淨。只同步以下檔案，逐支 SHA-256 相同；兩支 scarlet
的同步前後 SHA-256 亦相同，未覆寫其他修改。

| L 來源 | 外部倉庫目標 |
|---|---|
| `lua/ai/isolated-bootstrap.lua` | `ai/isolated-bootstrap.lua` |
| `lua/ai/isolated-facades.lua` | `ai/isolated-facades.lua` |
| `lua/ai/isolated/decision-core.lua` | `ai/isolated/decision-core.lua` |

本地權威工作樹已收到 PR 02–07 的累積 Lua 內容；**沒有 commit／push**，
遠端發布、釘版與乾淨環境 gate 仍未完成，不能把本地同步當作上游已發布。

### 本批明確沒有做的部分

| 項目 | 狀態／處置 |
|---|---|
| 任意 n ≥ 2 技能／花色點數繼承／裝備或私有牌堆成本 | 沒做；未宣告 independent 契約即 unsupported。沒有替任何正式武將啟用 opt-in，原生正例是測試專用技能。 |
| 重複投票、超過本次投影上限的完整組合 | 沒做；unsupported。分頁只讀本次完整投影，沒有跨 request 續取／同步 gameplay Lua callback。 |
| Collateral 正式策略、Duel／拆牌多目標估值、其他牌族 | 沒做；候選投影完整不等於該牌策略完成。Duel／拆牌遇多目標仍 unsupported。 |
| Response／ResponseUse 的五張牌正式策略 | 沒做；上下文能傳遞且守門，不代表救援／回應策略完成。 |
| 假設裝備後重算距離、真實 history／flags／quota 模擬 | 沒做；scratch 只有純值預留與分支紀錄。 |
| 跨 request 意圖、VM 重建後的持久記憶恢復、Shadow／覆蓋率／性能基線 | 沒做；不宣稱真實 activate 覆蓋率。 |
| 生產 Lua 錯誤日誌、引擎／UI 其他批次 | 未改；`ai-runtime.cpp` 本批只增加 DTO 序列化。 |
| 完整對局、GUI、Linux／Docker／Android、full CTest | 本批未執行。 |
| commit／push／上游發布 | 未執行；Lua 上游发布阻斷仍存在。 |
