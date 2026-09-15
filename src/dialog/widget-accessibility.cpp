#include "widget-accessibility.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAccessible>
#include <QApplication>
#include <QChildEvent>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QTimer>
#include <QVariant>
#include <QWidget>

namespace {

constexpr auto FallbackNameProperty = "_qsan_accessibility_fallback_name";

QString plainAccessibleText(QString text)
{
    text = text.trimmed();
    if (Qt::mightBeRichText(text))
        text = QTextDocumentFragment::fromHtml(text).toPlainText();

    // In Qt label/button text, a single ampersand marks a mnemonic and a
    // doubled ampersand renders as one literal ampersand.
    const QChar escapedAmpersand(0x1f);
    text.replace(QStringLiteral("&&"), QString(escapedAmpersand));
    text.remove(QLatin1Char('&'));
    text.replace(escapedAmpersand, QLatin1Char('&'));
    return text.trimmed();
}

QString effectiveAccessibleName(QWidget *widget)
{
    QAccessibleInterface *interface = QAccessible::queryAccessibleInterface(widget);
    return interface ? interface->text(QAccessible::Name).trimmed() : QString();
}

QString fallbackNameFor(QWidget *widget)
{
    if (QAbstractButton *button = qobject_cast<QAbstractButton *>(widget)) {
        const QString text = plainAccessibleText(button->text());
        return text.isEmpty() ? plainAccessibleText(button->toolTip()) : text;
    }

    if (QLineEdit *lineEdit = qobject_cast<QLineEdit *>(widget)) {
        const QString placeholder = plainAccessibleText(lineEdit->placeholderText());
        return placeholder.isEmpty() ? plainAccessibleText(lineEdit->toolTip()) : placeholder;
    }

    if (QComboBox *comboBox = qobject_cast<QComboBox *>(widget))
        return plainAccessibleText(comboBox->toolTip());

    if (QAbstractSpinBox *spinBox = qobject_cast<QAbstractSpinBox *>(widget))
        return plainAccessibleText(spinBox->toolTip());

    if (QTextEdit *textEdit = qobject_cast<QTextEdit *>(widget)) {
        const QString placeholder = plainAccessibleText(textEdit->placeholderText());
        return placeholder.isEmpty() ? plainAccessibleText(textEdit->toolTip()) : placeholder;
    }

    if (QPlainTextEdit *textEdit = qobject_cast<QPlainTextEdit *>(widget)) {
        const QString placeholder = plainAccessibleText(textEdit->placeholderText());
        return placeholder.isEmpty() ? plainAccessibleText(textEdit->toolTip()) : placeholder;
    }

    if (QDialog *dialog = qobject_cast<QDialog *>(widget))
        return plainAccessibleText(dialog->windowTitle());

    return QString();
}

void applyFallbackName(QWidget *widget)
{
    if (!widget)
        return;

    const QString ownedName = widget->property(FallbackNameProperty).toString();
    if (!ownedName.isEmpty()) {
        // Update only a name still owned by this helper. An application change
        // to accessibleName takes ownership and is left untouched.
        if (widget->accessibleName() != ownedName) {
            widget->setProperty(FallbackNameProperty, QVariant());
            return;
        }

        const QString replacement = fallbackNameFor(widget);
        if (replacement.isEmpty()) {
            widget->setAccessibleName(QString());
            widget->setProperty(FallbackNameProperty, QVariant());
        } else if (replacement != ownedName) {
            widget->setAccessibleName(replacement);
            widget->setProperty(FallbackNameProperty, replacement);
        }
        return;
    }

    // Respect names supplied by Qt's widget interface, including a QLabel
    // buddy relation, and names explicitly assigned by the application.
    if (!widget->accessibleName().trimmed().isEmpty()
        || !effectiveAccessibleName(widget).isEmpty())
        return;

    const QString name = fallbackNameFor(widget);
    if (!name.isEmpty()) {
        widget->setAccessibleName(name);
        widget->setProperty(FallbackNameProperty, name);
    }
}

class WidgetAccessibilityFilter final : public QObject
{
public:
    explicit WidgetAccessibilityFilter(QObject *parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        QWidget *widget = qobject_cast<QWidget *>(watched);
        if (!widget)
            return QObject::eventFilter(watched, event);

        if (event->type() == QEvent::Show || event->type() == QEvent::ToolTipChange
            || event->type() == QEvent::WindowTitleChange
            || event->type() == QEvent::Polish) {
            applyFallbackName(widget);
        } else if (event->type() == QEvent::ChildAdded) {
            auto *childEvent = static_cast<QChildEvent *>(event);
            QWidget *child = qobject_cast<QWidget *>(childEvent->child());
            if (child) {
                // ChildAdded can occur before translated properties are set.
                QPointer<QWidget> guardedChild(child);
                QTimer::singleShot(0, this, [guardedChild]() {
                    if (guardedChild)
                        applyFallbackName(guardedChild.data());
                });
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

void installWidgetAccessibility(QObject *application)
{
    if (!application || !qobject_cast<QApplication *>(application))
        return;

    static QPointer<WidgetAccessibilityFilter> filter;
    if (!filter)
        filter = new WidgetAccessibilityFilter(application);

    // One application-level filter observes current and future widget events.
    for (QWidget *widget : QApplication::allWidgets())
        applyFallbackName(widget);
    application->installEventFilter(filter);
}
