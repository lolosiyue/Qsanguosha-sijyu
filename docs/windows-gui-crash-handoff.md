# Handoff：Windows GUI client 閃退調查

**寫俾邊個：**喺 Windows 側開嘅 Claude Code session。
**日期：**2026-09-04　**來源分支：**`debug` @ `da10e77`（Linux/WSL 側）
**寫呢份嘢嘅原因：**Linux 側已經花咗兩輪（ASan build + 統計 A/B）查同一類崩潰，
查到一個確認根因、一個只係緩解、同一批未查完嘅嫌疑位。你唔好由零再考古一次。

---

## 0. 你嘅範圍（請照跟）

1. **第一階段淨係攞證據，唔好改 code。** 冇 symbol 嘅 crash 點估都係估。
2. **唔好 push 去 `debug`。** Linux 側同時喺呢條分支度做嘢。你要改就開
   `windows/<topic>` 分支，或者淨係交報告 + patch 檔。
3. 交付物：**symbolised crash stack + 崩潰率統計 + 你行過乜**。有 patch 更好，
   但冇 stack 嘅 patch 我哋收唔到（見 §6 判定門檻）。

---

## 1. 一句話背景

GUI client 打真正一局（尤其 5 人局）會中途 SIGSEGV／access violation，
server 隨後 `Room::~Room` → `stopGameThreads(10000)` 逾時 → abort。
**server 嗰個係後果，唔係另一個 bug，唔好去查佢。**

呢個唔係新 regression：`AGENTS.md`「GUI runtime 唔入 CI」一節已經記低
「Windows 環境有同樣問題，headless mode 閃退本身就係遊戲中已有現象」。
Windows CI 嘅 `headless` / `network` job 因為咁喺 2026-08-28 已經移除。

---

### 1.1 一條好重要嘅線索：**淨係 GUI 崩**

同一個 server、同一套協議，三個 client 之中：

| Client | 打完一局 | 有冇崩 |
|---|---|---|
| TUI（`qsanguosha_tui`） | 03_1v2、05p 真局 | **冇** |
| Web（`web/src/*.ts`，node headless） | 05p | **冇** |
| GUI（`QSanguosha`） | 02p / 05p | **崩** |

TUI 同 web **完全唔行 Qt scene graph**。所以：

> **唔好去查 engine、room logic 或者協議。** 崩潰源頭幾乎肯定喺 presentation 層
> ——`QGraphicsItem` 嘅生命週期管理 —— 即係 §4 嗰張表。

（TUI 側查到嘅缺陷全部係顯示層嘅嘢：牌印成 `牌 <id>`、日誌睇唔到、`勢力=` 恆空
之類，冇一個係崩潰。唯一跨 client 都會崩嘅係**打完一局之後 server 收檔階段**嘅
兩個 UAF，已經另外查到根因，同你要查嘅中途閃退唔同件事。）

## 2. 已經查實嘅根因（Linux 側，唔使再驗）

崩潰位置係 Qt 自己嘅 `QGraphicsScene` BSP index：

```
QGraphicsSceneBspTree::climbTree()  →  解引用一個已經 free 咗嘅 QGraphicsItem
```

即係「有人 `delete` 咗一個 graphics item，但冇先 `removeItem()` 佢出 scene」，
BSP index 個 leaf 清單仲留住嗰個野指針，下一次 paint 就踩爆。

已證實嘅其中一個來源：`PlayerCardContainer::updateMark()`（`&mark` 歸零嗰條路）
同步 `delete` 個 `QGraphicsProxyWidget`。**兩個條件缺一不可**（每個變體跑 8 局）：

| 變體 | 崩／總 |
|---|---|
| 原狀（同步 `delete`，冇 `removeItem`） | 8/8 |
| 淨係改做 `deleteLater()` | 8/8 |
| 淨係加 `removeItem()`，仍然同步 delete | 8/8 |
| **`removeItem()` + `deleteLater()`** | **0/8** |

所以安全寫法係四步，缺一不可：

```cpp
QGraphicsProxyWidget *proxy = _m_privatePiles.take(name);
if (proxy->widget()) proxy->widget()->deleteLater();
proxy->setWidget(nullptr);
if (proxy->scene()) proxy->scene()->removeItem(proxy);   // ← 先離開 scene
proxy->deleteLater();                                     // ← 唔喺訊號堆疊上銷毀
```

已落嘅 commit：

| commit | 做咗乜 |
|---|---|
| `b8181b7` | `QSanUiUtils::produceShadow` 用 `new uchar[]` 但 cleanup 用 `free()` → 改 `delete[]`。**呢個對 Windows 特別重要，見 §4。** |
| `1eb3f76` | `updatePile()` / `updateGeneralPile()` / `updateMark()` 三處改成上面四步 |
| `d5e62de` | `RoomScene` 設 `setItemIndexMethod(NoIndex)`（`src/ui/roomscene.cpp:656`） |

---

## 3. 最重要嘅一句：`NoIndex` 係緩解，唔係根治

實測（同一 binary、同一 seed、只差環境變數）：

| 產品 build 配置 | 崩／總 |
|---|---|
| BSP tree index（Qt 預設） | **5/5** |
| `setItemIndexMethod(NoIndex)` | **0/5** |

但**落咗 `1eb3f76` 之後，非 ASan 產品 build 同一 seed 仍然 6/6 崩，stack 一模一樣**。
即係：`updateMark` 嗰個 proxy 只係**其中一個**野指針來源；ASan 之下拆走佢就夠
（ASan 唔回收已 free 記憶體，時序完全唔同），真 allocator 之下唔夠。

`NoIndex` 只不過令 Qt 唔再去 walk 個 index，所以踩唔中 —— **啲野指針仲喺度**。
你喺 Windows 見到「更多未知閃退」，第一個假設應該係：
**同一批殘留野指針，喺 Windows 嘅 allocator 同時序之下用另一種形態爆。**

---

## 4. 未還嘅債：六個嫌疑位（已排優先次序）

全部喺 `src/ui/generic-cardcontainer-ui.cpp`，全部係**同步 `delete` 一個
`QGraphicsItem` 而冇 `removeItem()`**，同已證實根因同一個 class 嘅錯誤。

| 優先 | 行 | 函數 | 刪緊乜 | 點解可疑 |
|---|---|---|---|---|
| **1** | `1319` | `repaintAll(all=true)` | `foreach (QGraphicsProxyWidget *widget, _m_privatePiles.values()) delete widget;` | **同已證實根因完全同一類物件**，只係另一條路。頭號嫌疑。 |
| **2** | `1356` | `setPlayer()` | 同上，一模一樣嘅 foreach delete | 換座位／重連時行。第二號嫌疑。 |
| 3 | `1302` | `repaintAll` | `delete _m_equipCards[i]`（`CardItem`，係 `QGraphicsObject`） | 裝備區每次全刷都行 |
| 4 | `1313` `1433` `1443` | `repaintAll` / `updateDelayedTricks` | `delete _m_judgeIcons[i]`（`QGraphicsPixmapItem`） | **注意：唔係 QObject，冇 `deleteLater()`**，要另諗（見下） |
| 5 | `1413` | `removeDelayedTricks` | `delete _m_judgeIcons.takeAt(index)` | 同上 |
| 6 | `1439` `1720` | `updateDelayedTricks` / `stopHuaShen` | `delete _m_judgeCards[i]`、`delete _m_huashenItem` | 化身動畫收檔 |

`QGraphicsPixmapItem` 唔係 `QObject`，所以「唔喺訊號堆疊上同步銷毀」呢個條件要
用第啲方法滿足 —— 例如 `removeItem()` 之後掛入一條 pending 清單、下一個 event
loop tick 先真正 `delete`。**唔好淨係加 `removeItem()` 就當修好**，上面張表已經
證明單獨做一樣係 8/8 照崩。

同類寫法喺其他檔案仲有（未查）：`src/ui/dashboard.cpp:1261,1961,2256`、
`src/ui/cardcontainer.cpp:118`、`src/ui/rolecombobox.cpp:46,157`、
`src/ui/CharacterSpineActionController.cpp:611`。

---

## 5. Windows 特有嘅嫌疑（Linux 側查唔到嘅）

1. **Allocator 唔同 —— 呢個係你最大嘅優勢。**
   MSVC CRT 對 `new[]`/`free()`、`new`/`delete[]` 嘅錯配係真炸（debug CRT 直接
   assert，release 會靜靜咁爆 heap）；glibc 完全容忍。`produceShadow` 嗰個
   (`b8181b7`) 就係咁匿咗好耐冇人發現。**同類 UB 好可能仲有，而你行 heap
   validation 就即刻見到。** 值得 grep 全個 `src/ui`：`new .*\[` 配 `free(`。
2. **Windows deploy tree 有齊資產同 `extensions/`。**
   `/mnt/d/game/sgs/QSanguoshaFinal/` 嗰套（`image/` `audio/` `font/`
   `hero-skin/` `lua/`）加上 `GER.lua` 之類只喺 deploy tree 出現嘅檔案，
   令 Windows 行緊一批 Linux 從來冇行過嘅 code path（動畫、音效、皮膚、特效）。
   **崩潰多過 Linux 好可能就係呢個原因，唔一定係 Windows 本身。**
   反過來講：可以試吓喺 Windows 拎走資產再跑，睇吓崩潰率變唔變 —— 呢個係一個
   好平嘅實驗，直接分辨「Windows-specific」定「資產 code path specific」。
3. **Qt 版本。** Linux 側係 Qt 6.11.1。Windows 側可能行緊 Qt5（`c27ff44`
   "fix(xp): support Qt5 runtime"）。**第一件事確認清楚**：Qt5 嘅
   `QGraphicsScene` BSP 實作同 6.x 唔同，上面啲結論嘅 stack 位置可能對唔上。
4. **`windeployqt` 部署嘅 DLL 版本可能同 build 用嗰套唔同。** 確認一次。

---

## 6. 第一階段：攞證據（做完呢步先諗修）

### 6.1 一定要有 PDB
**唔好用出貨嘅 `QSanguosha.exe` 去查。** 自己 build 一個 `RelWithDebInfo`
（唔使 Debug，`/O2 /Zi` 一樣解到 file:line），否則個 stack 冇任何用。

### 6.2 攞 full minidump
兩個方法揀一個：
- `procdump -ma -e -x <dumpdir> QSanguosha.exe` —— 最直接
- 或者開 WER LocalDumps（`HKLM\SOFTWARE\Microsoft\Windows\Windows Error
  Reporting\LocalDumps`，`DumpType=2` 即 full dump）

用 WinDbg / cdb 開 dump，行 `!analyze -v` 同 `k`。**我要見到嘅係：崩潰喺
`Qt6Widgets!QGraphicsSceneBspTree::climbTree` 或者附近嗰類 scene traversal，
定係完全另一個位置。** 呢一條就決定咗你係咪同 Linux 撞同一個 bug。

### 6.3 分類，唔好一鑊熟
你講「更多未知閃退」—— 好可能唔止一個 bug。**用 crash 嘅 faulting
module + top 3 frames 做 key 分組統計**，唔好把所有 crash 當同一件事查。
Linux 側就係曾經把兩個唔同嘅 bug 歸做同一個「base defect」，兜咗大圈。

---

## 7. 第二階段：instrumentation（Windows 呢度比 Linux 強）

- **MSVC ASan**：`/fsanitize=address`（VS 2019 16.9+）。呢個係我哋喺 Linux
  冇嘅嘢 —— **Windows ASan 見到 Windows heap**，而我哋喺 Linux 嘅 ASan
  **完全睇唔到 Qt 內部嘅存取**（`libQt6*.so` 冇 instrument，喺 Qt 入面發生嘅
  UAF 係全靜嘅），呢點卡死咗成條路。你行得通嘅話會快好多。
- **Application Verifier + page heap**：對「free 咗仲用」嘅 case 係最直接嘅
  武器，開咗之後 UAF 即刻變成即場 access violation，唔使等到下一次 paint。
  **如果你只做得一樣嘢，做呢樣。**
- **`NoIndex` A/B**：`src/ui/roomscene.cpp:656` 已經係 `NoIndex`。試吓改返
  `BspTreeIndex` 再跑 —— 如果崩潰率飆升，即係你撞緊同一個 bug family，
  上面 §4 張表就係你嘅工作清單；如果冇分別，即係 Windows 嗰啲係另一件事，
  照 §6.3 分類行落去。

---

## 8. 判定門檻（**唔好跳呢節**）

呢個 bug 係 **flaky** 嘅：同一個 seed 都唔會出同一局（seed 只釘死 server 側發牌，
邊個贏、client 見到邊啲 askFor 每局都唔同）。

- **基線最少跑 8 局**，唔好用一兩局講「修好咗」。
- Linux 側用嘅係 Fisher exact test（8/8 vs 0/8 → p≈7.8e-5）。你至少要做到
  同一量級嘅證據先當數。
- **量基線嗰陣一定要關咗你自己嘅 instrumentation。** 見 §9。

---

## 9. 兩個方法學陷阱（Linux 側踩過，直接送你）

1. **診斷工具會改變你想量嘅嘢。**
   我寫過一個 per-paint scene audit，每 frame 叫一次 `items()`。點知
   `QGraphicsSceneBspTreeIndex::items()` 第一件事就係 `purgeRemovedItems()`
   —— 等於每 frame 幫佢清一次殘留項目，把崩潰率由 **100% 壓到 ~7%**。
   頭幾輪「統計唔顯著」全部係因為咁。
2. **ASan 嘅 0/8 唔可以外推去產品 build。** 已經寫喺 §3，但值得講兩次：
   ASan 唔回收已 free 記憶體，時序完全唔同，喺 ASan 之下「修好」可以完全唔代表
   產品 build 修好。**最終判定一定要喺產品 build 度做。**

---

## 10. 已排除，唔好再走一次

- **`873fa2c` 唔係成因，係修正。** `873fa2c^` 嘅 `client.cpp:1085/1138` 用
  `args[i].value<JsonArray>().first()` 去讀 V2 具名 map → 空 list `.first()`
  → null deref（`SEGV @ 0x18`，`Client::loseCards`），第一次牌移動就死。
- `EffectAnimation::deleteEffect()` 係死 code（`loop_finished` 冇 emitter）。
- 幾何 NaN／inf：逐 frame 掃過 `pos` / `boundingRect` / `sceneTransform`，冇。
- Qt 版本混用 / private header：Linux 側 `ldd` 全部指向同一個 prefix。
  **（呢一條 Windows 側未驗，見 §5.4 —— 你要自己確認。）**

---

## 11. 順手一提：已知但未修嘅周邊問題

- `src/ui/heroskincontainer.cpp` 係死 code（唔喺任何 CMake source list，
  而且已經同自己個 header 對唔上）。
- `./extensions/RAFTOM.lua` 嘅 `assert(io.open(GER, "r"))` 每局 throw 8 次，
  因為 `GER.lua` 只喺 Windows deploy tree 出貨，唔喺 repo 入面。
  **喺你嗰邊呢個 assert 應該唔會 throw** —— 如果照 throw，值得查。

---

## 12. 交返嚟嘅格式

麻煩包含：

1. Qt 版本 + build 配置（MSVC 版本、RelWithDebInfo？ASan？AppVerifier？）
2. Crash 分類表：`faulting module + top 3 frames` → 出現次數
3. 至少一個 symbolised full stack
4. 崩潰率統計：`<配置> → 崩/總`，基線同修正後各一行
5. Patch（如果有），連同「呢個 patch 喺產品 build 跑咗 N 局，崩 M 次」

Linux 側對應嘅記錄喺 memory：`qgraphicsscene-paint-crash`、`asan-gui-build`、
`gui-client-reply-path-defects`、`linux-gui-network-smoke`。
