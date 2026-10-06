#include "controller-text-entry.h"

#include <QGridLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QScrollArea>
#include <QTextCursor>
#include <QTextEdit>
#include <QValidator>
#include <QVBoxLayout>

namespace {

QPushButton *makeButton(const QString &text, const QString &objectName, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setObjectName(objectName);
    button->setAutoDefault(false);
    button->setDefault(false);
    button->setFocusPolicy(Qt::StrongFocus);
    return button;
}

bool isHighSurrogate(QChar character)
{
    return character.isHighSurrogate();
}

bool isLowSurrogate(QChar character)
{
    return character.isLowSurrogate();
}

} // namespace

ControllerTextEntry::ControllerTextEntry(QWidget *target, QWidget *parent)
    : QDialog(parent), m_target(target)
{
    setObjectName(QStringLiteral("controllerTextEntryDialog"));
    setWindowTitle(tr("On-screen text entry"));
    setModal(true);
    // Native themes can draw an almost invisible focus cue on these compact
    // buttons. Give controller navigation an explicit, high-contrast marker.
    setStyleSheet(QStringLiteral("QPushButton:focus { border: 2px solid #9a6700; background-color: #fff1ad; color: #1a1a1a; }"));
    resize(760, 690);
    QScreen *screen = parent && parent->screen() ? parent->screen() : QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        resize(qMax(160, qMin(760, available.width() - 24)),
               qMax(200, qMin(690, available.height() - 24)));
    }

    if (auto *lineEdit = qobject_cast<QLineEdit *>(target)) {
        m_buffer = lineEdit->text();
        m_cursor = lineEdit->cursorPosition();
        m_maxLength = lineEdit->maxLength();
    } else if (auto *textEdit = qobject_cast<QTextEdit *>(target)) {
        m_buffer = textEdit->toPlainText();
        m_cursor = textEdit->textCursor().position();
        m_multiline = true;
    } else if (auto *plainTextEdit = qobject_cast<QPlainTextEdit *>(target)) {
        m_buffer = plainTextEdit->toPlainText();
        m_cursor = plainTextEdit->textCursor().position();
        m_multiline = true;
    }
    m_cursor = qBound(0, m_cursor, static_cast<int>(m_buffer.size()));

    auto *layout = new QVBoxLayout(this);
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("textEntryScrollArea"));
    scrollArea->setWidgetResizable(true);
    auto *content = new QWidget(scrollArea);
    auto *contentLayout = new QVBoxLayout(content);
    auto *bufferLabel = new QLabel(tr("Text buffer (read-only preview)"), this);
    bufferLabel->setObjectName(QStringLiteral("textBufferLabel"));
    // Keep feedback visible while focus scrolls through the character grid.
    // Otherwise Unicode controls can scroll the preview's first line away.
    layout->addWidget(bufferLabel);

    m_preview = new QPlainTextEdit(this);
    m_preview->setObjectName(QStringLiteral("textBufferPreview"));
    m_preview->setReadOnly(true);
    m_preview->setTabChangesFocus(true);
    m_preview->setMaximumHeight(76);
    m_preview->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_preview);

    m_cursorStatus = new QLabel(this);
    m_cursorStatus->setObjectName(QStringLiteral("textCursorPosition"));
    layout->addWidget(m_cursorStatus);
    m_validationMessage = new QLabel(this);
    m_validationMessage->setObjectName(QStringLiteral("textValidationMessage"));
    m_validationMessage->setTextFormat(Qt::PlainText);
    m_validationMessage->setWordWrap(true);
    m_validationMessage->hide();
    layout->addWidget(m_validationMessage);

    auto *cursorControls = new QHBoxLayout;
    auto *left = makeButton(tr("Cursor left"), QStringLiteral("cursorLeftButton"), this);
    auto *right = makeButton(tr("Cursor right"), QStringLiteral("cursorRightButton"), this);
    auto *backspaceButton = makeButton(tr("Backspace"), QStringLiteral("backspaceButton"), this);
    auto *clearButton = makeButton(tr("Clear"), QStringLiteral("clearTextButton"), this);
    auto *caseButton = makeButton(tr("Case: lower"), QStringLiteral("toggleCaseButton"), this);
    cursorControls->addWidget(left);
    cursorControls->addWidget(right);
    cursorControls->addWidget(backspaceButton);
    cursorControls->addWidget(clearButton);
    cursorControls->addWidget(caseButton);
    contentLayout->addLayout(cursorControls);
    connect(left, &QPushButton::clicked, this, [this]() { moveCursor(false); });
    connect(right, &QPushButton::clicked, this, [this]() { moveCursor(true); });
    connect(backspaceButton, &QPushButton::clicked, this, [this]() { backspace(); });
    connect(clearButton, &QPushButton::clicked, this, [this]() { clearBuffer(); });
    connect(caseButton, &QPushButton::clicked, this, [this, caseButton]() {
        toggleCase();
        caseButton->setText(m_upperCase ? tr("Case: upper") : tr("Case: lower"));
    });

    auto *characters = new QGridLayout;
    characters->setObjectName(QStringLiteral("asciiCharacterGrid"));
    const QStringList rows = {
        QStringLiteral("0123456789"),
        QStringLiteral("qwertyuiop"),
        QStringLiteral("asdfghjkl"),
        QStringLiteral("zxcvbnm"),
        QStringLiteral("!@#$%^&*()"),
        QStringLiteral("-_+={}[]\\|"),
        QStringLiteral(";:'\"<>,.?/`~")
    };
    for (int row = 0; row < rows.size(); ++row) {
        const QString &charactersInRow = rows.at(row);
        for (int column = 0; column < charactersInRow.size(); ++column) {
            const QChar character = charactersInRow.at(column);
            const QString code = QStringLiteral("character_%1")
                                     .arg(static_cast<uint>(character.unicode()), 4, 16, QLatin1Char('0'));
            // QPushButton uses ampersands for mnemonics; show a literal '&'
            // without changing the character inserted into the text buffer.
            const QString label = character == QLatin1Char('&') ? QStringLiteral("&&") : QString(character);
            auto *button = makeButton(label, code, this);
            button->setMinimumSize(48, 40);
            if (character.isLetter()) m_letterButtons.append(button);
            characters->addWidget(button, row, column);
            connect(button, &QPushButton::clicked, this, [this, character]() {
                appendCharacter(character);
            });
        }
    }
    auto *spaceButton = makeButton(tr("Space"), QStringLiteral("insertSpaceButton"), this);
    spaceButton->setMinimumHeight(40);
    characters->addWidget(spaceButton, rows.size(), 0, 1, 4);
    connect(spaceButton, &QPushButton::clicked, this, [this]() { appendText(QStringLiteral(" ")); });
    if (m_multiline) {
        auto *newlineButton = makeButton(tr("Newline"), QStringLiteral("insertNewlineButton"), this);
        newlineButton->setMinimumHeight(40);
        characters->addWidget(newlineButton, rows.size(), 4, 1, 4);
        connect(newlineButton, &QPushButton::clicked, this, [this]() { appendText(QStringLiteral("\n")); });
    }
    contentLayout->addLayout(characters);

    auto *unicodeGroup = new QGroupBox(tr("Unicode code point"), this);
    unicodeGroup->setObjectName(QStringLiteral("unicodeEntryGroup"));
    auto *unicodeLayout = new QVBoxLayout(unicodeGroup);
    auto *unicodeControls = new QHBoxLayout;
    m_codePointPreview = new QLineEdit(unicodeGroup);
    m_codePointPreview->setObjectName(QStringLiteral("unicodeCodePointPreview"));
    m_codePointPreview->setReadOnly(true);
    m_codePointPreview->setFocusPolicy(Qt::NoFocus);
    m_codePointPreview->setPlaceholderText(tr("Enter hexadecimal digits, e.g. 4E2D"));
    unicodeControls->addWidget(m_codePointPreview);
    auto *hexBackspace = makeButton(tr("Backspace"), QStringLiteral("unicodeBackspaceButton"), unicodeGroup);
    auto *hexClear = makeButton(tr("Clear"), QStringLiteral("unicodeClearButton"), unicodeGroup);
    auto *insertUnicode = makeButton(tr("Insert U+"), QStringLiteral("insertUnicodeButton"), unicodeGroup);
    unicodeControls->addWidget(hexBackspace);
    unicodeControls->addWidget(hexClear);
    unicodeControls->addWidget(insertUnicode);
    unicodeLayout->addLayout(unicodeControls);
    m_codePointStatus = new QLabel(tr("Use the buttons below to enter 1–6 hex digits."), unicodeGroup);
    m_codePointStatus->setObjectName(QStringLiteral("unicodeValidationStatus"));
    unicodeLayout->addWidget(m_codePointStatus);
    auto *hexGrid = new QGridLayout;
    hexGrid->setObjectName(QStringLiteral("unicodeHexGrid"));
    const QString hexDigits = QStringLiteral("0123456789ABCDEF");
    for (int index = 0; index < hexDigits.size(); ++index) {
        const QChar digit = hexDigits.at(index);
        auto *button = makeButton(QString(digit),
            QStringLiteral("unicodeHex_%1").arg(digit), unicodeGroup);
        button->setMinimumSize(48, 40);
        hexGrid->addWidget(button, index / 8, index % 8);
        connect(button, &QPushButton::clicked, this, [this, digit]() { appendHexDigit(digit); });
    }
    unicodeLayout->addLayout(hexGrid);
    contentLayout->addWidget(unicodeGroup);
    connect(hexBackspace, &QPushButton::clicked, this, [this]() { backspaceCodePoint(); });
    connect(hexClear, &QPushButton::clicked, this, [this]() { clearCodePoint(); });
    connect(insertUnicode, &QPushButton::clicked, this, [this]() { insertCodePoint(); });

    scrollArea->setWidget(content);
    layout->addWidget(scrollArea, 1);

    auto *actions = new QHBoxLayout;
    actions->addStretch();
    auto *cancelButton = makeButton(tr("Cancel"), QStringLiteral("cancelTextEntryButton"), this);
    auto *doneButton = makeButton(tr("Done"), QStringLiteral("doneTextEntryButton"), this);
    actions->addWidget(cancelButton);
    actions->addWidget(doneButton);
    layout->addLayout(actions);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(doneButton, &QPushButton::clicked, this, [this]() { commit(); });

    if (m_target) {
        connect(m_target, &QObject::destroyed, this, &QDialog::reject);
    }
    refreshPreview();
}

void ControllerTextEntry::edit(QWidget *target, QWidget *parent)
{
    auto *lineEdit = qobject_cast<QLineEdit *>(target);
    auto *textEdit = qobject_cast<QTextEdit *>(target);
    auto *plainTextEdit = qobject_cast<QPlainTextEdit *>(target);
    if ((!lineEdit || lineEdit->isReadOnly())
        && (!textEdit || textEdit->isReadOnly())
        && (!plainTextEdit || plainTextEdit->isReadOnly())) return;

    QPointer<QWidget> targetGuard(target);
    auto *dialogWidget = new ControllerTextEntry(target, parent);
    QPointer<ControllerTextEntry> dialog(dialogWidget);
    dialogWidget->exec();
    if (targetGuard && targetGuard->isVisible() && targetGuard->isEnabled())
        targetGuard->setFocus(Qt::OtherFocusReason);
    if (dialog) delete dialog.data();
}

bool ControllerTextEntry::targetIsEditable() const
{
    if (!m_target) return false;
    if (const auto *lineEdit = qobject_cast<const QLineEdit *>(m_target.data()))
        return !lineEdit->isReadOnly();
    if (const auto *textEdit = qobject_cast<const QTextEdit *>(m_target.data()))
        return !textEdit->isReadOnly();
    if (const auto *plainTextEdit = qobject_cast<const QPlainTextEdit *>(m_target.data()))
        return !plainTextEdit->isReadOnly();
    return false;
}

int ControllerTextEntry::targetMaxLength() const
{
    if (const auto *lineEdit = qobject_cast<const QLineEdit *>(m_target.data()))
        return lineEdit->maxLength();
    return -1;
}

bool ControllerTextEntry::appendText(const QString &text)
{
    if (text.isEmpty()) return false;
    const int currentMaxLength = targetMaxLength();
    const int maxLength = currentMaxLength >= 0 ? currentMaxLength : m_maxLength;
    if (maxLength >= 0 && m_buffer.size() + text.size() > maxLength) {
        m_validationMessage->setText(tr("Maximum text length reached."));
        m_validationMessage->show();
        return false;
    }
    m_buffer.insert(m_cursor, text);
    m_cursor += static_cast<int>(text.size());
    refreshPreview();
    return true;
}

void ControllerTextEntry::appendCharacter(QChar character)
{
    if (character.isLetter())
        character = m_upperCase ? character.toUpper() : character.toLower();
    appendText(QString(character));
}

void ControllerTextEntry::moveCursor(bool right)
{
    if (right) {
        if (m_cursor < m_buffer.size()) {
            if (isHighSurrogate(m_buffer.at(m_cursor)) && m_cursor + 1 < m_buffer.size()
                && isLowSurrogate(m_buffer.at(m_cursor + 1))) m_cursor += 2;
            else ++m_cursor;
        }
    } else if (m_cursor > 0) {
        if (isLowSurrogate(m_buffer.at(m_cursor - 1)) && m_cursor > 1
            && isHighSurrogate(m_buffer.at(m_cursor - 2))) m_cursor -= 2;
        else --m_cursor;
    }
    refreshPreview();
}

void ControllerTextEntry::backspace()
{
    if (m_cursor == 0) return;
    int start = m_cursor - 1;
    if (isLowSurrogate(m_buffer.at(start)) && start > 0
        && isHighSurrogate(m_buffer.at(start - 1))) --start;
    m_buffer.remove(start, m_cursor - start);
    m_cursor = start;
    refreshPreview();
}

void ControllerTextEntry::clearBuffer()
{
    m_buffer.clear();
    m_cursor = 0;
    refreshPreview();
}

void ControllerTextEntry::toggleCase()
{
    m_upperCase = !m_upperCase;
    for (QPushButton *button : m_letterButtons)
        button->setText(m_upperCase ? button->text().toUpper() : button->text().toLower());
}

void ControllerTextEntry::appendHexDigit(QChar digit)
{
    if (m_codePointHex.size() >= 6) return;
    m_codePointHex.append(digit.toUpper());
    m_codePointPreview->setText(m_codePointHex);
    m_codePointStatus->setText(tr("Code point: U+%1").arg(m_codePointHex));
}

void ControllerTextEntry::clearCodePoint()
{
    m_codePointHex.clear();
    m_codePointPreview->clear();
    m_codePointStatus->setText(tr("Use the buttons below to enter 1–6 hex digits."));
}

void ControllerTextEntry::backspaceCodePoint()
{
    if (m_codePointHex.isEmpty()) return;
    m_codePointHex.chop(1);
    m_codePointPreview->setText(m_codePointHex);
    m_codePointStatus->setText(m_codePointHex.isEmpty()
        ? tr("Use the buttons below to enter 1–6 hex digits.")
        : tr("Code point: U+%1").arg(m_codePointHex));
}

void ControllerTextEntry::insertCodePoint()
{
    bool ok = false;
    const uint value = m_codePointHex.toUInt(&ok, 16);
    if (!ok || m_codePointHex.isEmpty() || value > 0x10FFFF
        || (value >= 0xD800 && value <= 0xDFFF)) {
        m_codePointStatus->setText(tr("Invalid Unicode scalar value."));
        return;
    }
    const char32_t scalar = static_cast<char32_t>(value);
    if (!appendText(QString::fromUcs4(&scalar, 1))) {
        m_codePointStatus->setText(tr("Could not insert U+%1; check the field length.").arg(m_codePointHex));
        return;
    }
    m_codePointStatus->setText(tr("Inserted U+%1.").arg(m_codePointHex));
    m_codePointHex.clear();
    m_codePointPreview->clear();
}

void ControllerTextEntry::refreshPreview()
{
    QString visibleText = m_buffer;
    bool hiddenPassword = false;
    if (const auto *lineEdit = qobject_cast<const QLineEdit *>(m_target.data())) {
        if (lineEdit->echoMode() == QLineEdit::NoEcho) {
            visibleText.clear();
            hiddenPassword = true;
        } else if (lineEdit->echoMode() != QLineEdit::Normal) {
            const auto passwordCharacter = static_cast<ushort>(
                lineEdit->style()->styleHint(QStyle::SH_LineEdit_PasswordCharacter, nullptr, lineEdit));
            visibleText = QString(m_buffer.size(), QChar(passwordCharacter));
            hiddenPassword = true;
        }
    }
    m_preview->setPlainText(visibleText);
    QTextCursor cursor = m_preview->textCursor();
    cursor.setPosition(qBound(0, m_cursor, static_cast<int>(visibleText.size())));
    m_preview->setTextCursor(cursor);
    m_cursorStatus->setText(hiddenPassword
        ? tr("Password text is hidden.")
        : tr("Cursor position: %1 / %2").arg(m_cursor).arg(m_buffer.size()));
    if (m_validationMessage) {
        m_validationMessage->clear();
        m_validationMessage->hide();
    }
}

void ControllerTextEntry::commit()
{
    if (!targetIsEditable()) {
        reject();
        return;
    }
    QPointer<ControllerTextEntry> self(this);
    if (auto *lineEdit = qobject_cast<QLineEdit *>(m_target.data())) {
        QPointer<QLineEdit> target(lineEdit);
        const int maxLength = lineEdit->maxLength();
        if (maxLength >= 0 && m_buffer.size() > maxLength) {
            m_validationMessage->setText(tr("Text exceeds this field's maximum length."));
            m_validationMessage->show();
            return;
        }

        // Validate in a temporary widget configured like the target. This
        // avoids exposing an invalid intermediate value through target signals.
        QLineEdit probe;
        probe.setLocale(lineEdit->locale());
        probe.setMaxLength(maxLength);
        probe.setInputMask(lineEdit->inputMask());
        if (lineEdit->validator()) probe.setValidator(lineEdit->validator());
        probe.setText(m_buffer);
        if (!probe.hasAcceptableInput() || probe.text() != m_buffer) {
            m_validationMessage->setText(tr("Text does not satisfy this field's input mask or validator."));
            m_validationMessage->show();
            return;
        }

        const QString value = probe.text();
        target->setText(value);
        if (!self) return;
        if (!target) return;
        target->setCursorPosition(qBound(0, m_cursor, static_cast<int>(value.size())));
        if (!self || !target) return;
    } else if (auto *textEdit = qobject_cast<QTextEdit *>(m_target.data())) {
        QPointer<QTextEdit> target(textEdit);
        textEdit->setPlainText(m_buffer);
        if (!self) return;
        if (!target) return;
        QTextCursor cursor = target->textCursor();
        cursor.setPosition(qBound(0, m_cursor, static_cast<int>(m_buffer.size())));
        target->setTextCursor(cursor);
        if (!self || !target) return;
    } else if (auto *plainTextEdit = qobject_cast<QPlainTextEdit *>(m_target.data())) {
        QPointer<QPlainTextEdit> target(plainTextEdit);
        plainTextEdit->setPlainText(m_buffer);
        if (!self) return;
        if (!target) return;
        QTextCursor cursor = target->textCursor();
        cursor.setPosition(qBound(0, m_cursor, static_cast<int>(m_buffer.size())));
        target->setTextCursor(cursor);
        if (!self || !target) return;
    }
    accept();
}
