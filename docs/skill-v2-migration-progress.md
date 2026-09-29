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
| `ol-strengthen.cpp` | 5 |
| `ol.cpp` | 347 |
| `tenyear-strengthen.cpp` | 69 |
| `tenyear.cpp` | 144 |
| `tenyear2.cpp` | 368 |
| `yjcm2013.cpp` | 14 |
| `yjcm2014.cpp` | 14 |
| `yjcm2015.cpp` | 2 |
| `yjcm2022.cpp` | 6 |
| **合計** | **1,139** |

同一掃描另得到 3,120 個直接以 `*SkillV2` 為基底的宣告。這兩項不是武將數、
可用技能數或完成率；不能把未覆審的新類別算成完成。
2026-09-29 qs-cursor 這次只把 `tenyear2.cpp` 的戰意、暗涌、暗涌計數、殤決、困厲、朝鳳、涕泣改成 `*SkillV2`，並刪掉已併入的戰意效果殼與涕泣計數殼，所以 V2 總數先加 8、記為 3,128；沒有重掃其他檔。同一天續批再把 Zhuilie、ZhuilieSlash、Zhuihuan、ZhuihuanEffect、Xianjing、Dufeng、DufengRange、Qingleng、Feibai 改成 `*SkillV2`，V2 總數再加 9、記為 3,137；仍沒有重掃其他檔。同一天第三批再把 TenyearMingfa、TenyearMingfaEffect、TenyearJilei、TenyearJileiClear、Jini、JiqiaosyEnter、CixiaoSkill 改成 `*SkillV2`，V2 總數再加 7、記為 3,144；仍沒有重掃其他檔。同一天第四批再把 Juying、JuyingClear、JuyingTargetMod、Lieyi、LieyiTarget、TenyearLongyuan、Lvli、Choujue、Beishui、Qingjiao 改成 `*SkillV2`，V2 總數再加 10、記為 3,154；仍沒有重掃其他檔。同一天第五批再把 Yongbi、YongbiKeep、TenyearKuangfu、Weilie、Bihuof、FengshiMF、FengshiMFDamage、Liji、Miaoxian 改成 `*SkillV2`，V2 總數再加 9、記為 3,163；仍沒有重掃其他檔。同一天第六批再把 MouniDying、Zongfan、XiangshuZKEffect、TenyearZecai、TenyearZecaiLose、Aishou、Xiyan、Pingzhi、XiongraoClear 改成 `*SkillV2`，V2 總數再加 9、記為 3,172；仍沒有重掃其他檔。同一天第七批再把 Tongli、Qianzheng、Diou、Zhengyue、Laoyan、Wanchan、Xinghan、Cuichuan 改成 `*SkillV2`，V2 總數再加 8、記為 3,180；仍沒有重掃其他檔。同一天第八批再把 Xunji、XunjiLose、Liedan、Zhuangdan、ThZhengui、ThZhuyin、ThJiaowei、SpCuorui、Chuanshu、ChuanshuDeath 改成 `*SkillV2`，V2 總數再加 10、記為 3,190；仍沒有重掃其他檔。同一天第九批再把 Heqia、Fenyue、FenyueRevived、TenyearJinggong、ZhanyuanRecord、Anzhi、TenyearLingyin、TenyearLingyinEffect、TenyearSuifuRecord、TenyearSongci 改成 `*SkillV2`，V2 總數再加 10、記為 3,200；仍沒有重掃其他檔。BihuofDraw 仍是 `DrawCardsSkill`。

`ol.cpp` 與 `ol-strengthen.cpp` 的列是後來對這兩檔重掃的結果（348→347、14→5）。
合計只減去這個差額。其餘檔案與 3,120 的 V2 總數沒有重掃。
2026-09-29 這次只把 `yjcm2013.cpp` 的 Longyin、`yjcm2022.cpp` 的 Duanwan 從直接 legacy 改成 `TriggerSkillV2`，所以這兩格與合計各減 1；沒有重掃其他檔。
2026-09-29 qs-cursor：改 `tenyear2.cpp` 之前直接重數該檔是 450（表上仍是 451）。本批去掉 10 個直接 legacy 宣告後該格為 440，合計按表上 451 收到 440，減 11。續批再去掉 9 個，該格為 431，合計 1,202。第三批再去掉 7 個，該格為 424，合計 1,195。第四批再去掉 10 個，該格為 414，合計 1,185。第五批再去掉 9 個，該格為 405，合計 1,176。第六批再去掉 9 個，該格為 396，合計 1,167。第七批再去掉 8 個，該格為 388，合計 1,159。第八批再去掉 10 個，該格為 378，合計 1,149。第九批再去掉 10 個，該格為 368，合計 1,139。

## 複審的主要待辦

| 範圍 | 下一步與已知限制 |
| --- | --- |
| `mobile` | Fuman 配額改按 phase id 保存，結束內層出牌階段不再清掉外層；摸牌歸交牌者，該【殺】在受益者下一個回合結束時失效。Zhuhe 護甲分支維持 `zhuhe3`。Tom 2026-09-29：築墼從牌堆獲得並使用該花色裝備，不再讀棄牌堆。其餘 legacy 家族未遷移 |
| `mobileshiji` | Tom 2026-09-29：唔重做永久頂7。Xingzhen 維持現行交換／觀星流，不改架構。Mouli／Miewu 完成事件與 Yaohu 階段收據已讀，沿用 CardFinished／PostCardResponded／phase id，本輪未改。全檔其餘生命週期未結案 |
| `mobile-strengthen` | Dingpin 計入 response-use（`respond_card.is_use`），打出不計。Benxi 在 TurnBroken 清掉仍顯示 active 的階段距離。MobileAnjian 改為每個範圍外目標一條 preferredTarget，不再用裝備才解析的 `->` 字串。MobileZhuikong 用受益者自己的 `-SelfClear` 標記撐到其回合結束；OLZhuikong 仍共用舊 flag。這四項未跑對局 |
| `ol` | TongxieTargetMod 已拆成 System 修正：`#tongxie-target` 只處理 `tongxie_slash`；dongxin、bianyu、quanyu、qiangang、chixin、gengzhan、maozhu、ol2shanjia、wangong 各自註冊一次，不乘同協實例。同協本體與 TongxieEffect 仍舊制（共享 `&tongxie+#` 標記，逐實例會雙倍摸牌／抵傷）。直接 legacy 347。三個翻譯單元曾以 Qt 6.4.2／g++ 13 的 server 設定編譯；同協拆分區段沒有診斷，同檔其餘既有錯誤仍在。未對局 |
| `ol-strengthen` | 已遷 Jijiang、Qiaobian 族、Jiushi、Dangxian、Huashen／Select／Clear；OLQingjian 取消時按原牌堆收據還原。ol_zuoci 仍掛標準 V2 Xinsheng，化身池改為與它相同的私有 `Huashens`／`huashen_general`。仍舊制 5：OLGuhuo、OLXianzhou、OLJiaozhaoVS、OLJiaozhao、OLDanxin。Guhuo 與 Jiaozhao／Danxin 的裁定見 PR，未改規則。翻譯單元曾編譯；新家族區段沒有診斷，同檔其餘既有錯誤仍在。未對局 |
| `olwenwu` | 直接 legacy 仍為 0。JinTairan 回執改在回復與摸牌之後發布；標記按各回執快照重算；失去體力前取消會還原回執，進入 `loseHp` 後不還原。歷史查詢不完整時不發布猜測處罰。翻譯單元曾編譯；JinTairan 區段沒有診斷，同檔其餘既有錯誤仍在。未對局 |
| `tenyear*` | Tuicheng、Xianju、Kangming、Jiewei、Xuanfeng、Yongjin 已在 V2，本輪讀回沒有改規則。Yizhen 把缺席花色當 0，避免手牌只有一種花色時同時當成「唯一最多」和「最少」。**qs-cursor 2026-09-29：** `tenyear2.cpp` 遷了戰意、暗涌、殤決／困厲、朝鳳、涕泣。戰意加成改掛在各實例的回合清除標記上，錦囊／裝備／基本牌效果強制結算，距離修正讀父實例標記；棄牌階段忽略錦囊只做一次。暗涌計數仍在傷害優先序 5、詢問在 2，計數之後標記為 1 才問，也就是現行的「本回合第一筆且為 1 點」，沒有改成可能的「第二次」。殤決／困厲用遊戲配額，角色標記仍只醒一次；殤決授予困厲走效果來源。朝鳳出牌階段各實例一次。涕泣摸牌差仍是抽牌者的單一回合標記。選定類別在付款時已經沒有可棄的牌就不發加成。這些沒有對局。`tenyear2.cpp` 以 Qt 6.4.2／g++ 13.3 的 server 設定獨立 `-c` 通過；新家族區段沒有診斷，同檔既有 `insertMulti` 棄用警告仍在。Zhengxu 的回合令牌在失去技能後仍會消耗，拆成逐實例會重複發放，本輪不改。**qs-cursor 續批：** Zhuilie 只結算第一個實例，避免同一張殺重複判定；距離修正仍是 999。Zhuihuan 的詢問也只走第一個實例，`#zhuihuan` 改在 `recordEvent` 裡依標記結算，失去技能後既有標記仍會在下個回合開始時生效。Xianjing、Dufeng、Qingleng、Feibai 同樣只結算第一個實例，避免共享標記或棄牌被加兩次。Dufeng 的固定攻擊範圍用系統修正讀 `&dufeng-PlayClear`，技能失去後該標記在出牌階段內仍有效。Feibai 用強制收集，點擊不發動時仍更新字數標記。這些沒有對局。續批的 `tenyear2.cpp` 再次以 Qt 6.4.2／g++ 13.3 的 server 設定獨立 `-c` 通過；新家族區段沒有診斷，同檔既有 `insertMulti` 棄用警告仍在。**qs-cursor 第三批：** TenyearMingfa 只結算第一個實例，出牌階段標記仍連續加兩次。`#tenyearmingfa` 改在優先序 2 的 `recordEvent` 結算，失去技能後既有牌堆與目標標記仍會在回合結束或死亡時生效。TenyearJilei 只結算第一個實例，避免同一傷害把雞肋類別寫兩次；已有該類別時仍先廣播並寫日誌，然後不重複限制。`#tenyearjilei-clear` 維持優先序 5，在 `recordEvent` 清掉 `use,response,discard` 的 `類型|.|.|hand$0`，不要求仍持有技能。Jini 受傷重鑄只走第一個實例；帶 `jiniUse` 的牌在 PreCardUsed 強制寫入最後目標的 `no_respond_list`。JiqiaosyEnter 與 CixiaoSkill 都在優先序 2 的 `recordEvent` 結算，前者出牌階段結束清私有牌堆，後者依 `&cxyizi` 取得或卸下 panshi，都不要求持有者仍有技能。這些沒有對局。第三批的 `tenyear2.cpp` 以 Qt 6.4.2／g++ 13.3 的 server 設定獨立 `-c` 通過；新家族區段沒有診斷，同檔既有 `insertMulti` 棄用警告仍在。**qs-cursor 第四批：** Juying 強制進入選擇，只結算第一個實例；次數、手牌上限和摸牌仍是共享效果，選項超過體力才棄牌。`#juying` 在優先序 2 的 `recordEvent` 計出牌階段的殺，並在進入未行動時清掉當前回合的次數標記，仍要求持有該輔助技能。次數修正改為系統修正，只讀 `&juying_cishu+#回合`，不按實例累加。Lieyi 的瀕死標記維持優先序 5，每次瀕死派發仍各加一次；`#lieyi` 對帶 `lieyi_use_card` 的殺給 999 點次數和距離，系統修正只算一次。TenyearLongyuan、Choujue 在優先序 2 的 `recordEvent` 巡迴所有角色，角色標記仍只醒一次。Lvli 只問第一個實例，摸牌或回復仍補到體力或手牌，不另乘係數。Beishui 只醒第一個實例。Qingjiao 出牌階段只問第一個實例；結束階段清 `qingjiao-Clear` 並棄置仍在 `recordEvent`，不要求當時還持有技能。仇決、背水裡 `hasSkill("lvli"), true` 的寫法保留。這些沒有對局。第四批的 `tenyear2.cpp` 以 Qt 6.4.2／g++ 13.3 的 server 設定獨立 `-c` 通過；新家族區段沒有診斷，同檔既有 `insertMulti` 棄用警告仍在。Zhengxu 仍不改。**qs-cursor 第五批：** Yongbi 的減傷看 `&yongbi_buff2`，不要求仍有技能實例；減到不大於 0 仍中止該次傷害。`#yongbi` 用手牌上限的系統修正讀兩個標記，各乘 2，只算一次。TenyearKuangfu 維持優先序 5，狂斧牌不計入手牌使用次數，並在造成傷害時寫 `tenyearkuangfu_damage-Clear`。Weilie 每次 `fuping_record` 只加一次 `&weilie_time`。Bihuof 每次傷害只問一次並加上下一回合的加減摸牌標記；`BihuofDraw` 仍是舊的摸牌修正。FengshiMF 只結算一次棄牌並把房間標記加一；`#fengshimf` 在傷害造成時加上該標記，牌結束時清掉，不要求持有技能。Liji 回合開始的存活人數和當前回合棄牌計數都只加一次，回合開始優先序 5、移牌優先序 2。Miaoxian 用紅色牌後手牌沒有紅色才摸一張，只摸一次。這些沒有對局。第五批的 `tenyear2.cpp` 以 Qt 6.4.2／g++ 13.3 的 server 設定獨立 `-c` 通過；新家族區段沒有診斷，同檔既有 `insertMulti` 棄用警告仍在。**qs-cursor 第六批：** MouniDying 維持優先序 5，在 `recordEvent` 結算：有人帶謀逆回合標記時給瀕死角色旗標；帶謀逆殺的傷害完成後清旗並把房間標記加一。不要求持有技能，謀逆與 secondmouni 各掛一份。Zongfan 在優先序 2 的 `recordEvent` 結算。跳過出牌階段仍加 `zongfanSkipPlay-Clear`，不要求當時持有技能；進入未行動時，角色標記已醒、沒有技能、或該跳過標記大於 0 就不醒。醒的條件仍是謀逆回合標記或 canWake，然後設標記、分牌、改體力上限、回復，並換成 zhangu。XiangshuZKEffect 在出牌階段結束依既有 `&xiangshuzk` 標記結算，不要求仍持有技能。TenyearZecai 限定技只結算一次；錦囊數唯一最高者是自己時當成沒有最高者。TenyearZecaiLose 是全域技能，優先序比擇才高 1，所以回合結束先卸 nosjizhi 再授予；PreCardUsed 計錦囊，失去技能後既有標籤仍會卸。Aishou 的 BeforeCardsMove 優先序 0、其餘事件 2，仍要求持有技能：結束階段按體力上限摸牌並標 qrasai，回合開始棄這些牌，棄牌數大於體力且體力上限小於 8 就加上限；手牌裡最後一張 qrasai 離開時減上限。Xiyan 只在牌進入自己的 zyyyuan 且花色滿四種時結算一次，清掉別人的 `&yuanyu` 標記並獲得該牌堆，然後給當前回合角色加或減 4 點手牌上限。Pingzhi 在傷害完成時，若牌帶 PingzhiDamage，給傷害來源加一次標記，不要求持有技能。XiongraoClear 維持優先序 5，回合結束或當前回合角色死亡時清 `xiongrao_debuff` 並刷新手牌；`XiongraoInvalidity` 仍是 `InvaliditySkill`，不在這次計數裡。縱反分牌仍把當下手牌交給 `askForyiji` 的非常量引用。這些沒有對局。第六批的 `tenyear2.cpp` 以 Qt 6.4.2／g++ 13.3 的 server 設定獨立 `-c` 通過；新家族區段沒有診斷，同檔既有 `insertMulti` 棄用警告仍在。**qs-cursor 第七批：** Tongli、Qianzheng、Diou、Zhengyue、Laoyan、Wanchan、Xinghan、Cuichuan 都維持原本優先序，各事件只收一個觸發。同勢在出牌階段花色數等於 `secondmobilexinzifu-PlayClear` 時才問，裝備和延時錦囊只記日誌；結算標記在牌結束時重用同一張牌，不要求當時還持有技能。強征移牌優先序 3，重鑄後同類型就不記標籤。敵歐、征越仍掛原來的視為技，只在持有技能時問一次。勞燕指定多目標時一次巡完所有持有者，進入未行動時丟掉帶 laoyan 標記的手牌。婉嬋在 PreCardUsed 展開相鄰目標並寫回使用資料，不要求持有技能。興漢只在第一張殺的傷害上，對鎮閣目標各摸一次。摧川進入未行動時把 `&cuichuan_extra_turn` 換成額外回合；裝備移動仍是移牌前優先序 0、移牌後優先序 5，只認摧川自己的移動原因。這些沒有對局。第七批的 `tenyear2.cpp` 以 Qt 6.4.2／g++ 13.3 的 server 設定獨立 `-c` 通過；新家族區段沒有診斷，同檔既有 `insertMulti` 棄用警告仍在。**qs-cursor 第八批：** Xunji 在結束階段只結算一次，按 `&xunji_debuff` 讓對應角色打出決鬥，不要求結束階段的角色持有技能。`#xunji` 在那張決鬥造成傷害時讓使用者失去等量體力，只結算一次。Liedan 在回合開始巡一次其他持有者，比較手牌、體力和裝備；次數為 0 就失去體力並加 `&xhjldlie`，該標記大於 4 且沒有壯膽時擊殺持有者。Zhuangdan 在進入未行動時，給手牌唯一最多的其他持有者加 `&zhuangdan-SelfClear`，只加一次。ThZhengui 在牌結束或回復時清 `thzhenguiTarget`、展示手牌，並按 thzhengui 標記失去體力，不要求仍持有技能；非實體牌結束不會清標記。ThZhuyin 成為殺或非延時錦囊目標時只結算一次，牌堆有同名牌就抵消全體目標，否則把宴佐次數加到不超過 2 並放入一張同名牌。ThJiaowei 初始手牌仍標 `thjiaowei_xuan`；手牌離開時把 `&thjiaowei-Clear` 設為 1，清旗用的仍是 `-tyjiaowei_xuan`；有標記時把傷害減到 0 並中止該次傷害。SpCuorui 在回合開始的 `recordEvent` 給每個角色加 `spcuorui_round-Keep`，只有第一次且體力大於 0 並持有技能才詢問。Chuanshu 與 secondchuanshu 在回合開始各問一次，死亡輔助也各問一次；限定標記扣掉後授予對應朝鳳，並取得 longdan、congjian、chuanyun。這些沒有對局。第八批的 `tenyear2.cpp` 以 Qt 6.4.2／g++ 13.3 的 server 設定獨立 `-c` 通過；新家族區段沒有診斷，同檔既有 `insertMulti` 棄用警告仍在。**qs-cursor 第九批：** Heqia 在出牌階段只問一次 `@@heqia1`。Fenyue 在出牌階段的階段開始、牌結束、獲得技能和死亡時，用 `recordEvent` 把 `fenyue-PlayClear` 設成非友方人數，只寫一次。`#fenyue-revived` 復活時從行動序開頭連續更新仍在出牌階段的奮鉞，遇到第一個不符合的角色就停。TenyearJinggong 把帶該技能名的殺的傷害改成距離，最多 5；距離不大於 0 仍中止該次傷害。ZhanyuanRecord 與 secondzhanyuan 在蠻嗣牌入手時各加一次對應標記，持有技能用公開標記，否則用私有標記。Anzhi 受傷且沒有 `anzhi_wuxiao-Clear` 時只問一次。TenyearLingyin 出牌階段牌堆不空且回合長度大於 0 時，把長度寫進 `tenyearlingyin_lun-PalyClear` 再詢問。`#tenyearlingyin` 在確認傷害時，來源不是受傷者且有 `&tenyearlingyinBuff-Clear` 才加上該標記；進入未行動時清 `ViewAsSkill_tenyearlingyinEffect`。`#tenyearsuifu` 在有當前回合時把傷害點數加進受傷者的 `tenyearsuifu_damage-Clear`，不要求持有技能。TenyearSongci 使用筆伐牌時，存活角色都有對應標記才摸一張。這些沒有對局。Zhengxu 仍不改。`tenyear.cpp`、`tenyear-strengthen.cpp` 與 `tenyear2.cpp` 其餘 legacy 家族未遷移，不能把這一批當成整包結案 |
| `dream` | SV2 Codex：Mishou 首次殺收據改在 cost 消耗，整體／逐目標取消與 bypass 不回滾，並保留精確來源及目標攔截結果；修 IfShenfeng 預覽牌 lease API。Tunshi：Tom 裁定 B（2026-09-29）落地——死者同名多實例逐實例 grant，受益者已有該名亦再加；不再按技能名去重。最後一批未完整讀回。dream TU 獨立編譯通過，未做玩法驗收。**SV2 Grok leftover（2026-09-29）**：headless 讀回 inventory，`--max-turns 80` 耗盡且無程式／玩法改動；其後 Tunshi 裁定 B 已落地 |
| `yczh2016/2017`、`yin` | SV2 Codex：修 Danxin 收據寫入 protected tag 的編譯錯誤、YinShicai 用牌歷史 ID 欄位；Taoluan 提供牌／完成回執、新 2017 家族仍待複審。yczh2016／yin TU 曾獨立編譯，未跑玩法；Jiaozhao／Danxin 先前指定 findings 閉合狀態不擴大為整包完成。**SV2 Grok（2026-09-29）**：Juzhan／Chenglve／OLLijun 限時收據在嵌套回合正常結束、未開階段的額外回合、內層階段結束與 TurnBroken 時按當時的 turn／phase 投影；成略棄牌花色改依公開移牌歷史，歷史不完整時用本次棄牌而不是當成沒有棄牌。yin TU 再次獨立編譯通過，未跑玩法。額外回合在開階段前被 TurnBroken 時，排程器現會先 trigger(TurnBroken) 再視需要結束階段，技能可見收尾事件已補上（SV2 Grok continue 2026-09-29）。**SV2 Grok yczh2017**：諫征、專對、天辯、福綿／怠宴、忠鑒／才識、清弦／絕響及弦系、問卦／伏誅、復難／誡訓、守璽／惠民、辟撰／通博，連同手殺清弦／絕響／殘韻與 OL 忠鑒／才識已讀回。借刀的額外目標改寫 Collateral 會讀的 `attachTarget`；復難的「本回合不能用」跟著當前回合，插入回合先卸下、外層回合回來再套上，來源失去技能也不撤銷；忠鑒「限兩次」以該回合的收據為準，才識同時有兩種禁牌時兩邊都禁。誡訓待升級不再直接寫 protected tag。惠民交出的牌改走 `obtainCard(Card*)`，否則這個翻譯單元編不過。yczh2017 TU 獨立編譯通過，未跑玩法。福綿「下一張紅」目標加點只認 PreCardUsed；紅色回應使用不再消耗收據（回應管線無目標列表；SV2 Grok continue 2026-09-29） |
| `yjcm*`、`zombine` | SV2 Codex：2012 TU 獨立編譯通過，旁路付款／序列化僅局部讀回，尚未玩法驗收；2013／2014／2022 legacy、Huomo／Zhanjue、zombine 全包複審保留。Fencheng 新舊 C++ 均先詢問可棄牌再走 damage，未另加免傷裁定；免傷對局仍待驗證。2014 Sidi 未找到可直接套用草稿，未遷移。**SV2 Grok leftover（2026-09-29）**：max-turns 耗盡，未閉合 yjcm*／Huomo／Zhanjue／zombine；無 TU 編譯。**SV2 Grok yjcm（2026-09-29）**：奇策／戰絕在效果裡把整手牌補成當下的牌；戰絕傷害名單去重，OL 在傷害歷史不完整時仍只摸自己；活墨查不到歷史時保留上一份「已知」投影；弓騎按回合 id 卸範圍；醇醪缺標記時不再把存放當成救人。龍吟、斷腕改為 V2（斷腕配額在付款與 EventSkillInvoking 提交，疊嶂升級留在效果）。2014 司敵仍無草稿，未遷移。zombine 未在該輪改動。**SV2 Grok zombine（2026-09-29）**：感染 `viewAs` 只產生鐵鎖，改由 `filterCards` 接管，不再把包裝牌交回去；區域改查 `Sanguosha->getCardPlace`。完殺補 `#zb_wansha-limit`，回合內除持有者外只有瀕死角色能使用桃。誕育在回合開始事件記錄時凍結各實例當時的標記，只發動這些實例；標記在目標效果通過取消檢查後才扣，結算途中新得到的留下，同一事件不再補發。失去技能時只在失去的是誕育時重算公開標記。zombine 翻譯單元曾 `-c`，未跑玩法。休眠復活與未掛武將的 Cee 仍開。**SV2 Grok continue（2026-09-29）**：口令補上已寫好卻未訂閱的 CardUsed／CardsMoveOneTime；宴誅 Trigger 舊殼已註解，現由 ViewAsSkillV2 退休分支負責；討襲仍依使用者暫緩；2013 巧思／ExtraCollateral、2014 司敵、2022 別君／三顧／疊嶂與口令本體、zombine 休眠復活／Cee 無正文授權，記 DEFER。傾襲棄牌與滅計選牌只改成現有手牌／裝備介面，讓這兩個翻譯單元編得過，沒有改規則。四個翻譯單元只做 `-c`，未跑玩法 |
| 其餘早期包 | 有作者來源檢查點，仍需依 [Skill V2 新機制](SkillV2新機制說明.md) 完成整包複審，不能只依零 legacy 盤點結案 |

討襲（Taoxi）由使用者指定暫緩：保留既有實作，不擴充 `ServerPlayer::getHandPile`
的多實例借牌入口。此項不是完成，也不以共享單一 `TaoxiId` 冒充逐實例能力。

2026-09-28 只複審 `mobile.cpp`、`mobileshiji.cpp`、`mobile-strengthen.cpp`、`tenyear.cpp`、`tenyear2.cpp`、`tenyear-strengthen.cpp` 裡點名的家族。上表這些行是讀碼結果。這次沒有把未建置、未對局的技能標成完成。

2026-09-29 Tom 裁定：Zhuhe 從牌堆獲得並使用該花色裝備。Xingzhen 唔重做永久頂7，現行交換／觀星流維持。Tunshi 不在這次改動裡。

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
