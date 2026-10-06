#ifndef CONTROLLER_TEXT_ENTRY_H
#define CONTROLLER_TEXT_ENTRY_H

#include <QDialog>
#include <QList>
#include <QPointer>
#include <QString>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTextEdit;
class QWidget;

// Button-only text entry for a gamepad. The target is changed only after the
// user activates Done; closing/cancelling the dialog leaves it untouched.
class ControllerTextEntry final : public QDialog
{
public:
    explicit ControllerTextEntry(QWidget *target, QWidget *parent = nullptr);

    // Opens a modal editor only for editable QLineEdit/QTextEdit/QPlainTextEdit.
    // The target is guarded while the nested dialog loop is active.
    static void edit(QWidget *target, QWidget *parent);

private:
    bool appendText(const QString &text);
    void appendCharacter(QChar character);
    void moveCursor(bool right);
    void backspace();
    void clearBuffer();
    void toggleCase();
    void appendHexDigit(QChar digit);
    void clearCodePoint();
    void backspaceCodePoint();
    void insertCodePoint();
    void refreshPreview();
    void commit();
    bool targetIsEditable() const;
    int targetMaxLength() const;

    QPointer<QWidget> m_target;
    QPlainTextEdit *m_preview = nullptr;
    QLabel *m_cursorStatus = nullptr;
    QLineEdit *m_codePointPreview = nullptr;
    QLabel *m_codePointStatus = nullptr;
    QLabel *m_validationMessage = nullptr;
    QString m_buffer;
    QString m_codePointHex;
    QList<QPushButton *> m_letterButtons;
    int m_cursor = 0;
    int m_maxLength = -1;
    bool m_multiline = false;
    bool m_upperCase = false;
};

#endif // CONTROLLER_TEXT_ENTRY_H
