// main.qml
import QtQuick 2.12

Rectangle {
    id: main
    color: "#333333"
    signal animationCompleted()

    property int score: 0
    property bool gameRunning: true
    property real startTime: 0
    property int bombProbability: 40 // 20%概率生成炸弹

    // 调试边界（红色边框）
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border { color: "red"; width: 2 }
    }

    Text {
        id: timerText
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        color: "white"
        font.pixelSize: 20
    }

    Text {
        id: scoreText
        anchors.top: timerText.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        color: "white"
        font.pixelSize: 16
        text: "得分: " + main.score
    }

    Text {
        id: gameOverText
        anchors.centerIn: parent
        color: "red"
        font.pixelSize: 30
        text: "游戏结束! 得分：" + main.score
        visible: false
    }

    Timer {
        id: gameTimer
        interval: 5000
        running: true
        onTriggered: endGame()
    }

    Timer {
        id: fruitTimer
        interval: 800
        running: true
        repeat: true
        onTriggered: createGameItem()
    }

    Timer {
        id: updateTimer
        interval: 100
        running: true
        repeat: true
        onTriggered: updateTime()
    }

    Timer {
        id: delayTimer
        interval: 2000
        onTriggered: main.animationCompleted()
    }
    function cleanScore() {
            score = 0  // 分数清零
        }

    function endGame() {
        gameRunning = false
        gameOverText.visible = true
        fruitTimer.stop()
        delayTimer.start()
        const success = fileHandler.writeFile("zhengjing.txt", score)
        clearGameItems()
    }

    function updateTime() {
        var remain = (5000 - (new Date().getTime() - startTime))/1000
        timerText.text = "剩余时间: " + (remain > 0 ? remain.toFixed(1) : "0.0")
    }

    function createGameItem() {
        if(!gameRunning) return

        // 随机生成水果或炸弹
        var isBomb = Math.random() * 100 < bombProbability
        var component = Qt.createComponent(isBomb ? "Bomb.qml" : "Fruit.qml")

        if (component.status === Component.Ready) {
            var obj = component.createObject(main, {"parent": main})
        }
    }

    function clearGameItems() {
        var children = main.children
        for(var i=0; i<children.length; i++){
            if(children[i].objectName === "gameItem"){
                children[i].destroy()
            }
        }
    }

    Component.onCompleted: {
        startTime = new Date().getTime()
        Qt.createComponent("Fruit.qml")
        Qt.createComponent("Bomb.qml")  // 预加载炸弹组件
    }
}
