import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

ColumnLayout {
    id: panel

    property alias settingsBtn: settingsBtn
    property alias aboutBtn: aboutBtn
    property alias updateBtn: updateBtn

    signal settingsClicked()
    signal aboutClicked()
    signal updateClicked()

    spacing: 12

    Rectangle {
        Layout.preferredWidth: 120
        Layout.preferredHeight: 36
        Layout.alignment: Qt.AlignHCenter

        radius: 18
        color: "#20FFFFFF"
        visible: versionLabel.text !== ""

        Text {
            id: versionLabel
            anchors.centerIn: parent
            text: homeController.version
            color: "#80FFFFFF"
            font.pixelSize: 12
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