#include "tui-board-view.h"

#include "client-game-state.h"
#include "player.h"
#include "tui-board-layout.h"
#include "tui-screen.h"
#include "tui-text-width.h"

#include <QSet>
#include <QVector>

#include <algorithm>
#include <utility>

// The Chinese status words, brackets and headings below ("✖阵亡", "手牌",
// "牌堆"...) are written out as plain literals rather than routed through
// tuiText()/TUICommon.lua. Everything TUICommon carries is a full sentence or
// a wire-token translation shared with classic mode; these are fixed layout
// furniture that the spec (docs/tui-board-ui.md §3.1) pins to an exact glyph,
// and tui-board-layout.cpp already sets the precedent of a literal Chinese
// string for board-only text ("终端太小..." in tuiComputeBoardGeometry).

namespace {

// Mirrors tui-board-layout.cpp's own (file-local) CellWidth/CellHeight --
// the spec's "每個玩家格固定 20 × 3" (§3.1). Duplicated rather than shared
// because TuiBoardGeometry does not expose them directly, only their effect
// via TuiSeatSlot::rect; drawPile() below needs the raw numbers back to
// reconstruct which grid cell tui-board-layout.cpp left empty for it.
constexpr int CellWidth = 20;
constexpr int CellHeight = 3;

QString cardName(const TuiResolvers &resolvers, int cardId)
{
    return resolvers.card ? resolvers.card(cardId) : QString::number(cardId);
}

// "wei" -> "魏", "wei+shu" (kingdom not yet declared) -> "魏/蜀", matching
// TuiRenderer::kingdomText()'s own handling of the same wire value -- but
// read structurally (general -> kingdom code -> translated) through
// resolvers rather than through a pre-composed sentence, per
// docs/tui-board-ui.md §2.2's split between the two UIs.
QString translatedKingdom(const TuiResolvers &resolvers, const QString &generalName)
{
    const QString code = resolvers.kingdom ? resolvers.kingdom(generalName) : QString();
    if (code.isEmpty())
        return QString();
    if (code.contains(QLatin1Char('+'))) {
        QStringList shown;
        for (const QString &part : code.split(QLatin1Char('+'), Qt::SkipEmptyParts))
            shown << (resolvers.name ? resolvers.name(part) : part);
        return shown.join(QLatin1Char('/'));
    }
    return resolvers.name ? resolvers.name(code) : code;
}

// spec §3.4: filled/hollow hearts up to 8 maxHp, numeric past that so a
// boosted general's cell does not blow past its 20-column budget.
QString hpText(int hp, int maxHp)
{
    if (maxHp <= 0)
        return QString();
    if (maxHp <= 8) {
        QString hearts;
        for (int i = 0; i < maxHp; ++i)
            hearts += QChar(i < hp ? 0x2665 : 0x2661); // ♥ / ♡
        return hearts;
    }
    return QStringLiteral("♥ %1/%2").arg(hp).arg(maxHp);
}

bool isGameStarted(const ClientGameState &state)
{
    // The reducer's real value is "active" (client-game-state-reducer.cpp,
    // S_COMMAND_GAME_START), not the literal "playing" the brief's condition
    // names -- but by the time that command lands every player already has a
    // seat (same handler assigns them), so testing for a seat is the check
    // that actually matches production traffic. The status comparison is
    // kept too so a future rename of the status string, or a hand-built
    // fixture that sets one without seats, still resolves correctly.
    if (state.gameValue(QStringLiteral("status")).toString() == QLatin1String("playing"))
        return true;
    for (const QString &name : state.playerNames()) {
        if (state.playerValue(name, QStringLiteral("seat")).toInt() > 0)
            return true;
    }
    return false;
}

// Opponents in seat order starting just downstream of the local player --
// index 0 is TuiSeatSlot::seatOffset 1, index 1 is offset 2, and so on
// wrapping around the table. Mirrors ClientPlayer::seatStep()'s ring but,
// unlike that one, keeps dead players in the ring: the board still owes them
// a cell (spec §3.1's 貂蝉 example is dead and still drawn).
QStringList seatOrderFromSelf(const ClientGameState &state)
{
    QVector<std::pair<int, QString>> ring;
    for (const QString &name : state.playerNames())
        ring.append({state.playerValue(name, QStringLiteral("seat")).toInt(), name});
    std::sort(ring.begin(), ring.end(),
        [](const std::pair<int, QString> &a, const std::pair<int, QString> &b) {
            return a.first < b.first;
        });
    QStringList names;
    for (const auto &entry : ring)
        names << entry.second;
    const qsizetype selfIndex = names.indexOf(state.selfName());
    if (selfIndex < 0)
        return {};
    QStringList order;
    for (qsizetype i = 1; i < names.size(); ++i)
        order << names.at((selfIndex + i) % names.size());
    return order;
}

// The three fixed lines of one player's cell (spec §3.1):
//   line 1: [seat]name kingdom hp        -- prefixed with ▶ if it is this
//                                            player's turn, suffixed (我) for
//                                            self.
//   line 2: role handN [装.../装N] [判...]
//   line 3: status markers (blank when none apply).
// `width` is CellWidth (20) for an opponent slot, or the room's own width for
// the self cell -- see TuiBoardGeometry::self, which is not 20 wide because
// it does not have to share a row with siblings the way the ring's grid
// cells do. That extra room is also why only the self cell spells out full
// equipment names (spec's worked example does this for 时语 but shows only a
// count for 曹操): a narrow opponent cell has room for a role, a hand count
// and an equipment count, but not "装【青釭剑】【八卦阵】" as well.
QStringList playerCellLines(const TuiResolvers &resolvers, const ClientGameState &state,
                            const QString &name, bool isSelf, int width)
{
    const QVariantMap player = state.player(name);
    const int seat = player.value(QStringLiteral("seat")).toInt();
    const QString generalName = player.value(QStringLiteral("general")).toString();
    const QString kingdom = translatedKingdom(resolvers, generalName);
    const int hp = player.value(QStringLiteral("hp")).toInt();
    const int maxHp = player.value(QStringLiteral("max_hp")).toInt();
    const bool alive = state.isPlayerAlive(name);
    const bool current = !name.isEmpty()
        && name == state.gameValue(QStringLiteral("current_player")).toString();

    // The desktop's own photo shows the general's face and name for every
    // seat, self included -- never the human's screen handle -- so line 1
    // names the general (translated through the same generic wire-token
    // resolver kingdom/role text goes through), not resolvers.player().
    // resolvers.player() is still what the pre-game roster uses, where no
    // general has been picked yet and a human identity is the only one
    // available (see drawWaitingRoom()).
    const QString displayName = generalName.isEmpty()
        ? QString() : (resolvers.name ? resolvers.name(generalName) : generalName);
    // The current-player marker is a line-1 prefix, not a line-3 status word:
    // spec §3.1's worked example shows a bare ▶ in front of 时语's own seat
    // bracket ("▶[1]时语(我)") with nothing added to her (blank) third line.
    const QString prefix = current ? QStringLiteral("▶") : QString();
    const QString suffix = isSelf ? QStringLiteral("(我)") : QString();
    const QString seatTag = QStringLiteral("[%1]").arg(seat);
    const QString hp1 = hpText(hp, maxHp);
    QString kingdomHp = kingdom;
    if (!hp1.isEmpty())
        kingdomHp = kingdomHp.isEmpty() ? hp1 : kingdomHp + QLatin1Char(' ') + hp1;

    // The name is what gets cut when a cell is tight: every other field on
    // line 1 is short and fixed by game rules (a seat number, a one- or
    // two-character kingdom, at most eight hearts), so budgeting the name
    // against what is left over -- rather than building the full line and
    // truncating from the right with tuiPadTo -- keeps a long general name
    // from eating into the hp display instead of its own field.
    const int fixed = tuiDisplayWidth(prefix) + tuiDisplayWidth(seatTag)
        + tuiDisplayWidth(suffix) + (kingdomHp.isEmpty() ? 0 : 1 + tuiDisplayWidth(kingdomHp));
    const int nameWidth = std::max(1, width - fixed);
    QString line1 = prefix + seatTag + tuiElide(displayName, nameWidth) + suffix;
    if (!kingdomHp.isEmpty())
        line1 += QLatin1Char(' ') + kingdomHp;
    line1 = tuiPadTo(line1, width);

    const QString roleCode = player.value(QStringLiteral("role")).toString();
    const QString role = roleCode.isEmpty()
        ? QString() : (resolvers.name ? resolvers.name(roleCode) : roleCode);
    const int handCount = player.value(QStringLiteral("hand_count"),
        state.cardsForPlayer(name, Player::PlaceHand).size()).toInt();
    const QList<int> equip = state.cardsForPlayer(name, Player::PlaceEquip);
    const QList<int> judge = state.cardsForPlayer(name, Player::PlaceDelayedTrick);

    QString line2 = role;
    if (!line2.isEmpty())
        line2 += QLatin1Char(' ');
    line2 += QStringLiteral("手%1").arg(handCount);
    // Only the self cell spells out card names in brackets (spec's worked
    // example does this for 时语's own equipment). A 20-column opponent slot
    // does not have room for even one typical card name in brackets once the
    // role, hand count and a card-count prefix are in -- an earlier version
    // of this code tried names there too and tuiPadTo's final elide cut mid
    // bracket ("判【乐…"), a worse result than just showing how many.
    if (!equip.isEmpty()) {
        if (isSelf) {
            line2 += QStringLiteral(" 装");
            for (int cardId : equip)
                line2 += QStringLiteral("【%1】").arg(cardName(resolvers, cardId));
        } else {
            line2 += QStringLiteral(" 装%1").arg(equip.size());
        }
    }
    if (!judge.isEmpty()) {
        if (isSelf) {
            line2 += QStringLiteral(" 判");
            for (int cardId : judge)
                line2 += QStringLiteral("【%1】").arg(cardName(resolvers, cardId));
        } else {
            line2 += QStringLiteral(" 判%1").arg(judge.size());
        }
    }
    line2 = tuiPadTo(line2, width);

    QStringList markers;
    if (!alive) {
        // A dead player's chain/facing state stops mattering the moment they
        // are dead, so this is the one marker line-3 shows alone.
        markers << QStringLiteral("✖阵亡");
    } else {
        if (player.value(QStringLiteral("flags")).toStringList().contains(QStringLiteral("Global_Dying")))
            markers << QStringLiteral("濒死");
        // faceup is only ever broadcast when it changes (see
        // TuiRenderer::renderPlayers()'s identical default), so a player the
        // server never mentioned is still face up.
        if (!player.value(QStringLiteral("faceup"), true).toBool())
            markers << QStringLiteral("翻面");
        if (player.value(QStringLiteral("chained")).toBool())
            markers << QStringLiteral("连环");
    }
    const QString line3 = tuiPadTo(markers.join(QLatin1Char(' ')), width);

    return {line1, line2, line3};
}

// Greedy word-wrap of hand entries into at most 5 lines of `width` columns.
// Returns the number of lines actually used (always >= 1, even for an empty
// hand, so the caller always has a real row to draw the border against).
int layoutHandLines(const QStringList &entries, int width, QStringList *lines)
{
    lines->clear();
    constexpr int MaxLines = 5;
    qsizetype idx = 0;
    while (idx < entries.size() && lines->size() < MaxLines) {
        QString line;
        while (idx < entries.size()) {
            const QString candidate = line.isEmpty()
                ? entries.at(idx) : line + QStringLiteral("  ") + entries.at(idx);
            if (!line.isEmpty() && tuiDisplayWidth(candidate) > width)
                break;
            line = candidate;
            ++idx;
        }
        lines->append(line);
    }
    if (idx < entries.size()) {
        // More cards than 5 lines can show. Rather than silently dropping
        // them (or inventing a second, hand-only pagination scheme), cut the
        // last line down and name exactly how many are off-screen -- spec
        // §3.2's "超出加 ...(+N)".
        const int hidden = static_cast<int>(entries.size() - idx);
        const QString suffix = QStringLiteral(" …(+%1)").arg(hidden);
        QString &last = (*lines)[lines->size() - 1];
        last = tuiElide(last, std::max(0, width - tuiDisplayWidth(suffix))) + suffix;
    }
    if (lines->isEmpty())
        lines->append(QString());
    return static_cast<int>(lines->size());
}

// One full-width border row: '-' filled, with the given corner glyphs and,
// when junctionCol is a real interior column, a junction glyph marking where
// a vertical divider from the row above/below ends without continuing
// through this row.
QString borderRow(int cols, QChar left, QChar right, QChar junction, int junctionCol)
{
    QString row(cols, QChar(0x2500)); // ─
    if (cols > 0)
        row[0] = left;
    if (cols > 1)
        row[cols - 1] = right;
    if (junctionCol > 0 && junctionCol < cols - 1)
        row[junctionCol] = junction;
    return row;
}

// Overlays " <title> " on a border row the way TuiScreen::drawBox() places
// its own title (left+2), so the frame this function hand-draws reads
// consistently with any standalone box the rest of the client still uses
// drawBox() for.
void putTitle(TuiScreen &screen, int row, int col, const QString &title, int maxWidth)
{
    if (title.isEmpty())
        return;
    const QString framed = QLatin1Char(' ') + tuiElide(title, std::max(0, maxWidth - 2)) + QLatin1Char(' ');
    screen.putText(row, col, framed);
}

// Draws the whole outer frame in one pass: the room and log panes stand
// side by side down to the room/hand separator, and below that the hand and
// input panes each span the FULL board width -- spec §3.1's worked example
// shows this plainly (the "├ 手牌 ...┴...┤" separator's lone "┴" is the
// ghost of the room/log divider terminating, and neither the hand nor the
// input row shows an internal "│" anywhere). TuiBoardGeometry's own rects
// only describe pure content areas (no border rows/cols), by construction --
// hand.cols and input.cols still measure just the room's width, which is
// why this function does not draw the frame from those rects directly and
// instead recomputes the full-width span itself.
void drawFrame(TuiScreen &screen, const TuiBoardGeometry &geom, const QString &roomTitle)
{
    const int rows = screen.rows();
    const int cols = screen.cols();
    const int dividerCol = geom.room.col + geom.room.cols;
    const int roomBottomRow = geom.room.row + geom.room.rows;
    const int handBottomRow = geom.hand.row + geom.hand.rows;
    const int bottomRow = rows - 1;

    screen.putText(0, 0, borderRow(cols, QChar(0x250C), QChar(0x2510), QChar(0x252C), dividerCol));
    screen.putText(roomBottomRow, 0,
        borderRow(cols, QChar(0x251C), QChar(0x2524), QChar(0x2534), dividerCol));
    screen.putText(handBottomRow, 0, borderRow(cols, QChar(0x251C), QChar(0x2524), QChar(0), -1));
    screen.putText(bottomRow, 0, borderRow(cols, QChar(0x2514), QChar(0x2518), QChar(0), -1));

    putTitle(screen, 0, 2, roomTitle, dividerCol - 2);
    putTitle(screen, 0, dividerCol + 2, QStringLiteral("战报"), cols - 1 - (dividerCol + 2));
    putTitle(screen, roomBottomRow, 2, QStringLiteral("手牌"), cols - 4);

    const QChar vertical(0x2502); // │
    for (int row = geom.room.row; row < roomBottomRow; ++row) {
        screen.putText(row, 0, vertical);
        screen.putText(row, dividerCol, vertical);
        screen.putText(row, cols - 1, vertical);
    }
    for (int row = roomBottomRow + 1; row < handBottomRow; ++row) {
        screen.putText(row, 0, vertical);
        screen.putText(row, cols - 1, vertical);
    }
    for (int row = handBottomRow + 1; row < bottomRow; ++row) {
        screen.putText(row, 0, vertical);
        screen.putText(row, cols - 1, vertical);
    }
}

void drawWaitingRoom(TuiScreen &screen, const TuiResolvers &resolvers,
                     const ClientGameState &state, const TuiBoardGeometry &geom)
{
    const QVariantMap setup = state.setup();
    const QString serverName = setup.value(QStringLiteral("server_name")).toString();
    const QString mode = setup.value(QStringLiteral("game_mode")).toString();
    const int total = setup.value(QStringLiteral("player_count")).toInt();
    const int joined = state.playerNames().size();

    int row = geom.room.row;
    const int width = geom.room.cols;
    const int lastRow = geom.room.row + geom.room.rows;
    auto putLine = [&](const QString &text) {
        if (row >= lastRow)
            return;
        screen.putText(row, geom.room.col, tuiPadTo(text, width));
        ++row;
    };

    if (!serverName.isEmpty())
        putLine(QStringLiteral("服务器 %1").arg(serverName));
    putLine(QStringLiteral("模式 %1  人数 %2/%3").arg(mode).arg(joined).arg(total));
    putLine(QString());
    // Readiness is not yet a wire field ClientGameState carries (grep found
    // no per-player "ready" key anywhere in the reducer): standing in for it
    // with "has this player picked a general" is the closest proxy the
    // current protocol offers, and it is a one-line swap for the real flag
    // once the lobby grows one.
    for (const QString &name : state.playerNames()) {
        const QVariantMap player = state.player(name);
        const bool ready = !player.value(QStringLiteral("general")).toString().isEmpty();
        const QString display = resolvers.player ? resolvers.player(name) : name;
        putLine(QStringLiteral("%1 %2").arg(display, ready ? QStringLiteral("已就绪") : QStringLiteral("未就绪")));
    }
}

void drawSelf(TuiScreen &screen, const TuiResolvers &resolvers, const ClientGameState &state,
             const TuiBoardGeometry &geom)
{
    const QString self = state.selfName();
    if (self.isEmpty())
        return;
    const QStringList lines = playerCellLines(resolvers, state, self, true, geom.self.cols);
    // Bold rather than a drawn box: an actual border would have to steal one
    // of the cell's three content rows, and spec §3.1's worked example shows
    // no border glyph around 时语's cell either -- just the ▶ prefix and the
    // (我) suffix, both already plain text so the stripped golden keeps the
    // distinction without the attribute.
    for (int i = 0; i < lines.size() && i < geom.self.rows; ++i)
        screen.putText(geom.self.row + i, geom.self.col, lines.at(i), TuiAttr::Bold, geom.self.cols);
}

TuiAttr cellAttr(const ClientGameState &state, const QString &name)
{
    if (!state.isPlayerAlive(name))
        return TuiAttr::Dead;
    if (state.playerValue(name, QStringLiteral("flags")).toStringList()
            .contains(QStringLiteral("Global_Dying"))) {
        return TuiAttr::Danger;
    }
    if (name == state.gameValue(QStringLiteral("current_player")).toString())
        return TuiAttr::Current;
    return TuiAttr::Normal;
}

// Finds the one opponent-grid cell this page's seats did not claim (spec
// §3.6: capacity always reserves exactly one for the pile) and writes
// "牌堆 N   弃牌 N" into it, centred. Recomputes the same (row, col) grid
// tui-board-layout.cpp's orderedGridCells() fills rather than asking it
// directly -- that helper is file-local -- by reading back where this page's
// slots actually landed and taking the first grid position none of them used.
void drawPile(TuiScreen &screen, const ClientGameState &state, const TuiBoardGeometry &geom, int page)
{
    const int cellRows = (geom.room.rows - CellHeight) / CellHeight;
    if (geom.cellCols <= 0 || cellRows <= 0)
        return;

    QSet<int> occupied;
    for (const TuiSeatSlot &slot : geom.seatSlots) {
        if (slot.page != page)
            continue;
        const int gridRow = (slot.rect.row - geom.room.row) / CellHeight;
        const int gridCol = (slot.rect.col - geom.room.col) / CellWidth;
        occupied.insert(gridRow * 1000 + gridCol);
    }

    for (int r = 0; r < cellRows; ++r) {
        for (int c = 0; c < geom.cellCols; ++c) {
            if (occupied.contains(r * 1000 + c))
                continue;
            const int row = geom.room.row + r * CellHeight + 1;
            const int col = geom.room.col + c * CellWidth;
            const int width = std::min(CellWidth, geom.room.col + geom.room.cols - col);
            const QString text = QStringLiteral("牌堆 %1   弃牌 %2")
                .arg(state.gameValue(QStringLiteral("draw_pile_count")).toInt())
                .arg(state.gameValue(QStringLiteral("discard_pile")).toList().size());
            const int pad = std::max(0, (width - tuiDisplayWidth(text)) / 2);
            screen.putText(row, col + pad, tuiElide(text, std::max(0, width - pad)));
            return;
        }
    }
}

void drawSeatRing(TuiScreen &screen, const TuiResolvers &resolvers, const ClientGameState &state,
                  const TuiBoardGeometry &geom, int page)
{
    const QStringList order = seatOrderFromSelf(state);
    for (const TuiSeatSlot &slot : geom.seatSlots) {
        if (slot.page != page)
            continue;
        if (slot.seatOffset < 1 || slot.seatOffset > order.size())
            continue;
        const QString &name = order.at(slot.seatOffset - 1);
        const QStringList lines = playerCellLines(resolvers, state, name, false, slot.rect.cols);
        const TuiAttr attr = cellAttr(state, name);
        for (int i = 0; i < lines.size() && i < slot.rect.rows; ++i)
            screen.putText(slot.rect.row + i, slot.rect.col, lines.at(i), attr, slot.rect.cols);
    }
    drawPile(screen, state, geom, page);
}

void drawLog(TuiScreen &screen, const TuiBoardGeometry &geom, const QStringList &lines)
{
    const int width = geom.log.cols;
    const int capacity = geom.log.rows;
    const qsizetype start = std::max<qsizetype>(0, lines.size() - capacity);
    for (qsizetype i = start; i < lines.size(); ++i)
        screen.putText(geom.log.row + static_cast<int>(i - start), geom.log.col,
                       tuiPadTo(lines.at(i), width));
}

void drawHand(TuiScreen &screen, const TuiBoardGeometry &geom, const QStringList &lines)
{
    // The hand pane spans the full board width (see drawFrame()'s comment),
    // not geom.hand.cols, which only measures the room's own width.
    const int col = 1;
    const int width = screen.cols() - 2;
    for (int i = 0; i < lines.size() && i < geom.hand.rows; ++i)
        screen.putText(geom.hand.row + i, col, tuiPadTo(lines.at(i), width));
}

// The input pane is two content rows (spec §3.2: "1 行提示 + 1 行輸入").
// docs/tui-board-ui.md §3.1's worked example draws them as a single visual
// row, but that is the doc compressing its illustration for space -- the
// pane-rule table is explicit about two rows, and TuiBoardGeometry.input
// reserves two, so this treats them as separate: row 1 is the prompt (or a
// pending writeError() notice standing in for it, since the pane has no
// third row to give a notice of its own), row 2 is what has been typed so
// far plus a cursor glyph -- a real character, not just an attribute, so a
// monochrome terminal still sees where typing will land.
// col/width are the caller's, not the rect's: input.cols measures only the
// room's width (see drawFrame's note above) while the drawn input row spans
// the full board, so the framed path passes the frame's own insets. The
// below-the-floor path (§3.5) draws no frame and passes the whole width --
// hard-coding col 1 / cols-2 there wasted a column at each edge on exactly
// the size where every column counts.
void drawInput(TuiScreen &screen, const TuiBoardGeometry &geom, const TuiBoardViewState &view,
               int col, int width)
{
    const bool hasNotice = !view.notice.isEmpty();
    screen.putText(geom.input.row, col, tuiPadTo(hasNotice ? view.notice : view.promptLine, width),
                   hasNotice ? TuiAttr::Danger : TuiAttr::Normal);
    if (geom.input.rows < 2)
        return;
    screen.putText(geom.input.row + 1, col, tuiPadTo(view.inputLine, width));
    const int cursorCol = col + std::max(0, std::min(width - 1, view.inputCursorColumn));
    screen.putText(geom.input.row + 1, cursorCol, QStringLiteral("▌"), TuiAttr::Current);
}

// Shared by render() and the public computeGeometry() so the two can never
// disagree about which page an opponent lands on: the hand-line estimate
// depends on the actual hand entries, and duplicating that computation
// instead of factoring it out is exactly how a presenter's idea of "page 2"
// would end up one page off from what render() actually draws there.
// `handLinesOut`, when not null, receives the wrapped hand-pane text so
// render() can draw it without re-running layoutHandLines() a second time;
// computeGeometry()'s callers only ever want the geometry, so they leave it
// null.
TuiBoardGeometry geometryFor(const TuiResolvers &resolvers, const ClientGameState &state,
                            int rows, int cols, QStringList *handLinesOut = nullptr)
{
    QStringList handEntries;
    const QList<int> handCards = state.cardsForPlayer(state.selfName(), Player::PlaceHand);
    for (int cardId : handCards)
        handEntries << QStringLiteral("[%1]%2").arg(handEntries.size() + 1)
            .arg(cardName(resolvers, cardId));
    QStringList handLines;
    const int handInteriorWidth = std::max(1, cols - 2);
    const int handLineCount = layoutHandLines(handEntries, handInteriorWidth, &handLines);
    if (handLinesOut != nullptr)
        *handLinesOut = handLines;

    const int playerCount = std::max(1, static_cast<int>(state.playerNames().size()));
    return tuiComputeBoardGeometry(rows, cols, playerCount, handLineCount);
}

} // namespace

TuiBoardView::TuiBoardView(TuiResolvers resolvers) : m_resolvers(std::move(resolvers))
{
}

void TuiBoardView::render(TuiScreen *screen, const ClientGameState &state,
                          const TuiBoardViewState &view) const
{
    if (screen == nullptr)
        return;

    const int rows = screen->rows();
    const int cols = screen->cols();
    QStringList handLines;
    const TuiBoardGeometry geom = geometryFor(m_resolvers, state, rows, cols, &handLines);

    screen->clear();

    if (!geom.usable) {
        // spec §3.5: "提示行與輸入行是最後才犧牲的兩行" -- below the size
        // floor the room/log/hand panes are what go away, never the
        // prompt/input rows, because they are the only way a player can
        // still act while waiting for the window to grow (an interaction
        // request must still be answerable in this state). The previous
        // implementation had the order backwards: it drew one centred
        // message and returned, which sacrificed prompt+input on every
        // single too-small frame right along with the rest of the board.
        //
        // Reserve up to the last two rows for exactly the same prompt+input
        // drawInput() draws once the board is usable -- fed a minimal
        // geometry, since tuiComputeBoardGeometry() never computed a real
        // `input` rect for this path (bailing out with usable=false is
        // precisely what happened instead) -- and use whatever is left
        // above that for the "too small" message itself.
        const int inputRows = std::min(2, rows);
        const int messageRows = rows - inputRows;
        if (messageRows > 0)
            screen->putText(messageRows / 2, 0, tuiPadTo(geom.unusableReason, cols));
        if (inputRows > 0) {
            TuiBoardGeometry minimal;
            minimal.input = TuiRect{rows - inputRows, 0, inputRows, cols};
            drawInput(*screen, minimal, view, 0, cols);
        }
        return;
    }

    const bool started = isGameStarted(state);
    QString roomTitle = QStringLiteral("房间");
    const QString mode = state.setup().value(QStringLiteral("game_mode")).toString();
    if (!mode.isEmpty())
        roomTitle += QLatin1Char(' ') + mode;
    if (started) {
        roomTitle += QStringLiteral(" 轮次%1").arg(state.gameValue(QStringLiteral("round")).toInt());
        if (geom.pageCount > 1)
            roomTitle += QStringLiteral(" ‹%1/%2›").arg(view.page + 1).arg(geom.pageCount);
    }

    drawFrame(*screen, geom, roomTitle);

    if (started) {
        drawSeatRing(*screen, m_resolvers, state, geom, view.page);
        drawSelf(*screen, m_resolvers, state, geom);
    } else {
        drawWaitingRoom(*screen, m_resolvers, state, geom);
    }

    drawLog(*screen, geom, view.logLines);
    drawHand(*screen, geom, handLines);
    drawInput(*screen, geom, view, 1, screen->cols() - 2);
}

TuiBoardGeometry TuiBoardView::computeGeometry(const ClientGameState &state, int rows, int cols) const
{
    return geometryFor(m_resolvers, state, rows, cols);
}

int TuiBoardView::pageForPlayer(const ClientGameState &state, const TuiBoardGeometry &geometry,
                                const QString &name) const
{
    if (name.isEmpty() || name == state.selfName())
        return 0; // the self cell is fixed at the bottom of the room; it never pages
    if (!isGameStarted(state))
        return 0; // no seat ring exists before GAME_START

    const QStringList order = seatOrderFromSelf(state);
    const qsizetype index = order.indexOf(name);
    if (index < 0)
        return 0; // not a seated opponent -- nothing to page to

    const int seatOffset = static_cast<int>(index) + 1;
    for (const TuiSeatSlot &slot : geometry.seatSlots) {
        if (slot.seatOffset == seatOffset)
            return slot.page;
    }
    return 0;
}
