# 標準國戰魏勢力技能共用與 V2

2026-09-24 原始碼檢查點。以目前 xxyheaven 規則與身份局原版實作比對：相同效果在原版位置升級 V2，國戰只引用同一技能 ID；規則不同者保留 `heg_*`。不新增同效果別名技能。

## 共用技能

| 國戰武將 | 技能 ID | 原版來源與本次處理 |
| --- | --- | --- |
| 曹操 | `nosjianxiong` | `standard-generals.cpp`，改為 TriggerSkillV2；重查傷害牌仍全部在處理區，避免不同實例重複取得。 |
| 司馬懿 | `nosfankui` | `standard-generals.cpp`，改為 TriggerSkillV2；傷害來源放入 targets，抽牌經 effectTarget。 |
| 張遼 | `tenyeartuxi` | `tenyear-strengthen.cpp`，改為 TriggerSkillV2；cost 選人、effect 減摸牌、effectTarget 沿用唯一 TenyearTuxiCard 抽牌效果及移牌原因。保留優先度 1。 |
| 郭嘉 | `tiandu` | `standard-generals.cpp`，改為 TriggerSkillV2；保留語音選擇，選擇與效果階段均核對判定牌位置。 |
| 甄姬 | `qingguo` | `standard-generals.cpp`，改為 ViewAsSkillV2；保留黑色手牌、可回應手牌堆、Jink 花色點數與語音。 |
| 張郃 | `qiaobian` | `mountain.cpp`，原觸發技能及回應視為技改為 V2，刪除 HQiaobian；選牌在 cost，棄牌在 pay。 |
| 荀彧 | `quhu` | `fire.cpp`，原視為技改為 ViewAsSkillV2。 |
| 曹丕 | `xingshang` | `thicket.cpp`，改為 TriggerSkillV2；死亡角色的牌在整體 effect 處理，避免活目標過濾。 |
| 曹丕 | `mobilefangzhu` | `mobile-strengthen.cpp`，改為 TriggerSkillV2；採該變體完整規則，包含零失血時仍可選失去體力，保留極略提示及語音分支。 |
| 樂進 | `xiaoguo` | 直接引用 `sp.cpp` 已有 TriggerSkillV2，刪除重複 HXiaoguo。 |

十個技能均由 `General::addSkill(QString)` 引用原版註冊。驅虎保留唯一的 `QuhuCard` 拼點／伤害效果，巧變保留唯一的 `QiaobianCard` 取牌／移牌效果；兩者經現有原生 SkillCard 橋接，保留 class、response pattern、history 與 AI 協定，未複製效果或改成另一組國戰技能卡。這不是把兩個 SkillCard 的效果回呼全部改寫成 V2 proxy。

巧變沿用原版階段跳過語意：跳過判定階段不額外跳過摸牌階段；取牌、移牌由原版回應卡處理。移除舊 `heg_qiaobian` 翻譯中「同時跳過摸牌階段」的錯誤敘述，直接使用原版翻譯。

## 保留的規則差異

| 國戰技能 | 不直接共用的原因 |
| --- | --- |
| `heg_guicai` | 單獨只有手牌堆也可改判；原版 guicai 的 nude 檢查及極略強制改判路徑不同。 |
| `heg_ganglie` | 每次傷害判定一次；身份 ganglie 按傷害點数迴圈。 |
| `heg_luoyi` | 摸牌階段結束棄一張牌；身份 luoyi／nosluoyi 的發動與付款不同。 |
| `heg_yiji` | 每次傷害分配兩張；nosyiji 按傷害點數分次發動。 |
| `heg_luoshen` | 黑色判定牌整次循環結束才取得，保留 V2 move helper。 |
| `heg_shensu` | 包含失去體力並跳過棄牌階段的第三選項。 |
| `heg_duanliang` | 無距離限制，對距離大於 2 的目標使用後本階段停用。 |
| `heg_jushou` | 摸勢力數、棄非裝備或使用裝備，摸超過兩張才翻面。 |
| `heg_qiangxi` | 沒有身份強襲的攻擊範圍限制。 |
| `heg_jieming` | 每次傷害一次；身份節命按傷害點數發動。 |

據守選牌 `HJushouSelect` 與斷糧 `HDuanliangVS` 直接繼承 `ViewAsSkillV2`，以原生選牌／建卡 API 實作；魏將來源不再依賴 `xxy-hegemony-viewas`。據守保留 MethodNone 選牌，斷糧保留黑色基本／裝備牌轉兵糧寸斷與階段停用規則。

## 指定變體接線（2026-09-24）

使用者明確指定保留 `heg_ganglie`、`heg_shensu`、`heg_qiangxi`、`heg_yiji`；本批只將 `heg_tuxi` → `tenyeartuxi`、`heg_fangzhu` → `mobilefangzhu`，刪除 HTuxi／HFangzhu 及其翻譯副本。原生共用技能在原來源升級 V2，不改動四個保留技能。

突襲 AI 使用既有十週年策略選目標，將舊回應文字轉成候選角色清單，不再執行舊回應卡。國戰 callback 改接同一技能 ID 的 playerschosen；建安仍授予獨立 `heg_tuxi_egf`。放逐的國戰棄牌 callback 改接 `mobilefangzhu` 的原版 reason。

外部 AI 本批八個引用檔 L/H 完整 SHA-256 一致；十週年 AI 原有 `zishou`／`noszishou` 差異保留，僅在兩端追加完全相同的 V2 adapter，未覆寫整檔。相關 metadata 測試來源同步，未執行。

## 引用與相容性

- 移除 HStandardWeiGeneral 的巧變／驍果重複翻譯與提示，使用原技能翻譯。
- 建安選項改為共用 `qiaobian`／`xiaoguo`，授予時仍正確對應規則不同的 `heg_qiaobian_egf`／`heg_xiaoguo_egf`。
- 外部 AI 的八個受影響檔案移除舊別名映射並更新精確技能 ID；保留 `_egf` 獨立技能。L/H 修改前雜湊一致，回寫前再次核對未被其他工作改動，回寫後逐檔 SHA-256 一致。外部倉庫既有 dirty 與分歧保留；共用技能與 AI 適配已分別提交為 `6d8617f7`、`3035659`，未 push。
- 既有 content 契約同步共用技能 ID、V2 類型、原技能指標及已刪除重複 ID；驍果付款案例改測共用實作。

## 驗證邊界

| 項目 | 狀態 |
| --- | --- |
| 定向原始碼／引用／翻譯、git diff --check | 靜態檢查完成。 |
| content／驍果測試來源 | 已更新，未執行。 |
| 建置、focused executable、CTest、GUI、完整對局、CI | NOT RUN；未取得本檢查點的建置／執行授權。 |

參考 [TriggerSkillV2 系統說明](TriggerSkillV2系統說明.md)、[ViewAsSkillV2 遷移規範](active-skill-v2-migration-guide.md)。
