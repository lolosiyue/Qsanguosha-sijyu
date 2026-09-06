# Handoff：Windows GUI client 閃退調查

**收件對象：**在 Windows 側開啟的 Claude Code session。
**日期：**2026-09-04　**來源分支：**`debug` @ `da10e77`（Linux/WSL 側）
**撰寫本文件的原因：**Linux 側已經花了兩輪（ASan build + 統計 A/B）調查同一類崩潰，
查到一個確認根因、一個只是緩解、同一批未查完的嫌疑點。你不要從零再重新考古一次。

---

## 0. 你的範圍（請照辦）

1. **第一階段只拿證據，不要改 code。** 沒有 symbol 的 crash，怎麼估都只是估。
2. **不要 push 到 `debug`。** Linux 側同時在這條分支上工作。你要修改就開
   `windows/<topic>` 分支，或者只交報告 + patch 檔。
3. 交付物：**symbolised crash stack + 崩潰率統計 + 你做過什麼**。有 patch 更好，
   但沒有 stack 的 patch 我們收不到（見 §6 判定門檻）。

---

## 1. 一句話背景

GUI client 打真正一局（尤其 5 人局）會中途 SIGSEGV／access violation，
server 隨後 `Room::~Room` → `stopGameThreads(10000)` 逾時 → abort。
**server 那個是後果，不是另一個 bug，不要去查它。**

這不是新 regression：`AGENTS.md`「GUI runtime 不入 CI」一節已經記載
「Windows 環境有同樣問題，headless mode 閃退本身就是遊戲中已有現象」。
Windows CI 的 `headless` / `network` job 因為這樣在 2026-08-28 已經移除。

---

### 1.1 一條很重要的線索：**只有 GUI 崩**

同一個 server、同一套協議，三個 client 之中：

| Client | 打完一局 | 有沒有崩 |
|---|---|---|
| TUI（`qsanguosha_tui`） | 03_1v2、05p 真局 | **無** |
| Web（`web/src/*.ts`，node headless） | 05p | **無** |
| GUI（`QSanguosha`） | 02p / 05p | **崩** |

TUI 和 web **完全不使用 Qt scene graph**。所以：

> **不要去查 engine、room logic 或協議。** 崩潰源頭幾乎肯定在 presentation 層
> ——`QGraphicsItem` 的生命週期管理 —— 即 §4 那張表。

（TUI 側查到的缺陷全部是顯示層的問題：牌印成 `牌 <id>`、日誌看不到、`勢力=` 恆空
之類，沒有一個是崩潰。唯一跨 client 都會崩的是**打完一局之後 server 收尾階段**的
兩個 UAF，已經另外查到根因，與你要查的中途閃退是不同的事。）

## 2. 已經查實的根因（Linux 側，無需再驗）

崩潰位置是 Qt 自己的 `QGraphicsScene` BSP index：

```
QGraphicsSceneBspTree::climbTree()  →  解引用一個已經 free 了的 QGraphicsItem
```

也就是「有人 `delete` 了一個 graphics item，但沒有先用 `removeItem()` 把它移出 scene」，
BSP index 的 leaf 清單還留住那個野指針，下一次 paint 就會引爆。

已證實的其中一個來源：`PlayerCardContainer::updateMark()`（`&mark` 歸零那條路）
同步 `delete` 該 `QGraphicsProxyWidget`。**兩個條件缺一不可**（每個變體跑 8 局）：

| 變體 | 崩／總 |
|---|---|
| 原狀（同步 `delete`，沒有 `removeItem`） | 8/8 |
| 只改做 `deleteLater()` | 8/8 |
| 只加 `removeItem()`，仍然同步 delete | 8/8 |
| **`removeItem()` + `deleteLater()`** | **0/8** |

所以安全寫法是四步，缺一不可：

```cpp
QGraphicsProxyWidget *proxy = _m_privatePiles.take(name);
if (proxy->widget()) proxy->widget()->deleteLater();
proxy->setWidget(nullptr);
if (proxy->scene()) proxy->scene()->removeItem(proxy);   // ← 先離開 scene
proxy->deleteLater();                                     // ← 不在訊號堆疊上銷毀
```

已套用的 commit：

| commit | 做了什麼 |
|---|---|
| `b8181b7` | `QSanUiUtils::produceShadow` 用 `new uchar[]` 但 cleanup 用 `free()` → 改 `delete[]`。**這個對 Windows 特別重要，見 §4。** |
| `1eb3f76` | `updatePile()` / `updateGeneralPile()` / `updateMark()` 三處改成上面四步 |
| `d5e62de` | `RoomScene` 設 `setItemIndexMethod(NoIndex)`（`src/ui/roomscene.cpp:656`） |

---

## 3. 最重要的一句：`NoIndex` 是緩解，不是根治

實測（同一 binary、同一 seed、只差環境變數）：

| 產品 build 配置 | 崩／總 |
|---|---|
| BSP tree index（Qt 預設） | **5/5** |
| `setItemIndexMethod(NoIndex)` | **0/5** |

但**套用了 `1eb3f76` 之後，非 ASan 產品 build 同一 seed 仍然 6/6 崩，stack 一模一樣**。
也就是：`updateMark` 那個 proxy 只是**其中一個**野指針來源；ASan 之下拆掉它就夠
（ASan 不回收已 free 記憶體，時序完全不同），真 allocator 之下不夠。

`NoIndex` 只不過讓 Qt 不再去 walk 該 index，所以踩不中 —— **那些野指針還在**。
你在 Windows 見到「更多未知閃退」，第一個假設應該是：
**同一批殘留野指針，在 Windows 的 allocator 與時序之下用另一種形態爆。**

---

## 4. 未還的債：六個嫌疑點（已排優先次序）

全部在 `src/ui/generic-cardcontainer-ui.cpp`，全部是**同步 `delete` 一個
`QGraphicsItem` 而沒有 `removeItem()`**，與已證實根因同一個 class 的錯誤。

| 優先 | 行 | 函數 | 刪除對象 | 為何可疑 |
|---|---|---|---|---|
| **1** | `1319` | `repaintAll(all=true)` | `foreach (QGraphicsProxyWidget *widget, _m_privatePiles.values()) delete widget;` | **同已證實根因完全同一類物件**，只是另一條路。頭號嫌疑。 |
| **2** | `1356` | `setPlayer()` | 同上，一模一樣的 foreach delete | 換座位／重連時執行。第二號嫌疑。 |
| 3 | `1302` | `repaintAll` | `delete _m_equipCards[i]`（`CardItem`，是 `QGraphicsObject`） | 裝備區每次全刷都執行 |
| 4 | `1313` `1433` `1443` | `repaintAll` / `updateDelayedTricks` | `delete _m_judgeIcons[i]`（`QGraphicsPixmapItem`） | **注意：不是 QObject，沒有 `deleteLater()`**，要另想辦法（見下） |
| 5 | `1413` | `removeDelayedTricks` | `delete _m_judgeIcons.takeAt(index)` | 同上 |
| 6 | `1439` `1720` | `updateDelayedTricks` / `stopHuaShen` | `delete _m_judgeCards[i]`、`delete _m_huashenItem` | 化身動畫收尾 |

`QGraphicsPixmapItem` 不是 `QObject`，所以「不在訊號堆疊上同步銷毀」這個條件要
用別的方法滿足 —— 例如 `removeItem()` 之後掛入一條 pending 清單、下一個 event
loop tick 才真正 `delete`。**不要只加 `removeItem()` 就當修好**，上面那張表已經
證明單獨做仍然是 8/8 照樣崩。

同類寫法在其他檔案還有（未查）：`src/ui/dashboard.cpp:1261,1961,2256`、
`src/ui/cardcontainer.cpp:118`、`src/ui/rolecombobox.cpp:46,157`、
`src/ui/CharacterSpineActionController.cpp:611`。

---

## 5. Windows 特有的嫌疑（Linux 側查不到的）

1. **Allocator 不同 —— 這是你最大的優勢。**
   MSVC CRT 對 `new[]`/`free()`、`new`/`delete[]` 的錯配是真炸（debug CRT 直接
   assert，release 會悄悄爆 heap）；glibc 完全容忍。`produceShadow` 那個
   (`b8181b7`) 就是這樣匿藏了很久沒人發現。**同類 UB 很可能還有，而你跑 heap
   validation 就立刻見到。** 值得 grep 整個 `src/ui`：`new .*\[` 配 `free(`。
2. **Windows deploy tree 有齊資產與 `extensions/`。**
   `/mnt/d/game/sgs/QSanguoshaFinal/` 那套（`image/` `audio/` `font/`
   `hero-skin/` `lua/`）加上 `GER.lua` 之類只在 deploy tree 出現的檔案，
   令 Windows 跑著一批 Linux 從來沒跑過的 code path（動畫、音效、皮膚、特效）。
   **崩潰多於 Linux 很可能就是這個原因，不一定是 Windows 本身。**
   反過來說：可以試著在 Windows 拿走資產再跑，看崩潰率變不變 —— 這是一個
   成本很低的實驗，直接分辨「Windows-specific」還是「資產 code path specific」。
3. **Qt 版本。** Linux 側是 Qt 6.11.1。Windows 側可能跑著 Qt5（`c27ff44`
   "fix(xp): support Qt5 runtime"）。**第一件事確認清楚**：Qt5 的
   `QGraphicsScene` BSP 實作與 6.x 不同，上面這些結論的 stack 位置可能對不上。
4. **`windeployqt` 部署的 DLL 版本可能與 build 用那套不同。** 確認一次。

---

## 6. 第一階段：拿證據（做完這步才考慮修）

### 6.1 一定要有 PDB
**不要用出貨的 `QSanguosha.exe` 去查。** 自己 build 一個 `RelWithDebInfo`
（無需 Debug，`/O2 /Zi` 一樣能解析出 file:line），否則該 stack 沒有任何用。

### 6.2 拿 full minidump
兩個方法任選其一：
- `procdump -ma -e -x <dumpdir> QSanguosha.exe` —— 最直接
- 或者開 WER LocalDumps（`HKLM\SOFTWARE\Microsoft\Windows\Windows Error
  Reporting\LocalDumps`，`DumpType=2` 即 full dump）

用 WinDbg / cdb 開 dump，跑 `!analyze -v` 與 `k`。**我要見到的是：崩潰在
`Qt6Widgets!QGraphicsSceneBspTree::climbTree` 或者附近那類 scene traversal，
還是完全另一個位置。** 這一條就決定了你是否與 Linux 撞同一個 bug。

### 6.3 分類，不要混為一談
你講「更多未知閃退」—— 很可能不止一個 bug。**用 crash 的 faulting
module + top 3 frames 做 key 分組統計**，不要把所有 crash 當同一件事查。
Linux 側就曾經把兩個不同的 bug 歸做同一個「base defect」，繞了一大圈。

---

## 7. 第二階段：instrumentation（Windows 這裡比 Linux 強）

- **MSVC ASan**：`/fsanitize=address`（VS 2019 16.9+）。這是我們在 Linux
  沒有的東西 —— **Windows ASan 見到 Windows heap**，而我們在 Linux 的 ASan
  **完全看不到 Qt 內部的存取**（`libQt6*.so` 沒有 instrument，在 Qt 裡面發生的
  UAF 是完全靜默的），這一點卡死了整條路。你跑得通的話會快很多。
- **Application Verifier + page heap**：對「free 了還在用」的 case 是最直接的
  武器，開了之後 UAF 立刻變成當場 access violation，無需等到下一次 paint。
  **如果你只能做一件事，就做這一件。**
- **`NoIndex` A/B**：`src/ui/roomscene.cpp:656` 已經是 `NoIndex`。試著改回
  `BspTreeIndex` 再跑 —— 如果崩潰率飆升，就是你正撞上同一個 bug family，
  上面 §4 那張表就是你的工作清單；如果沒有分別，就是 Windows 那些屬於另一件事，
  照 §6.3 分類繼續進行。

---

## 8. 判定門檻（**不要跳過這節**）

這個 bug 是 **flaky** 的：同一個 seed 都不會出同一局（seed 只釘死 server 側發牌，
誰贏、client 見到哪些 askFor 每局都不同）。

- **基線最少跑 8 局**，不要用一兩局說「修好了」。
- Linux 側用的是 Fisher exact test（8/8 vs 0/8 → p≈7.8e-5）。你至少要做到
  同一量級的證據才算數。
- **量基線時一定要關掉你自己的 instrumentation。** 見 §9。

---

## 9. 兩個方法學陷阱（Linux 側踩過，直接送你）

1. **診斷工具會改變你想量的東西。**
   我寫過一個 per-paint scene audit，每 frame 呼叫一次 `items()`。沒想到
   `QGraphicsSceneBspTreeIndex::items()` 第一件事就是 `purgeRemovedItems()`
   —— 等於每 frame 幫它清一次殘留項目，把崩潰率由 **100% 壓到 ~7%**。
   前幾輪「統計不顯著」全部是因為這樣。
2. **ASan 的 0/8 不可以外推到產品 build。** 已經寫在 §3，但值得講兩次：
   ASan 不回收已 free 記憶體，時序完全不同，在 ASan 之下「修好」可以完全不代表
   產品 build 修好。**最終判定一定要在產品 build 上做。**

---

## 10. 已排除，不要再走一次

- **`873fa2c` 不是成因，是修正。** `873fa2c^` 的 `client.cpp:1085/1138` 用
  `args[i].value<JsonArray>().first()` 去讀 V2 具名 map → 空 list `.first()`
  → null deref（`SEGV @ 0x18`，`Client::loseCards`），第一次牌移動就死。
- `EffectAnimation::deleteEffect()` 是死 code（`loop_finished` 沒有 emitter）。
- 幾何 NaN／inf：逐 frame 掃過 `pos` / `boundingRect` / `sceneTransform`，沒有。
- Qt 版本混用 / private header：Linux 側 `ldd` 全部指向同一個 prefix。
  **（這一條 Windows 側未驗，見 §5.4 —— 你要自己確認。）**

---

## 11. 順帶一提：已知但未修的周邊問題

- `src/ui/heroskincontainer.cpp` 是死 code（不在任何 CMake source list，
  而且已經與自己的 header 對不上）。
- `./extensions/RAFTOM.lua` 的 `assert(io.open(GER, "r"))` 每局 throw 8 次，
  因為 `GER.lua` 只在 Windows deploy tree 出貨，不在 repo 裡面。
  **在你那邊這個 assert 應該不會 throw** —— 如果照樣 throw，值得查。

---

## 12. 交回來的格式

麻煩包含：

1. Qt 版本 + build 配置（MSVC 版本、RelWithDebInfo？ASan？AppVerifier？）
2. Crash 分類表：`faulting module + top 3 frames` → 出現次數
3. 至少一個 symbolised full stack
4. 崩潰率統計：`<配置> → 崩/總`，基線與修正後各一行
5. Patch（如果有），連同「這個 patch 在產品 build 跑了 N 局，崩 M 次」

Linux 側對應的記錄在 memory：`qgraphicsscene-paint-crash`、`asan-gui-build`、
`gui-client-reply-path-defects`、`linux-gui-network-smoke`。
