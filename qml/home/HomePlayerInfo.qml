import QtQuick
import "."

// 左上角玩家資訊：頭像＋名稱（資料源與快速加入對話框相同，純顯示）
Item {
    id: root

    property int avatarSize: 64

    implicitWidth: avatarCircle.width + 14 + nameColumn.width
    implicitHeight: avatarSize

    Rectangle {
        id: avatarCircle

        width: root.avatarSize
        height: root.avatarSize
        radius: width / 2

        color: HomeTheme.pillBg
        border.width: 2
        border.color: HomeTheme.panelBorder

        Image {
            id: avatarImage

            anchors.fill: parent
            anchors.margins: 3

            source: homeController.playerAvatar
            fillMode: Image.PreserveAspectCrop
            clip: true
            antialiasing: true
            mipmap: false
        }

        // 無頭像或載入失敗時，以名稱首字代替
        Text {
            id: fallbackText

            anchors.centerIn: parent

            text: nameText.text.length > 0 ? nameText.text[0] : "?"

            color: HomeTheme.pillText
            font.pixelSize: root.avatarSize * 0.4
            font.weight: Font.Bold

            visible: avatarImage.status !== Image.Ready
        }
    }

    Column {
        id: nameColumn

        anchors.left: avatarCircle.right
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter

        Text {
            id: nameText

            text: homeController.playerName

            color: HomeTheme.navTextActive
            font.pixelSize: 22
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }
}
