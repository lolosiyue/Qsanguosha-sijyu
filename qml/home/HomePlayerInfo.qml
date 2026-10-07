import QtQuick
import "."

// Top-left player info. The character actions sit under the name, off the artwork.
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
        spacing: 2
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

        Row {
            spacing: 12

            Text {
                text: homeController.qtTranslate("HomeScene", "Change character")
                color: changeArea.containsMouse ? HomeTheme.navTextActive : HomeTheme.pillText
                font.pixelSize: root.compact ? 13 : 14
                style: Text.Outline
                styleColor: HomeTheme.onArtScrim

                MouseArea {
                    id: changeArea
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: homeController.openGenerals()
                }
            }

            Text {
                visible: homeController.homeCharacter.length > 0
                text: homeController.qtTranslate("HomeScene", "Restore default character")
                color: restoreArea.containsMouse ? HomeTheme.navTextActive : HomeTheme.pillText
                font.pixelSize: root.compact ? 13 : 14
                style: Text.Outline
                styleColor: HomeTheme.onArtScrim

                MouseArea {
                    id: restoreArea
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: homeController.clearHomeCharacter()
                }
            }
        }
    }
}
