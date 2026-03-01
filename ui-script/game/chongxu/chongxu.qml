// main.qml
import QtQuick 2.12

Item {
    id: root
    signal animationCompleted()
    width: 600
    height: 800
    focus: true

    property int score: 0
    property bool gameRunning: true
    property int remainingTime: 10
    property int maxScore: 5 // 最大分数限制
    Component.onCompleted: {
           forceActiveFocus()

       }
    // 游戏背景
    Rectangle {
        anchors.fill: parent
        color: "#303030"
    }

    // 玩家角色（添加平滑移动）
    Rectangle {
        id: player
        width: 80
        height: 80
        color: "blue"
        radius: 10
         activeFocusOnTab: true
        y: parent.height - height - 20
        x: (parent.width - width) / 2

        Behavior on x {
            NumberAnimation { duration: 200 }
        }
    }

    // 计时器显示
    Text {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 20
        text: "剩余时间: " + remainingTime
        font.pixelSize: 24
        color: "white"
        style: Text.Outline
        styleColor: "black"
    }

    // 分数显示
    Text {
        id: scoreText
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        text: "分数: " + score
        font.pixelSize: 32
        color: "white"
        style: Text.Outline
        styleColor: "black"
    }

    // 游戏结束显示
    Text {
        id: gameOverText
        anchors.centerIn: parent
        text: "游戏结束！得分: " + score
        font.pixelSize: 48
        color: "red"
        visible: false
        style: Text.Outline
        styleColor: "white"
    }

    // 键盘控制（改进长按响应）
    Keys.onPressed: {
        if (!activeFocus) forceActiveFocus()
        if (gameRunning) {
            if (event.key === Qt.Key_Left) leftPressed = true
            if (event.key === Qt.Key_Right) rightPressed = true
        }
    }

    Keys.onReleased: {
        if (event.key === Qt.Key_Left) leftPressed = false
        if (event.key === Qt.Key_Right) rightPressed = false
    }

    property bool leftPressed: false
    property bool rightPressed: false

    Timer {
        id: moveTimer
        interval: 50
        running: gameRunning
        repeat: true
        onTriggered: {
            if (leftPressed) player.x = Math.max(0, player.x - 75)
            if (rightPressed) player.x = Math.min(root.width - player.width, player.x + 75)
        }
    }
    function cleanupItems() {
        // 遍历所有子对象
        for (var i = children.length - 1; i >= 0; i--) {
            var child = children[i]
            // 通过对象名称识别掉落物
            if (child.objectName === "fallingItem") {
                child.destroy()
            }
        }
    }
    // 游戏时间控制
    Timer {
        id: gameTimer
        interval: 1000
        running: gameRunning
        repeat: true
        onTriggered: {
            remainingTime--
            if (remainingTime <= 0) {
                gameRunning = false
                gameOverText.visible = true
                cleanupItems()  // 新增清理操作
                const success = fileHandler.writeFile("chongxu.txt", score)
                endTimer.start()
            }
        }
    }

    Timer {
        id: endTimer
        interval: 2000
        onTriggered: root.animationCompleted()
    }

    // 物体生成计时器
    Timer {
        id: spawnTimer
        interval: 250  // 频率提升到每250ms生成（每秒4个）
        running: gameRunning
        repeat: true
        onTriggered: {
            // 每次生成2个物品
            for(var i=0; i<2; i++){
                var component = Qt.createComponent("FallingItem.qml")
                if (component.status === Component.Ready) {
                    var item = component.createObject(root)
                    item.startFall(root)
                }
            }
        }
    }



    function checkCollision(item) {
        if (!gameRunning) return  // 达到最高分后停止检测

        if (item.y + item.height >= player.y &&
            item.x + item.width >= player.x &&
            item.x <= player.x + player.width) {

            // 分数封顶处理
           score = Math.min(maxScore, Math.max(0, score + item.value))
            item.destroy()
            collisionEffect.start()


        }
    }

    // 碰撞特效
    SequentialAnimation {
        id: collisionEffect
        ParallelAnimation {
            NumberAnimation {
                target: player
                property: "scale"
                from: 1
                to: 1.2
                duration: 100
            }
            ColorAnimation {
                target: player
                property: "color"
                to: "lightblue"
                duration: 100
            }
        }
        ParallelAnimation {
            NumberAnimation {
                target: player
                property: "scale"
                from: 1.2
                to: 1
                duration: 100
            }
            ColorAnimation {
                target: player
                property: "color"
                to: "blue"
                duration: 100
            }
        }
    }
}
