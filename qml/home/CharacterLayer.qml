import QtQuick

Item {
    id: root

    Image {
        id: character

        anchors.left: parent.left
        anchors.bottom: parent.bottom

        width: parent.width * 1.05
        height: parent.height * 1.05

        source: homeController.characterImage
        fillMode: Image.PreserveAspectFit
        horizontalAlignment: Image.AlignLeft
        verticalAlignment: Image.AlignBottom

        mipmap: true
        asynchronous: true

        transform: Translate {
            x: mouseArea.normalizedX * 10
            y: mouseArea.normalizedY * 5
        }

        Behavior on x {
            NumberAnimation { duration: 250 }
        }

        Behavior on y {
            NumberAnimation { duration: 250 }
        }

        SequentialAnimation on anchors.bottomMargin {
            loops: Animation.Infinite

            NumberAnimation {
                to: 4
                duration: 2400
                easing.type: Easing.InOutSine
            }

            NumberAnimation {
                to: -4
                duration: 2400
                easing.type: Easing.InOutSine
            }
        }

        onStatusChanged: {
            if (status === Image.Error)
                console.error("Character load failed:", source)
        }
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        hoverEnabled: true

        property real normalizedX: (mouseX / width - 0.5) * 2
        property real normalizedY: (mouseY / height - 0.5) * 2
    }
}
