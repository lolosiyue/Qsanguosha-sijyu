#include "../../src/dialog/widget-accessibility.h"

#include <QAccessible>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QtTest>

namespace {

QString accessibleName(QWidget *widget)
{
    QAccessibleInterface *interface = QAccessible::queryAccessibleInterface(widget);
    return interface ? interface->text(QAccessible::Name) : QString();
}

} // namespace

class WidgetAccessibilityTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        installWidgetAccessibility(qApp);
    }

    void preservesNativeButtonName()
    {
        QPushButton button(QStringLiteral("&Continue"));
        button.show();
        QCoreApplication::processEvents();

        QVERIFY(!accessibleName(&button).isEmpty());
        QVERIFY(button.accessibleName().isEmpty());
    }

    void explicitNameSurvivesTooltipChanges()
    {
        QPushButton button;
        button.setAccessibleName(QStringLiteral("Manual action"));
        button.setToolTip(QStringLiteral("Initial hint"));
        button.show();
        QCoreApplication::processEvents();

        button.setToolTip(QStringLiteral("Changed hint"));
        QCoreApplication::processEvents();

        QCOMPARE(button.accessibleName(), QStringLiteral("Manual action"));
        QCOMPARE(accessibleName(&button), QStringLiteral("Manual action"));
    }

    void iconOnlyFallbackTracksTooltip()
    {
        QPushButton button;
        button.setToolTip(QStringLiteral("Open settings"));
        button.show();
        QCoreApplication::processEvents();
        QCOMPARE(accessibleName(&button), QStringLiteral("Open settings"));

        button.setToolTip(QStringLiteral("Close settings"));
        QCoreApplication::processEvents();
        QCOMPARE(accessibleName(&button), QStringLiteral("Close settings"));
    }

    void buddyNamesEditableFieldWithoutOverwritingIt()
    {
        QDialog dialog;
        QLabel label(QStringLiteral("Search:"), &dialog);
        QLineEdit edit(&dialog);
        label.setBuddy(&edit);
        dialog.show();
        QCoreApplication::processEvents();

        QCOMPARE(accessibleName(&edit), QStringLiteral("Search:"));
        QVERIFY(edit.accessibleName().isEmpty());
    }

    void dynamicallyAddedButtonReceivesEffectiveName()
    {
        QDialog dialog;
        dialog.show();
        QCoreApplication::processEvents();

        auto *button = new QPushButton(&dialog);
        button->setToolTip(QStringLiteral("Additional action"));
        button->show();
        QCoreApplication::processEvents();

        QCOMPARE(accessibleName(button), QStringLiteral("Additional action"));
    }

    void decorativeLabelIsNotAssignedFallbackName()
    {
        QLabel decorative;
        decorative.setPixmap(QPixmap(2, 2));
        decorative.show();
        QCoreApplication::processEvents();

        QVERIFY(decorative.accessibleName().isEmpty());
    }
};

QTEST_MAIN(WidgetAccessibilityTest)
#include "widget-accessibility-test.moc"
