#include "tui-line-editor.h"
#include "tui-text-width.h"

namespace {

constexpr int kMaxLineLength = 16384; // matches tui-input.cpp's classic-mode cap

bool isCsiFinalByte(unsigned char byte)
{
    return byte >= 0x40 && byte <= 0x7E;
}

bool isUtf8ContinuationByte(unsigned char byte)
{
    return (byte & 0xC0) == 0x80;
}

// Length (including the lead byte) of the UTF-8 sequence this byte starts,
// or 0 if it cannot start one (a stray continuation or invalid byte).
int utf8SequenceLength(unsigned char byte)
{
    if ((byte & 0x80) == 0x00)
        return 1;
    if ((byte & 0xE0) == 0xC0)
        return 2;
    if ((byte & 0xF0) == 0xE0)
        return 3;
    if ((byte & 0xF8) == 0xF0)
        return 4;
    return 0;
}

} // namespace

QVector<TuiKeyEvent> TuiKeyDecoder::feed(const QByteArray &bytes)
{
    QVector<TuiKeyEvent> events;
    for (const char rawByte : bytes)
        processByte(static_cast<unsigned char>(rawByte), events);
    return events;
}

QVector<TuiKeyEvent> TuiKeyDecoder::resolvePendingEscape()
{
    QVector<TuiKeyEvent> events;
    // Only a *lone* ESC resolves this way. Once "[" (or anything else) has
    // arrived, the sequence is either progressing through Csi -- which
    // waits for its final byte with no timeout, exactly like the
    // half-a-sequence test above -- or it was already resolved on the spot
    // by processByte's non-"[" fallback.
    if (m_state == State::Escape) {
        events.append(TuiKeyEvent{TuiKey::Escape, QString()});
        m_state = State::Ground;
    }
    return events;
}

void TuiKeyDecoder::processByte(unsigned char byte, QVector<TuiKeyEvent> &events)
{
    switch (m_state) {
    case State::Ground: {
        if (byte == 0x1B) {
            m_state = State::Escape;
            return;
        }
        // Ctrl+C: decoded defensively even though TuiTerminal leaves ISIG on
        // (see tui-terminal.cpp), which normally turns Ctrl+C into a real
        // SIGINT before it ever reaches read(). Anything that can still hand
        // this decoder a literal 0x03 -- a test, a pty without ISIG, input
        // replayed from a file -- must not have it come out as a Backspace
        // or a stray control character.
        if (byte == 0x03) {
            events.append(TuiKeyEvent{TuiKey::Interrupt, QString()});
            return;
        }
        if (byte == 0x7F || byte == 0x08) {
            events.append(TuiKeyEvent{TuiKey::Backspace, QString()});
            return;
        }
        if (byte == 0x0D || byte == 0x0A) {
            events.append(TuiKeyEvent{TuiKey::Enter, QString()});
            return;
        }
        if (byte == 0x09) {
            events.append(TuiKeyEvent{TuiKey::Tab, QString()});
            return;
        }
        // Readline-style line-editing shortcuts. Ctrl+A/E map cleanly onto
        // the Home/End this decoder already has to produce for the arrow-key
        // equivalents. Ctrl+U/K/W (kill-to-start, kill-to-end, kill-word)
        // have no faithful match among the fixed TuiKey values above -- each
        // deletes a different, cursor-relative span that no existing key
        // reproduces -- so rather than overload an unrelated key (Escape's
        // job is "abort", not "edit") this decoder swallows them silently
        // for now instead of leaking a raw control byte into the line as
        // text. A later task can grow TuiKey if it wants them back.
        if (byte == 0x01) {
            events.append(TuiKeyEvent{TuiKey::Home, QString()});
            return;
        }
        if (byte == 0x05) {
            events.append(TuiKeyEvent{TuiKey::End, QString()});
            return;
        }
        if (byte == 0x15 || byte == 0x0B || byte == 0x17)
            return;
        if (byte < 0x20)
            return; // other control bytes: not in the table, dropped rather than guessed at
        if (byte < 0x80) {
            events.append(TuiKeyEvent{TuiKey::Char, QString(QChar(byte))});
            return;
        }
        const int length = utf8SequenceLength(byte);
        if (length <= 1) {
            return; // stray continuation byte or invalid lead byte: drop it
        }
        m_state = State::Utf8;
        m_utf8Buffer = QByteArray(1, static_cast<char>(byte));
        m_utf8Expected = length;
        return;
    }
    case State::Escape: {
        if (byte == '[') {
            m_state = State::Csi;
            m_csiParams.clear();
            return;
        }
        // Not a CSI: the parked ESC really was the Escape key, and this byte
        // starts whatever comes next. Resolve the ESC now and let the byte
        // fall through Ground normally instead of being swallowed.
        m_state = State::Ground;
        events.append(TuiKeyEvent{TuiKey::Escape, QString()});
        processByte(byte, events);
        return;
    }
    case State::Csi: {
        if (isCsiFinalByte(byte)) {
            finishCsi(byte, events);
            m_state = State::Ground;
            m_csiParams.clear();
            return;
        }
        m_csiParams.append(static_cast<char>(byte));
        return;
    }
    case State::Utf8: {
        if (!isUtf8ContinuationByte(byte)) {
            // Broken sequence: drop what was buffered and reprocess this
            // byte fresh rather than emitting garbage text.
            m_state = State::Ground;
            m_utf8Buffer.clear();
            processByte(byte, events);
            return;
        }
        m_utf8Buffer.append(static_cast<char>(byte));
        if (m_utf8Buffer.size() < m_utf8Expected)
            return;
        events.append(TuiKeyEvent{TuiKey::Char, QString::fromUtf8(m_utf8Buffer)});
        m_state = State::Ground;
        m_utf8Buffer.clear();
        return;
    }
    }
}

void TuiKeyDecoder::finishCsi(unsigned char finalByte, QVector<TuiKeyEvent> &events)
{
    TuiKey key = TuiKey::None;
    switch (finalByte) {
    case 'D': key = TuiKey::Left; break;
    case 'C': key = TuiKey::Right; break;
    case 'H': key = TuiKey::Home; break;
    case 'F': key = TuiKey::End; break;
    case 'A': key = TuiKey::Up; break;
    case 'B': key = TuiKey::Down; break;
    case '~':
        if (m_csiParams == "1" || m_csiParams == "4")
            key = (m_csiParams == "1") ? TuiKey::Home : TuiKey::End;
        else if (m_csiParams == "3")
            key = TuiKey::Delete;
        else if (m_csiParams == "5")
            key = TuiKey::PageUp;
        else if (m_csiParams == "6")
            key = TuiKey::PageDown;
        break;
    default:
        break;
    }
    if (key != TuiKey::None)
        events.append(TuiKeyEvent{key, QString()});
    // Anything else is a recognized-shape-but-unmapped (or malformed) CSI
    // sequence -- dropped rather than leaked into the line as raw escape
    // bytes.
}

void TuiLineEditor::setCompleter(std::function<QString(const QString &, QStringList *)> completer)
{
    m_completer = std::move(completer);
}

void TuiLineEditor::insertText(const QString &text)
{
    if (text.isEmpty())
        return;
    if (m_text.size() + text.size() > kMaxLineLength)
        return; // dropped whole, not truncated mid-character -- see tui-input.cpp
    m_text.insert(m_cursor, text);
    m_cursor += text.size();
}

int TuiLineEditor::cursorColumn() const
{
    return tuiDisplayWidth(m_text.left(m_cursor));
}

void TuiLineEditor::reset()
{
    m_text.clear();
    m_cursor = 0;
    m_history.clear();
    m_historyIndex = 0;
}

bool TuiLineEditor::handle(const TuiKeyEvent &event, QString *submitted)
{
    if (submitted != nullptr)
        submitted->clear();

    switch (event.key) {
    case TuiKey::Char:
        if (event.text.isEmpty())
            return false;
        insertText(event.text);
        return true;

    case TuiKey::Left:
        if (m_cursor <= 0)
            return false;
        --m_cursor;
        return true;

    case TuiKey::Right:
        if (m_cursor >= m_text.size())
            return false;
        ++m_cursor;
        return true;

    case TuiKey::Home:
        if (m_cursor == 0)
            return false;
        m_cursor = 0;
        return true;

    case TuiKey::End:
        if (m_cursor == m_text.size())
            return false;
        m_cursor = m_text.size();
        return true;

    case TuiKey::Backspace:
        if (m_cursor <= 0)
            return false;
        m_text.remove(m_cursor - 1, 1);
        --m_cursor;
        return true;

    case TuiKey::Delete:
        if (m_cursor >= m_text.size())
            return false;
        m_text.remove(m_cursor, 1);
        return true;

    case TuiKey::Enter: {
        const QString line = m_text;
        if (submitted != nullptr)
            *submitted = line;
        if (!line.isEmpty())
            m_history.append(line);
        m_text.clear();
        m_cursor = 0;
        m_historyIndex = m_history.size();
        return true;
    }

    case TuiKey::Tab: {
        if (!m_completer)
            return false;
        QStringList matches;
        const QString next = m_completer(m_text, &matches);
        const bool changed = next != m_text;
        m_text = next;
        m_cursor = m_text.size();
        return changed || !matches.isEmpty();
    }

    case TuiKey::Escape:
        // Deliberate: clears the line and submits nothing. A stray Escape
        // keypress -- a mistimed press, a terminal quirk, a split sequence
        // this decoder misread -- must never turn into a wire effect.
        // Cancelling an in-flight interaction stays the explicit "/cancel" a
        // player types; if Escape is ever wired to submit, or to fall
        // through into some other handler, every accidental Escape becomes
        // a silent protocol action the player never asked for.
        m_text.clear();
        m_cursor = 0;
        m_historyIndex = m_history.size();
        return true;

    case TuiKey::Up:
        if (m_history.isEmpty() || m_historyIndex <= 0)
            return false;
        --m_historyIndex;
        m_text = m_history.at(m_historyIndex);
        m_cursor = m_text.size();
        return true;

    case TuiKey::Down:
        if (m_historyIndex >= m_history.size())
            return false;
        ++m_historyIndex;
        m_text = (m_historyIndex == m_history.size()) ? QString() : m_history.at(m_historyIndex);
        m_cursor = m_text.size();
        return true;

    case TuiKey::None:
    case TuiKey::PageUp:
    case TuiKey::PageDown:
    case TuiKey::Interrupt:
        // Not line-editing concerns: PageUp/PageDown scroll the board and
        // Interrupt is the disconnect path, both handled above this class.
        return false;
    }
    return false;
}
