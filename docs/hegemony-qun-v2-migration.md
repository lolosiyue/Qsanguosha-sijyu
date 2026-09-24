# 群雄技能共用與 V2 遷移

2026-09-24 原始碼檢查點。以目前工作樹的新國戰規則為準，取代 2026-09-22 舊版共用清單。相同效果在身份局原定義升級 V2，`HStandardPackage::addQunGenerals()` 以技能 ID 引用；不另建同效果的 `H*` 類別或 `heg_*` 別名。

## 共用來源

| 國戰武將 | 技能 ID | 唯一定義 | 本批處理 |
| --- | --- | --- | --- |
| 華佗 | `jijiu`、`chuli` | `standard-generals.cpp` | 原急救、除癘升級 ViewAsSkillV2 |
| 貂蟬 | `lijian`、`biyue` | `standard-generals.cpp` | 原離間升級 ViewAsSkillV2；原閉月升級 TriggerSkillV2 |
| 賈詡 | `luanwu`、`wansha` | `thicket.cpp` | 原亂武升級 ViewAsSkillV2；完殺升級 TriggerSkillV2，限制 helper 保留國戰明置及回合條件 |
| 龐德、馬騰 | `mashu` | `standard-generals.cpp` | 已為 DistanceSkillV2，沿用 |
| 龐德 | `tenyearjianchu` | `fire.cpp` | 原鞬出升級 TriggerSkillV2 |
| 顏良文醜 | `shuangxiong` | `fire.cpp` | 原觸發／轉化直接 V2，所有模式統一累積顏色並在回合結束清除 |
| 張角 | `nosleiji` | `wind.cpp` | 原黑桃 2 傷害雷擊升級 TriggerSkillV2；國戰排除自身目標 |
| 孔融 | `lirang` | `sp.cpp` | 原禮讓恢復為唯一 TriggerSkillV2，沿用逐實例棄牌追蹤 |
| 馬騰 | `xiongyi` | `sp.cpp` | 原雄異恢復為唯一 ViewAsSkillV2，保留模式差異與 @arise 付款 |
| 田豐 | `sijian` | `sp.cpp` | 原死諫恢復為唯一 TriggerSkillV2，群雄按 ID 引用 |
| 紀靈 | `shuangren` | `sp.cpp` | 恢復已刪舊包的共用技能，直接 TriggerSkillV2；群雄按 ID 引用 |
| 潘鳳 | `kuangfu` | `sp.cpp` | 直接引用已有 TriggerSkillV2 |
| 張角 | `guidao` | `wind.cpp` | 原鬼道升級 TriggerSkillV2 |
| 蔡文姬 | `beige` | `mountain.cpp` | 原悲歌升級 TriggerSkillV2 |

合計 17 個技能 ID、18 處國戰引用，其中 15 個原定義已升級；馬術與狂斧沿用已有 V2。身份局的註冊位置與國戰的字串引用保持一致；無新增重複技能註冊。

## 遷移契約

| 技能 | 契約 |
| --- | --- |
| 急救 | 保留非當前回合旗標、桃回應模式、禁桃標記、红色手牌／裝備及可回應手牌堆；純 createCard 產生 Peach，保持花色點數與語音選擇。 |
| 離間 | V2 核對可棄手牌／裝備；沿用既有 LijianCard、選人順序、卡牌付款及決鬥效果，不建立國戰副本。保留卡名及 history key，兼容既有 AI 與 NosLijianCard。 |
| 閉月 | 結束階段產生本席候選；cost 詢問、effect 摸牌，由 V2 綁定精確實例。 |
| 亂武 | 保留 LuanwuCard 的逐人結算與 AI 卡名；限定標記在 V2 pay 由 initiator 支付。createCard 標記原生 V2 路徑，避免 Card::use 再扣；MobileMouLuanwuCard 等舊子類仍沿原卡牌付款。 |
| 鬼道 | AskForRetrial 已逐席派送，只產生本席候選。cost 以 MethodNone 選黑牌，資料存 ctx.extra_data；effect 重查所有權、區域、顏色及回應限制，再經 Room::retrial 原子換判並交換舊判定牌。 |
| 悲歌 | Damaged 對傷者枚舉持有者；cost 只選牌、pay 重查並棄牌，傷者進入 ctx.targets。FinishJudge 用單次 recordEvent 保存花色，逐目標效果保留四種判定結果。 |

原技能的 ID、翻譯鍵、提示與既有 AI 入口不變，無需重複翻譯或新增國戰 AI 回呼。完殺的國戰引用與 AI 判斷改為 `wansha`；被動禁桃在國戰要求回合內且已明置或為取得技能，身份局及 `mobilemouwansha` 分支保留原行為。主動技沿用既有卡牌，並未宣稱全部 SkillCard 已退役。

## 除癘／鞬出／狂斧歸位

依本輪共用要求，刪除 HChuli、HJianchu、HKuangfu、HKuangfuEffect 及專屬 Jink helper，群雄將直接引用 chuli、tenyearjianchu、kuangfu。

| 技能 | 共用結果 |
| --- | --- |
| 除癘 | 原 Chuli 直接實作 V2 callbacks，沿用唯一 ChuliCard 的付款、效果與 AI；目標在國戰按已明置友方關係去重，身份局保留 kingdom 判斷。 |
| 鞬出 | 原 TenyearJianchu 直接改 TriggerSkillV2，逐目標詢問並核對棄牌結果；裝備牌沿原 no_respond_list，非裝備牌令目標取得仍在桌面的殺。 |
| 狂斧 | 使用原 kuangfu 的殺傷害後移動／棄裝備，與既有技能正文一致。移除原 H 副本的使用殺時取裝備、階段旗標及未造成傷害後棄牌機制；不再把該副本視為必須保留的國戰版本。 |

技能名稱、正文與語音字幕由身份版來源提供，移除重複 heg_* 翻譯。AI 刪除已退役副本回呼，既有身份 AI 保持權威；metadata contract 加入原技能型別與綁定、舊技能不再註冊的檢查，未執行。

群雄專屬技能及 helper 統一放在 `h-standard-qun-generals.cpp`，直接由對應武將 `addSkill(new ...)` 加入：袁紹持有亂擊、響應摸牌及花色清理 helper，蔡文姬持有斷腸；相關技能設定置於武將旁。不另設集中 addSkills、variants 檔案或註冊轉接函式。國戰亂擊、帷幕、斷腸的缺失名稱、正文與必要提示／日誌已補回 HStandardQunGeneral.lua。

雙刃共用原 `shuangren` ID：身份局拼點贏後可對任意合法目標出殺並保留用殺次數，國戰只可選拼點對象或其友方且不計次數。差異集中於 `Config.EnableHegemony`；共用 distance helper、AI 選人／拼點入口，移除舊 `@@heg_shuangren` 卡牌回應副本。`:<skill>_p` 是身份局說明，雙刃與除癘均按 `Skill::getDescription` 規則配置。

死諫共用 `sijian`：僅最後手牌移動的技能持有者產生觸發，cost 選擇可棄牌的其他角色，effect 重查存活及可棄牌條件再棄牌。移除 `HSijian` 及 `heg_sijian` 註冊、AI 與翻譯副本。

雄異共用 `xiongyi`：身份局自行選人並自動包含自己，以目標數不超過存活人數一半判定回血；國戰自動選友方，以最小勢力判定回血。限定標記 `@arise` 僅由 initiator 在 pay 消耗，統一目標結算後回傳 FinishSkill，避免重複摸牌。

禮讓共用 `lirang`：保留 V2 逐實例棄牌追蹤、棄牌堆重查及私人分配預覽清理。身份局保留原優先序 3 與可取消發動詢問，接受後至少分配一次；國戰沿用已明置／未明置的可取消分配契約。


本輪依指定保留 `heg_duanchang` 與 `heg_luanji`；已比對 mobileluanji／olluanji，因規則不同不替換。雷擊改引用原 `nosleiji` V2，維持黑桃判定 2 傷害，僅國戰排除自身目標。

雙雄依指定直接以新版取代原 `shuangxiong`，不保留模式分支：所有模式累積本回合判定顏色並於回合結束清除。判定牌獲得、轉化材料及決鬥效果共用，群雄只引用技能 ID；同回合判過紅黑後可用兩色手牌轉化。

## 保留的國戰規則

| 技能 | 不能直接替換的差異 |
| --- | --- |
| `heg_wushuang` | 額外允許非轉化決鬥增加目標；身份無雙沒有此項。 |
| `heg_weimu` | 現有國戰實作含明置時機、目標取消及延時錦囊移入改道；身份版為禁止指定。 |
| 其他 `heg_*` | 保留國戰專屬效果及現有註冊，不因技能同名而合併。 |

名士、隨勢、禍水、傾城與跨檔案的國戰技能均不在本批共用 API 升級範圍。現有檔案內仍有舊 API 的國戰特有技能；本文件不將整個國戰包宣告為全 V2。

## 驗證邊界

| Gate | 狀態 |
| --- | --- |
| 靜態來源／定義唯一性／國戰引用／callback 簽名／git diff --check | 本批核對 |
| 建置、focused executable | NOT RUN；本輪未授權 |
| 本地 CTest、GUI／完整對局、CI | NOT RUN |

待授權的 focused 驗收：急救手牌堆／禁桃、離間有序目標與付款、亂武取消／付款攔截／舊子類不重扣、鬼道取消與換判交換、悲歌多持有者／付款後攔截／四花色，以及隱藏來源明置與多實例。靜態核對不能代替這些執行證據。
