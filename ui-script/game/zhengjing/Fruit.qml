// Fruit.qml: high-intensity mode.
import QtQuick 2.12

Rectangle {
    id: fruit
    width: 40
    height: 40
    radius: 20
    color: "red"
    objectName: "gameItem"
    property bool isSliced: false
    property real velocityX: (Math.random() - 0.5) * 8  // Increase horizontal spread.
    property real velocityY: -30  // Raise initial velocity by 50% from the previous -12.
    property real gravity: 0.5    // Reduce gravity by 25% so fruit stays airborne longer.
    property var rootParent: parent

    Rectangle {
        id: hitEffect
        anchors.fill: parent
        color: "#FFD700"
        opacity: 0
        radius: parent.radius
    }

    Timer {
        id: motionTimer
        interval: 30
        running: true
        repeat: true
        onTriggered: {
            velocityY += gravity
            x += velocityX * 0.8
            y += velocityY * 0.9

            // Keep fruit alive until it rises to twice the screen height.
            if(y < -height*2 || y > rootParent.height*2) {
                destroy()
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            if(!isSliced) {
                isSliced = true
                main.score += 1
                hitEffect.opacity = 1
                scaleAnim.start()
                destroy(500)
            }
        }
    }

    NumberAnimation on scale {
        id: scaleAnim
        from: 1.0
        to: 2.0  // Make the scale effect more pronounced.
        duration: 300
        running: false
    }

    Component.onCompleted: {
        x = Math.random()*(rootParent.width - width*2) + width/2
        y = rootParent.height - height/2 - 20  // Subtract 20 so fruit launches from lower on the screen.
    }
}
