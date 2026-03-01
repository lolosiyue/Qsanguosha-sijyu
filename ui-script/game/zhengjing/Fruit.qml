// Fruit.qml 激情增高版
import QtQuick 2.12

Rectangle {
    id: fruit
    width: 40
    height: 40
    radius: 20
    color: "red"
    objectName: "gameItem"
    property bool isSliced: false
    property real velocityX: (Math.random() - 0.5) * 8  // 加大横向散布幅度
    property real velocityY: -30  // 初速度提升50%！原来-12太娘炮
    property real gravity: 0.5    // 重力减弱25%，让丫飞更久
    property var rootParent: parent

    // 点击特效（保持原样）
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

            // 延长生存时间（飞出两倍高度才销毁）
            if(y < -height*2 || y > rootParent.height*2) {
                destroy()
            }
        }
    }

    // 点击逻辑（保持原样）
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

    // 动画参数增强
    NumberAnimation on scale {
        id: scaleAnim
        from: 1.0
        to: 2.0  // 放大效果更夸张
        duration: 300
        running: false
    }

    Component.onCompleted: {
        // 初始位置再提升20像素
        x = Math.random()*(rootParent.width - width*2) + width/2
        y = rootParent.height - height/2 - 20  // 这里减20让水果从更下面开始抛
    }
}
