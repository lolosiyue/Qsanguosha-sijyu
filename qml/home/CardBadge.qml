import QtQuick
import "."

Rectangle {
    property alias text: badgeLabel.text
    property color tone: HomeTheme.cardBadgeInfo
    implicitWidth: badgeLabel.implicitWidth + HomeTheme.cardBadgeHPadding * 2
    implicitHeight: HomeTheme.cardBadgeHeight
    radius: HomeTheme.cardBadgeRadius
    color: tone
    Text {
        id: badgeLabel
        anchors.centerIn: parent
        color: HomeTheme.cardBadgeText
        font.pixelSize: HomeTheme.cardMetaFontSize
        font.bold: true
    }
}
