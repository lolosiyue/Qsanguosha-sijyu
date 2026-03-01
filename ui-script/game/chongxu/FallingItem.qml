// FallingItem.qml
import QtQuick 2.12

Item {
    id: root
    objectName: "fallingItem"  // 添加识别标识
    width: 40
    height: 40

    property int value: 1
    property color itemColor: "green"
    property var gameRoot: null

    Rectangle {
        anchors.fill: parent
        color: itemColor
        radius: width/2
        opacity: 0.8
        transformOrigin: Item.Center

        SequentialAnimation on scale {
            running: true
            loops: Animation.Infinite
            NumberAnimation {
                from: 1;
                to: 1.2;
                duration: 500;
                easing.type: Easing.InOutQuad
            }
            NumberAnimation {
                from: 1.2;
                to: 1;
                duration: 500;
                easing.type: Easing.InOutQuad
            }
        }
    }

    function startFall(container) {
        gameRoot = container
        x = Math.random() * (container.width - width)
        y = 0  // 明确设置初始位置
        fallAnimation.start()
    }

    NumberAnimation {
        id: fallAnimation
        target: root
        property: "y"
        from: 0
        to: gameRoot ? gameRoot.height : 0
        duration: 2000
        easing.type: Easing.InQuad
        onStopped: root.destroy()
    }

    onYChanged: if(gameRoot) gameRoot.checkCollision(root)

    Component.onCompleted: {
        // 保持50%红球概率
        if (Math.random() < 0.5) {
            value = -2  // 调整为扣2分
            itemColor = "red"
        } else {
            value = 1   // 好球加1分
        }
    }

    // 修复后的消失动画
    SequentialAnimation {
        id: destroyAnim
        ParallelAnimation {
            NumberAnimation {
                target: root
                property: "opacity"
                from: 1
                to: 0
                duration: 200
            }
            NumberAnimation {
                target: root
                property: "scale"
                from: 1
                to: 0.5
                duration: 200
            }
        }
        ScriptAction { script: root.destroy() }
    }

    function destroy() {
        fallAnimation.stop()
        destroyAnim.start()
    }
}
