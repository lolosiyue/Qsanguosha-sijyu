import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "."

GridLayout {
    id: panel
    property bool compact: false
    property bool portraitRail: false
    columns: compact && !portraitRail ? 4 : 1

    property alias settingsBtn: settingsBtn
    property alias aboutBtn: aboutBtn
    property alias updateBtn: updateBtn
    property alias themeToggle: themeToggle

    signal settingsClicked()
    signal aboutClicked()
    signal updateClicked()
    signal themeToggleClicked()

    rowSpacing: 10
    columnSpacing: 10

    BASlantedPanel {
        Layout.columnSpan: panel.columns
        Layout.preferredWidth: panel.compact ? panel.width : 124
        Layout.preferredHeight: 32
        Layout.alignment: Qt.AlignHCenter

        visible: !panel.portraitRail && versionLabel.text !== ""
        slant: -0.08
        cornerRadius: 8
        shadowBlur: 6
        shadowOffset: 3
        borderWidth: 1

        topColor: HomeTheme.baToolTop
        bottomColor: HomeTheme.baToolBottom
        borderColor: HomeTheme.baDockBorder
        shadowColor: HomeTheme.baDockShadow

        Text {
            id: versionLabel
            anchors.centerIn: parent
            text: homeController.version
            color: HomeTheme.pillText
            font.pixelSize: 12
        }
    }

    BAToolButton {
        id: themeToggle
        implicitWidth: panel.portraitRail ? 56 : 72

        Layout.alignment: Qt.AlignHCenter

        Accessible.name: qsTranslate("HomeScene", "Toggle theme")
        Layout.fillWidth: panel.compact
        KeyNavigation.tab: panel.portraitRail ? aboutBtn : settingsBtn
        KeyNavigation.backtab: updateBtn

        iconSource: homeController.isDarkTheme
                    ? "qrc:/QSanguosha/Home/icons/moon.svg"
                    : "qrc:/QSanguosha/Home/icons/sun.svg"

        onClicked: {
            homeController.toggleTheme()
            panel.themeToggleClicked()
        }
    }

    BAToolButton {
        id: settingsBtn
        visible: !panel.portraitRail

        Layout.alignment: Qt.AlignHCenter
        Layout.fillWidth: panel.compact
        implicitWidth: panel.portraitRail ? 56 : panel.compact ? 64 : 124

        text: panel.compact ? "" : qsTranslate("HomeScene", "Settings")
        Accessible.name: qsTranslate("HomeScene", "Settings")
        ToolTip.visible: hovered
        ToolTip.text: Accessible.name
        iconSource: "qrc:/QSanguosha/Home/icons/settings.svg"

        onClicked: panel.settingsClicked()

        KeyNavigation.tab: aboutBtn
        KeyNavigation.backtab: updateBtn
    }

    BAToolButton {
        id: aboutBtn

        Layout.alignment: Qt.AlignHCenter
        Layout.fillWidth: panel.compact
        implicitWidth: panel.portraitRail ? 56 : panel.compact ? 64 : 124

        text: panel.compact ? "" : qsTranslate("HomeScene", "About")
        Accessible.name: qsTranslate("HomeScene", "About")
        ToolTip.visible: hovered
        ToolTip.text: Accessible.name
        iconSource: "qrc:/QSanguosha/Home/icons/about.svg"

        onClicked: panel.aboutClicked()

        KeyNavigation.tab: updateBtn
        KeyNavigation.backtab: panel.portraitRail ? themeToggle : settingsBtn
    }

    BAToolButton {
        id: updateBtn

        Layout.alignment: Qt.AlignHCenter
        Layout.fillWidth: panel.compact
        implicitWidth: panel.portraitRail ? 56 : panel.compact ? 64 : 124

        text: panel.compact ? "" : qsTranslate("HomeScene", "Check for updates")
        Accessible.name: qsTranslate("HomeScene", "Check for updates")
        ToolTip.visible: hovered
        ToolTip.text: Accessible.name
        iconSource: "qrc:/QSanguosha/Home/icons/update.svg"

        onClicked: panel.updateClicked()

        KeyNavigation.tab: panel.portraitRail ? themeToggle : settingsBtn
        KeyNavigation.backtab: aboutBtn
    }
}
