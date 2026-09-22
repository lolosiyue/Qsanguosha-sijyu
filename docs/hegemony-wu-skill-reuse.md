# 國戰標準吳將：共用技能與制衡／度勢 V2

2026-09-22 原始碼檢查點。同名舊技能引用現有版本；使用者後續指定制衡例外，恢復國戰體力上限選牌限制並以 V2 重做。其餘共用技能不重做。

| 武將 | 直接引用的技能 ID |
| --- | --- |
| 孫權 | 獨立 V2 `heg_zhiheng` |
| 甘寧 | `qixi` |
| 呂蒙 | `keji` |
| 黃蓋 | `kurou` |
| 周瑜 | `yingzi`、`fanjian` |
| 大喬 | `guose`、`liuli` |
| 陸遜 | `qianxun`；另保留新技能 `heg_duoshi` |
| 孫尚香 | `jieyin`、`xiaoji` |
| 孫堅 | `yinghun` |
| 小喬 | `tianxiang`、`hongyan` |
| 太史慈 | `tianyi` |
| 周泰 | `buqu` |
| 魯肅 | `haoshi`、`dimeng` |
| 張昭張紘 | `zhijian`、`guzheng` |
| 丁奉 | `duanbing`、`fenxun` |

| 邊界 | 處理 |
| --- | --- |
| 共用版本 | 21 個舊技能使用既有的效果、翻譯、AI 與 related helpers。苦肉、英姿、反間、國色、謙遜、不屈等採目前註冊的版本；不額外補入原武將其他技能，例如詐降。 |
| V2 範圍 | `HZhiheng` 與 `HDuoshi` 均使用 `ViewAsSkillV2`。共用技能仍維持現有 V1／V2 類型；本批不聲稱全部共用技能已遷移 V2，也不新增 V1 國戰適配。 |
| 制衡 | 出牌階段每 activation instance 限一次；選取一至體力上限張自己的可棄置手牌／裝備牌，經通用 proxy 原子棄置後摸等量牌，棄光手牌不額外摸牌。選牌與付款前均檢查上限及可棄置性。`cost` 保存張數至 execution-local amount，允許 V2 數值攔截；效果只在 invoker 存活時摸牌。普通 `zhiheng` 不修改。 |
| 制衡相容 | 沿用 `heg_zhiheng` 翻譯及 `HZhihengCard` history key；實際卡牌為 `ActiveSkillCard`，不恢復舊 HZhihengCard 類別。一般及國戰 AI 改建通用 proxy，保留原選牌策略與 activation identity；次數由 V2 實例配額管線判斷。 |
| 度勢 | 一張紅色手牌轉為 `HAwaitExhausted`；僅出牌使用。`createCard()` 無副作用，材料付款、目標與效果沿用普通錦囊管線。 |
| 次數 | 使用 `Limit_Phase`、上限 4，按 activation instance 計算；沿用 `DuoshiAE` history key。刪除錦囊內額外增加同一 history 的邏輯，避免一次計兩次。 |
| 舊定義 | 刪除本檔其餘 H 技能及 11 個 H SkillCard 類別／meta-object 註冊；不另設別名殼。舊技能 ID 與舊 SkillCard 序列化不在本批相容範圍。 |
| 孫策魂殤 | 改為授予共用 `yinghun`／`yingzi`；只撤銷本次暫時授予的技能，保留角色原有同名技能。不保留 `HYinghun`／`HYingzi` 類別。 |
| 本地待提交測試來源 | 吳將共用註冊／related helpers、度勢 V2 契約，以及制衡最大體力與當前體力差異、空選／超量／重複拒絕、付款摸牌及單次配額案例；付款後精確揭將案例使用度勢的普通牌轉換。好施案例查共用 ID。 |
| 驗證 | 靜態引用與差異檢查；建置、focused executable、GUI、完整局與 CI 均 NOT RUN。本地 CTest 未執行。 |

舊英姿 V2 的示範與測試不再代表目前吳將接線。共用 V1 技能在暗將、多實例等情境的行為沿用現有管線，尚未驗收。
