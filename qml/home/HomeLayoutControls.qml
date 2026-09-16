import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

BAToolButton {
    id: launcher
    property var safeInsets: ({left: 0, top: 0, right: 0, bottom: 0})
    text: qsTr("版面與單手操作")
    iconSource: "qrc:/QSanguosha/Home/icons/settings.svg"
    implicitWidth: 200
    objectName: "homeLayoutSettings"
    onClicked: layoutPopup.open()

    Popup {
        id: layoutPopup
        parent: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(parent.width - launcher.safeInsets.left - launcher.safeInsets.right - HomeTheme.compactMargin * 2, HomeTheme.compactHandWidth)
        height: Math.min(parent.height - launcher.safeInsets.top - launcher.safeInsets.bottom - HomeTheme.compactMargin * 2, options.implicitHeight + padding * 2)
        x: Config.oneHandedness === 1 ? launcher.safeInsets.left + HomeTheme.compactMargin
           : Config.oneHandedness === 2 ? parent.width - width - launcher.safeInsets.right - HomeTheme.compactMargin
           : (parent.width - width) / 2
        y: parent.height - height - launcher.safeInsets.bottom - HomeTheme.compactMargin
        padding: HomeTheme.compactMargin
        background: BASlantedPanel {
            slant: -0.03
            cornerRadius: 10
            topColor: HomeTheme.baDockTop
            bottomColor: HomeTheme.baDockBottom
            borderColor: HomeTheme.baDockBorder
            shadowColor: HomeTheme.baDockShadow
            accentVisible: true
            accentColor: HomeTheme.baSky
        }
        contentItem: ScrollView {
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                id: options
                width: layoutPopup.availableWidth
                spacing: HomeTheme.compactGap
                Label {
                    text: qsTr("首頁、對話框與牌桌共用")
                    color: HomeTheme.btnSecondaryText
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                HomeMainButton {
                    text: Config.responsiveUiEnabled ? qsTr("自適應版面：開") : qsTr("自適應版面：關")
                    Layout.fillWidth: true
                    implicitHeight: 56
                    compact: true
                    iconSource: "qrc:/QSanguosha/Home/icons/settings.svg"
                    primary: Config.responsiveUiEnabled
                    onClicked: Config.responsiveUiEnabled = !Config.responsiveUiEnabled
                }
                Repeater {
                    model: [qsTr("雙手／無偏好"), qsTr("左手操作"), qsTr("右手操作")]
                    HomeMainButton {
                        required property int index
                        required property string modelData
                        text: modelData
                        implicitHeight: 56
                        compact: true
                        iconSource: "qrc:/QSanguosha/Home/icons/settings.svg"
                        primary: Config.oneHandedness === index
                        Layout.fillWidth: true
                        onClicked: {
                            Config.oneHandedness = index
                            // Choosing a hand must take effect immediately, including before joining.
                            Config.responsiveUiEnabled = true
                        }
                    }
                }
                HomeMainButton {
                    text: qsTr("完成")
                    implicitHeight: 56
                    compact: true
                    iconSource: "qrc:/QSanguosha/Home/icons/home.svg"
                    Layout.fillWidth: true
                    onClicked: layoutPopup.close()
                }
            }
        }
        onClosed: launcher.forceActiveFocus()
    }
}
