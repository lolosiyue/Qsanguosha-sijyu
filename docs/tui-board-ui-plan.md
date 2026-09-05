# TUI 牌桌模式（board UI）實作計劃

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 為 `qsanguosha_tui` 新增一套全螢幕 ASCII 牌桌呈現層（board），與現有滾動行模式（classic）並存，由啟動時決定，且不改變任何與 server 的交互。

**Architecture:** 在 `TuiApplicationController` 與輸出之間插入 `TuiPresenter` 介面，classic 與 board 各實作一次。board 側用 cell grid 幀緩衝 + 差分輸出，輸入側以 raw mode + 行編輯器餵回既有的 `lineReady(QString)` 出口，因此 parser、`ClientCore`、reply encoder 三層不知道 UI 模式存在。

**Tech Stack:** C++17、Qt 6 Core/Network（不得引入其他第三方庫）、POSIX `termios`/`TIOCGWINSZ`/`SIGWINCH`、Windows Console API、CMake、倉庫自製的 `check()` 測試框架。

**Spec:** [`docs/tui-board-ui.md`](tui-board-ui.md)（commit 7455806）

## Global Constraints

以下每一條都是 spec 的專案級要求，每個 task 的驗收都隱含包含本節。

- **依賴上限**：只可用 Qt Core、Qt Network、C++ 標準庫、POSIX/Win32 系統 API。禁止新增任何第三方庫。`cmake/VerifyTuiDependencies.cmake` 的禁用清單（`qt6gui qt6widgets qt6quick qt6qml qt6multimedia qt6opengl qt6websockets fmod recorder replayer`）必須維持通過。
- **不變式 1 — 視圖操作永不觸及 wire**：翻頁、overlay、捲動、resize、重繪不得產生、延遲或重排任何 protocol 訊息。
- **不變式 2 — `lineReady(QString)` 是唯一輸入出口**：兩套 UI 交出的 `InteractionResponse` 必須逐欄相同。
- **不變式 3 — classic 行為零改動**：除 Task 5 明列的 Linux Ctrl+C 修正外，classic 的輸出與現有實作逐位元組相同。
- **`--log-file` 內容與模式無關**：sanitize 與 `appendLogLine()` 留在 controller，presenter 只負責「寫去邊」。
- **退出碼沿用現有分配**，不新增：`0` 正常、`2` CLI 用法錯、`3` 連線、`4` Protocol、`5` 版本／signup 拒絕、`6` 本地 TUI runtime/input、`7` script。
- **尺寸下限 `60 × 18`**；玩家格固定 `20 × 3`。
- **字元集全程 Unicode**；色彩只承載已在文字表達過的資訊，`toPlainText()` 去色後必須可讀。
- **測試框架**：倉庫自製 `check(bool, const char *)` + `failures` 計數，每個 suite 一個 `runXxxTests(int, char **)` 進入點。禁用 QTest。
- **既有閘不得修改**：七個現有 `tests/tui/*-test.cpp`、`qsanguosha_tui_contract`、`qsanguosha_tui_live_tcp`、dumpbin 依賴閘、`deploy-tui` package smoke、`tools/autotest/tui_network_smoke.py`。若需改它們才過，即抽象有漏，返回修正設計。

### 本機建置與執行命令

```bash
# 建置測試執行檔
cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8

# 跑單一 suite
./build-linux-gcc/tests/qsanguosha_tui_tests --suite <suite-name>

# 建置 TUI 本體（產物落 ./debug/qsanguosha_tui）
cmake --build build-linux-gcc --target qsanguosha_tui -j8

# 跑全部 TUI ctest
ctest --test-dir build-linux-gcc -L tui --output-on-failure
```

### 新增一個測試 suite 的四處登記

每個新測試檔都要改四個地方，缺一不可：

1. `tests/CMakeLists.txt` 的 `add_executable(qsanguosha_tui_tests ...)` 加入來源檔。
2. `tests/CMakeLists.txt` 加 `set_source_files_properties(tui/<name>-test.cpp PROPERTIES COMPILE_DEFINITIONS main=run<Name>Tests)`。
3. `tests/CMakeLists.txt` 加 `qsan_add_ctest(qsanguosha_<name> qsanguosha_tui_tests SUITE <suite> LABELS "client;tui;fast" TIMEOUT 120)`。
4. `tests/tui-tests-main.cpp` 加前置宣告、`if (suite == ...)` 分派、`runIsolatedTestCases` 條目。

新增的 `src/tui/*.cpp` 另需加入 `CMakeLists.txt:628` 的 `add_library(qsanguosha_tui_support STATIC ...)` 來源清單（`.cpp` 與 `.h` 都要列，倉庫慣例）。

---

### Task 1: TuiPresenter 抽象與 TuiStreamPresenter

把 controller 的輸出動作抽成介面，classic 成為它的第一個實作。此 task 唯一的驗收標準是**輸出逐位元組不變**。

**Files:**
- Create: `src/tui/tui-presenter.h`
- Create: `src/tui/tui-stream-presenter.h`, `src/tui/tui-stream-presenter.cpp`
- Modify: `src/tui/tui-application-controller.h`（加 `#include <memory>` 與 `m_presenter` 成員）
- Modify: `src/tui/tui-application-controller.cpp:896-909`（`writeOutput()` / `writeError()` 主體）
- Modify: `CMakeLists.txt:628`（`qsanguosha_tui_support` 來源）
- Modify: `tests/CMakeLists.txt:327`（測試來源與登記）
- Modify: `tests/tui-tests-main.cpp`
- Test: `tests/tui/tui-presenter-test.cpp`

**Interfaces:**
- Consumes: `TuiRenderer::sanitize(const QString &, qsizetype)`（既有）。
- Produces:
  - `class TuiPresenter`，純虛：`void writeOutput(const QString &)`、`void writeError(const QString &)`、`void shutdown()`。Task 9 會在此介面上追加成員。
  - `class TuiStreamPresenter final : public TuiPresenter`，建構子 `explicit TuiStreamPresenter(Sink out = {}, Sink err = {})`，其中 `using Sink = std::function<void(const QString &)>`；空 sink 表示寫 stdout/stderr。

- [ ] **Step 1: 寫失敗測試**

建立 `tests/tui/tui-presenter-test.cpp`：

```cpp
#include "tui-stream-presenter.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        std::printf("[FAIL] %s\n", what);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    QStringList out;
    QStringList err;
    TuiStreamPresenter presenter([&out](const QString &text) { out << text; },
                                 [&err](const QString &text) { err << text; });

    presenter.writeOutput(QStringLiteral("hello"));
    check(out.size() == 1 && out.at(0) == QStringLiteral("hello\n"),
          "stream presenter writes the text with a trailing newline and nothing else");
    check(err.isEmpty(), "output does not leak into the error sink");

    presenter.writeError(QStringLiteral("bad"));
    check(err.size() == 1 && err.at(0) == QStringLiteral("TUI_ERROR bad\n"),
          "errors keep the TUI_ERROR prefix the automation greps for");

    // The presenter must not sanitize: the controller already did, and doing it
    // twice would let a board presenter and a stream presenter disagree about
    // what reached --log-file.
    presenter.writeOutput(QStringLiteral("a\x1b[31mb"));
    check(out.size() == 2 && out.at(1) == QStringLiteral("a\x1b[31mb\n"),
          "the presenter passes text through untouched");

    presenter.shutdown();
    check(out.size() == 2 && err.size() == 1,
          "shutdown on the stream presenter writes nothing");

    std::printf("[AUTOTEST] TUI_PRESENTER_RESULT status=%s\n",
        failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: 登記測試 suite（四處）**

`tests/CMakeLists.txt`：在 `add_executable(qsanguosha_tui_tests` 的來源列加 `tui/tui-presenter-test.cpp`，並加：

```cmake
    set_source_files_properties(tui/tui-presenter-test.cpp
        PROPERTIES COMPILE_DEFINITIONS main=runTuiPresenterTests)
```

```cmake
    qsan_add_ctest(qsanguosha_tui_presenter qsanguosha_tui_tests
        SUITE presenter
        LABELS "client;tui;fast"
        TIMEOUT 120
    )
```

`tests/tui-tests-main.cpp`：加 `int runTuiPresenterTests(int argc, char **argv);`、
`if (suite == QLatin1String("presenter")) return runTuiPresenterTests(argc, argv);`，
以及 `runIsolatedTestCases` 內 `{QStringLiteral("presenter"), {QStringLiteral("--suite"), QStringLiteral("presenter")}},`。

- [ ] **Step 3: 執行測試,確認編譯失敗**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8`
Expected: FAIL，`fatal error: tui-stream-presenter.h: No such file or directory`

- [ ] **Step 4: 寫介面與實作**

`src/tui/tui-presenter.h`：

```cpp
#ifndef TUI_PRESENTER_H
#define TUI_PRESENTER_H

#include <QString>

#include <functional>

// Where the controller's output goes. The controller sanitizes and writes the
// log file itself, so --log-file content stays identical no matter which
// presenter is installed; a presenter only decides where text lands on screen.
class TuiPresenter
{
public:
    using Sink = std::function<void(const QString &)>;

    virtual ~TuiPresenter() = default;
    virtual void writeOutput(const QString &text) = 0;
    virtual void writeError(const QString &text) = 0;
    // Called once before the process exits, on every path that unwinds. A
    // presenter that took the terminal over gives it back here.
    virtual void shutdown() = 0;
};

#endif
```

`src/tui/tui-stream-presenter.h`：

```cpp
#ifndef TUI_STREAM_PRESENTER_H
#define TUI_STREAM_PRESENTER_H

#include "tui-presenter.h"

// The line-oriented client the TUI has always been: one line per write, straight
// to stdout. Kept as a peer of the board presenter rather than a fallback --
// scripts, redirected output and CI all run through it.
class TuiStreamPresenter final : public TuiPresenter
{
public:
    // Empty sinks mean stdout and stderr; tests inject their own.
    explicit TuiStreamPresenter(Sink out = {}, Sink err = {});

    void writeOutput(const QString &text) override;
    void writeError(const QString &text) override;
    void shutdown() override;

private:
    Sink m_out;
    Sink m_err;
};

#endif
```

`src/tui/tui-stream-presenter.cpp`：

```cpp
#include "tui-stream-presenter.h"

#include <QTextStream>

TuiStreamPresenter::TuiStreamPresenter(Sink out, Sink err)
    : m_out(std::move(out)), m_err(std::move(err))
{
}

void TuiStreamPresenter::writeOutput(const QString &text)
{
    const QString line = text + QLatin1Char('\n');
    if (m_out) {
        m_out(line);
        return;
    }
    QTextStream(stdout) << line << Qt::flush;
}

void TuiStreamPresenter::writeError(const QString &text)
{
    const QString line = QStringLiteral("TUI_ERROR ") + text + QLatin1Char('\n');
    if (m_err) {
        m_err(line);
        return;
    }
    QTextStream(stderr) << line << Qt::flush;
}

void TuiStreamPresenter::shutdown()
{
    // Nothing to give back: this presenter never took the terminal over.
}
```

`CMakeLists.txt:628` 的來源清單加入（依字母序插入）：

```cmake
        src/tui/tui-presenter.h
        src/tui/tui-stream-presenter.cpp
        src/tui/tui-stream-presenter.h
```

- [ ] **Step 5: 執行測試,確認通過**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8 && ./build-linux-gcc/tests/qsanguosha_tui_tests --suite presenter`
Expected: PASS，輸出 `[AUTOTEST] TUI_PRESENTER_RESULT status=PASS`

- [ ] **Step 6: 把 controller 接上 presenter**

`src/tui/tui-application-controller.h`：加 `#include "tui-presenter.h"` 與 `#include <memory>`，在私有成員區 `QFile m_log;` 之前加：

```cpp
    // Installed once at construction and never swapped: switching UI mode
    // mid-session is deliberately out of scope.
    std::unique_ptr<TuiPresenter> m_presenter;
```

`src/tui/tui-application-controller.cpp`：在建構子初始化列 `m_input(this)` 之後加 `, m_presenter(std::make_unique<TuiStreamPresenter>())`，並加 `#include "tui-stream-presenter.h"`。改寫兩個主體：

```cpp
void TuiApplicationController::writeOutput(const QString &text)
{
    const QString safe = TuiRenderer::sanitize(text, 16384);
    m_presenter->writeOutput(safe);
    appendLogLine(safe);
}

void TuiApplicationController::writeError(const QString &text)
{
    const QString safe = TuiRenderer::sanitize(text, 4096);
    m_presenter->writeError(safe);
    appendLogLine(QStringLiteral("TUI_ERROR %1").arg(safe));
}
```

- [ ] **Step 7: 證明 classic 輸出逐位元組不變**

Run:
```bash
cmake --build build-linux-gcc --target qsanguosha_tui -j8
./debug/qsanguosha_tui --help > /tmp/after-help.txt 2>&1
git stash && cmake --build build-linux-gcc --target qsanguosha_tui -j8 \
  && ./debug/qsanguosha_tui --help > /tmp/before-help.txt 2>&1 && git stash pop
diff /tmp/before-help.txt /tmp/after-help.txt
```
Expected: `diff` 無輸出。

- [ ] **Step 8: 跑全部既有 TUI 測試**

Run: `ctest --test-dir build-linux-gcc -L tui --output-on-failure`
Expected: 全部 PASS，包含七個既有 suite 與新的 `qsanguosha_tui_presenter`。

- [ ] **Step 9: Commit**

```bash
git add src/tui/tui-presenter.h src/tui/tui-stream-presenter.h \
        src/tui/tui-stream-presenter.cpp src/tui/tui-application-controller.h \
        src/tui/tui-application-controller.cpp CMakeLists.txt \
        tests/CMakeLists.txt tests/tui-tests-main.cpp tests/tui/tui-presenter-test.cpp
git commit -m "refactor(tui): route controller output through a presenter interface"
```

---

### Task 2: 抽出 TuiResolvers

`TuiRenderer::Resolvers` 是 controller 對 engine 的九個查詢。board 需要同一組查詢，但不需要 `TuiRenderer` 的排版字串，所以把它移到獨立標頭。這是純搬移，行為零改動。

**Files:**
- Create: `src/tui/tui-resolvers.h`
- Modify: `src/tui/tui-renderer.h:16-70`（刪去 struct 定義，改為 include + 型別別名）
- Modify: `CMakeLists.txt:628`
- Test: 既有七個 suite 即為此 task 的測試（純搬移不引入新行為，不新增測試檔）

**Interfaces:**
- Consumes: 無。
- Produces: `struct TuiResolvers`，欄位 `card`、`name`、`player`、`kingdom`、`cardHint`、`playerHint`、`cardTargets`、`handHint`、`skillHint`，以及 `struct TuiCardTargets`（原 `TuiRenderer::CardTargets`，欄位 `known`、`targetFixed`、`targets`、`maxVotes`）。Task 8 的 `TuiBoardView` 消費它。

- [ ] **Step 1: 建立新標頭**

`src/tui/tui-resolvers.h`——把 `src/tui/tui-renderer.h` 第 16 至 70 行的 `using` 宣告、`struct CardTargets` 與 `struct Resolvers` 原文搬入，型別更名為 `TuiCardTargets` 與 `TuiResolvers`，註解一併搬走：

```cpp
#ifndef TUI_RESOLVERS_H
#define TUI_RESOLVERS_H

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

// Which players the engine would let a card be aimed at, asked before the
// player has picked anything -- Card::targetFilter() with an empty selection,
// which is how the desktop lights up its photos. Only the first target is
// knowable this way: on a multi-target card the rest depend on what came
// before, so the menu shows the opening move and the full list is checked when
// the answer is submitted.
struct TuiCardTargets
{
    bool known = false;
    bool targetFixed = false;
    QStringList targets;
    // How many times each of them may be named. Above one is the answer
    // Collateral and GreatYeyanCard give.
    QHash<QString, int> maxVotes;
};

// Everything a presenter needs the engine for. Each one is optional; an absent
// resolver means the raw value is shown. Both UI modes share one set: they ask
// the engine the same questions and differ only in how they draw the answers.
struct TuiResolvers
{
    std::function<QString(int)> card;
    std::function<QString(const QString &)> name;
    std::function<QString(const QString &)> player;
    std::function<QString(const QString &)> kingdom;
    std::function<QString(int)> cardHint;
    std::function<QString(const QString &)> playerHint;
    std::function<TuiCardTargets(int)> cardTargets;
    std::function<QString(int)> handHint;
    std::function<QString(const QString &, int)> skillHint;
};

#endif
```

- [ ] **Step 2: 讓 TuiRenderer 用別名,保住所有既有呼叫點**

`src/tui/tui-renderer.h`：刪去第 16-70 行的 struct 定義，改為：

```cpp
#include "tui-resolvers.h"

class ClientGameState;
struct InteractionRequest;

class TuiRenderer
{
public:
    // The renderer used to own these; they moved out so the board presenter can
    // ask the engine the same questions without pulling in line formatting.
    // The aliases keep every existing TuiRenderer::Resolvers reference valid.
    using CardTargets = TuiCardTargets;
    using Resolvers = TuiResolvers;
    using CardResolver = std::function<QString(int)>;
    using NameResolver = std::function<QString(const QString &)>;
    using PlayerResolver = std::function<QString(const QString &)>;
    using KingdomResolver = std::function<QString(const QString &)>;
    using CardHintResolver = std::function<QString(int)>;
    using PlayerHintResolver = std::function<QString(const QString &)>;
    using SkillHintResolver = std::function<QString(const QString &, int)>;
    using CardTargetResolver = std::function<TuiCardTargets(int)>;
```

其餘成員（`explicit TuiRenderer(...)` 起）一行不動。

- [ ] **Step 3: 加入建置**

`CMakeLists.txt:628` 加 `src/tui/tui-resolvers.h`（依字母序）。

- [ ] **Step 4: 編譯並跑全部 TUI 測試**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests qsanguosha_tui -j8 && ctest --test-dir build-linux-gcc -L tui --output-on-failure`
Expected: 全部 PASS。零來源檔需要因更名而修改——若有任何 `.cpp` 編譯失敗，代表別名漏了一個，補上別名而不是改呼叫點。

- [ ] **Step 5: Commit**

```bash
git add src/tui/tui-resolvers.h src/tui/tui-renderer.h CMakeLists.txt
git commit -m "refactor(tui): lift the engine resolvers out of the renderer"
```

---

### Task 3: 顯示寬度（tui-text-width）

終端按顯示格數排版，中文字佔兩格。沒有這一層，任何對齊都是錯的。

**Files:**
- Create: `src/tui/tui-text-width.h`, `src/tui/tui-text-width.cpp`
- Modify: `CMakeLists.txt:628`, `tests/CMakeLists.txt`, `tests/tui-tests-main.cpp`
- Test: `tests/tui/tui-text-width-test.cpp`

**Interfaces:**
- Consumes: 無。
- Produces:
  - `int tuiDisplayWidth(const QString &text)` — 整串的顯示格數。
  - `int tuiCharWidth(char32_t code)` — 單一碼位：組合字元 0，East Asian Wide/Fullwidth 2，其餘 1。
  - `QString tuiElide(const QString &text, int maxWidth)` — 裁到 `maxWidth` 格以內，超出時尾端加 `…`（本身佔 1 格），且**永不從中間劈開一個全形字**。
  - `QString tuiPadTo(const QString &text, int width)` — 右補空格到剛好 `width` 格；已超出則先 elide。

- [ ] **Step 1: 寫失敗測試**

建立 `tests/tui/tui-text-width-test.cpp`：

```cpp
#include "tui-text-width.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        std::printf("[FAIL] %s\n", what);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    check(tuiDisplayWidth(QStringLiteral("abc")) == 3, "ASCII is one column each");
    check(tuiDisplayWidth(QString::fromUtf8("曹操")) == 4, "Han characters are two columns each");
    check(tuiDisplayWidth(QString::fromUtf8("时语sgs1")) == 8, "mixed text adds up");
    check(tuiDisplayWidth(QString::fromUtf8("【杀】")) == 6,
          "CJK brackets are full width, which is why they must not be counted as one");
    check(tuiDisplayWidth(QString::fromUtf8("♥♡")) == 2,
          "the heart glyphs the board draws hp with are narrow");
    check(tuiDisplayWidth(QString::fromUtf8("─│┌┤")) == 4, "box drawing is narrow");

    // A surrogate pair is one code point, not two.
    check(tuiDisplayWidth(QString::fromUtf8("\xF0\x9F\x80\x84")) == 2,
          "a non-BMP code point counts once");

    // The rule that keeps the grid from tearing.
    const QString name = QString::fromUtf8("张飞张飞");
    check(tuiDisplayWidth(tuiElide(name, 5)) <= 5, "elide never exceeds the budget");
    check(tuiElide(name, 5) == QString::fromUtf8("张飞…"),
          "elide drops a whole wide character rather than splitting one");
    check(tuiElide(name, 8) == name, "text that fits is returned untouched");
    check(tuiElide(name, 1) == QString::fromUtf8("…"), "a one column budget still yields the mark");
    check(tuiElide(name, 0).isEmpty(), "a zero budget yields nothing");

    check(tuiDisplayWidth(tuiPadTo(QString::fromUtf8("曹操"), 7)) == 7,
          "padding counts columns, not characters");
    check(tuiPadTo(QString::fromUtf8("曹操"), 7) == QString::fromUtf8("曹操   "),
          "padding is trailing spaces");
    check(tuiDisplayWidth(tuiPadTo(name, 5)) == 5, "padding an over-long string elides it first");

    std::printf("[AUTOTEST] TUI_TEXT_WIDTH_RESULT status=%s\n",
        failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: 登記 suite（四處,同 Task 1 Step 2 的做法）**

`tests/CMakeLists.txt` 加來源 `tui/tui-text-width-test.cpp`、
`set_source_files_properties(tui/tui-text-width-test.cpp PROPERTIES COMPILE_DEFINITIONS main=runTuiTextWidthTests)`、
`qsan_add_ctest(qsanguosha_tui_text_width qsanguosha_tui_tests SUITE text-width LABELS "client;tui;fast" TIMEOUT 120)`。
`tests/tui-tests-main.cpp` 加宣告、分派與 `runIsolatedTestCases` 條目。

- [ ] **Step 3: 執行測試,確認失敗**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8`
Expected: FAIL，`fatal error: tui-text-width.h: No such file or directory`

- [ ] **Step 4: 實作**

`src/tui/tui-text-width.h`：

```cpp
#ifndef TUI_TEXT_WIDTH_H
#define TUI_TEXT_WIDTH_H

#include <QString>

// How many terminal columns a code point occupies: 0 for combining marks, 2 for
// East Asian Wide and Fullwidth, 1 otherwise. Every grid calculation in the
// board UI goes through here -- counting QString::size() instead is what makes
// a table of Chinese names tear.
int tuiCharWidth(char32_t code);
int tuiDisplayWidth(const QString &text);
// Cuts to maxWidth columns, appending U+2026 when anything was dropped. Never
// splits a wide character: dropping a whole one is correct, half of one is a
// torn cell.
QString tuiElide(const QString &text, int maxWidth);
// Exactly width columns: elided when too long, space padded when too short.
QString tuiPadTo(const QString &text, int width);

#endif
```

`src/tui/tui-text-width.cpp`：

```cpp
#include "tui-text-width.h"

#include <iterator>

namespace {

struct Range
{
    char32_t first;
    char32_t last;
};

// Unicode 15 East Asian Wide and Fullwidth, trimmed to the blocks this client
// can actually print: Han, kana, Hangul, CJK punctuation and the fullwidth
// forms. Widening more than this would be wrong for the box drawing and card
// suit glyphs the board draws with.
constexpr Range wideRanges[] = {
    {0x1100, 0x115F}, {0x2E80, 0x303E}, {0x3041, 0x33FF},
    {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xA000, 0xA4CF},
    {0xAC00, 0xD7A3}, {0xF900, 0xFAFF}, {0xFE10, 0xFE19},
    {0xFE30, 0xFE6F}, {0xFF00, 0xFF60}, {0xFFE0, 0xFFE6},
    {0x1F300, 0x1F64F}, {0x1F900, 0x1F9FF}, {0x20000, 0x3FFFD},
};

// Combining marks and other zero-width code points.
constexpr Range zeroRanges[] = {
    {0x0300, 0x036F}, {0x200B, 0x200F}, {0xFE00, 0xFE0F},
    {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF},
};

bool inRanges(char32_t code, const Range *ranges, int count)
{
    for (int i = 0; i < count; ++i) {
        if (code >= ranges[i].first && code <= ranges[i].last)
            return true;
    }
    return false;
}

} // namespace

int tuiCharWidth(char32_t code)
{
    if (code == 0)
        return 0;
    if (inRanges(code, zeroRanges, int(std::size(zeroRanges))))
        return 0;
    if (inRanges(code, wideRanges, int(std::size(wideRanges))))
        return 2;
    return 1;
}

int tuiDisplayWidth(const QString &text)
{
    int width = 0;
    for (auto it = text.begin(); it != text.end(); ++it) {
        char32_t code = it->unicode();
        if (it->isHighSurrogate() && (it + 1) != text.end() && (it + 1)->isLowSurrogate()) {
            code = QChar::surrogateToUcs4(*it, *(it + 1));
            ++it;
        }
        width += tuiCharWidth(code);
    }
    return width;
}
```

```cpp
QString tuiElide(const QString &text, int maxWidth)
{
    if (maxWidth <= 0)
        return QString();
    if (tuiDisplayWidth(text) <= maxWidth)
        return text;
    if (maxWidth == 1)
        return QString(QChar(0x2026));

    // One column is reserved for the ellipsis. Stopping before a wide character
    // that would not fit whole is the point: half a glyph tears the cell.
    const int budget = maxWidth - 1;
    QString result;
    int width = 0;
    for (auto it = text.begin(); it != text.end(); ++it) {
        QString unit(*it);
        char32_t code = it->unicode();
        if (it->isHighSurrogate() && (it + 1) != text.end() && (it + 1)->isLowSurrogate()) {
            code = QChar::surrogateToUcs4(*it, *(it + 1));
            ++it;
            unit.append(*it);
        }
        const int next = tuiCharWidth(code);
        if (width + next > budget)
            break;
        result.append(unit);
        width += next;
    }
    return result + QChar(0x2026);
}

QString tuiPadTo(const QString &text, int width)
{
    const QString fitted = tuiElide(text, width);
    const int pad = width - tuiDisplayWidth(fitted);
    return pad > 0 ? fitted + QString(pad, QLatin1Char(' ')) : fitted;
}
```

- [ ] **Step 5: 執行測試,確認通過**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8 && ./build-linux-gcc/tests/qsanguosha_tui_tests --suite text-width`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/tui/tui-text-width.h src/tui/tui-text-width.cpp CMakeLists.txt \
        tests/CMakeLists.txt tests/tui-tests-main.cpp tests/tui/tui-text-width-test.cpp
git commit -m "feat(tui): measure text in terminal columns, not characters"
```

---

### Task 4: 幀緩衝（TuiScreen）

board 不直接寫 stdout：所有繪製先落 cell grid，再與上一幀比對，只送出變更。這既消除閃爍，也讓整幅畫面可以 dump 成純文字做 golden 比對。

**Files:**
- Create: `src/tui/tui-screen.h`, `src/tui/tui-screen.cpp`
- Modify: `CMakeLists.txt:628`, `tests/CMakeLists.txt`, `tests/tui-tests-main.cpp`
- Test: `tests/tui/tui-screen-test.cpp`

**Interfaces:**
- Consumes: `tuiDisplayWidth`、`tuiElide`（Task 3）。
- Produces:
  - `struct TuiRect { int row = 0; int col = 0; int rows = 0; int cols = 0; };`
  - `enum class TuiAttr { Normal, Dim, Bold, Kingdom, Current, Danger, Dead };`
  - `class TuiScreen`：
    - `void resize(int rows, int cols)` — 重建並標記下一次為全量輸出。
    - `void clear()`
    - `void putText(int row, int col, const QString &text, TuiAttr attr = TuiAttr::Normal, int maxWidth = -1)`
    - `void drawBox(const TuiRect &rect, const QString &title = {})`
    - `QString flush()` — 回傳把上一幀變成當前幀所需的最小 ANSI；無變更時回傳空字串。呼叫後當前幀成為上一幀。
    - `QString toPlainText() const` — 去色純文字，逐行右端不留尾隨空白。
    - `int rows() const`、`int cols() const`

- [ ] **Step 1: 寫失敗測試**

建立 `tests/tui/tui-screen-test.cpp`：

```cpp
#include "tui-screen.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        std::printf("[FAIL] %s\n", what);
    }
}

QString lineAt(const TuiScreen &screen, int row)
{
    return screen.toPlainText().split(QLatin1Char('\n')).value(row);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    TuiScreen screen;
    screen.resize(5, 20);
    check(screen.rows() == 5 && screen.cols() == 20, "the screen takes the size it was given");

    screen.putText(1, 2, QString::fromUtf8("曹操"));
    check(lineAt(screen, 1) == QString::fromUtf8("  曹操"),
          "a wide string starts where it was put and occupies four columns");

    // Clipping, not wrapping: a torn row is worse than a cut word.
    screen.putText(2, 18, QString::fromUtf8("张飞"));
    check(tuiDisplayWidth(lineAt(screen, 2)) <= 20,
          "text that would run past the right edge is clipped to the screen");

    screen.putText(3, 0, QString::fromUtf8("时语时语时语"), TuiAttr::Normal, 5);
    check(lineAt(screen, 3) == QString::fromUtf8("时语…"),
          "maxWidth elides through the same rule tuiElide uses");

    screen.drawBox(TuiRect{0, 0, 5, 20}, QString::fromUtf8("房间"));
    check(lineAt(screen, 0).startsWith(QString::fromUtf8("┌")),
          "a box draws its own corners");
    check(lineAt(screen, 0).contains(QString::fromUtf8("房间")),
          "a box carries its title on the top edge");
    check(lineAt(screen, 4).startsWith(QString::fromUtf8("└")), "and closes at the bottom");

    // The property that makes the board usable over a slow link.
    const QString first = screen.flush();
    check(!first.isEmpty(), "the first flush after a resize emits the whole screen");
    const QString second = screen.flush();
    check(second.isEmpty(), "flushing an unchanged screen emits nothing at all");

    screen.putText(1, 2, QString::fromUtf8("孙权"));
    const QString third = screen.flush();
    check(!third.isEmpty() && third.length() < first.length() / 2,
          "changing one cell emits far less than a full repaint");
    check(!third.contains(QString::fromUtf8("房间")),
          "an unchanged box title is not re-sent");

    screen.clear();
    check(lineAt(screen, 1).isEmpty(), "clear empties every cell");

    std::printf("[AUTOTEST] TUI_SCREEN_RESULT status=%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: 登記 suite（四處）**

來源 `tui/tui-screen-test.cpp`、`COMPILE_DEFINITIONS main=runTuiScreenTests`、
`qsan_add_ctest(qsanguosha_tui_screen qsanguosha_tui_tests SUITE screen LABELS "client;tui;fast" TIMEOUT 120)`，
以及 `tests/tui-tests-main.cpp` 三處。

- [ ] **Step 3: 執行測試,確認失敗**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8`
Expected: FAIL，`fatal error: tui-screen.h: No such file or directory`

- [ ] **Step 4: 實作**

`src/tui/tui-screen.h`：

```cpp
#ifndef TUI_SCREEN_H
#define TUI_SCREEN_H

#include "tui-text-width.h"

#include <QString>
#include <QVector>

struct TuiRect
{
    int row = 0;
    int col = 0;
    int rows = 0;
    int cols = 0;
};

// Colour carries meaning that the text already carries; it never carries meaning
// alone. toPlainText() drops it, and that is what golden tests compare.
enum class TuiAttr
{
    Normal,
    Dim,
    Bold,
    Kingdom,
    Current,
    Danger,
    Dead,
};

// A character grid the board draws into. Nothing reaches stdout until flush(),
// which emits only the cells that changed since the previous frame.
class TuiScreen
{
public:
    void resize(int rows, int cols);
    void clear();
    void putText(int row, int col, const QString &text,
                 TuiAttr attr = TuiAttr::Normal, int maxWidth = -1);
    void drawBox(const TuiRect &rect, const QString &title = {});
    QString flush();
    QString toPlainText() const;
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }

private:
    struct Cell
    {
        QChar glyph = QLatin1Char(' ');
        TuiAttr attr = TuiAttr::Normal;
        // The trailing half of a wide glyph. Nothing may be written here on its
        // own; overwriting it blanks the head cell too.
        bool continuation = false;
        bool operator==(const Cell &other) const;
    };

    int index(int row, int col) const { return row * m_cols + col; }
    void writeCell(int row, int col, QChar glyph, TuiAttr attr, bool continuation);

    int m_rows = 0;
    int m_cols = 0;
    QVector<Cell> m_current;
    QVector<Cell> m_previous;
    bool m_fullRepaint = true;
};

#endif
```

實作要點，逐條都由上面的測試釘住：

- `resize()` 重建兩個 vector 並設 `m_fullRepaint = true`。
- `putText()` 先 `tuiElide(text, maxWidth)`（`maxWidth < 0` 時用 `m_cols - col`），再逐碼位寫入；寬字元寫兩格，第二格 `continuation = true`。寫到 `col >= m_cols` 即停（裁切，不換行）。覆寫一個 `continuation` 格時，把它前一格一併填成空白，否則會剩下半個字。
- `drawBox()` 用 `┌ ┐ └ ┘ ─ │`；有 title 時在頂邊 `col + 2` 處寫 ` <title> `，並以 `tuiElide` 夾在 `rect.cols - 4` 格內。
- `flush()`：`m_fullRepaint` 為真時輸出 `ESC[H` 後整幅；否則逐行找出連續變更區段，每段一個 `ESC[<row>;<col>H` 加內容。輸出後 `m_previous = m_current`、`m_fullRepaint = false`。屬性變更時插入對應 SGR，段末補 `ESC[0m`。
- `toPlainText()` 跳過 `continuation` 格（寬字元已在頭格輸出完整字元），並 `QString::trimmed()` 掉每行右端空白。

- [ ] **Step 5: 執行測試,確認通過**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8 && ./build-linux-gcc/tests/qsanguosha_tui_tests --suite screen`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/tui/tui-screen.h src/tui/tui-screen.cpp CMakeLists.txt \
        tests/CMakeLists.txt tests/tui-tests-main.cpp tests/tui/tui-screen-test.cpp
git commit -m "feat(tui): add a diffing character grid for the board UI"
```

---

### Task 5: 終端狀態與 Ctrl+C 修正

進 raw mode 與 alternate screen，並保證每一條退出路徑都還原。順帶補上 Linux 上完全缺失的 SIGINT 處理——這是本計劃唯一刻意改變 classic 行為的地方。

**Files:**
- Create: `src/tui/tui-terminal.h`, `src/tui/tui-terminal.cpp`
- Modify: `src/tui/tui-input.cpp`（Unix 側接上 SIGINT → `interruptRequested`）
- Modify: `CMakeLists.txt:628`, `tests/CMakeLists.txt`, `tests/tui-tests-main.cpp`
- Test: `tests/tui/tui-terminal-test.cpp`

**Interfaces:**
- Consumes: 無。
- Produces:
  - `class TuiTerminal : public QObject`
    - `bool enter(QString *error)` — 存 termios、關 `ICANON|ECHO`（**保留 `ISIG`**）、`ESC[?1049h`、隱藏游標、裝 signal handler。失敗回 false 並填 `error`。
    - `void leave()` — 還原；可重入，第二次是 no-op。
    - `QSize size() const` — 目前 `rows × cols`；查不到時回 `QSize(24, 80)`。
    - `static QByteArray restoreSequence()` — 還原用的 ANSI 位元組串（`ESC[?1049l` + 顯示游標 + `ESC[0m`），signal handler 也用同一份。
    - signal `void resized()`、signal `void interrupted()`
  - 自由函式 `void tuiInstallInterruptHandler(std::function<void()> callback)` — 兩個平台共用的 SIGINT 出口，classic 亦使用。

- [ ] **Step 1: 寫失敗測試**

單元測試只覆蓋可在無 pty 下驗證的部分：還原序列的組成、size 的退回值、`leave()` 的重入。真正的 raw mode 與 SIGWINCH 由 Task 12 的 pty smoke 覆蓋——這一點在測試檔頂端註明，避免日後有人以為這裡已經測夠。

建立 `tests/tui/tui-terminal-test.cpp`：

```cpp
// Only the parts that are verifiable without a pty live here. Raw mode,
// SIGWINCH and alternate screen restoration need a real terminal and are
// covered by tools/autotest/tui_board_smoke.py, which is a local gate.
#include "tui-terminal.h"

#include <QCoreApplication>
#include <QSize>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        std::printf("[FAIL] %s\n", what);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    const QByteArray restore = TuiTerminal::restoreSequence();
    check(restore.contains("\x1b[?1049l"), "restoring leaves the alternate screen");
    check(restore.contains("\x1b[?25h"), "restoring shows the cursor again");
    check(restore.contains("\x1b[0m"), "restoring drops any attribute still in force");
    check(!restore.isEmpty() && restore.size() < 64,
          "the sequence stays small enough to write from a signal handler");

    TuiTerminal terminal;
    // Tests run with stdout redirected, so entering must fail rather than
    // wedge the harness -- and must say why.
    QString error;
    const bool entered = terminal.enter(&error);
    check(!entered, "entering without a terminal fails");
    check(!error.isEmpty(), "and reports a reason a player can act on");

    const QSize fallback = terminal.size();
    check(fallback.height() == 24 && fallback.width() == 80,
          "an unknown size falls back to 80x24 rather than zero");

    terminal.leave();
    terminal.leave();
    check(true, "leaving twice is a no-op and does not crash");

    std::printf("[AUTOTEST] TUI_TERMINAL_RESULT status=%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: 登記 suite（四處）**

來源 `tui/tui-terminal-test.cpp`、`COMPILE_DEFINITIONS main=runTuiTerminalTests`、
`qsan_add_ctest(qsanguosha_tui_terminal qsanguosha_tui_tests SUITE terminal LABELS "client;tui;fast" TIMEOUT 120)`，
以及 `tests/tui-tests-main.cpp` 三處。

- [ ] **Step 3: 執行測試,確認失敗**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8`
Expected: FAIL，`fatal error: tui-terminal.h: No such file or directory`

- [ ] **Step 4: 實作 TuiTerminal**

`enter()` 依序：`isatty(STDOUT_FILENO)` 與 `isatty(STDIN_FILENO)` 皆須為真，否則填 `error` 回 false；`tcgetattr` 存起原始 termios；複製一份清掉 `ICANON | ECHO`，**保留 `ISIG`**，`VMIN = 1`、`VTIME = 0`，`tcsetattr(TCSAFLUSH)`；寫 `ESC[?1049h` 與 `ESC[?25l`；裝 `SIGWINCH`、`SIGINT`、`SIGTERM`、`SIGHUP`、`SIGSEGV`、`SIGABRT` handler。

handler 只做 async-signal-safe 的事：

```cpp
extern "C" void tuiSignalHandler(int number)
{
    // Written from a signal handler: no Qt, no allocation, no QString.
    if (g_restoreLength > 0)
        ::write(STDOUT_FILENO, g_restore, size_t(g_restoreLength));
    if (number == SIGSEGV || number == SIGABRT) {
        // Do not try to continue. The terminal is back; let the crash be a crash.
        ::signal(number, SIG_DFL);
        ::raise(number);
        return;
    }
    const char token = number == SIGWINCH ? 'w' : 'i';
    ::write(g_wakePipe[1], &token, 1);
}
```

`g_wakePipe` 為 `enter()` 建立的 self-pipe，讀端交給 `QSocketNotifier`，收到 `'w'` 發 `resized()`、`'i'` 發 `interrupted()`。所有 Qt 動作只在 notifier 的 slot 內發生，不在 handler 內。

`leave()` 用 `std::atomic<bool>` 守住重入；還原 termios、寫 `restoreSequence()`、把 signal 設回 `SIG_DFL`、關 self-pipe。解構函式呼叫 `leave()`；另在 `enter()` 成功後把 `leave()` 接上 `QCoreApplication::aboutToQuit`。

`size()` 用 `ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)`；失敗或回傳 0 時用 `QSize(80, 24)`（`QSize(width, height)` = `(cols, rows)`，注意測試斷言的是 `height()==24`）。

- [ ] **Step 5: 補上 Unix 的 SIGINT 出口**

`src/tui/tui-input.cpp` 的 Unix 分支目前沒有任何 SIGINT 處理，`interruptRequested` 只由 Windows console 分支的第 155 行發出。在 `TuiInput::start()` 的 Unix 路徑加：

```cpp
    // Ctrl+C used to be the terminal driver's business on Unix, which killed the
    // process without a graceful disconnect -- docs/tui-client.md claimed
    // otherwise. Both platforms now leave through the same signal.
    tuiInstallInterruptHandler([this]() { emit interruptRequested(); });
```

`tuiInstallInterruptHandler()` 內部同樣走 self-pipe，並且**在 classic 模式下也生效**——這正是本計劃唯一刻意改變 classic 行為之處。

- [ ] **Step 6: 執行測試,確認通過**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests qsanguosha_tui -j8 && ./build-linux-gcc/tests/qsanguosha_tui_tests --suite terminal`
Expected: PASS

- [ ] **Step 7: 手動驗證 Ctrl+C（classic 模式,無 board）**

Run:
```bash
./debug/qsanguosha_server --port 16013 --game-mode 02p --seed 2020 &
./debug/qsanguosha_tui --host 127.0.0.1 --port 16013 --ui classic
# 連上之後撳 Ctrl+C
```
Expected: 行程做 graceful disconnect 後以 exit code 0 結束，server 端見到正常斷線而非 socket reset。

- [ ] **Step 8: Commit**

```bash
git add src/tui/tui-terminal.h src/tui/tui-terminal.cpp src/tui/tui-input.cpp \
        CMakeLists.txt tests/CMakeLists.txt tests/tui-tests-main.cpp \
        tests/tui/tui-terminal-test.cpp
git commit -m "feat(tui): take the terminal safely, and hand it back on every path"
```

---

### Task 6: 行編輯器（TuiLineEditor）

raw mode 之後沒有人幫你組行。這一層把按鍵變回一行文字，出口仍是既有的 `lineReady(QString)`，所以 grammar、parser、`ClientCore` 全部不知道有事發生。

**Files:**
- Create: `src/tui/tui-line-editor.h`, `src/tui/tui-line-editor.cpp`
- Modify: `CMakeLists.txt:628`, `tests/CMakeLists.txt`, `tests/tui-tests-main.cpp`
- Test: `tests/tui/tui-line-editor-test.cpp`

**Interfaces:**
- Consumes: `tuiDisplayWidth`（Task 3）。
- Produces:
  - `enum class TuiKey { None, Char, Left, Right, Home, End, Backspace, Delete, Enter, Tab, Escape, Up, Down, PageUp, PageDown, Interrupt };`
  - `struct TuiKeyEvent { TuiKey key = TuiKey::None; QString text; };`
  - `class TuiKeyDecoder`：`QVector<TuiKeyEvent> feed(const QByteArray &bytes)` — 帶未完成緩衝的狀態機，處理跨 read 斷開的 escape sequence。
  - `class TuiLineEditor`：
    - `void setCompleter(std::function<QString(const QString &, QStringList *)>)`
    - `bool handle(const TuiKeyEvent &event, QString *submitted)` — 回傳畫面是否需要重繪；`submitted` 非空表示這一鍵完成了一行。
    - `QString text() const`、`int cursorColumn() const`（顯示格數，非字元數）
    - `void reset()`

- [ ] **Step 1: 寫失敗測試**

建立 `tests/tui/tui-line-editor-test.cpp`：

```cpp
#include "tui-line-editor.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        std::printf("[FAIL] %s\n", what);
    }
}

void type(TuiLineEditor &editor, const QString &text)
{
    for (const QChar &character : text) {
        QString submitted;
        editor.handle(TuiKeyEvent{TuiKey::Char, QString(character)}, &submitted);
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    TuiKeyDecoder decoder;

    // The single property that matters most: a terminal is free to split an
    // escape sequence across reads, and it routinely does.
    QVector<TuiKeyEvent> events = decoder.feed(QByteArray("\x1b["));
    check(events.isEmpty(), "half an escape sequence yields no key yet");
    events = decoder.feed(QByteArray("D"));
    check(events.size() == 1 && events.at(0).key == TuiKey::Left,
          "the rest of the sequence completes the left arrow");

    events = decoder.feed(QByteArray("ab"));
    check(events.size() == 2 && events.at(0).text == QStringLiteral("a"),
          "plain bytes decode as characters");

    events = decoder.feed(QByteArray("\x1b[5~"));
    check(events.size() == 1 && events.at(0).key == TuiKey::PageUp, "PgUp decodes");

    events = decoder.feed(QByteArray("\x03"));
    check(events.size() == 1 && events.at(0).key == TuiKey::Interrupt,
          "Ctrl+C decodes as an interrupt rather than a control character");

    // UTF-8 arrives in pieces too.
    events = decoder.feed(QByteArray("\xe6\x97"));
    check(events.isEmpty(), "half a UTF-8 code point yields no key yet");
    events = decoder.feed(QByteArray("\xb6"));
    check(events.size() == 1 && events.at(0).text == QString::fromUtf8("时"),
          "the rest of the code point completes the character");

    TuiLineEditor editor;
    type(editor, QString::fromUtf8("时语1"));
    check(editor.text() == QString::fromUtf8("时语1"), "typing accumulates a line");
    check(editor.cursorColumn() == 5, "the cursor is measured in columns, not characters");

    QString submitted;
    editor.handle(TuiKeyEvent{TuiKey::Left}, &submitted);
    check(editor.cursorColumn() == 4, "left moves one character, which is two columns here");
    editor.handle(TuiKeyEvent{TuiKey::Backspace}, &submitted);
    check(editor.text() == QString::fromUtf8("时1"),
          "backspace removes a whole wide character");

    editor.handle(TuiKeyEvent{TuiKey::Home}, &submitted);
    check(editor.cursorColumn() == 0, "home goes to the start");
    editor.handle(TuiKeyEvent{TuiKey::End}, &submitted);
    check(editor.cursorColumn() == 3, "end goes to the end");

    editor.handle(TuiKeyEvent{TuiKey::Enter}, &submitted);
    check(submitted == QString::fromUtf8("时1"), "enter submits the line");
    check(editor.text().isEmpty(), "and clears the buffer");

    editor.handle(TuiKeyEvent{TuiKey::Up}, &submitted);
    check(editor.text() == QString::fromUtf8("时1"), "up recalls the previous line");

    // Deliberate: a stray key must never produce a wire effect. Cancelling an
    // interaction stays an explicit /cancel.
    editor.handle(TuiKeyEvent{TuiKey::Escape}, &submitted);
    check(editor.text().isEmpty() && submitted.isEmpty(),
          "escape clears the line and submits nothing");

    editor.setCompleter([](const QString &prefix, QStringList *matches) {
        if (QStringLiteral("/status").startsWith(prefix)) {
            *matches << QStringLiteral("/status");
            return QStringLiteral("/status");
        }
        return prefix;
    });
    type(editor, QStringLiteral("/sta"));
    editor.handle(TuiKeyEvent{TuiKey::Tab}, &submitted);
    check(editor.text() == QStringLiteral("/status"), "tab completes through the injected completer");

    editor.reset();
    for (int i = 0; i < 17000; ++i)
        type(editor, QStringLiteral("x"));
    check(editor.text().size() == 16384, "the line stops growing at the existing 16384 limit");

    std::printf("[AUTOTEST] TUI_LINE_EDITOR_RESULT status=%s\n",
        failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: 登記 suite（四處）**

來源 `tui/tui-line-editor-test.cpp`、`COMPILE_DEFINITIONS main=runTuiLineEditorTests`、
`qsan_add_ctest(qsanguosha_tui_line_editor qsanguosha_tui_tests SUITE line-editor LABELS "client;tui;fast" TIMEOUT 120)`，
以及 `tests/tui-tests-main.cpp` 三處。

- [ ] **Step 3: 執行測試,確認失敗**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8`
Expected: FAIL，`fatal error: tui-line-editor.h: No such file or directory`

- [ ] **Step 4: 實作**

`TuiKeyDecoder::feed()` 是狀態機，非字串比對。狀態：`Ground`、`Escape`（見到 `0x1B`）、`Csi`（見到 `ESC [`，累積參數直到見到最終位元組 `0x40`–`0x7E`）、`Utf8`（累積續位元組）。未完成的序列留在成員緩衝，下次 `feed()` 續接。對應表：

| 位元組 | 鍵 |
|---|---|
| `ESC [ D` / `ESC [ C` | Left / Right |
| `ESC [ H` / `ESC [ F` / `ESC [ 1~` / `ESC [ 4~` | Home / End |
| `ESC [ A` / `ESC [ B` | Up / Down |
| `ESC [ 3~` | Delete |
| `ESC [ 5~` / `ESC [ 6~` | PageUp / PageDown |
| `0x7F` / `0x08` | Backspace |
| `0x0D` / `0x0A` | Enter |
| `0x09` | Tab |
| `0x03` | Interrupt |
| 單獨 `0x1B`（其後 20ms 無位元組） | Escape |
| `0x01` `0x05` `0x15` `0x0B` `0x17` | Ctrl+A / E / U / K / W |

`TuiLineEditor` 以 `QString m_text` 與 `int m_cursor`（字元索引）為狀態；`cursorColumn()` 回傳 `tuiDisplayWidth(m_text.left(m_cursor))`。歷史為 `QStringList`，只在記憶體，不落磁碟。長度上限 16384，達到後丟棄後續輸入字元，與現行 `tui-input.cpp` 一致。

- [ ] **Step 5: 執行測試,確認通過**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8 && ./build-linux-gcc/tests/qsanguosha_tui_tests --suite line-editor`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/tui/tui-line-editor.h src/tui/tui-line-editor.cpp CMakeLists.txt \
        tests/CMakeLists.txt tests/tui-tests-main.cpp tests/tui/tui-line-editor-test.cpp
git commit -m "feat(tui): assemble lines from keys, keeping lineReady the one exit"
```

---

### Task 7: 佈局與分頁（TuiBoardLayout）

純函數：給定終端尺寸與人數，算出各 pane 的矩形、一頁裝得下幾個玩家格、以及每個座位落在哪一頁的哪一格。不碰 IO，因此可以密集測試。

**Files:**
- Create: `src/tui/tui-board-layout.h`, `src/tui/tui-board-layout.cpp`
- Modify: `CMakeLists.txt:628`, `tests/CMakeLists.txt`, `tests/tui-tests-main.cpp`
- Test: `tests/tui/tui-board-layout-test.cpp`

**Interfaces:**
- Consumes: `TuiRect`（Task 4）。
- Produces:
  - `struct TuiSeatSlot { int seatOffset = 0; int page = 0; TuiRect rect; };`（`seatOffset` 為「自己之後第幾個」，1 = 下家）
  - `struct TuiBoardGeometry { bool usable = false; QString unusableReason; TuiRect room; TuiRect log; TuiRect hand; TuiRect input; TuiRect self; int capacity = 0; int pageCount = 1; int cellCols = 0; QVector<TuiSeatSlot> slots; };`
  - `TuiBoardGeometry tuiComputeBoardGeometry(int rows, int cols, int playerCount, int handLines);`

- [ ] **Step 1: 寫失敗測試**

建立 `tests/tui/tui-board-layout-test.cpp`：

```cpp
#include "tui-board-layout.h"

#include <QCoreApplication>
#include <QSet>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        std::printf("[FAIL] %s\n", what);
    }
}

bool overlaps(const TuiRect &a, const TuiRect &b)
{
    return a.row < b.row + b.rows && b.row < a.row + a.rows
        && a.col < b.col + b.cols && b.col < a.col + a.cols;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    // Below the floor the board refuses to draw rather than drawing a torn one.
    const TuiBoardGeometry tooSmall = tuiComputeBoardGeometry(14, 52, 5, 1);
    check(!tooSmall.usable, "a terminal under 60x18 is not drawn at all");
    check(tooSmall.unusableReason.contains(QStringLiteral("60")),
          "and the reason names the size the player needs");

    const TuiBoardGeometry wide = tuiComputeBoardGeometry(40, 120, 5, 1);
    check(wide.usable, "a large terminal is usable");
    check(wide.cellCols >= 3, "120 columns fit the full three region ring");
    check(wide.pageCount == 1, "five players fit on one page there");
    check(wide.slots.size() == 4, "every opponent gets a slot");
    check(!overlaps(wide.room, wide.log) && !overlaps(wide.room, wide.hand)
          && !overlaps(wide.hand, wide.input),
          "panes never overlap");
    check(wide.log.cols >= 22 && wide.log.cols <= 34, "the log pane stays within its clamp");
    check(wide.self.row > wide.room.row, "self sits at the bottom of the room pane");

    // The floor case: one column of cells, so the ring degrades to a stack and
    // five players need more than one page.
    const TuiBoardGeometry floorSize = tuiComputeBoardGeometry(18, 60, 5, 1);
    check(floorSize.usable, "exactly 60x18 is usable");
    check(floorSize.cellCols == 1, "60 columns leave room for a single cell column");
    check(floorSize.pageCount > 1, "which means five players page");

    // Seat order is the property that survives every degradation step.
    for (int players : {2, 3, 5, 8, 10, 20}) {
        const TuiBoardGeometry geometry = tuiComputeBoardGeometry(40, 120, players, 1);
        check(geometry.slots.size() == players - 1,
              "every opponent is placed exactly once at any player count");
        QSet<int> offsets;
        for (const TuiSeatSlot &slot : geometry.slots)
            offsets.insert(slot.seatOffset);
        check(offsets.size() == players - 1, "no seat offset is duplicated or dropped");
        check(offsets.contains(1), "the player's downstream neighbour is always placed");
        for (const TuiSeatSlot &slot : geometry.slots) {
            check(slot.page >= 0 && slot.page < geometry.pageCount,
                  "every slot lands on a real page");
        }
    }

    // 20p is not a special case: it is the same paging path a 9 player game
    // takes in an 80x24 terminal.
    const TuiBoardGeometry twenty = tuiComputeBoardGeometry(40, 120, 20, 1);
    const TuiBoardGeometry nineSmall = tuiComputeBoardGeometry(24, 80, 9, 1);
    check(twenty.pageCount > 1 && nineSmall.pageCount > 1,
          "both crowded cases page rather than degrade to a list");

    std::printf("[AUTOTEST] TUI_BOARD_LAYOUT_RESULT status=%s\n",
        failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 2: 登記 suite（四處）**

來源 `tui/tui-board-layout-test.cpp`、`COMPILE_DEFINITIONS main=runTuiBoardLayoutTests`、
`qsan_add_ctest(qsanguosha_tui_board_layout qsanguosha_tui_tests SUITE board-layout LABELS "client;tui;fast" TIMEOUT 120)`，
以及 `tests/tui-tests-main.cpp` 三處。

- [ ] **Step 3: 執行測試,確認失敗**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8`
Expected: FAIL，`fatal error: tui-board-layout.h: No such file or directory`

- [ ] **Step 4: 實作**

依 spec §3.2、§3.5、§3.6：

```text
if (rows < 18 || cols < 60) -> usable = false,
    unusableReason = tr("终端太小(需 60×18,当前 %1×%2),请放大窗口或用 --ui classic 启动")

logCols  = clamp(cols * 3 / 10, 22, 34)
roomCols = cols - logCols - 3            // 兩條外框 + 一條分隔
handRows = clamp(handLines, 1, 5)
inputRows = 2                            // 提示行 + 輸入行
roomRows = rows - handRows - inputRows - 4   // 四條水平框線
cellCols = roomCols / 20
cellRows = (roomRows - 3) / 3            // 扣起自己那格
capacity = max(1, cellCols * cellRows - 1)   // 扣起中央牌堆／棄牌格
pageCount = ceil((playerCount - 1) / capacity)
```

座位派位沿用桌面版的 `s_regularSeatIndex`（`src/ui/roomscene.cpp:1771`），歸約為三區：區域 `1` 與 `7` → 上排，`3` 與 `5` → 左欄，`4` 與 `6` → 右欄。`cellCols` 不足 3 時依 spec §3.6 的階梯退化：

| `cellCols` | 佈局 |
|---|---|
| ≥ 3 | 左欄／上排／右欄 |
| 2 | 去掉上排，由下家起左右交替 |
| 1 | 單欄，由下家起由上而下 |

每一階都保持 `seatOffset` 由 1 遞增的順序，這正是測試中「no seat offset is duplicated or dropped」所釘住的。

- [ ] **Step 5: 執行測試,確認通過**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8 && ./build-linux-gcc/tests/qsanguosha_tui_tests --suite board-layout`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/tui/tui-board-layout.h src/tui/tui-board-layout.cpp CMakeLists.txt \
        tests/CMakeLists.txt tests/tui-tests-main.cpp tests/tui/tui-board-layout-test.cpp
git commit -m "feat(tui): compute the board layout, paging when the table will not fit"
```

---

### Task 8: 牌桌繪製（TuiBoardView）

把 `ClientGameState` 畫入 `TuiScreen`。這是唯一知道遊戲語意的繪製層，也是 golden 測試的對象。

**Files:**
- Create: `src/tui/tui-board-view.h`, `src/tui/tui-board-view.cpp`
- Create: `tests/tui/golden/board-05p-play.txt`, `tests/tui/golden/board-waiting.txt`, `tests/tui/golden/board-09p-page2.txt`
- Modify: `CMakeLists.txt:628`, `tests/CMakeLists.txt`, `tests/tui-tests-main.cpp`
- Test: `tests/tui/tui-board-view-test.cpp`

**Interfaces:**
- Consumes: `TuiScreen`（Task 4）、`tuiComputeBoardGeometry`（Task 7）、`TuiResolvers`（Task 2）、`ClientGameState`（既有）。
- Produces:
  - `struct TuiBoardViewState { int page = 0; QStringList logLines; QString promptLine; QString inputLine; int inputCursorColumn = 0; QString notice; };`
  - `class TuiBoardView`：`explicit TuiBoardView(TuiResolvers resolvers)`；`void render(TuiScreen *screen, const ClientGameState &state, const TuiBoardViewState &view) const;`

- [ ] **Step 1: 寫失敗測試**

建立 `tests/tui/tui-board-view-test.cpp`。它以 `ClientGameState` fixture 繪製，與 golden 檔比對；`QSAN_TUI_GOLDEN_WRITE=1` 時改為寫回 golden（沿用倉庫 `QSAN_TUI_COVERAGE_WRITE=1` 的做法）。

```cpp
#include "client-game-state.h"
#include "tui-board-view.h"
#include "tui-screen.h"

#include <QCoreApplication>
#include <QFile>
#include <QString>

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        std::printf("[FAIL] %s\n", what);
    }
}

QString goldenPath(const QString &name)
{
    return QStringLiteral("%1/tui/golden/%2.txt").arg(QStringLiteral(QSAN_TEST_SOURCE_DIR), name);
}

void compareGolden(const QString &name, const QString &actual)
{
    if (qEnvironmentVariableIsSet("QSAN_TUI_GOLDEN_WRITE")) {
        QFile file(goldenPath(name));
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
              "golden file opens for writing");
        file.write(actual.toUtf8());
        return;
    }
    QFile file(goldenPath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        ++failures;
        std::printf("[FAIL] missing golden %s\n", qPrintable(name));
        return;
    }
    const QString expected = QString::fromUtf8(file.readAll());
    if (expected != actual) {
        ++failures;
        std::printf("[FAIL] golden %s differs\n--- expected ---\n%s\n--- actual ---\n%s\n",
            qPrintable(name), qPrintable(expected), qPrintable(actual));
    }
}

} // namespace
```

主體建三個 fixture 並各自比對：

1. `board-waiting` — `GAME_START` 之前。斷言房間區顯示等待室（模式、`3/5` 人數進度、就緒狀態），且**不繪製座位環**：
   `check(!screen.toPlainText().contains(QString::fromUtf8("体力")), "no seats are drawn before GAME_START");`
2. `board-05p-play` — 5 人出牌階段，自己在底、當前玩家有 `▶` 標記、一名玩家 `✖阵亡`、手牌區列出五張牌。額外斷言：
   `check(screen.toPlainText().contains(QString::fromUtf8("▶")), "the current player is marked");`
   `check(!screen.toPlainText().contains(QStringLiteral("‹")), "a single page shows no page indicator");`
3. `board-09p-page2` — 9 人、`tuiComputeBoardGeometry(24, 80, 9, 1)`、`view.page = 1`。斷言：
   `check(screen.toPlainText().contains(QStringLiteral("‹2/")), "a paged board shows which page is on screen");`
   `check(screen.toPlainText().contains(QString::fromUtf8("(我)")), "self stays on screen on every page");`

每個 fixture 之後呼叫 `compareGolden(<name>, screen.toPlainText())`，並以
`std::printf("[AUTOTEST] TUI_BOARD_VIEW_RESULT status=%s\n", failures == 0 ? "PASS" : "FAIL");` 收尾。

- [ ] **Step 2: 登記 suite,並把來源目錄傳給測試**

`tests/CMakeLists.txt` 加來源 `tui/tui-board-view-test.cpp`、
`set_source_files_properties(tui/tui-board-view-test.cpp PROPERTIES COMPILE_DEFINITIONS main=runTuiBoardViewTests)`、
`qsan_add_ctest(qsanguosha_tui_board_view qsanguosha_tui_tests SUITE board-view LABELS "client;tui;fast" TIMEOUT 120)`，
並在既有的 `target_compile_definitions(qsanguosha_tui_tests PRIVATE QSAN_ENGINE_TEST_BUILD)` 加一項：

```cmake
        QSAN_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
```

`tests/tui-tests-main.cpp` 加宣告、分派與 `runIsolatedTestCases` 條目。

- [ ] **Step 3: 執行測試,確認失敗**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8`
Expected: FAIL，`fatal error: tui-board-view.h: No such file or directory`

- [ ] **Step 4: 實作繪製**

依 spec §3.1、§3.3、§3.4、§3.7：

- 玩家格 `20 × 3`，三行分別為 `[座位]名字 勢力 血量`、`身份 手N 装N 判【…】`、狀態標記（`▶行动中`／`✖阵亡`／`濒死`／`翻面`／`连环`）。每行以 `tuiPadTo(..., 20)` 對齊，長名以 `tuiElide` 收。
- 血量：`maxHp <= 8` 用 `♥`／`♡`；否則 `♥ 9/12`。
- 自己那格額外加框並標 `(我)`。
- 房間區中央繪 `牌堆 N   弃牌 N`。
- 未開局（`state.game()["status"] != "playing"` 且無玩家座位）時走等待室分支，不繪座位環。
- 分頁時房間區標題附 `‹<page+1>/<pageCount>›`。
- 色彩依 `TuiAttr` 指派，資訊本身已在文字中，`toPlainText()` 去色後仍完整——golden 比對的正是去色文字。

- [ ] **Step 5: 生成 golden 並人手審閱**

Run:
```bash
cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8
QSAN_TUI_GOLDEN_WRITE=1 ./build-linux-gcc/tests/qsanguosha_tui_tests --suite board-view
cat tests/tui/golden/board-05p-play.txt
```
Expected: 印出的牌桌與 spec §3.1 的示意圖同形。**逐個檢查對齊**：每條 `│` 必須在同一欄。不對就是 Task 3 或 Task 4 有問題，回去修那裡，不要調整 golden 遷就。

- [ ] **Step 6: 執行測試,確認通過**

Run: `./build-linux-gcc/tests/qsanguosha_tui_tests --suite board-view`
Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add src/tui/tui-board-view.h src/tui/tui-board-view.cpp tests/tui/golden \
        CMakeLists.txt tests/CMakeLists.txt tests/tui-tests-main.cpp \
        tests/tui/tui-board-view-test.cpp
git commit -m "feat(tui): draw the room, log, hand and input as one screen"
```

---

### Task 9: board presenter、overlay 與自動跟隨

把前面幾層裝成一個 presenter：接 `stateChanged` 重繪、路由文字輸出、處理翻頁與 overlay。

**Files:**
- Create: `src/tui/tui-board-presenter.h`, `src/tui/tui-board-presenter.cpp`
- Modify: `src/tui/tui-presenter.h`（追加兩個虛擬成員）
- Modify: `src/tui/tui-stream-presenter.h`, `src/tui/tui-stream-presenter.cpp`（實作新成員為 no-op）
- Modify: `src/tui/tui-application-controller.cpp`（`stateChanged` slot 通知 presenter；`/board` 指令）
- Modify: `src/tui/tui-command.cpp`, `src/tui/tui-command.h`（`/board <page>`）
- Modify: `CMakeLists.txt:628`, `tests/CMakeLists.txt`, `tests/tui-tests-main.cpp`
- Test: `tests/tui/tui-board-presenter-test.cpp`

**Interfaces:**
- Consumes: `TuiBoardView`、`TuiScreen`、`TuiTerminal`、`TuiLineEditor`、`tuiComputeBoardGeometry`。
- Produces:
  - `TuiPresenter` 追加：`virtual void stateChanged(const ClientGameState &state) = 0;`、`virtual void interactionChanged(const InteractionRequest *request) = 0;`（`nullptr` 表示沒有進行中的請求）。
  - `class TuiBoardPresenter final : public TuiPresenter`
    - `TuiBoardPresenter(QSize size, TuiResolvers resolvers, TuiTerminal *terminal = nullptr)` — `terminal` 為 null 時 presenter 純畫入自己的 `TuiScreen` 而不寫終端，測試即用此形態。
    - `void setPage(int page)`、`int page() const`、`int pageCount() const`
    - `void toggleOverlay(const QString &content)` — 空字串表示關閉
    - `void setViewportSize(QSize size)` — resize 時重建 grid 並強制全畫
    - `bool handleKey(const TuiKeyEvent &event, QString *submitted)` — 先給 overlay 與翻頁，其餘轉交 `TuiLineEditor`
    - `QString screenText() const`（測試用，等同 `TuiScreen::toPlainText()`）

- [ ] **Step 1: 寫失敗測試**

建立 `tests/tui/tui-board-presenter-test.cpp`，釘住三件事：

```cpp
    // 1. Auto-follow: the player whose turn it is is always on screen.
    presenter.setPage(1);
    presenter.stateChanged(stateWithCurrentPlayer(QStringLiteral("sgs2")));
    check(presenter.screenText().contains(QString::fromUtf8("曹操")),
          "a turn change pages to whoever is acting");

    // 2. A manual flip holds, but only until the next turn or request.
    presenter.setPage(2);
    check(presenter.page() == 2, "a manual flip is respected");
    presenter.interactionChanged(&request);
    check(presenter.page() != 2 || presenter.screenText().contains(firstCandidateName),
          "a new request pages to its first candidate");

    // 3. Long dumps do not scroll the board away.
    presenter.writeOutput(longPlayersDump);
    check(presenter.screenText().contains(QString::fromUtf8("座位=")),
          "a long dump opens as an overlay");
    presenter.toggleOverlay(QString());
    check(presenter.screenText().contains(QString::fromUtf8("牌堆")),
          "closing the overlay puts the board back");

    // 4. Short messages land in the log pane instead.
    presenter.writeOutput(QString::fromUtf8("时语 打出【闪】"));
    check(presenter.screenText().contains(QString::fromUtf8("打出")),
          "a short message joins the log scrollback");
```

fixture 以 `TuiScreen` 直接注入尺寸，不需要真終端：`TuiBoardPresenter` 的建構子取一個 `QSize`，真實執行時由 `TuiTerminal::size()` 提供，測試時由測試提供。

- [ ] **Step 2: 登記 suite（四處）**

來源 `tui/tui-board-presenter-test.cpp`、`COMPILE_DEFINITIONS main=runTuiBoardPresenterTests`、
`qsan_add_ctest(qsanguosha_tui_board_presenter qsanguosha_tui_tests SUITE board-presenter LABELS "client;tui;fast" TIMEOUT 120)`，
以及 `tests/tui-tests-main.cpp` 三處。

- [ ] **Step 3: 執行測試,確認失敗**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8`
Expected: FAIL，`fatal error: tui-board-presenter.h: No such file or directory`

- [ ] **Step 4: 擴充介面並實作 presenter**

`TuiPresenter` 加兩個純虛成員；`TuiStreamPresenter` 兩個都實作為空主體，並註明原因：

```cpp
void TuiStreamPresenter::stateChanged(const ClientGameState &)
{
    // The line client prints what it is told to print; it has no view to refresh.
}

void TuiStreamPresenter::interactionChanged(const InteractionRequest *)
{
    // Likewise: TuiInteractionView already wrote the prompt through writeOutput().
}
```

`TuiBoardPresenter` 依 spec §3.6 與 §5.2：

- `stateChanged()`：合併重繪——`QTimer::singleShot(0, ...)` 設一個 pending flag，同一個 event loop turn 內多次通知只畫一次。
- 自動跟隨：記錄上一次的 `current_player`；變更時把 `m_page` 設為該玩家所在頁並清除手動標記。`interactionChanged()` 非 null 時同理，取第一個候選目標。
- `setPage()` 設手動標記，該標記在下一次回合轉換或請求到達時清除。
- `writeOutput()`：文字行數 `> 3` 走 overlay，否則進 log pane 的 scrollback（上限 200 行，與現有 `/log` 一致）。
- `writeError()`：寫入 notice 行並同時進 scrollback。
- 翻頁與 overlay **不得**呼叫任何 session 或 core 方法——這是不變式 1，Task 11 會逐條驗。

controller 端：`ClientLiveSession::stateChanged` 的 lambda 內加 `m_presenter->stateChanged(m_session.state());`；`/board <page>` 在 `tui-command.cpp` 解析成 `TuiCommandIntent`，由 controller 轉呼 `setPage()`。`/board` 是純本地指令，**不得**進入 `handleCommand()` 中送往 session 的分支。

- [ ] **Step 5: 接上 resize 與按鍵**

這兩條線是 board 模式唯一還沒接上的地方，缺了它畫面永遠不會更新、鍵永遠到不了 parser。

`src/tui/tui-board-presenter.cpp` 建構子內（`terminal` 非 null 時）：

```cpp
    // spec §3.8: a resize rebuilds the grid and forces one full frame; the
    // diff resumes from there.
    QObject::connect(terminal, &TuiTerminal::resized, this, [this, terminal]() {
        setViewportSize(terminal->size());
    });
```

`handleKey()` 的優先次序，由上而下，第一個命中即消耗該鍵：

```cpp
bool TuiBoardPresenter::handleKey(const TuiKeyEvent &event, QString *submitted)
{
    if (!m_overlay.isEmpty()) {
        // Esc/q/space close and swallow; anything printable closes and falls
        // through, so the player never loses the first character they typed.
        if (event.key == TuiKey::Escape || event.text == QStringLiteral("q")
            || event.text == QStringLiteral(" ")) {
            toggleOverlay(QString());
            return true;
        }
        if (event.key == TuiKey::Up || event.key == TuiKey::Down
            || event.key == TuiKey::PageUp || event.key == TuiKey::PageDown) {
            scrollOverlay(event.key);
            return true;
        }
        toggleOverlay(QString());
    }
    if (event.key == TuiKey::PageUp || event.key == TuiKey::PageDown) {
        setPage(m_page + (event.key == TuiKey::PageDown ? 1 : -1));
        return true;
    }
    return m_editor.handle(event, submitted);
}
```

`src/tui/tui-application-controller.cpp`：board 模式下，`TuiInput` 發出的原始位元組經
`TuiKeyDecoder` 解成按鍵，逐個交 `handleKey()`；`submitted` 非空時發出**既有的**
`lineReady(QString)`。這一步是不變式 2 的落地位——board 不得繞過 `handleInputLine()`
自行組裝任何回覆。

- [ ] **Step 6: 執行測試,確認通過**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8 && ./build-linux-gcc/tests/qsanguosha_tui_tests --suite board-presenter`
Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add src/tui/tui-board-presenter.h src/tui/tui-board-presenter.cpp \
        src/tui/tui-presenter.h src/tui/tui-stream-presenter.h \
        src/tui/tui-stream-presenter.cpp src/tui/tui-application-controller.cpp \
        src/tui/tui-command.cpp src/tui/tui-command.h CMakeLists.txt \
        tests/CMakeLists.txt tests/tui-tests-main.cpp \
        tests/tui/tui-board-presenter-test.cpp
git commit -m "feat(tui): drive the board from game state, paging to what matters"
```

---

### Task 10: 模式決議、記憶與啟動詢問

把兩套 UI 接上 CLI。決議表的每一列都要有測試——包括那條刻意報錯而非降級的。

**Files:**
- Create: `src/tui/tui-ui-mode.h`, `src/tui/tui-ui-mode.cpp`
- Modify: `src/tui/tui-main.cpp:110-290`（`--ui` 選項、決議、presenter 安裝）
- Modify: `src/tui/tui-application-controller.h`（`TuiApplicationOptions` 加 `uiMode`）
- Modify: `lang/zh_CN/TUICommon.lua`（啟動選單與衝突訊息的文字鍵）
- Modify: `CMakeLists.txt:628`, `tests/CMakeLists.txt`, `tests/tui-tests-main.cpp`
- Test: `tests/tui/tui-ui-mode-test.cpp`

**Interfaces:**
- Consumes: 無。
- Produces:
  - `enum class TuiUiMode { Classic, Board };`
  - `struct TuiUiModeInputs { QString flag; bool hasScript = false; bool stdoutIsTty = false; bool stdinIsTty = false; bool plain = false; QString savedChoice; };`
  - `struct TuiUiModeDecision { TuiUiMode mode = TuiUiMode::Classic; bool askUser = false; bool conflict = false; QString conflictReason; };`
  - `TuiUiModeDecision tuiResolveUiMode(const TuiUiModeInputs &inputs);`
  - `QString tuiSavedUiMode();`、`void tuiSaveUiMode(TuiUiMode mode);`（`QSettings`，`QStandardPaths::AppConfigLocation`）

- [ ] **Step 1: 寫失敗測試**

`tests/tui/tui-ui-mode-test.cpp` 逐列覆蓋 spec §6.1：

```cpp
    TuiUiModeInputs inputs;
    inputs.stdoutIsTty = true;
    inputs.stdinIsTty = true;

    // A script always wins, whatever else was asked for.
    TuiUiModeInputs scripted = inputs;
    scripted.hasScript = true;
    check(tuiResolveUiMode(scripted).mode == TuiUiMode::Classic, "a script forces classic");
    check(!tuiResolveUiMode(scripted).askUser, "and never asks");

    TuiUiModeInputs piped = inputs;
    piped.stdoutIsTty = false;
    check(tuiResolveUiMode(piped).mode == TuiUiMode::Classic, "redirected output forces classic");

    TuiUiModeInputs plain = inputs;
    plain.plain = true;
    check(tuiResolveUiMode(plain).mode == TuiUiMode::Classic, "--plain forces classic");

    // The deliberate one: asking for board under a forcing condition is an
    // error, not a silent downgrade.
    TuiUiModeInputs conflicting = plain;
    conflicting.flag = QStringLiteral("board");
    const TuiUiModeDecision conflict = tuiResolveUiMode(conflicting);
    check(conflict.conflict, "--ui board with --plain is a conflict");
    check(conflict.conflictReason.contains(QStringLiteral("--plain")),
          "and the message names the flag that conflicts");

    TuiUiModeInputs explicitBoard = inputs;
    explicitBoard.flag = QStringLiteral("board");
    check(tuiResolveUiMode(explicitBoard).mode == TuiUiMode::Board, "--ui board is honoured");
    check(!tuiResolveUiMode(explicitBoard).askUser, "an explicit flag never asks");

    TuiUiModeInputs remembered = inputs;
    remembered.savedChoice = QStringLiteral("board");
    check(tuiResolveUiMode(remembered).mode == TuiUiMode::Board, "a saved choice is used");
    check(!tuiResolveUiMode(remembered).askUser, "and is not asked again");

    check(tuiResolveUiMode(inputs).askUser,
          "a bare tty run with no saved choice asks once");

    TuiUiModeInputs badFlag = inputs;
    badFlag.flag = QStringLiteral("fancy");
    check(tuiResolveUiMode(badFlag).conflict, "an unknown --ui value is a usage error");
```

- [ ] **Step 2: 登記 suite（四處）**

來源 `tui/tui-ui-mode-test.cpp`、`COMPILE_DEFINITIONS main=runTuiUiModeTests`、
`qsan_add_ctest(qsanguosha_tui_ui_mode qsanguosha_tui_tests SUITE ui-mode LABELS "client;tui;fast" TIMEOUT 120)`，
以及 `tests/tui-tests-main.cpp` 三處。

- [ ] **Step 3: 執行測試,確認失敗**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8`
Expected: FAIL，`fatal error: tui-ui-mode.h: No such file or directory`

- [ ] **Step 4: 實作決議與接線**

`tuiResolveUiMode()` 嚴格依 spec §6.1 的次序，第一個命中即回傳。`conflict` 為真時 `tui-main.cpp` 印訊息並回 exit code `2`。

`tui-main.cpp` 加 `QCommandLineOption uiOption(QStringList{QStringLiteral("ui")}, tr("界面模式:classic 或 board"), QStringLiteral("mode"))`；決議後：

- `askUser` 為真：在 `connectToHost` 之前，用現有的 `writeUtf8(stdout, ...)` 印選單、以阻塞的 `std::getline` 讀一行（此時尚未進 event loop，可安全阻塞），必要時 `tuiSaveUiMode()`。
- 依決議建立 `TuiStreamPresenter` 或 `TuiBoardPresenter`，經 `TuiApplicationOptions` 交給 controller。
- board 模式下 `TuiTerminal::enter()` 失敗即回 exit code `6`。

Windows 另需 `ENABLE_VIRTUAL_TERMINAL_PROCESSING`：設不到時，明寫 `--ui board` 回 exit `2`；自動／記憶路徑退 classic 並印一行說明。

- [ ] **Step 5: 執行測試,確認通過**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8 && ./build-linux-gcc/tests/qsanguosha_tui_tests --suite ui-mode`
Expected: PASS

- [ ] **Step 6: 手動驗證三條路徑**

Run:
```bash
cmake --build build-linux-gcc --target qsanguosha_tui -j8
./debug/qsanguosha_tui --ui board --plain --host 127.0.0.1 ; echo "exit=$?"
./debug/qsanguosha_tui --ui fancy --host 127.0.0.1 ; echo "exit=$?"
echo "" | ./debug/qsanguosha_tui --host 127.0.0.1 --port 1 ; echo "exit=$?"
```
Expected: 前兩者 `exit=2` 並各自印出衝突原因；第三者不出現選單（stdin 非 TTY），以連線失敗的 `exit=3` 結束。

- [ ] **Step 7: Commit**

```bash
git add src/tui/tui-ui-mode.h src/tui/tui-ui-mode.cpp src/tui/tui-main.cpp \
        src/tui/tui-application-controller.h lang/zh_CN/TUICommon.lua \
        CMakeLists.txt tests/CMakeLists.txt tests/tui-tests-main.cpp \
        tests/tui/tui-ui-mode-test.cpp
git commit -m "feat(tui): pick a UI mode at startup, and refuse to downgrade silently"
```

---

### Task 11: Parity 測試——不變式的證據

這是本計劃的驗收閘。沒有它，「board 不改變交互」只是一句話。

**Files:**
- Create: `tests/tui/tui-ui-parity-test.cpp`
- Modify: `tests/CMakeLists.txt`, `tests/tui-tests-main.cpp`

**Interfaces:**
- Consumes: `TuiStreamPresenter`、`TuiBoardPresenter`、`ClientCore`、`InteractionRequest`、`InteractionResponse`、`artifacts/tui-flow-coverage.json`。
- Produces: 無新型別。

- [ ] **Step 1: 寫測試**

對每個有 presenter 的 interaction request 各跑一次對照。核心結構：

```cpp
// The invariant this whole feature rests on: a view is a view. Paging,
// overlays and resizes are local, so the bytes that reach the server must not
// depend on which UI drew them.
struct Recorded
{
    QList<InteractionResponse> responses;
};

Recorded runWith(TuiPresenter *presenter, const QStringList &input,
                 const QList<ViewAction> &viewActions);

for (const QString &requestType : presenterBackedRequestTypes()) {
    const QStringList input = scriptedAnswerFor(requestType);

    TuiStreamPresenter classic(nullptr, nullptr);
    const Recorded fromClassic = runWith(&classic, input, {});

    TuiBoardPresenter board(QSize(120, 40), resolvers);
    // Deliberately noisy: every local view action the player can take.
    const Recorded fromBoard = runWith(&board, input, {
        ViewAction::pageNext, ViewAction::pagePrev, ViewAction::openOverlay,
        ViewAction::closeOverlay, ViewAction::resize(80, 24),
        ViewAction::resize(120, 40),
    });

    check(fromClassic.responses.size() == fromBoard.responses.size(),
          "both UIs send the same number of replies");
    for (int i = 0; i < fromClassic.responses.size(); ++i) {
        check(responsesEqual(fromClassic.responses.at(i), fromBoard.responses.at(i)),
              "both UIs send byte-identical replies");
    }
}
```

`presenterBackedRequestTypes()` 從 `artifacts/tui-flow-coverage.json` 讀出 29 個有 presenter 的 request，**不得**硬編成清單——日後新增 flow 時測試要自動涵蓋。`responsesEqual()` 逐欄比對 `InteractionResponse`（含 `reply_to` 的完整 `quint64`），不是比對字串。

額外一條斷言，直接釘住不變式 1：

```cpp
    // A view action must not produce a reply of its own.
    TuiBoardPresenter idle(QSize(120, 40), resolvers);
    const Recorded fromViewOnly = runWith(&idle, {}, {
        ViewAction::pageNext, ViewAction::openOverlay, ViewAction::resize(80, 24),
    });
    check(fromViewOnly.responses.isEmpty(),
          "paging, overlays and resizes send nothing at all");
```

- [ ] **Step 2: 登記 suite（四處）**

來源 `tui/tui-ui-parity-test.cpp`、`COMPILE_DEFINITIONS main=runTuiUiParityTests`、
`qsan_add_ctest(qsanguosha_tui_ui_parity qsanguosha_tui_tests SUITE ui-parity LABELS "client;tui;protocol;fast" TIMEOUT 180)`，
以及 `tests/tui-tests-main.cpp` 三處。

- [ ] **Step 3: 執行測試**

Run: `cmake --build build-linux-gcc --target qsanguosha_tui_tests -j8 && ./build-linux-gcc/tests/qsanguosha_tui_tests --suite ui-parity`
Expected: PASS，且輸出顯示涵蓋了 29 個 request 而非更少。若涵蓋數少於 29，是 `presenterBackedRequestTypes()` 讀錯了 artifact，修它而不是降低期望。

- [ ] **Step 4: Commit**

```bash
git add tests/tui/tui-ui-parity-test.cpp tests/CMakeLists.txt tests/tui-tests-main.cpp
git commit -m "test(tui): prove the board UI changes nothing the server sees"
```

---

### Task 12: pty smoke 與文件

單元測試碰不到真終端。這個 task 補上本機 pty 閘，並把文件改成與實作一致。

**Files:**
- Create: `tools/autotest/tui_board_smoke.py`
- Modify: `docs/tui-client.md`（`--ui` 選項列、board 章節、`/board`、Ctrl+C 敘述修正）
- Modify: `docs/tui-client-architecture.md`（`TuiPresenter` 分層）

**Interfaces:**
- Consumes: `./debug/qsanguosha_tui`、`./debug/qsanguosha_server`。
- Produces: 無新型別。

- [ ] **Step 1: 寫 pty smoke**

`tools/autotest/tui_board_smoke.py` 用 `pty.openpty()` 起 client，依序驗四件事：

```python
# 1. Entering board mode switches to the alternate screen.
assert b"\x1b[?1049h" in output

# 2. Resizing repaints rather than tearing: after SIGWINCH the client must
#    emit a full frame, which starts by homing the cursor.
os.write(master, b"")
fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
os.kill(child, signal.SIGWINCH)
assert b"\x1b[H" in read_until(master, timeout=5)

# 3. Ctrl+C leaves the terminal as it was found.
os.kill(child, signal.SIGINT)
tail = read_until(master, timeout=5)
assert b"\x1b[?1049l" in tail, "alternate screen was not left"
assert b"\x1b[?25h" in tail, "cursor was not restored"

# 4. And the client exited cleanly rather than being killed.
assert os.waitpid(child, 0)[1] == 0
```

子行程建立獨立 process group，逾時後才強制清理，並確認無 orphan——與 `tools/autotest/tui_network_smoke.py` 同一套做法。

- [ ] **Step 2: 執行 pty smoke**

Run:
```bash
./debug/qsanguosha_server --port 16013 --game-mode 02p --seed 2020 &
python3 tools/autotest/tui_board_smoke.py --exe-root . --port 16013
```
Expected: 四項斷言全過，退出碼 0。

- [ ] **Step 3: 更新 docs/tui-client.md**

三處：

1. CLI 選項表加一列：`| --ui <classic\|board> | 界面模式;預設在 TTY 首次啟動時詢問並可記住 |`
2. 全域命令一行加 `/board`，並註明它是純本地視圖指令，不產生 wire 訊息。
3. **修正**「State、輸入與安全」一節關於 Ctrl+C 的敘述。現行文字聲稱 EOF、Ctrl+C 及 `/quit` 都會 graceful disconnect，但在 Task 5 之前 Linux 並無 SIGINT 處理；改為描述兩平台共用 `interruptRequested` 的現況。

另加一節簡述 board 模式，並明寫 spec §7.5 的證據紀律：board 在 CI 無法執行，其可用性由本機 pty smoke 與 golden test 支撐，不得以 CI 綠燈冒充。

- [ ] **Step 4: 更新 docs/tui-client-architecture.md**

在共用架構圖的 `TuiApplicationController` 之下加入 `TuiPresenter` 分層，與 spec §2 的圖一致。

- [ ] **Step 5: 跑全部閘**

Run:
```bash
ctest --test-dir build-linux-gcc -L tui --output-on-failure
python3 tools/autotest/tui_network_smoke.py --exe-root . --mode 03_1v2 --seed 20260831 \
  --artifact-dir /tmp/tui-board-regression
```
Expected: 全部 PASS。`tui_network_smoke.py` 走 classic，其行為必須與本計劃開始前一致。

- [ ] **Step 6: Commit**

```bash
git add tools/autotest/tui_board_smoke.py docs/tui-client.md docs/tui-client-architecture.md
git commit -m "docs(tui): document the board UI, and correct the Ctrl+C claim"
```

---

## 完成標準

全部十二個 task 完成後，以下每一項都要有實際輸出佐證，不接受「應該可以」：

1. `ctest --test-dir build-linux-gcc -L tui --output-on-failure` 全綠，含七個既有 suite 與九個新 suite。
2. `qsanguosha_tui_ui_parity` 涵蓋 29 個 interaction request，全部逐欄相同。
3. `tools/autotest/tui_board_smoke.py` 四項斷言通過。
4. `tools/autotest/tui_network_smoke.py` 的 `03_1v2` 完整對局與 reconnect 通過，且行為與計劃開始前一致。
5. Windows dumpbin 依賴閘與 `deploy-tui` package smoke 通過（remote-only，需 CI 結果）。
6. `docs/tui-client.md` 與 `docs/tui-client-architecture.md` 已更新。

第 5 項是 remote-only：未取得 CI 結果之前不得視為完成證據。
