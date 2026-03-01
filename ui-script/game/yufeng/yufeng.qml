import QtQuick 2.12
import QtQuick.Window 2.12

Item {
    id: root
    signal animationCompleted()
    width: 800
    height: 600
    property int score: 0
    property bool isRunning: true
    property real groundHeight: 20
    property int starPipesCreated: 0
    property int timeLeft: 10  // 剩余时间属性

    MouseArea {
        anchors.fill: parent
        onClicked: {
            if(root.isRunning) {
                bird.velocity = -8
            }
        }
    }
    Rectangle {
        anchors.fill: parent
        color: "#303030"
    }

    Rectangle {
        id: bird
        width: 30
        height: 30
        radius: 15
        color: "yellow"
        x: 200
        y: parent.height/2 - height/2
        property real velocity: 0
    }

    Rectangle {
        id: ground
        width: parent.width
        height: groundHeight
        color: "green"
        anchors.bottom: parent.bottom
    }

    // 胜利计时器（原10秒计时器）
    Timer {
        id: victoryTimer
        interval: 10000
        running: true
        onTriggered: {
            if(root.isRunning) {
                root.isRunning = false
                resultText.text = "胜利！得分：" + score
            }
            const success = fileHandler.writeFile("yufeng.txt", score)
            closeTimer.start()  // 启动关闭计时
        }
    }

    // 倒计时显示
    Text {
        id: timeText
        text: "剩余时间: " + timeLeft
        font {
            pixelSize: 24
            bold: true
        }
        color: "white"
        style: Text.Outline
        styleColor: "black"
        anchors {
            top: parent.top
            topMargin: 20
            right: parent.right
            rightMargin: 20
        }
    }

    // 倒计时计时器
    Timer {
        id: countdownTimer
        interval: 1000
        repeat: true
        running: true
        onTriggered: {
            if(timeLeft > 0) timeLeft--
        }
    }

    // 关闭应用计时器
    Timer {
        id: closeTimer
        interval: 2000
        onTriggered: root.animationCompleted()
    }

    Timer {
        id: pipeSpawner
        interval: 1500
        running: root.isRunning
        repeat: true
        onTriggered: createPipePair()
    }

    Text {
        id: scoreText
        text: "得分: " + score
        font {
            pixelSize: 36
            bold: true
        }
        color: "white"
        style: Text.Outline
        styleColor: "black"
        anchors {
            top: parent.top
            topMargin: 20
            horizontalCenter: parent.horizontalCenter
        }
    }

    Text {
        id: resultText
        font.pixelSize: 32
        anchors.centerIn: parent
        visible: !root.isRunning
        z: 0
    }

    Component {
        id: pipeComponent
        Item {
            id: pipe
            width: 60
            height: root.height
            z: 2
            property real pipeY: 100 + Math.random() * (root.height - 300)
            property real gap: 180

            Rectangle {
                width: parent.width
                height: pipeY
                color: "green"
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Rectangle {
                width: parent.width
                height: root.height - pipeY - gap - groundHeight
                color: "green"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
            }

            NumberAnimation on x {
                from: root.width
                to: -width
                duration: 3500
                running: root.isRunning
            }

            onXChanged: {
                var pipeRect = mapToItem(root, 0, 0)
                var birdRect = {x: bird.x, y: bird.y, width: bird.width, height: bird.height}

                if (birdRect.x + birdRect.width > pipeRect.x &&
                    birdRect.x < pipeRect.x + width) {

                    var gapTop = pipeY
                    var gapBottom = gapTop + gap

                    if (birdRect.y < gapTop ||
                        birdRect.y + birdRect.height > gapBottom) {
                        gameOver()
                    }
                }
            }

            function createStar() {
                var star = starComponent.createObject(root)
                star.x = pipe.x + 30
                star.y = pipeY + gap/2 - 10
            }
        }
    }

    Component {
        id: starComponent
        Rectangle {
            width: 20
            height: 20
            radius: 10
            color: "gold"
            z: 2

            NumberAnimation on x {
                from: root.width
                to: -width
                duration: 3500
                running: root.isRunning
            }

            Timer {
                interval: 50
                running: true
                repeat: true
                onTriggered: {
                    if (Math.abs(bird.x - parent.x) < 30 &&
                        Math.abs(bird.y - parent.y) < 30) {
                        root.score += 1
                        parent.destroy()
                    }
                }
            }
        }
    }

    function createPipePair() {
        var pipe = pipeComponent.createObject(root)
        pipe.x = root.width

        if (root.starPipesCreated < 3) {
            pipe.createStar()
            root.starPipesCreated++
        }
    }

    function gameOver() {
        if(root.isRunning) {
            root.isRunning = false

            resultText.text = "失败！得分：" + score
        }
    }

    Timer {
        interval: 20
        running: root.isRunning
        repeat: true
        onTriggered: {
            bird.velocity = Math.min(bird.velocity + 0.5, 15)
            bird.y += bird.velocity

            if(bird.y + bird.height > ground.y) {
                gameOver()
                bird.y = ground.y - bird.height
            }
        }
    }
}
