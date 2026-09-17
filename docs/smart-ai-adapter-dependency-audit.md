# SmartAI 共用轉接層依賴盤點

日期：2026-09-17。範圍：通用轉接層（Adapter），不是逐技能策略移植。

後續實作註記：B0「代理型別與集合」已加入 AIValue／AIList、sandbox 集合 helpers 及
SmartAIView:hasSkills，詳見 [契約規格 §15.2.2](lua-ai-spec.md#1522-共用轉接層request-內物件映射)。
已補契約案例，尚未執行驗證。下表來源雜湊／行號保留為盤點當下基準，並非修改後版本；
其他舊共用入口的 userdata guard 與 native 查詢仍未接入。

## 架構順序的目前狀態（2026-09-17）

外部 AI 腳本的同步狀態：`lua/ai/` 不在本倉庫版控內，對應
[lolosiyue/extensions](https://github.com/lolosiyue/extensions) 的 `ai/`。第二至十六批的
Lua 端改動（`smart-ai.lua`、`value-boundary.lua`、`isolated-bootstrap.lua`、
`isolated-facades.lua`、`isolated/ask-for-use-card.lua`、`isolated/ask-for-choice.lua`、
`isolated/decision-core.lua`）已同步並提交為該倉庫 `main` 的 `e33a1f4`（尚未 push）。
該倉庫 `core.autocrlf=true`，所以工作區換行會轉成 CRLF，內容與本工作樹逐字相同；
`ai/data/` 是執行期資料，不納入同步。


| 項 | 內容 | 狀態 |
|---|---|---|
| 1 | 共用入口型別契約 | 程式完成（第三批） |
| 2 | 回呼介面與分派 | 程式完成（第二批） |
| 3 | 結果轉換 | 程式完成（第二批） |
| 4 | 所有 AI 請求種類 | 程式完成，二十個公開入口都走新通路（第四批） |
| 5 | 可見牌區投影 | 程式完成（第五批） |
| 6 | 玩家／卡牌／技能共用資料 | 程式完成（第六批） |
| 7 | 合法候選契約 | 程式完成（第七批） |
| 8 | 值型出牌與推演 | 程式完成（第八批） |
| 9 | 技能實例接線 | 程式完成（第九批） |
| 10 | 值型事件上下文 | 程式完成（第十批） |
| 11 | AI 狀態與生命週期 | 程式完成（第十一批） |
| 12 | 模式／身份共用服務 | 程式完成（第十二批） |
| 13 | 通用決策流程 | 隔離版決策核心完成（第十四批）；個別武將策略仍在舊 AI |
| 14 | 載入與相容層 | 程式完成（第十五批） |
| 15 | 提交與過期結果 | 程式完成（第十三批） |
| 16 | 切換與驗收 | 統計與程序完成（第十六批）；**驗收未執行**——要建置、跑完整對局與效能量測 |

所有批次都只做過 Lua 語法檢查與靜態檢查：**尚未建置、尚未執行任何測試或對局**。
在通過 §15.2.17 的門檻之前，不應宣稱任何入口已完成切換。

## 結論與來源邊界

下一批應先統一「代理型別／集合／回呼契約」，再補可見資料投影。只增加同名 getter，
仍會被舊程式的 userdata 判斷、QList 操作及回呼參數差異阻擋。

| 項目 | 本輪基準／限制 |
|---|---|
| 主倉庫 | L 工作樹 `debug`，HEAD `c7b758bff159f4b9fe738bb2b032fa917c067fdb`，包含前批尚未提交的 Adapter 修改；不是純 HEAD 快照 |
| SmartAI | `lua/ai/smart-ai.lua` SHA-256 `86366CA7581406F2D841AFC30F7E0195B56B2A9C75E71E2B7F64ED4F90B7DF89` |
| 現行 facade | `lua/ai/isolated-facades.lua` SHA-256 `77775C48C4704BA1B145D15F27063C5BE83FFE2FB1CF05C0F26E4C7C0666318A` |
| 模式橋接 | `lua/ai/mode-ai.lua` SHA-256 `95A2822146AFA2AD31F1F2F3DE39A785D2CA5267CF2BE031518805904229D1F5` |
| 盤點方式 | C++ 用圖查詢定位 `AiDecisionCoordinator`／`AiLuaRuntime`，再核對工作樹；Lua AI 為索引排除區，定向讀取共用函式及 API 引用 |
| 圖的限制 | 部分 snippet 行號受前批修改影響而偏移，部分 call edge 是同名誤配；不以圖輸出單獨證明呼叫語意 |
| 完整度 | 覆蓋下表共用入口族與直接邊界；動態 registry、`self["useCard"..name]` 和外部擴展 callback 未做全量傳遞閉包，也不宣稱所有技能可遷移 |
| 驗證 | 本輪唯讀來源分析、文件與靜態差異檢查；未建置、未跑 Lua 契約／focused executable／CTest／對局／CI |

來源連結以檔案與符號為準；以下行號是此份盤點當下的定位，後續修改須重新核對。

## 現有通路與三種分類

```text
Room 的權威狀態
  → AiDecisionCoordinator.makeRequest / buildWorldView
  → AIRequest + viewer-scoped AIWorldView
  → AiLuaRuntime.pushRequest
  → isolated-bootstrap.ai_decide
  → SmartAIView + RoomView + PlayerView/CardView/SkillView
  → 純值結果 → parseResult → applyResult → 原有 gameplay 執行流程
```

| 類別 | 定義 | 處理原則 |
|---|---|---|
| A：快照已具備 | AIRequest／AIWorldView 已有必要純值；可能還缺 facade helper 或相容契約 | 在 request 內轉接，不回查原生物件 |
| B：需補投影／契約 | 資料、合法候選、事件上下文或 AI 私有狀態目前不完整 | 先定義來源、可見性、生命週期與缺失語意，再增加有界純值資料 |
| C：禁止原樣跨界 | native pointer／userdata、live gameplay 查詢回呼、原生 mutation、任意檔案或 Engine 存取 | 原生操作留在權威端；AI 只收資料／提出值型動作，不做 SWIG 透傳 |

B 與 C 可以出現在同一函式：例如 `CardFilter` 的「過濾後卡牌值」可研究 B 投影，
但它現在暫改 Room card mapping 的實作是 C。禁止的是直接帶入原操作，不是永久禁止該能力。

Room gameplay Lua VM 本來已按房間分離；`sgs.*` 在這裡是 VM 內全域，不應稱作所有房間
共用的一份進程全域。此次遷移要消除的是對 gameplay VM 的隱式上下文與 native 物件依賴。

## 共用入口族

| 入口／來源定位 | 直接依賴 | 分類與下一個接點 |
|---|---|---|
| `CloneAI`、`SmartAI:initialize`（smart-ai:22、338） | `sgs.LuaAI`、player:getRoom、current_self、global_room、initialized tag、Config、registry | A：viewer／room 值已存在；B：明確的初始化設定／私有狀態；C：不可在隔離 VM 建 native LuaAI 或寫 Room tag |
| `askForUseCard`（3887） | `sgs.ai_skill_use`、pattern／prompt fallback、method、compulsive `!`、cardEffect、轉牌與合法性 | A：request 欄位；B：相容分派／結果契約、effect 與候選；C：不可直接搬 native cardsView／dummyCard 路徑 |
| `activate` → `getTurnUse`（5067、4839） | self.toUse／use_to、fillSkillCards、排序、canMethodUse、aiUseCard、CardUseStruct | B：request 內候選、推演暫存及值型 use；C：不保留 native Card、QList、request userdata 到下一次決策 |
| `askForSkillInvoke`／Choice／Suit／General／Discard／Card／AG／CardShow／Yiji／Pindian／PlayerChosen／PlayersChosen／SinglePeach（2841–4774） | 各自 registry、data、candidate list、原生結果型別 | B：尚需各種 request/result 契約與 C++ 接點；DecisionKind 已含 Activate／UseCard 與值型詢問五種（第四批），其餘仍缺，不能只註冊 Lua 函式便宣稱已接通 |
| `getCachedAlivePlayers`／AllPlayers、qlist_cached（427–463） | global_room、事件清 cache、QList | A：RoomView 名單；B：array 相容與 request 內快取契約；不搬 live QList cache |
| `sgs.getPlayerSkillList`、`aiConnect`、hasSkills（464、3118、5963） | native Skill 類型、lord skill、marks、pile names、equips、字串 helper、cache | A：可見 SkillView／mark／equip 值；B：技能種類／主公技有效性、pile 名稱、型別辨識；不可用 getSkill 回查秘密能力 |
| objectiveLevel／isFriend／isEnemy／getFriends／getEnemies、控制鏈（1407–2093；mode-ai:297） | mode_policy 或舊 roleValue／ai_role／lua_ai、Controller_Name tag | A：managed mode_policy 與 controller；B：舊模式未投影的評分與每觀察者推測；不能把未覆蓋模式當 neutral／無敵人 |
| `updateIntention`／updatePlayers／evaluateAlivePlayersRole（1430、2030、2094） | 每次事件的意圖、全域四身份推測、sgs.ais | B：每 Room／viewer 的純值推測與事件；C：不共享別的觀察者秘密，也不把另一 VM closure 複製進來 |
| `filterEvent`（2389） | QVariant data:to*、filterData、cardEffect、cache invalidation、native flags、AiData | B：可見且有界的事件 envelope／generation；C：不轉送 QVariant、Card／Player userdata 或整個 Room tag 字典 |
| `assignKeep`／getKeepValue／getUseValue／getUsePriority／cardNeed、sort 系列（702–1400） | registry 數值與函式、getDefense、技能、手牌、合法性、os.time、math.random | A：基本 card/player 值；B：純值規則註冊與依賴閉包、缺失欄位；不能把所有排序一概列為「可直接搬」 |
| `addHandPile`／getCards／getCard／getKnownCards／getDisplayCards（4775、5642–5954） | 完整手牌、可見 flags、display_cards property、hand pile、CardFilter／ViewAs | A：本人 h/e/j；B：他人可見牌、特殊牌堆、候選 CardView 索引；C：未知 card ID 不得由 Engine 補讀 |
| `cardsView`／getSkillViewCard／fillSkillCards／useSkillCard（5454–5575、5977–6075） | ViewAsSkill、request provenance、native 虛擬卡、subcards、activation/source instance | A：單次 request 已有 skill_action；B：多候選 activation／source 描述及可用性；C：不可傳 ViewAsSkill、Card_Parse 結果或 native factory |
| `aiSlashAvailable`／canMethodUse（4792、4825） | 暫增 history → isAvailable → 還原；current use reason/pattern | A：reason／pattern；B：假設 history 的純值查詢／候選模型；C：隔離決策不能改真實 history 作推演 |
| `CardFilter`／flushDeferredDeleteCards（5392–5431） | setCardMapping、filterCards、viewFilter/viewAs、cloneCard、deleteLater | B：權威端產生的過濾／轉牌候選值；C：整段原生副作用與生命週期管理不進隔離層 |
| `aiUseCard`／useCardByClassName／targetRevises／canUse（8963、8888、8909、8633） | 動態 handler、canSlash/isProhibited/targetFixed/correctCardTarget、use.to、aiNoTo property | B：值型候選／use builder／合法性契約；C：不寫玩家 property、flags 或原生 use.to |
| 傷害／AOE／防禦／retrial／選目標（565、5185–5391、5432、6076–7379、8075、10734） | 技能 callback、距離、裝備效果、傷害／判定 userdata、事件關係、隨機數 | B：需要各族依賴分層；不能由 hp/card 種類幾個欄位假裝重現完整規則 |
| SetAiData／GetAiData／saveItemData、debug 輸出（2777–2840、activate、dumpGameState） | io.open、os、Engine AiData、console／event stack | A：已有受控 ai_data.read/write 與 RNG；B：資料格式／用途分類；C：不帶 raw io/os、Room log 或任意路徑 |

## A：資料已有，但尚未等於相容

| 已有資料 | 現有入口 | 可完成的轉接／尚有限制 |
|---|---|---|
| viewer、decision_id、state_revision、reason、pattern、prompt、handling_method | `AIRequest`；ai-runtime:895 `pushRequest` | 可替代 Engine current use context；舊 `request:get*()` userdata 與新 request table 尚非同介面 |
| 本人／他人可見 player scalar | `AIPlayerView`、PlayerView 動態 scalar getter | HP、maxHP、phase、seat、handcard_count、alive/dead/removed、wounded、face_up、chained、可見 role／kingdom 等；maxCards、hujia、gender、range 等未在此 DTO |
| 可見 marks／skills／equips／judging；本人 hand_cards | `PlayerView`、`CardView`、`SkillView` | 手／裝備／判定區選取已接；他人手牌未知回 nil；CardView 仍缺 type_id、subcards、handling、targetFixed 等能力 |
| mode_id、current_player、player_order、alive_player_order | RoomView | 可替代基本名單／current 查詢；保留 native RoomRoster 的 current rotation 與無 current fallback，不按 self-first 重排 |
| mode_policy、controller | SmartAIView relation／objective／friends／enemies | 只覆蓋 managed policy；getLord／getLieges／主公技不能僅以「身份可見」推導所有原生語意 |
| 單次 activation/source owner、skill、instance、quota booleans | request.skill_action | 尚不是所有可選技能／可轉卡的候選全集，也不是 Skill* |
| decision 執行 RNG、受控 AiData | AiLuaRuntime sandbox | 可使用既有入口；不要另造 global randomseed(os.time()) 或通用 filesystem bridge |

## B：優先需要補的是契約

### B0：型別、集合與分派

| 已查明落差 | 具體證據 | 下一批應定義的契約 |
|---|---|---|
| PlayerView 是 table，舊函式把 table 當玩家清單 | `SmartAI:hasSkills`（5963）對 table 執行 ipairs；單一 facade 會走錯分支 | 明確辨識 player/card/skill facade 與普通 array；不假裝讓 Lua type 回 userdata |
| CardView／PlayerView 被 userdata guard 拒絕 | `getDisplayCards`（5642）、`getKnownCards`（5715）、`aiUseCard`（8963）；`isCard`（5558）把非 userdata 交給 Engine:getCard | 逐共用入口更新型別邊界；不以新增 getter 掩蓋這些分支 |
| 集合形狀不同 | 原生使用 sgs.qlist／QList2Table／SPlayerList 及 length/isEmpty/contains/append；facade 現回 Lua array | 決定共用 array helper 或小型值集合相容介面，保留順序／副本／未知語意；禁止 re-export SWIG list |
| registry 與 callback 參數不同 | legacy `sgs.ai_skill_use[pattern](self,prompt,method,pattern,request)`；isolated `ai_skill_use[pattern](self,prompt,request)` | 明確註冊 ABI；不能只讓兩張表互為 alias，第三參數會由 method 變 request |
| 查找規則不同 | legacy 另有 prompt 前綴 fallback、`!` 與 compulsory 處理；isolated 是 activation_skill 優先再 exact pattern | 定義哪些 legacy dispatch 規則要相容，哪些是新契約；不可默默改優先序 |
| 回傳形狀不同 | legacy 字串 `.`／card string 或舊結構化表；isolated parseResult 僅接受 kind=pass/use_card table，nil 表示未覆蓋 | 分開「拒絕／pass」「unhandled」「error」；字串／舊表需明確轉換，不能把 nil 一律轉 pass |
| enums／字串工具未等量提供 | installSandbox 只反射 Phase／Suit 到安全 sgs；smart-ai 用 Card_Method*、Place*、Type*、DamageStruct*、string:split/contains/startsWith 等 | 按共用入口列舉需求，延用反射與純值 helpers；不是整包 require gameplay utilities |
| facade 是查詢介面，並非深度防寫 proxy | `self.world`／`self.request`／`_view` 是 Lua table | 公開契約禁止修改輸入；若要可修改 scratch，使用獨立 request 區；不要聲稱目前所有欄位都由 metatable 強制不可變 |

### B1–B3：資料與狀態投影

| 批次 | 來源缺口／需求 | 可見性、有效期與限制 |
|---|---|---|
| B1：牌區與可見牌索引 | getCard/getEngineCard、getCardPlace/Owner、getHandPile/getPile/getPileName、getKnownCards、display_cards、discard pile | 僅納入 viewer 可見的牌與場所；隱藏／部分可見／已知空必須區分。本人擁有牌堆不自動代表可見全部牌 ID。不可把全牌庫索引當缺牌 fallback |
| B1：玩家／技能衍生資料 | maxCards、hujia、gender、range、equip slot、lord skill、技能種類與有效性、特定可見 flags | HP 差等可由既有欄位計算；受技能／模式影響的值由權威端投影。SkillView 只含已提供 metadata，不等價於 native Skill 的 inherits/isLordSkill API |
| B2：規則與候選 | matchExpPattern、correctCardTarget、distanceTo、canSlash、isAvailable、isCardLimited、canDiscard、isProhibited、viewFilter／viewAs | 按 request 的 reason／method／候選、已選牌與已選目標定義有界值模型；不是無參數 boolean。來源可能觸發技能 callback，不能開同步 live Engine RPC 來繞過隔離 |
| B2：值型 use／推演 | dummy()、use.card/use.to、subcards、暫增 history、aiNoTo、card flags、active_skill_requests | 只用 ID、值型 card spec、target names、activation/source instance 與 request 私有 scratch。真正造卡／提交留給 C++；假設狀態不修改 Room |
| B3：事件上下文 | filterData、cardEffect、getCurrentDyingPlayer、getUseStruct、QVariant toDamage/toMoveOneTime/toCardEffect/toJudge | 建立必要且 viewer-filtered 的事件摘要。current player 不等於傷害來源、受影響者、瀕死者或正在結算的 use；不能用一個 current 欄位代替 |
| B3：AI 私有狀態 | roleValue／ai_role、aiData、turncount、self.toUse、keepdata、use_to、cardsview_requests、各類 cache | 規則註冊屬 Room VM；推測屬 Room＋viewer；決策 scratch／facade cache 屬 request；持久化學習資料另走 AiData。跨 request 只保存有意義的純值並定義失效，不保存 facade/native pointer |

`mode-ai.lua` 已有 Room VM 的 registry／viewer mind 與 revision＋generation 快取；
此部分優先沿用。它在 gameplay VM 評估後將純值 mode_policy 複製到隔離 VM，
不代表它的 closure 或 SmartAI 全域表已遷移。

## C：不可直接開放的通道

| 禁止直接跨界 | 已見使用點 | 可接受替代 |
|---|---|---|
| `sgs.Sanguosha` 原生物件與方法全集 | patterns、getCard、getGeneral、getSkill、getViewAsSkill、matchPattern、correctCardTarget | 有界靜態規則值／可見 card 索引／權威候選投影；缺資料回未支援 |
| Room／Player live mutation | setCardMapping/filterCards；setCardFlag/setPlayerFlag/setTag；addHistory；removeCard/addCard；setProperty(aiNoTo) | AI scratch 是獨立純值；gameplay 動作由結果提交後執行 |
| native card factory／生命週期 | Card_Parse、dummyCard、cloneCard、setId/addSubcards/setSkillName、deleteLater | CardActionSpec／值型 candidate；C++ owns card construction and lifetime |
| QVariant、RoomThread／event stack、任意 tag/property | filterEvent data:to*、getThread、getTag、getUseStruct | 有名稱、有 schema、有 viewer 可見性的事件／狀態投影 |
| raw io／os／loader／bootstrap 全域 | SetAiData、debug dump、require middleclass、dofile package AI、os.time/randomseed | C++ loader allowlist、受控 AiData／RNG／audit；bootstrap 不在每次 decision 重跑 |
| secret role／完整他人手牌／未公開武將技能 | legacy 推測與可見牌掃描會接觸原生全量資料 | C++ 先裁剪；adapter 不以 canSeeRole 等同名函式當成獲取秘密資料的後門 |

## Engine 與 Room 直接 API 對照

下表由 smart-ai.lua 的 `sgs.Sanguosha:`、`global_room:`、`self.room:` 定向掃描後按語意整理。
不把出現次數當作執行頻率，也不據此排除 `room`／`player` 等別名與外部 callback 的依賴。

| API 群 | 分類／歸宿 |
|---|---|
| getAlivePlayers/getPlayers/getAllPlayers/getOtherPlayers/getCurrent/getMode/findPlayerByObjectName | A，RoomView 已提供基本查詢；需要型別／集合契約才能接舊共用程式 |
| alivePlayerCount、isRoleRevealed | A，可由名單／role_revealed 補 wrapper；目前不是 RoomView 已有方法 |
| findPlayerBySkillName/findPlayersBySkillName | B，先定義 visible／invalid／同名多 instance 語意；不能查原生隱藏技能 |
| getLord/getLieges、Engine:getPlayerCount | B，模式／身份／名單語意需明確，不能一律用 role 字串或 alive 數代替 |
| getCard/getEngineCard/getCardCount、getCardOwner/getCardPlace/getDiscardPile | B，可見 card catalog／location 投影；C 禁止任意 ID 讀 Engine 真實卡 |
| getSkill/getTriggerSkill/getViewAsSkill/getViewAsEquipSkill/getSkillNames | B，必要的 metadata／規則候選；C 禁止 Skill* 與 native callback |
| getCurrentCardUseReason/getCurrentCardUsePattern | A，改讀 request.reason／pattern，不讀 Engine TLS 當前狀態 |
| getAiSkillActionContext/getAiSkillActionInstanceId | A 單次 skill_action 已有；B 多技能候選仍缺；C 不取得 AiLegacyRequestView.initiator 指標 |
| matchExpPattern/matchPattern/correctCardTarget、Room:isProhibited | B，權威規則投影／有界候選契約；不是重新暴露 live 查詢 |
| getCurrentDyingPlayer/getUseStruct/getTag | B，typed context；C 禁止整個 QVariant/tag map |
| getBanPackages/getExtensions/getModScenarioNames/getAllGenerals/getGeneral/translate | B，按需靜態規則／設定值與載入期需求；不把註冊／顯示文字處理混入每次 decision |
| getAiData | A，已有 ai_data 受控入口；資料轉換仍需契約 |
| cloneCard/setCardMapping/filterCards/setCardFlag/setPlayerFlag/setTag/removeTag | C；改成候選值、scratch 或權威提交 |
| writeToConsole/outputEventStack/getThread | C；改用 bounded audit／錯誤回報，不能取 RoomThread |

## 結果與 revision 的現況邊界

| 項目 | 核對結果 |
|---|---|
| 路由 | `AiDecisionCoordinator::decide` 區分 LegacyDirect／LegacyAdapted／Isolated／Shadow；Isolated 未處理或錯誤時回舊 AI，Shadow 只記錄比對 |
| 新輸入 | `AIRequest` 的 decision/state IDs、viewer、reason/method、world_view 與可選 skill_action 都已是純值 |
| 舊橋接 | `AiLegacyRequestView` 仍有 ServerPlayer* initiator；只供 legacy，不是可直接放入隔離 VM 的 request |
| 新輸出 | parseResult 接受 pass/use_card 表，驗型別、大小及選牌／選目標去重；不接受舊字串直接返回 |
| 過期檢查 | applyResult 檢查 decision ID、request/result/current revision；不得在隔離路由事後改 stamp 來接受過期結果 |
| legacy 例外 | 目前 LegacyAdapted 會把 liveRequest/result revision 更新為 legacy 執行後的值（coordinator:530 附近）；這是既存相容路徑，不應照搬為隔離 AI 的做法 |
| 合法性 | applyResult 的 legacyCardString 分支有 parse 與技能來源核對，但不能據此宣稱全部 target/card 規則已在此完成；仍須沿原 gameplay 執行流程檢查。此次不展開原生缺陷除錯 |
| fallback 的範圍 | 尚未將 askForChoice、askForDiscard、filterEvent 等全部建立 isolated request/result；本輪不擴大 DecisionKind 或改路由 |

## 下一批可獨立交付的工作

| 順序 | 範圍／產出 | 約定驗證內容（需另獲執行授權） |
|---|---|---|
| 1 | B0：代理型別辨識與 array 操作契約，選少量共用 helper 接入；列出仍未支援入口 | facade 與 list 不混淆、排序不改快照、空／未知分離、跨 request 不重用代理；不需要具體武將 |
| 2 | B0：明確的 legacy-style callback adapter 與結果 normalization；保留新版 registry ABI | 第三參數 method/request 不互換、prompt/skill/pattern 優先序、compulsory 語意、nil/pass/error 不混淆 |
| 3 | B1：viewer-scoped card lookup／location／pile 投影及必要 scalar metadata | 部分可見不洩漏、未知 ID 不補讀 Engine、牌移動後舊 request 失效；不以完整對局代替邊界契約 |
| 4 | B2／B3：依實際共用入口需要，再分批設計合法候選、值型推演、事件與觀察者狀態 | 另定檢查點與範圍；不在本輪建立通用規則引擎或搬全部技能策略 |

2026-09-17 第二批：回呼 ABI、分派與結果轉換已實作，對應架構順序第 2、3 項。
`ask-for-use-card.lua` 分成新舊兩張 registry（`ai_skill_use`／`ai_skill_use_legacy`，
skill 層各有註冊函式），同一 key 跨 ABI 重複註冊在註冊當下報錯；分派固定
skill exact → pattern exact（key 保留 `!`）→ prompt 前綴；`AIResultValue.normalize`
把舊字串、`"."`、`{accepted=…}` 與 `{cards,targets,user_string}` 轉成值型結果，
並把 unhandled／declined／pass／use_card／error 分開，錯誤答案一律 error 不轉 pass。
legacy callback 的第五參數改用純值 `AILegacyRequest`（見
[契約規格 §15.2.3](lua-ai-spec.md#1523-共用轉接層回呼-abi分派與結果轉換)）。

此批新增而仍未覆蓋的項目：`sgs.cardEffect` 自動拒絕層（需 B3 事件上下文）、legacy 尾端
自動找牌（需 B2 候選模型）、`CardUseStruct::CardUseReason` 常數（無 `QMetaEnum` 可反射，
因此不提供 `getReason()`／`getDecisionKind()`）、`string:split/contains/startsWith` 等工具。
沙箱 `sgs` 本批只多反射 `Card::HandlingMethod`。程式與契約案例已寫，尚未建置或執行。

2026-09-17 第三批：架構順序第 1 項，共用入口的型別契約。`smart-ai.lua` 新增
`aiValueKind`／`aiRejectValueView`／`aiCardId`／`aiSkillKey` 四個邊界函式，並改寫
`isCard`、`getKnownCards`、`getDisplayCards`／`hasDisplaySkills`、`aiUseCard`、`CardFilter`、
`sgs.getPlayerSkillList` 與 `aiCardKey`：代理不再被當成「沒有」或當成 ID 回查 Engine，
`isCard` 能直接用 CardView 投影作答，技能身份統一為名稱加實例（見
[契約規格 §15.2.4](lua-ai-spec.md#1524-共用入口的型別邊界legacy-側)）。
原生輸入行為不變——新分支只在 `AIValue` 存在時成立，gameplay VM 不載入 facade。
邊界函式獨立成 `lua/ai/value-boundary.lua`（`smart-ai.lua` 以 `dofile` 載入），只用純 Lua，
契約案例 `tests/lua/value-boundary-contract.lua` 由 room-runtime-isolation suite 以獨立
Lua state 載同一份定義驗證。

修改後 `lua/ai/smart-ai.lua` SHA-256
`7BFB480DE8354354D00D628AE6E0CFC88E691333B56BC16BF0A0C19F1C549ED4`，
上表雜湊仍是盤點當下基準。`evaluateWeapon`／`needToThrowArmor` 等傷害／防禦族的舊 guard
按盤點分層留待整族處理。本批只做 Lua 語法檢查，未建置、未執行。

2026-09-17 第四批：架構順序第 4 項第一段。`DecisionKind` 增加 `SkillInvoke`／`Choice`／
`Suit`／`Kingdom`／`General`，request 帶純值 `AIChoiceOptions`（reason、候選、預設值、
可拒答、數量上下限），結果新增 `AIResult::Answer`。`AiDecisionCoordinator` 多了
`decideSkillInvoke`／`decideChoice`／`decideSuit`／`decideKingdom`／`decideGeneral`，
共用一個 `runAnswer` 路由核心；`PlayerDecisionService` 的五個呼叫點改走 Room facade，
舊 AI 呼叫移入 coordinator 當回退。隔離側新增 `lua/ai/isolated/ask-for-choice.lua`
（每個 kind 一張以 reason 為鍵的 registry，handler 收 `(self, options, request)`），
`AIResultValue.normalize` 改為依 decision kind 轉換（見
[契約規格 §15.2.5](lua-ai-spec.md#1525-值型詢問請求種類候選與答案)）。

同一批接著補上選牌與選人族：`Discard`／`AmazingGrace`／`CardChosen`／`Yiji`／
`PlayerChosen`／`PlayersChosen`。`AIChoiceOptions` 多了 `cardIds`／`playerNames` 候選，
答案用既有的 `selectedCardIds`／`selectedTargetNames`，C++ 端再驗一次「答案必須來自候選」，
不合就整份拒絕並回退舊 AI。

回應牌族（`askForCard`／nullification／cardShow／pindian／singlePeach）共用 `RespondCard`
一種 kind，以 `options.question` 分辨；legacy 路由的 `const Card *` 原樣回傳（虛擬牌不繞經值
模型），隔離路由只接受自己持有的實體牌 ID。`Guanxing` 用 `cards`＋`bottomCardIds` 兩堆有序
牌並檢查兩堆合起來等於題目給的整組；`TriggerOrder` 的候選是 `skill[#instance][:owner]` 字串。

新 kind 沒有預設路由，`routeFor` 回 `LegacyDirect`，所以未設定時行為與改動前相同；
可路由的 callback 名稱改由白名單列舉。二十個公開入口都已接上同一條通路，仍留在舊 AI 的是
轉化牌與需要 `QVariant data` 的事件上下文。本批只做 Lua 語法檢查與靜態檢查，未建置、未執行。

2026-09-17 第五批：架構順序第 5 項，可見牌區投影。快照加入 `discardPile`、
`knownCards`／`handVisible`、具名牌堆（`AICardPileView`：count／open／hand_pile／ids）與
`displayCards`；`RoomView` 在建立時用這些純值組出位置索引，提供 `getCardOwner`／
`getCardPlace`／`isCardKnown`，未進索引的 ID 回 nil 而不回查 Engine。`Player::Place` 一併
反射進沙箱 `sgs`（見 [契約規格 §15.2.6](lua-ai-spec.md#1526-牌區投影可見牌牌堆與位置索引)）。
本批只做 Lua 語法檢查與靜態檢查，未建置、未執行。

2026-09-17 第六批：架構順序第 6 項，共用衍生資料。`AIPlayerView` 補 `maxCards`／`hujia`／
`attackRange`／`gender`／`lord`／`equipSlots`，`AICardView` 補 `typeId`／`handlingMethod`／
`virtualCard`／`targetFixed`／`damageCard`／`subcardIds`，`AISkillView` 補原生類別鏈
`skillClasses` 與 `frequency`／`lordSkill`／`attachedLordSkill`／`lordSkillEffective`；沙箱再反射
`Card::CardType`、`Skill::Frequency`、`General::Gender`，並補上 `string.split`／`contains`／
`startsWith`／`endsWith` 純值工具（見
[契約規格 §15.2.7](lua-ai-spec.md#1527-共用衍生資料玩家卡牌技能與純值工具)）。
本批只做 Lua 語法檢查與靜態檢查，未建置、未執行。

2026-09-17 第七批：架構順序第 7 項，合法候選契約。出牌類 request 帶
`AICardCandidateView`（available／limited／jilei／targetFixed／maxTargets／legalTargets），
world view 帶存活玩家之間的距離表；兩者都由權威端在建立 request 時算好，隔離側只查表
（`CandidateView`、`RoomView:distanceTo`、`PlayerView:inMyAttackRange`），沒有回頭查 Engine
的路（見 [契約規格 §15.2.8](lua-ai-spec.md#1528-合法候選契約可用牌可選目標與距離)）。
逐步收斂的「選了 A 之後」候選與轉牌結果留給值型推演批次。
本批只做 Lua 語法檢查與靜態檢查，未建置、未執行。

2026-09-17 第八批：架構順序第 8 項，值型出牌與推演。結果新增 `AICardSpec`
（name／suit／number／skill／subcards），權威端 `buildSpecCard` 先驗花色點數範圍、技能歸屬與
每張 subcard 的持有，再 `cloneCard` 造牌並以 `setOwnedCard` 交給 `CardUseStruct`；隔離側
不再需要（也仍然不能用）`dummyCard`／`Card_Parse`。`ai_decide` 每次決策提供 `request.scratch`
純值暫存，取代舊實作暫改 flag／property／history 的推演手法（見
[契約規格 §15.2.9](lua-ai-spec.md#1529-值型出牌與推演暫存)）。
本批只做 Lua 語法檢查與靜態檢查，未建置、未執行。

2026-09-17 第九批：架構順序第 9 項，技能實例接線。`buildSkillActionContext` 從
`buildSkillActionRequest` 抽出（候選不必各自重建 world view），出牌類 request 帶
`skillActions` 全部可啟動實例；結果可用 `skill_action = {skill, instance, owner}` 指名，
權威端檢查 owner、必須是提供過的候選，再重建上下文重跑 `canActivate` 與次數。
隔離側有 `SkillActionView`／`getSkillActions()`／`getSkillAction()`，同名多實例不合併，
借用與轉化由 source 欄位保留（見
[契約規格 §15.2.10](lua-ai-spec.md#15210-技能實例候選與來源關係)）。
本批只做 Lua 語法檢查與靜態檢查，未建置、未執行。

2026-09-17 第十批：架構順序第 10 項，值型事件上下文。`RoomThread` 在通知舊 AI 的同一
位置呼叫 `Room::recordAiEvent`，`AiDecisionCoordinator` 把 `DamageStruct`／
`CardsMoveOneTimeStruct`／`CardEffectStruct`／`JudgeStruct`／`DyingStruct`／`CardUseStruct`
當下轉成 `AIEventView`（sequence＋revision＋可見性分流的牌 ID），保留 64 筆有界紀錄並隨快照
投影；隔離側用 `EventView`／`getEvents()`／`getLastEvent()` 讀（見
[契約規格 §15.2.11](lua-ai-spec.md#15211-值型事件上下文)）。
本批只做 Lua 語法檢查與靜態檢查，未建置、未執行。

2026-09-17 第十一批：架構順序第 11 項，AI 狀態分層。隔離 VM 加入 `ai_memory`：跨 request
的觀察者推測記憶，按 viewer 分區、只收純值（拒收代理／函式／metatable 物件）、深複製存取、
深度與筆數有上限，VM 重建即歸零；handler 以 `SmartAIView:remember/recall`（及帶 revision 的
`rememberAt/recallAt`）存取，讀不到別人的記憶。加上既有的 registry（Room VM）、
`request.scratch`（單次決策）與 `ai_data`（持久化），四層狀態各自有生命週期（見
[契約規格 §15.2.12](lua-ai-spec.md#15212-ai-狀態分層規則推測暫存與持久化)）。
本批只做 Lua 語法檢查與靜態檢查，未建置、未執行。

2026-09-17 第十二批：架構順序第 12 項，模式／身份共用服務。規則關係（mode policy）與
推測關係（`believeRelation`／`believedRelationTo`，存在 ai_memory 並附 revision）分成兩套查詢；
新增 `isModeManaged()`／`requireModePolicy()` 讓「未覆蓋模式」可被明確偵測，不再可能被當成
中立或沒有敵人。身份與控制鏈補上 `RoomView:getLord()`／`getLieges()`、
`PlayerView:isSameKingdom()`（勢力未公開回 nil）、`isControlledBy()` 與
`SmartAIView:sharesController()`（見
[契約規格 §15.2.13](lua-ai-spec.md#15213-模式身份與控制鏈)）。
本批只做 Lua 語法檢查與靜態檢查，未建置、未執行。

2026-09-17 第十三批：架構順序第 15 項，提交與過期結果。`runAnswer` 改為先驗隔離答案的
decision ID 與 revision，過期就換舊 AI 作答（不再事後改 stamp，也不再因過期而讓呼叫點失去
AI 答案）；候選、數量與授權檢查只套用在隔離答案上，舊 AI 的指標與清單原樣通過，避免新增
檢查改動既有對局（由 `fromIsolated` 分界）。各結果種類的檢查表見
[契約規格 §15.2.14](lua-ai-spec.md#15214-提交與過期結果)。
本批只做靜態檢查，未建置、未執行。

2026-09-17 第十四批：架構順序第 13 項，通用決策流程。新增
`lua/ai/isolated/decision-core.lua`：估值註冊表與 `getKeepValue`／`getUseValue`／
`getUsePriority`、決定性排序、`getCardsNum`／`getDefense`／`isWeak`／`getThreat`、
由 `card_candidates` 產生的 `getTurnUse()`／`pickTargets()`／`planTurnUse()`，以及一個通用
`activate` handler。結果多了 `card_id` 值型寫法（打自己持有的實體牌，權威端驗持有後取牌）。
`card_candidates` 改成只在會算候選的 kind 出現，因此「空清單」代表問過但沒有合法選擇，
「沒有欄位」代表這次沒問（見
[契約規格 §15.2.15](lua-ai-spec.md#15215-通用決策流程隔離版)）。
本批只做 Lua 語法檢查與靜態檢查，未建置、未執行。

2026-09-17 第十五批：架構順序第 14 項，載入與相容層。載入分成 bootstrap／sandbox／
mandatory facade／允許清單腳本四段並寫進文件；新增 `ai_coverage`：每個 registry 自行申報鍵，
`covers()`／`describe()`／`summary()` 回報這個 VM 接得住哪些決策，沒申報就是沒覆蓋。
回退邊界（unhandled／過期／錯誤一律回舊 AI，audit 記 NotCovered／Error）一併寫明（見
[契約規格 §15.2.16](lua-ai-spec.md#15216-載入分層與覆蓋率)）。
本批只做 Lua 語法檢查與靜態檢查，未建置、未執行。

2026-09-17 第十六批：架構順序第 16 項的可執行部分。shadow audit 統計加上逐 callback 分項
與 `legacyFallbacks`（走 Isolated 卻由舊 AI 作答的次數，未覆蓋／過期／出錯都計入），
`AiLuaRuntime::shadowAuditSummary(callbackName)`／`auditedCallbacks()` 讓「還在回退的入口」
成為可列出的清單；切換與驗收程序（門檻 1–6）寫進
[契約規格 §15.2.17](lua-ai-spec.md#15217-切換與驗收程序)。
驗收本身要跑完整對局與效能量測，需另獲建置與執行授權，在此之前不宣稱任何入口已切換完成。

第 1 項應先於繼續擴充 RoomView 同名方法。前批 Adapter 建置／執行 gate 仍未通過，
此盤點只提供下一批實作依據，不提升任何執行期驗收狀態。

## 對照來源

| 檔案 | 關鍵符號／定位 |
|---|---|
| [smart-ai.lua](../lua/ai/smart-ai.lua) | 上述共用入口；非主倉庫 tracked 來源，版本以外部 extensions 倉庫與本表雜湊為準 |
| [isolated-facades.lua](../lua/ai/isolated-facades.lua) | PlayerView:139、手牌／牌區:181、RoomView:269、SmartAIView:359 |
| [mode-ai.lua](../lua/ai/mode-ai.lua) | modeAIWorld:275、current_ai:284、installModeAI:297 |
| [isolated-bootstrap.lua](../lua/ai/isolated-bootstrap.lua) | ai_register_handler、ai_decide |
| [ask-for-use-card.lua](../lua/ai/isolated/ask-for-use-card.lua) | exact skill／pattern registry，handler 第三參數是 request |
| [ai.h](../src/server/ai.h) | AICardView、AIPlayerView、AIWorldView、AIRequest、AiLegacyRequestView、CardActionSpec |
| [ai-decision-coordinator.cpp](../src/server/ai-decision-coordinator.cpp) | buildWorldView:198、makeRequest:302、applyResult:363、decide:464 |
| [ai-runtime.cpp](../src/server/ai-runtime.cpp) | pushAIWorldView:307、decideShadow:561、installSandbox:762、pushRequest:895、parseResult:938 |
| [lua-ai-spec.md](lua-ai-spec.md) | §15 執行邊界、§15.2.2 前批 Adapter 能力與未驗證狀態 |
| [ai-identity-mode-decoupling-plan.md](ai-identity-mode-decoupling-plan.md) | mode policy 與觀察者推測的既有分層 |
