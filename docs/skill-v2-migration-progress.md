# Skill V2 遷移進度與同步紀錄

更新：2026-09-29。本文是本次提交檢查點的狀態快照；**全部 package 遷移及最終複審尚未完成**。

## 目前到哪裡

| 層次 | 狀態 |
| --- | --- |
| 共用 V2 支援 | 精確來源、宣言實例、已接受效果、授技／標記回執、普通牌及回應入口、Resolution History 已落碼；主要介面已做靜態複審 |
| 先前完成檢查點 | `c12f9f44`，64 檔，11 個 package 及共用支援；不是建置或玩法通過證據 |
| 本次本地保全 | 將其餘原生 package 草稿、HUMAN Lua 接線、isolated AI 接線分題提交，避免未提交工作與遠端混合；提交草稿不代表遷移完成 |
| 最終複審 | 部分包已讀回閉合；多個包仍有確證待修與尚未覆審部分 |
| 建置／驗證 | 本次未執行 C++ 建置、SWIG 生成、執行期、GUI、對局或 CI；只做來源、差異、相依、雜湊與合併靜態核對 |

`c12f9f44` 的 package 檢查點：Assassins、Doudizhu、H-NewSGS、MeleeMode、MobileMougong、Thicket、TW、Wind、Wisdom、Yingbian、Yinhu。
其他 package 即使已沒有直接 legacy 基底，也不能據此判定已完成語意複審。

## 剩餘盤點

以下按本次凍結的 `src/package/*.cpp` 類別宣告掃描，計算直接繼承
`TriggerSkill`、`PhaseChangeSkill`、`DrawCardsSkill`、`ViewAsSkill`、
`ZeroCardViewAsSkill`、`OneCardViewAsSkill`、`TargetModSkill`、`DistanceSkill`、
`MaxCardsSkill`、`AttackRangeSkill` 的宣告。這是文字盤點，可能包含未註冊類別，
不涵蓋間接繼承；專用 Filter／Prohibit／Invalidity 等不在這個分母內。

| 檔案 | 直接 legacy 類別 |
| --- | ---: |
| `mobile.cpp` | 169 |
| `mobileshiji.cpp` | 1 |
| `ol-strengthen.cpp` | 14 |
| `ol.cpp` | 348 |
| `tenyear-strengthen.cpp` | 69 |
| `tenyear.cpp` | 144 |
| `tenyear2.cpp` | 451 |
| `yjcm2013.cpp` | 15 |
| `yjcm2014.cpp` | 14 |
| `yjcm2015.cpp` | 2 |
| `yjcm2022.cpp` | 7 |
| **合計** | **1,234** |

同一掃描另得到 3,120 個直接以 `*SkillV2` 為基底的宣告。這兩項不是武將數、
可用技能數或完成率；不能把未覆審的新類別算成完成。

## 複審的主要待辦

| 範圍 | 下一步與已知限制 |
| --- | --- |
| `mobile` | Fuman 配額改按 phase id 保存，結束內層出牌階段不再清掉外層；摸牌歸交牌者，該【殺】在受益者下一個回合結束時失效。Zhuhe 護甲分支錯寫成 `zhuhe2` 已改回 `zhuhe3`。牌來源仍待裁定：正文是從牌堆獲得並使用，C++ 仍從棄牌堆直接使用。其餘 legacy 家族未遷移 |
| `mobileshiji` | Xingzhen 不遷移：正文是牌堆頂 7 張可見並當基本／錦囊使用，C++ 是另一套換牌／觀星／殺流程，已留 TODO。Mouli／Miewu 完成事件與 Yaohu 階段收據已讀，沿用 CardFinished／PostCardResponded／phase id，本輪未改。全檔其餘生命週期未結案 |
| `mobile-strengthen` | Dingpin 計入 response-use（`respond_card.is_use`），打出不計。Benxi 在 TurnBroken 清掉仍顯示 active 的階段距離。MobileAnjian 改為每個範圍外目標一條 preferredTarget，不再用裝備才解析的 `->` 字串。MobileZhuikong 用受益者自己的 `-SelfClear` 標記撐到其回合結束；OLZhuikong 仍共用舊 flag。這四項未跑對局 |
| `ol` | 大量家族仍舊制；TongxieTargetMod 的跨技能混合修正需逐族拆分，避免漏算或雙算 |
| `ol-strengthen` | Jijiang、Guhuo、Huashen／Xinsheng、Qiaobian、Jiushi、Dangxian、Jiaozhao／Danxin 等未完；OLQingjian 取消後牌堆與收據的清理待修 |
| `olwenwu` | 已無直接 legacy 仍不代表完成；JinTairan 巢狀收益／到期順序、延期效果取消與受益者鉤子待複審。本次只修 Xijue 的 `qsizetype/int` 比較型別 |
| `tenyear*` | Tuicheng、Xianju、Kangming、Jiewei、Xuanfeng、Yongjin 已在 V2，本輪讀回沒有改規則。Yizhen 把缺席花色當 0，避免手牌只有一種花色時同時當成「唯一最多」和「最少」。三個檔案的其餘 legacy 家族未遷移，不能把這一批讀回當成整包結案 |
| `dream` | Mishou 首次殺機會在整體取消時的消耗、Tunshi 多實例授技裁定、最後一批修正尚未完整讀回 |
| `yczh2016/2017`、`yin` | Jiaozhao／Danxin 的指定 findings 已閉；Taoluan、新 2017 家族、Juzhan／Chenglve／OLLijun 限時收據尚待完整複審 |
| `yjcm*`、`zombine` | 2012 後續旁路付款／序列化差異需再審；2013／2014／2022 仍有 legacy；Huomo／Zhanjue 新族未閉合。Fencheng 舊 C++ 免傷選擇語意的後續調整、2014 Sidi 草稿尚未套入 |
| 其餘早期包 | 有作者來源檢查點，仍需依 [Skill V2 新機制](SkillV2新機制說明.md) 完成整包複審，不能只依零 legacy 盤點結案 |

討襲（Taoxi）由使用者指定暫緩：保留既有實作，不擴充 `ServerPlayer::getHandPile`
的多實例借牌入口。此項不是完成，也不以共享單一 `TaoxiId` 冒充逐實例能力。

2026-09-28 只複審 `mobile.cpp`、`mobileshiji.cpp`、`mobile-strengthen.cpp`、`tenyear.cpp`、`tenyear2.cpp`、`tenyear-strengthen.cpp` 裡點名的家族。上表這些行是讀碼結果。Zhuhe 的牌堆／棄牌堆、Xingzhen 的兩套正文都還沒有裁定，所以沒有改玩法。這次沒有把未建置、未對局的技能標成完成。

## 本輪已落的共用契約

- 配額使用 `Skill::LimitScope` 與精確 activation；技能私有狀態、根來源歸因及 Room 事件歷史是不同責任。
- 宣言、借用、已接受效果及限時授技保存精確來源；原授予技能退役不任意中斷已接受效果。
- `updated_card`、取消、目標存活及原生投票規則在主動／轉化／純回應入口一致處理；已提交配額不因效果取消回滾。
- 提供牌使用有界原生回執，外層不重付；原始 `response_event_id` 可供延後效果追蹤真正消耗點。
- 已生效實體改牌使用 Card／WrappedCard 私有來源回執，並接入快照；不把任意技能名視為可信來源。
- 歷史新增實際回復、護甲結果、死亡原因、回合體力、數值提交、摸牌／判定／瀕死／拼點／失去體力、技能目標及派發階段事實。
- 展示牌 `show_cards` 在回呼前保存不可變牌值、角色與觀眾；舊 journal 缺種類覆蓋時回未知，不能當作從未發生。

介面與歷史細節分別見 [Skill V2 新機制](SkillV2新機制說明.md)、
[Resolution History](resolution-history.md)、[主動技能遷移指南](active-skill-v2-migration-guide.md)。

## 遠端與本地整合範圍

本次同步前，本地 `debug` 位於 `c12f9f44`，相對 `origin/debug` 領先 1、落後 4。
遠端四個提交為 `f7f8441a`、`cf92cc39`、`5d7581d4`、`9f42e10d`。
核心交集在 Engine 與 WrappedCard：合併後同時保留精確 V2 provenance、目標修正歷史排除、
MaxCardsType／signed residue，以及遠端 distance definition cache 和 identity revision 通知鏈。
H50P 工具與其 CMake 目標隨遠端保留；本次沒有執行這些工具。

主倉庫原有 82 個 tracked dirty 檔及 3 份新增說明文件，按三個主題保全：

1. 原生 package 遷移與待複審草稿，連同互相依賴的標頭。
2. HUMAN 規則／裝備 Lua V2 bridge、Scenario 及載入設定。
3. A 開頭 isolated AI 的公開資料投影、manifest 與說明。

外部權威為 `lolosiyue/extensions` 的 `main`。本次同步前 HEAD 為 `4c8595d`，
遠端為 `a596049`，落後 110 個提交；本地 23 份待提交來源與 L 端副本 SHA-256 一致。
遠端修改 102 條路徑，與這批本地修改重疊的是 `extensions/newgenerals.lua`。
合併保留遠端既有技能的 V2 遷移，以及本地新增 HUMAN 武將內容；逐行差異比對確認本地新增內容完整保留。
外部提交與主倉庫分開記錄；ignored Lua／extension 不強制加入主倉庫。

本次使用普通提交與合併，保留雙方歷史，不 rebase、不 reset、不 force-push。
最終提交／合併／遠端 HEAD 以本文所屬分支的 Git 紀錄與交付回報為準。

### 本次整合結果

| 倉庫／提交 | 內容 |
| --- | --- |
| 主倉庫 `1619a8c0` | 71 個 package 檔與本進度文件，保全未完成的遷移檢查點 |
| 主倉庫 `b92c3798` | HUMAN Lua V2 bridge、Scenario、SWIG、載入設定及文件 |
| 主倉庫 `361ba031` | isolated AI 公開資料投影、manifest 及文件 |
| 主倉庫 `fd45765b` | 合併 `origin/debug` 的四個提交；無文字衝突 |
| 外部倉庫 `df3249e` | 六個 A package AI 與 facade，共七檔 |
| 外部倉庫 `bdaf625` | HUMAN 內容及 newgenerals 增補，共十六檔 |
| 外部倉庫 `6d7cbdb` | 合併 `origin/main` 的 110 個提交；無文字衝突 |

推送目標為主倉庫 `origin/debug` 與外部倉庫 `origin/main`。
外部合併後只部署本次變更集合：124 組來源／目標中，102 檔需要更新，
全部 124 組 SHA-256 相符。部署前再次核對 L 端與合併前版本一致，未覆蓋其他本地改動。

主倉庫相對遠端基線的 `git diff --check` 通過；外部 AI 提交亦通過。
外部 HUMAN 新匯入的九檔保留原有空白格式，共 445 處 `diff --check` 警告，
因此**外部完整提交範圍的空白檢查未通過**，不將乾淨工作目錄當成該項通過證據。
警告集中於 Shijia、Xiangqi、birth、guozhan、happyrebel、hezongkangqin、jiangshi、jieyi、system。
本次未改寫這些來源格式，也未執行 Lua 玩法驗證。

## 驗證與後續順序

本次可確認的是來源保存、限定範圍 `git diff --check`、遠端合併、提交內容清單及副本雜湊。
推送成功只代表遠端收到提交，不代表 CI 通過。

後續先修上表確證缺口，再接續尚未遷移家族；以整包為單位完成新機制複審。
取得檢查點驗證授權後再建置受影響目標、生成 SWIG，最後安排必要玩法驗收。
本次沒有新增測試套件，也沒有把未執行的 gate 標為通過。
