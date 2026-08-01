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

    spacing: 12

    Rectangle {
        Layout.preferredWidth: 120
        Layout.preferredHeight: 36
        Layout.alignment: Qt.AlignHCenter

        radius: 18
        color: HomeTheme.pillBg
        visible: versionLabel.text !== ""

        Text {
            id: versionLabel
            anchors.centerIn: parent
            text: homeController.version
            color: HomeTheme.pillText
            font.pixelSize: 12
        }
    }

    // 明暗主題切換：目前暗色顯示太陽(切亮)，亮色顯示月亮(切暗)
    Rectangle {
        id: themeToggle

        Layout.preferredWidth: 44
        Layout.preferredHeight: 44
        Layout.alignment: Qt.AlignHCenter

        radius: 22
        color: HomeTheme.pillBg
        border.width: 1
        border.color: HomeTheme.panelBorder

        Image {
            anchors.centerIn: parent
            width: 22
            height: 22

            source: homeController.isDarkTheme
                    ? "qrc:/QSanguosha/Home/icons/moon.svg"
                    : "qrc:/QSanguosha/Home/icons/sun.svg"
            fillMode: Image.PreserveAspectFit
            mipmap: true
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                homeController.toggleTheme()
                panel.themeToggleClicked()
            }
        }
    }

    HomeNavButton {
        id: settingsBtn
        Layout.alignment: Qt.AlignHCenter

        text: qsTr("设置")
        iconSource: "qrc:/QSanguosha/Home/icons/settings.svg"

        onClicked: panel.settingsClicked()

        KeyNavigation.tab: aboutBtn
        KeyNavigation.backtab: updateBtn
    }

    HomeNavButton {
        id: aboutBtn
        Layout.alignment: Qt.AlignHCenter

        text: qsTr("关于")
        iconSource: "qrc:/QSanguosha/Home/icons/about.svg"

        onClicked: panel.aboutClicked()

        KeyNavigation.tab: updateBtn
        KeyNavigation.backtab: settingsBtn
    }

    HomeNavButton {
        id: updateBtn
        Layout.alignment: Qt.AlignHCenter

        text: qsTr("检查更新")
        iconSource: "qrc:/QSanguosha/Home/icons/update.svg"

        onClicked: panel.updateClicked()

        KeyNavigation.tab: settingsBtn
        KeyNavigation.backtab: aboutBtn
    }
}
