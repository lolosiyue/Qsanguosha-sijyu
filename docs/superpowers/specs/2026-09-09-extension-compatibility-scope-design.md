# 讓正式使用的擴展包進入相容範圍 — 設計

日期:2026-09-09
狀態:設計已確認,未實作
基線:`debug` @ `401e7f3`,即 W1/W2/W3b 之後

## 0. 一句話

把 `builtin-v1` 那個「五個 Lua 檔以外一律拒絕」的固定內容契約,換成
**宣告驅動的內容身份**:code identity 仍然 build 時封死,content identity
改為執行期協商,令任意「使用既有 bindings 的 Lua 擴展」免重建 WASM 即可用。

---

## 1. 範圍與不變式

### 1.1 目標的精確定義

「任意擴展」實際可達成的定義是:**任意使用既有 bindings 的 Lua 擴展,免重建 WASM 即可用。**

其餘三類仍然是 build 事件,這條界是「code 已編進二進位」的直接後果,不會因為工程努力而消失:

| 擴展類型 | 可否免重建 | 原因 |
|---|---|---|
| 用既有 bindings 的 Lua | **可以** | 只是腳本位元組,WASM Lua VM 執行期可載入 |
| C++ package | 不可以 | 已編進二進位 |
| 新增原生 API 的 Lua | 不可以 | 要新 bindings → 新 wasm |
| 新互動 schema | 不可以 | 要 renderer 支援 |

### 1.2 四條不變式

後面每個決定都必須服從:

1. **註冊集合與順序決定 card ID**;身份必須同時覆蓋兩者。
2. **未宣告的內容一律拒絕** —— 閘不可以移走,只可以由「固定名單」轉為「宣告驅動」。
3. **code identity 必須完全相等**;content identity 執行期協商。
4. **註冊集合 ≠ 對局啟用集合。**

### 1.3 不在範圍

- desktop / TUI 執行期下載內容(desktop 維持「檔案已在磁碟」)
- 熱重載、live content replacement
- 多個具名 profile 切換
- 瀏覽器內單機(見 §7,只記約束不實作)

---

## 2. 擴展 manifest(engine 層)

### 2.1 現況

`Engine::Engine()` 的實際次序:

| 步驟 | 位置 | 動作 |
|---|---|---|
| 1 | `engine.cpp:312` | `builtinLuaSnapshot()` —— 在任何 Lua 執行之前影相 |
| 2 | `engine.cpp:322` | 執行 `lua/config.lua` |
| 3 | `engine.cpp:338-349` | 照 `package_names` map 順序註冊 C++ packages |
| 4 | `engine.cpp:409` | 執行 `lua/sanguosha.lua` → 載入 `extensions/*.lua` |

第 4 步的順序來自 `sgs.GetFileNames`(`swig/native.i:23-28`),用
`QDir::entryList(QDir::Files)`,預設排序 `Name | IgnoreCase`。

**所以順序是檔名排序,跨機器具決定性。** 真正的問題不是不決定性,而是:
順序**隱式且依賴整個檔案集合**。加一個、改名一個、刪一個擴展,排在其後的
所有擴展牌實體 ID 全部推移。今日無事,只因所有 shell 剛好讀同一個目錄。

`m_rulesPackageOrder`(`engine.cpp:750`)只是**記錄**結果,不是**規定**順序。

### 2.2 停用機制已經存在而且正確

`ServerInfo.BanPackages`(`server-info.h:17`)是**註冊之後才遮蔽**:牌照樣進
registry,只在發牌/選將時濾走(`package-dialogs.cpp:142`、
`customassigndialog.cpp:1747`),並經 setup 字串第 4 欄傳到 client
(`server-info.cpp:45`、`client.cpp:466`)。card ID 不會因停用而變。

**設計約束:manifest 與 BanPackages 必須是兩樣東西,不可合併。**
manifest 決定「註冊什麼、什麼順序」(影響 ID);BanPackages 決定「本局用什麼」
(不影響 ID)。明文禁止用 manifest 做停用機制。

### 2.3 manifest 位置與形狀

**位置**:`lua/config.lua`,放在 `package_names` 旁邊。理由:同一份檔、同一個
bootstrap Lua state(`config.lua` 與 `sanguosha.lua` 都在 `m_bootstrapLua` 執行),
而 `config.lua` 本身已在 hash closure 內 —— manifest 改動自動反映到 `lua_hash`,
不需要第二個 hash 來源。

**形狀**:`extension_names` 是**有序條目陣列**(不是 map;順序是語意的一部分,
必須顯式)。每個條目描述一個擴展連同它的衛星檔:

| 欄位 | 例 | 進註冊序列 | 進 `lua_hash` | 配送去 web |
|---|---|---|---|---|
| `script` | `extensions/sijyu.lua` | **是**(順序關鍵) | 是 | 是 |
| `libs` | `lua/luaoldenemy_lib.lua` | 否 | 是(可執行規則碼) | 是 |
| `lang` | `lang/zh_CN/Package/Sijyu.lua` | 否 | **否** | 是 |
| `ai` | `lua/ai/sijyu-ai.lua` | 否 | 否 | **否,可選**(見 §7) |

只有 `script` 的順序有語意;其餘三類是集合,排序做正規化即可。

`libs` 是真.可執行規則碼,與翻譯不同,**必須**進 `lua_hash`。

`lua/ai/` 已有 `<擴展名>-ai.lua` 命名慣例(`sijyu-ai.lua`、`Dragon-ai.lua`…),
即「擴展帶衛星檔」的關係本來就存在,只是靠檔名隱式成立;manifest 把它寫明。

### 2.4 載入器改動

`lua/sanguosha.lua:20` 由 `sgs.GetFileNames("extensions")` 改為照
`extension_names` 逐個 `require`:

- 清單有、檔案不存在 → **硬失敗**。不可靜靜跳過(靜靜跳過 = 其後所有擴展 ID 推移)。
- 目錄有、清單無 → 不載入,且身份掃描視為未宣告 → 拒絕。
- `GetFileNames` 本身保留,仍有第一類擴展使用。

`lua/sanguosha.lua:145-160` 的翻譯載入同樣用 `GetFileNames` 掃三個目錄
(`""`、`Audio`、`Package`)。建議一併改為照宣告載入,但**不是阻塞項** ——
它在 `addSkills` 之後才執行,最壞後果只是重複 key 蓋錯,不影響 ID。

### 2.5 影響面

四個 shell(`src/main.cpp`、`src/tui/tui-main.cpp`、`src/server-main.cpp`、
`src/client/runtime/client-rules-host.cpp`)都 `#include "core/engine-bootstrap.h"`
各自 `new Engine`,跑同一份 `sanguosha.lua`。所以 manifest 屬於 **Engine 層**,
一改四個 shell 同時受惠。

desktop 今日帶著同一個潛在缺陷 —— server 與 client 只是「剛好目錄一樣」所以
card ID 對得上,沒有任何東西**保證**。加入宣告式清單等於順手補上這個保證。

### 2.6 遷移

一次性機械動作:從現有 `extensions/` 生成初始 `extension_names`,內容即今日的
檔名排序結果,使現有 desktop 部署的 card ID 不變。驗證見 §6 G1 最後一行。

---

## 3. 身份模型拆分

### 3.1 欄位重新分組

現有身份欄位(`rules-bundle-exporter.cpp:104-113`)拆兩組:

| 組別 | 欄位 | 何時定 | 比對規則 |
|---|---|---|---|
| **code identity** | `protocol_version`、`bridge_schema`、`cpp_hash`、`bindings_abi`、`interaction_schemas` | build 時封 | 必須完全相等 |
| **content identity** | `ruleset`、`content_profile`、`packages`、`card_registry_hash`、`lua_hash` | 執行期 | 客戶端載入宣告內容後重算,必須相等 |

**不新增**「可註冊 C++ package 清單」欄位:`cpp_hash` 相等已蘊含兩邊編出同一批
package factory,加欄位是冗餘。

### 3.2 兩層封印

`bundle_id` 保留為最終總封印,**新增 `code_id`**(只封 code identity 五個欄位),
用途是在落載內容之前就能比對。無 `code_id` 的話,client 要下載完整內容才發現
bindings 不夾,浪費且訊息模糊。

`content_profile` 由字面值 `builtin-v1` 改為 `declared-v1` —— 由「這坨固定內容」
變成「內容以宣告方式描述」的**種類**標記。舊 client 見到 `declared-v1` 會在
`web/src/rules-identity.ts:45` 直接擲 `rules_content_unsupported`,乾脆失敗,
不需要額外版本邏輯。

### 3.3 snapshot 時序拆兩相

`m_rulesLuaSnapshot = builtinLuaSnapshot()` 在 `engine.cpp:312`,即任何 Lua 執行
之前。今日可行是因為五個檔名寫死;manifest 住在 `config.lua` 內,影相那刻還不
知道要影哪些擴展。

解法是影兩次,兩次都保持「執行前影相」:

- **相一**(維持 `engine.cpp:312`):核心五檔,與今日相同。
- **相二**(`config.lua` 執行後、`sanguosha.lua` 執行前,即 `engine.cpp:338` 附近):
  照 `extension_names` 宣告順序影擴展檔與 `libs`。

兩相都在任何擴展 Lua 執行之前完成,所以「不可用磁碟上的替換檔案重新標籤一個
已載入的 VM」這條性質保住。

`lang` 角色的檔案**不進 `lua_hash`**,但配送仍需逐檔 hash 才能驗證下載位元組。
因此配送清單的 hash 與 `lua_hash` 是兩個不同的計算:配送清單涵蓋
`script` ∪ `libs` ∪ `lang`(以及 §7 開啟單機後可選的 `ai`),`lua_hash` 只涵蓋
核心五檔 ∪ `script` ∪ `libs`。兩者不可混為一談。

`config.lua` 是在自己被 hash 之前執行的。可接受:hash 的是磁碟位元組,而執行
自己不會改自己。此點必須在實作註解中寫明,避免日後被當成缺陷「修正」。

### 3.4 核心五檔

`rules-bundle-exporter.cpp:21-23`:

```
lua/config.lua  lua/sanguosha.lua  lua/utilities.lua  lua/sgs_ex.lua  lua/lib/json.lua
```

留意今日一個未修改的 repo checkout **本身就過不了身份閘**:除 `extensions/` 外,
`lua/chat_config.lua`、`lua/luaoldenemy_lib.lua`、`lua/lib/sqlite3.lua` 三個都不在
五檔名單,一樣觸發 `rules_content_unsupported`。

### 3.5 拒絕掃描改寫

`builtinLuaSnapshot()` 內的 `lua/`、`extensions/`、`lang/` 掃描
(`rules-bundle-exporter.cpp:39-53`)由「五檔白名單以外一律拒絕」改為:

> 掃到的 `.lua` 集合必須**恰好等於** 核心五檔 ∪ 宣告的 `script` ∪ 宣告的 `libs`
> ∪ 宣告的 `lang` ∪ server-only AI 路徑

多出未宣告的檔 → 拒絕(否則 server 悄悄多一個檔就改了規則而 hash 不覺)。
少了宣告過的檔 → 拒絕。symlink 一律拒絕(不變)。

`lang/zh_CN/*.lua` 順帶解鎖:今日它們一律令身份失敗,即任何有翻譯檔的部署都
上不了 Web。擴展自己的翻譯是**內嵌**的(`extensions/sijyu.lua:4,9,20` 用
`sgs.LoadTranslationTable`),所以加擴展不會連帶要加 `lang/` 檔。

### 3.6 為何 `lang/` 排除在 `lua_hash` 之外

`lua/sanguosha.lua:145-160` 的翻譯載入在 `addSkills` **之後**,所以 `lang/` 對
card ID 零影響;翻譯本身是純表現層(server 傳 key,client 譯字)。兩邊翻譯不同,
規則不會錯。

跟 `lua/ai/` 的先例:**進配送清單,不進 `lua_hash` / `bundle_id`**。好處是改一個
錯別字不會令全世界 bundle 失效。代價是顯示文字可能不同步,可接受。

### 3.7 錯誤碼

全部沿用 `web/src/rules-identity.ts` 已有的,不新增:

| 情況 | 錯誤碼 |
|---|---|
| `code_id` 不夾 | `rules_version_mismatch` |
| 內容下載/hash 失敗 | `rules_reload_required` |
| 未宣告內容 | `rules_content_unsupported` |
| 宣告的互動 renderer 不支援 | `rules_interaction_unsupported` |

---

## 4. 配送機制與 bootstrap 時序

### 4.1 硬阻塞:內容現在焊死在 `.wasm` 內

`cmake/QSanguoshaRulesWasm.cmake:79`:

```cmake
"SHELL:--embed-file \"${qsan_wasm_assets}@/assets\""
```

Lua 資產是 **link 時嵌入二進位**的。所以今日「換內容」字面上等於「換 `.wasm`」——
這正是 W2 的身份可以 build 時封死的原因。

拆走 `--embed-file` 是本方案的核心動作,回報是:**`.wasm` 變成內容無關,一次
build 服務所有內容組合**。Loader 那句 "verifies embedded Lua bytes" 由驗嵌入
位元組改為驗注入位元組,檢查強度不變。

### 4.2 時序倒轉

W2 有一條性質:*The Web controller initializes before opening the WebSocket,
preserving the server's existing signup deadline*。內容執行期協商後這條**必然
打破** —— 未收到 Hello 就不知要載入什麼。

新序列:

| 步 | 動作 | 失敗碼 |
|---|---|---|
| 1 | 取 + 驗 WASM code 產物(build 時封,同 W2) | `rules_reload_required` |
| 2 | 開 WS,收 Hello | `rules_identity_required` |
| 3 | 比 `code_id`,**不下載任何內容** | `rules_version_mismatch` |
| 4 | 按宣告取內容位元組,逐檔驗 hash | `rules_reload_required` |
| 5 | 寫入 Emscripten FS(`lua/`、`extensions/`、`lang/` 相對路徑) | — |
| 6 | `initialize` → Engine bootstrap → 重算身份 | `rules_content_unsupported` |
| 7 | 比 `bundle_id` → Signup | `rules_version_mismatch` |

第 2 到 7 步全部要塞在 **30 秒**內(`server.cpp:2150`、
`server-connection-context.cpp:21`)。

### 4.3 預熱

補救不是改 deadline,而是預熱:頁面載入時由 localStorage 取回上次成功的內容集合,
在開 WS 之前先 fetch + bootstrap。Hello 一到,若 `bundle_id` 對得上就直接跳到
第 7 步。W2 那條「開 WS 前已 initialize」的性質在回頭客身上原樣保住;只有首次
連線或內容真的變了那次才走足七步。

預熱猜錯就要重來,故配一條規則:**一個 Worker 服務一個內容集合,內容變 = 換
Worker**。避免 FS 殘留上一次的 `extensions/*.lua` 污染 `QDir::entryList` ——
殘留一個檔就足以推移註冊順序。W1 已有「Worker 出事即替換」的先例。

### 4.4 傳輸:內容定址 HTTP(已定案)

hash 清單由 Hello 供應(權威),位元組由**頁面 origin** 取,路徑 =
origin + 固定前綴 + hash。

- W2 那條「沒有任何 module URL 來自 server metadata 或玩家輸入」原樣保住:
  URL 不是 server 說了算,是 origin + 內容 hash 砌出來的。
- 內容定址 = 可永久快取,同一個擴展跨部署只下載一次。
- 限制:client 只能玩 origin 有那批內容的 server。因目標是自家正式 server,
  前端與 server 同一部署,可接受。

**不採用**行 WebSocket 配送:需 base64(+33%)、食 30 秒窗、受原生協議 frame
上限限制,且 signup 之前就要向未認證 peer 送數 MB,是 DoS 面。

若將來要開放第三方 server,可加 fallback:origin 沒有該 hash 就轉行 WS 取。
**現在不建,YAGNI。**

---

## 5. TS fallback 清除

### 5.1 實際暴露面

`web/src/eligibility.ts` 對外只有三個 export 真正被 UI 使用:`cardSelectable`、
`playerSelectable`、`useMode`(`ui.ts:3-6`、`ui-cards.ts:8`)。其餘
`distanceTo`、`attackRange`、`inAttackRange`、`isTargetFixed`、`canSelectPlayer`、
`matchPattern` 只在 `eligibility.ts` 內部與 `web/tests/eligibility.test.ts` 使用。

`web/src/player-metrics.ts` **不** import eligibility;它走「原生 metric 優先 →
回落到 server 同步的 `fixed_distances`/`distanceTo_<to>` → 不知就顯示『？』」
(`player-metrics.ts:75-95`)。這已是正確做法,不需改動。

`useMode`(`eligibility.ts:409-418`)的涵蓋:

| 模式 | command | 現況 |
|---|---|---|
| `play` | `PLAY_CARD` | 已 native,TS 分支到不了 |
| `response` | `RESPONSE_CARD`、`ASK_PEACH`、`NULLIFICATION` | 已 native |
| `discard` | `DISCARD_CARD`、`EXCHANGE_CARD` | **未 native — 唯一活路徑** |
| `free` | 其餘全部 | `cardSelectable` 即刻 `return true`,不碰任何表 |

`supports()` 只查靜態 command 清單(`rules-client.ts:140-142`),不會因 runtime
掛掉而回落 —— runtime 未 ready 就是「沒有東西可以按」,不會偷偷用 TS 規則。
所以 `canSlash`、`WEAPON_RANGE`、坐騎表在實際執行路徑上**已是死碼**。

**結論:唯一會被擴展內容弄錯的活路徑,是 `DISCARD_CARD` / `EXCHANGE_CARD` 經
`matchPattern` 撞 `ANCESTORS`(`eligibility.ts:276`)。** 一張擴展牌不在
`ANCESTORS` 內,棄牌/交換牌時 pattern 比對會判錯 → 高亮錯。server 仍會拒絕
非法回覆,所以是體驗缺陷而非規則缺陷。

### 5.2 做法

1. **`DISCARD_CARD` / `EXCHANGE_CARD` 收進 native** —— 加入 `NATIVE_COMMANDS`,
   原生側 `ClientRulesSession` 要為這兩個 request 產出 `selectable_cards`。
   這是唯一有實質份量的工作,且**與 `docs/native-rules-ingress.md` 的 W4 投影
   工作重疊**,應當作 W4 的一個明確項,不是新工作。
2. **之後整個 `eligibility.ts` 可刪**。`cardSelectable` 塌縮成
   `rules.supports(cmd) ? native : true`(保住 `free` 模式現有行為);
   `playerSelectable` 只剩 `CHOOSE_PLAYER` 讀 `payload.players` 一句,內聯到
   `ui.ts`;`useMode` 無表依賴,搬到 `protocol.ts` 或 `ui-types.ts`。
3. `web/tests/eligibility.test.ts` 跟著刪(不是改寫 —— 測的東西消失了)。

### 5.3 排序

這是**擴展內容上線前的前置條件**,但不是 §2/§3/§4 的前置,可並行。
唯一硬約束:**擴展內容真正上線之前,第 1 步必須完成**,否則棄牌高亮會在新牌
上出錯。

---

## 6. 測試策略

擴充現有 `tests/client_runtime/check-rules-bundle.py`(已有 `negative_exports()`、
`check_server_ai_identity()`、artifact 變更偵測、root route allowlist),**不開
第二套 harness**。

### G1 — 註冊順序穩定性(原生,新增)

輸入 `--export-rules-bundle`,純原生,秒級。

| 案例 | 期望 |
|---|---|
| manifest **尾部**加一個擴展 | 原有每個 `(id, object_name)` 對**完全不變** |
| manifest **中間**插一個擴展 | `card_registry_hash` 必須變 |
| 只改 `BanPackages` | `card_registry_hash` **不變** |
| 生成初始 `extension_names` 前後 | `card_registry_hash` 完全相同(遷移安全網) |

### G2 — 宣告閘(原生,擴充 `negative_exports`)

- 宣告了、檔案不存在 → **硬失敗**,不准靜靜跳過
- 目錄有未宣告 `.lua` → `rules_content_unsupported`
- 宣告順序改變 → `bundle_id` 改變
- 改 `lang/` 檔 → `lua_hash` 與 `bundle_id` **都不變**
- 改 `lua/ai/` → 同上(`check_server_ai_identity` 已覆蓋一半)
- symlink → 拒絕(現有,保留)

第四、五項是角色分類的實證:沒有它們,「presentation / ai 排除在身份之外」
只是文件說法。

### G3 — 內容配送(瀏覽器,擴充現有 harness)

- hash 對 → 成功;位元組改一個 byte → `rules_reload_required`
- **`code_id` 不夾 → 一個內容請求都不應發出**(這是 §3.2 加 `code_id` 的唯一
  理由,沒有測試就沒有意義)
- root route allowlist 延伸到新內容前綴
  (`test_root_routes_are_an_exact_allowlist` 已有框架)
- 注入後 WASM 內 `QDir::entryList("extensions")` 的集合 = 宣告集合(防殘留)
- 內容變 → 換 Worker,舊 Worker 不可再回答

### G4 — 原生 ↔ WASM 身份對等

同一份 manifest,原生匯出與 WASM 執行期重算,`bundle_id` 必須逐字相等。
這是現有 W2 gate 的核心,輸入由「五檔固定」換成「宣告內容」。

**必須加一個非平凡案例**:manifest 內至少有一個真實擴展
(`extensions/sijyu.lua`),否則等於沒測過新路徑。

### G5 — 棄牌/交換牌 native 覆蓋

`tests/client_runtime/rules-controller.test.mjs` 加 `DISCARD_CARD` /
`EXCHANGE_CARD` 案例;`eligibility.ts` 刪除後 `web/tests/eligibility.test.ts`
一併刪。「擴展牌棄牌高亮正確」的案例需要真內容,掛在 G4 的非平凡案例上。

### 明文不做

- 不跑完整對局驗收 —— web client wasm 完成後由使用者自行驗證
- 不做 pthread / 單機
- 不做效能與記憶體上限
- **`ctest -L fast` 有既有紅燈基線**。新 gate 不可用「整體綠」作通過條件,
  必須逐項對照基線,否則會把舊紅燈當新缺陷(或反之)。

---

## 7. 未來里程碑:瀏覽器內單機

**不在本次範圍。** 此節只記錄約束,避免本次落下封死它的決定。

### 7.1 伺服器代碼已在 WASM 二進位內

`CMakeLists.txt:284` 的 `qsanguosha_engine` 靜態庫**無條件**包含 48 個
`src/server/*` 檔,包括 `src/server/room.cpp`、`roomthread.cpp`、`ai.cpp`、
`server.cpp`。而 `cmake/QSanguoshaWebClient.cmake:7` 是:

```cmake
"$<LINK_LIBRARY:WHOLE_ARCHIVE,qsanguosha_engine>"
```

即 `Room`、`RoomThread`、SmartAI 宿主、整個對局引擎**今日已編進**
`qsanguosha_client_wasm.wasm`。這不是「移植伺服器到 WASM」,而是「已經在裡面,
但沒有路徑叫得動它」。

### 7.2 四個真障礙(全部執行期,非編譯期)

1. **執行緒** —— `Room` 本身是 `QThread`(`room.cpp:200`),對局實際跑在
   `GameSessionController::run()` 起的 `RoomThread`,等回覆用
   `SEMA_COMMAND_INTERACTIVE` 阻塞式 semaphore。WASM 需 `-pthread` +
   SharedArrayBuffer,部署要有 COOP/COEP 跨源隔離標頭。阻塞發生在 worker 執行緒
   而非瀏覽器主執行緒,模型是夾的;但 `-pthread` 是另一個 ABI,可能需要第二個
   wasm profile。
2. **傳輸** —— `Server` 綁 QTcpServer。單機需要行程內 loopback 傳輸頂替 socket。
   GUI 版是 `new Server(qApp)`(`main.cpp:345`)再經真 socket 接自己,瀏覽器做不到。
3. **AI 內容** —— SmartAI 是 Lua,`lua/ai/` 162 個檔,本設計明文排除。無 AI 即無單機。
4. **對局壽命** —— 收尾路徑歷史上出過兩次 UAF,瀏覽器內重開一局要走同一條路。

### 7.3 本次不可違反的兩條約束

1. **「排除在身份之外」不等於「永遠不配送」。** §2.3 的角色欄位必須是**資料**
   (`rules` / `presentation` / `ai`),不可寫死成三個 if 分支。自寄主機的 client
   將來只需多取 `ai` 角色那批,身份計算一個字都不用改。
2. **`.wasm` 必須維持內容無關**(§4.1 拆走 `--embed-file` 的結果)。單機要載入的
   內容集合(多了 AI)與連線模式不同,焊死內容等於封死單機。

做完本次,單機只剩「加 pthread profile + loopback 傳輸」兩件事。

---

## 8. 決策紀錄

| 決定 | 選擇 | 理由 |
|---|---|---|
| 相容範圍 | 任意第一類 Lua 擴展 | 使用者指定 |
| manifest 位置 | `lua/config.lua` | 已在 hash closure 內,不需第二個 hash 來源 |
| desktop 內容來源 | 維持磁碟,不執行期取 | 範圍最小,四個 shell 都拿到 ID 穩定性 |
| 內容傳輸 | 內容定址 HTTP | 可快取、不食 signup 窗、無未認證 DoS 面 |
| `code_id` 欄位 | 加 | 避免下載完才發現 bindings 不夾 |
| `lang/` 身份歸屬 | 排除在 `lua_hash` 外 | 純表現層,不影響 card ID |
| 第三方 server 配送 fallback | 不建 | YAGNI |
| 瀏覽器內單機 | 不在本次範圍,只留門 | 需 pthread + loopback 傳輸,獨立里程碑 |
