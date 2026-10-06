import QtQuick
import "."

Item {
    id: root

    visible: HomeTheme.tvMode
    implicitHeight: visible ? 22 : 0
    implicitWidth: label.implicitWidth + 24

    Text {
        id: label
        anchors.centerIn: parent
        text: "Enter 確定 · Esc 返回 · PgUp/PgDn 切換分頁 · 方向鍵 移動"
        color: HomeTheme.navTextIdle
        font.pixelSize: HomeTheme.cardCaptionFontSize
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
