import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "."

ColumnLayout {
    id: panel

    property alias settingsBtn: settingsBtn
    property alias aboutBtn: aboutBtn
    property alias updateBtn: updateBtn
    property alias themeToggle: themeToggle

    signal settingsClicked()
    signal aboutClicked()
    signal updateClicked()
    signal themeToggleClicked()

    spacing: 10

    BASlantedPanel {
        Layout.preferredWidth: 124
        Layout.preferredHeight: 32
        Layout.alignment: Qt.AlignHCenter

        visible: versionLabel.text !== ""
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

        Layout.alignment: Qt.AlignHCenter

        Accessible.name: qsTr("切換主題")

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

        Layout.alignment: Qt.AlignHCenter

        text: qsTr("设置")
        iconSource: "qrc:/QSanguosha/Home/icons/settings.svg"

        onClicked: panel.settingsClicked()

        KeyNavigation.tab: aboutBtn
        KeyNavigation.backtab: updateBtn
    }

    BAToolButton {
        id: aboutBtn

        Layout.alignment: Qt.AlignHCenter

        text: qsTr("关于")
        iconSource: "qrc:/QSanguosha/Home/icons/about.svg"

        onClicked: panel.aboutClicked()

        KeyNavigation.tab: updateBtn
        KeyNavigation.backtab: settingsBtn
    }

    BAToolButton {
        id: updateBtn

        Layout.alignment: Qt.AlignHCenter

        text: qsTr("检查更新")
        iconSource: "qrc:/QSanguosha/Home/icons/update.svg"

        onClicked: panel.updateClicked()

        KeyNavigation.tab: settingsBtn
        KeyNavigation.backtab: aboutBtn
    }
}
