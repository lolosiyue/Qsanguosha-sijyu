// Bomb.qml
import QtQuick 2.12

Rectangle {
    id: bomb
    width: 40
    height: 40
    radius: 20
    color: "#333333"
    objectName: "gameItem"

    property real velocityX: (Math.random() - 0.5) * 8
    property real velocityY: -25  // 比水果稍慢的初速度
    property real gravity: 0.6    // 更大的重力加速度
    property var rootParent: parent

    // 炸弹符号（红色X）
    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.8
        height: 4
        rotation: 45
        color: "red"
        antialiasing: true
    }
    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.8
        height: 4
        rotation: -45
        color: "red"
        antialiasing: true
    }

    Timer {
        id: motionTimer
        interval: 30
        running: true
        repeat: true
        onTriggered: {
            velocityY += gravity
            x += velocityX
            y += velocityY

            if(y < -height*2 || y > rootParent.height*2) {
                destroy()
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            main.cleanScore()
            main.endGame()  // 点击炸弹立即结束游戏
            explodeAnimation.start()
            destroy(500)
        }
    }

    // 爆炸动画
    ParallelAnimation {
        id: explodeAnimation
        NumberAnimation {
            target: bomb
            property: "scale"
            from: 1
            to: 3
            duration: 200
        }
        NumberAnimation {
            target: bomb
            property: "opacity"
            from: 1
            to: 0
            duration: 200
        }
    }

    Component.onCompleted: {
        x = Math.random()*(rootParent.width - width*2) + width/2
        y = rootParent.height - height/2 - 20
    }
}
