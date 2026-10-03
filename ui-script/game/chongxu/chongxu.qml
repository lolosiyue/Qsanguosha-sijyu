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
    property int maxScore: 5 // Maximum score.
    Component.onCompleted: {
           forceActiveFocus()

       }
    // Game background.
    Rectangle {
        anchors.fill: parent
        color: "#303030"
    }

    // Player character with smooth movement.
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

    // Timer display.
    Text {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 20
        text: qsTr("Time remaining: %1").arg(remainingTime)
        font.pixelSize: 24
        color: "white"
        style: Text.Outline
        styleColor: "black"
    }

    // Score display.
    Text {
        id: scoreText
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        text: qsTr("Score: %1").arg(score)
        font.pixelSize: 32
        color: "white"
        style: Text.Outline
        styleColor: "black"
    }

    // Game-over display.
    Text {
        id: gameOverText
        anchors.centerIn: parent
        text: qsTr("Game over! Score: %1").arg(score)
        font.pixelSize: 48
        color: "red"
        visible: false
        style: Text.Outline
        styleColor: "white"
    }

    // Keyboard controls with improved key-repeat response.
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
        // Iterate over all child objects.
        for (var i = children.length - 1; i >= 0; i--) {
            var child = children[i]
            // Identify falling items by object name.
            if (child.objectName === "fallingItem") {
                child.destroy()
            }
        }
    }
    // Game timer.
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
                cleanupItems()  // Add cleanup.
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

    // Item spawn timer.
    Timer {
        id: spawnTimer
        interval: 250  // Increase spawn rate to every 250 ms (four per second).
        running: gameRunning
        repeat: true
        onTriggered: {
            // Spawn two items at a time.
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
        if (!gameRunning) return  // Stop checking after reaching the maximum score.

        if (item.y + item.height >= player.y &&
            item.x + item.width >= player.x &&
            item.x <= player.x + player.width) {

            // Cap the score.
           score = Math.min(maxScore, Math.max(0, score + item.value))
            item.destroy()
            collisionEffect.start()


        }
    }

    // Collision effect.
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
