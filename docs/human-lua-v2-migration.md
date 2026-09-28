# HUMAN extensions 的 V2 遷移

來源為 `TODO/HUMAN/extensions`。本批依「保留現版、補齊 HUMAN 缺漏」移植，
沒有用舊版同名檔覆蓋目前擴展，也沒有改寫其他現存技能。

## 內容與載入

| 檔案 | 內容 |
| --- | --- |
| birth、giantequip、guozhan | 囚籠、休整及裝備／卡牌；裝備來源和卸裝清理 |
| cdiy、ctg、xcx | 自訂武將、蓄力與陳塘關；實例狀態、主動技能與目標選擇 |
| game | 整經、衝虛、妙劍、蓮華、御風、天書；沿用既有小遊戲場景 |
| hezongkangqin | 四名秦將、三種牌、持續限制與光環 |
| happyrebel、jiangshi、jieyi、system | 身份／投票、僵屍感染、結義及補充身份模式 |
| Shijia、Xiangqi、xiuzheng | 世家／象棋卡牌與規則、休整武將 |
| newgenerals | 保留現版，補入 HUMAN 缺少的兩名武將及其技能 |

15 個新檔追加於 `lua/config.lua` 的既有 `extension_names` 末尾；
`newgenerals.lua` 沿用原載入位置，原內容逐位元保留至最終 return 前。
此安排保留既有擴展牌的註冊順序。外部來源仍由
`lolosiyue/extensions` 管理，主倉庫不強制追蹤 ignored extensions。

其餘十個同名 HUMAN 檔保留現版。`kearjsrg` 的表面缺項在註解內；
`godExam` 的場景註冊在兩版均停用；`yongjian` 的增兵已使用現版名稱。

結義模式使用 **`human_jieyi`**，顯示仍為「结义模式」，避免 `jieyi`
翻譯鍵覆蓋 `htms.lua` 的既有武將「结衣」。僵屍模式為
`08_jiangshi`／`16_jiangshi`，角色表修正為相應人数；`09pv`
依現版九人身份表替換一名忠臣為平民。

## V2 接線

規則與裝備 API 見 [human-lua-v2-api.md](human-lua-v2-api.md)。

- Trigger 的選擇、支付、效果及 record 依實際 callback 契約拆分；
  `ctx.owner` 和原事件玩家 `ctx.invoker` 分開處理。
- 主動技能使用 request 與原生 ActiveSkillCard／普通牌轉換；不以 legacy
  `on_trigger` 包裝或 SkillCard 外殼代替遷移。妙劍由 `ctx.updated_card`
  替換代理牌，保留同一次使用的精確來源與材料支付。
- 無持有者規則使用 `CreateRuleSkillV2`；裝備使用原生
  `EquipSkillV2` 的實體／虛擬來源驗證。精確附屬來源使用既有 parentRef API。
- 數值被動使用 CorrectSkillV2。無對應 V2 家族的專用 Filter、Prohibit、
  `CreateViewAsEquipSkill` 保留專用接口；這些不冒充 V2 工廠。
- 保留 donor 的實際玩法與翻譯。例如小遊戲技能的旗標限次原為每回合；
  不因翻譯寫「階段」而暗改其實際配額。僵屍感染移至既有 Death 復活邊界，
  其他 BuryVictim 技能及正常埋葬時序仍能執行。
- 歡樂反賊的原生死亡流程只在其 mode 與啟用 tag 同時符合時保留身份隱藏；
  死者旁觀沿用既有私人可見權限，其他模式不受影響。

## 驗證邊界

### 新機制複審修正

- 急斬改查當回合 `skill_invoked` facts，不再維護 `SkillTriggered` 次數 mark；
  化名同時接收 V2 發動與舊通知。舊 trigger adapter 不重複處理；裝備、隱藏
  helper 及兩個觀察技能本身不參與觀察，避免觀察者互相觸發無限結算。
- 化名只移除離開最近三個名單的 grant；留存項目保留 instance ID、usage 與
  state。新 grant 及 related helpers 以 parentRef 綁定，來源移除時一同退役。
- 殺神改查本回合的 use/response-use facts，將第一張殺的 event ID 與目前
  傷害的用牌祖先比對；中途獲得技能及巢狀用牌不再依赖 CardFinished 旗標。
- 本批修正 10 份 Lua 的相關數值效果，接入 `getEffectiveAmount(ctx)`；
  明理名字長度及其他動態公式以 effective amount 作倍率，據守 base amount
  為 5。花色及裝備欄序號移至 `extra_data`，不佔用 amount；成本與牌本身的
  效果不混入技能倍率。
- 不完整歷史會明確報 Lua 錯誤，不以空集合假裝零次；此分支與以上玩法均
  尚未經建置／執行期驗收。新 fact 必須配合本批原生引擎使用。

本批完成來源檢查、Lua 語法解析、宣告／翻譯／牌堆對照、載入順序、
名稱碰撞及 scoped whitespace 檢查。這些是靜態證據，不是玩法通過證明。
Lua 解析器只讀取語法，未載入 `sgs` 或執行擴展；既有 `continue`
按 grammar 相容方式解析，不證明其執行語義。

SWIG 生成、C++ 建置、Lua 啟動載入、GUI、小遊戲互動、對局及 CI 均未執行。
需另經檢查點授權，先正常建置受影響目標；舊執行檔不能驗證新增 API。
不新增測試套件。提交、遠端整合與推送狀態見 [Skill V2 遷移進度](skill-v2-migration-progress.md)。

以下既有核心／donor 限制保留為後續驗收注意點，未擴大修復：

- `Scenario::getRoles()` 使用 rebel 數產生 renegade metadata；結義實際
  9 席分配正確，但模式角色摘要可能為 10 項。
- 原生牌限制以 method＋pattern 儲存 reason；誘滅使用獨立 reason 清理，
  與其他技能設置完全相同 pattern 時仍需驗收覆蓋行為。
- 青囊沿用 donor 的毒牌抑制範圍；移牌中途發生 TurnBroken／StageChange
  時的暫存清理仍需執行期驗收。
- 小遊戲沿用 `ui-script/game` 及結果文字檔通訊，未驗收實際互動。

逐檔來源與同步 SHA-256 清單保存在本機 `builds/human-extension-v2/`；
它們是任務產物，不納入主倉庫。
