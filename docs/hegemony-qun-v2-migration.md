# 群雄技能共用與 V2 遷移

2026-09-22 原始碼檢查點。範圍為 `h-standard-qun-generals`；新國戰武將直接引用既有技能，只有無可重用定義的技能保留 `heg_*` 並改為 V2。共用技能採現版規則，不保留 H 複本或別名殼，也不重做既有技能。

## 共用技能

| 國戰武將 | 直接引用的既有技能 ID |
| --- | --- |
| 華佗 | `jijiu`、`qingnang` |
| 呂布 | `wushuang` |
| 貂蟬 | `lijian`、`biyue` |
| 袁紹 | `luanji` |
| 顏良文醜 | `shuangxiong` |
| 賈詡 | `wansha`、`luanwu`、`weimu` |
| 龐德 | `mashu`、`mengjin` |
| 張角 | `leiji`、`guidao` |
| 蔡文姬 | `beige`、`duanchang` |
| 馬騰 | `mashu` |
| 潘鳳 | `kuangfu` |

共 17 個不同技能 ID、18 處武將引用。相關 helper 由既有技能註冊表取得，包括完殺的 `#wansha-limit`。移除群雄檔內重複定義及六個專屬 SkillCard 宣告／註冊；其他武將仍使用的 `HMashu` 不在本次刪除範圍。現版與原國戰規則有差異的技能（例如雷擊、斷腸）亦沿用現版，而非以同名為由保留舊實作。既有技能目前使用哪一代 API，不由本次共用修改決定。

`lang/zh_CN/Package/HStandardQunGeneral.lua` 同步移除 51 個已退役或重複的翻譯鍵；保留 15 名武將的名稱／稱號、8 個獨立技能及其提示，並補齊雙刃的選殺目標與距離 helper 名稱。共用技能直接讀取原翻譯，狂斧裝備選項由 `OLStrengthenPackage.lua` 提供。技能正文維持簡體中文，保留原始來源註記與武將名稱後的 `[国]`。

## 獨立 V2 技能

| 技能 | 形式 | 生命週期與資料 |
| --- | --- | --- |
| 雄異 | `ViewAsSkillV2` | 無選定目標；`pay` 扣發起者的 `@arise`，效果依結算時友方建立列表，逐人經 `skillEffect` 發牌，再沿原勢力人數規則判斷回復。保留原限定標記支付規則。 |
| 名士 | `TriggerSkillV2` | 依原傷害來源明置條件選候選；減傷寫回 `original_data`，零傷害中斷原傷害。 |
| 禮讓 | `TriggerSkillV2` | `record` 僅追蹤每實例的棄牌 ID；選擇後以 `extra_data` 凍結本次牌表與可取消狀態。支援手牌／裝備直入棄牌堆及經桌面的棄牌，分配前重查牌位。私有預覽在離開作用域時清理。 |
| 雙刃 | `TriggerSkillV2` + `TargetModSkillV2` | `cost` 選拼點對象，`effectTarget` 才執行拼點；失敗中斷出牌階段，成功依原友方規則使用不計次數的殺。距離 helper 依精確根來源生效。 |
| 死諫 | `TriggerSkillV2` | `cost` 把選定角色寫入 `targets`，框架經目標攔截後棄其牌；不用 Player Tag 暫存。 |
| 隨勢 | `TriggerSkillV2` | 沿既有逐席派送，只處理本席持有者；資料內的瀕死／死亡角色與技能來源分開，避免再枚舉全場造成重複觸發。依原友方規則摸牌或失去體力。 |
| 禍水 | `TriggerSkillV2` + `ViewAsSkillV2` | 主動入口沿 V2 共用付款／明置流程。全域 `record` 按當前角色仍有效且已明置的實例重算持續禁亮效果；暗置、移除、死亡、失效及回合結束均可清理，移除其中一份不清掉另一份效果。 |
| 傾城 | `ViewAsSkillV2` | 選一張可棄裝備；共用 proxy `pay` 負責棄牌，`effectOnTarget` 選武將並暗置。目標必須有可暗置的武將。 |

所有候選查詢均無發動副作用；可取消的選擇不移牌。觸發技的精確來源、發動順序、明置及攔截沿現有 V2 管線。

雄異／禍水／傾城使用通用 `ActiveSkillCard`。AI 改為 `ai_fill_skill.heg_*` 與按技能 ID 分派的 `ai_skill_use_func`，不提交已刪除的專屬卡類別；history key 仍保留 `HXiongyiCard`、`HHuoshuiCard`、`HQingchengCard` 字串。AI 來源檔為外部 extensions 倉庫的 `ai/original-hegemony/heg-standard-qun-ai.lua`，L 副本位於 `lua/ai/original-hegemony/`。

## 驗證邊界

| Gate | 狀態 |
| --- | --- |
| 原始碼／靜態 | 核對引用 ID、V2 callback 簽名、刪除類別引用及 AI proxy 接線；既有 finish-interruption fixture 改用共用離間／亂武卡。 |
| 建置 | NOT RUN；本輪未授權。 |
| focused／本地 CTest | NOT RUN。 |
| GUI／完整對局／CI | NOT RUN。 |

後續執行驗收需覆蓋：共用技能及相關 helper 解析、雄異扣標記後效果取消、傾城付費前取消／付費後目標攔截、雙刃勝敗與中途移除來源、死諫改目標、禮讓多實例／嵌套移牌／中斷預覽、隨勢跨角色來源，以及禍水雙份來源／暗置／失效／最後一份移除。上述情境尚無本輪執行證據。
