import QtQuick
import "."

// Top-left player info: avatar and name, shared with Quick Join; display only.
Item {
    id: root

    property int avatarSize: 64
    property bool compact: false

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

        // Use the first character of the name when the avatar is missing or fails to load.
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
        width: root.compact ? Math.max(0, root.width - avatarCircle.width - 14) : implicitWidth

        Text {
            id: nameText
            width: root.compact ? nameColumn.width : implicitWidth

            text: homeController.playerName

            color: HomeTheme.navTextActive
            // Portrait artwork (e.g. a white halo) can rise behind the name.
            style: root.compact ? Text.Outline : Text.Normal
            styleColor: HomeTheme.onArtScrim
            font.pixelSize: 22
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }
}
