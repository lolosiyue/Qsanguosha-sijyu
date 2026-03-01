import QtQuick 2.12

Item {
    id: root
    // ?餅擃?.BB
    // 蝝??”嚗葷???矩1嚗?舫?頞1嚗摮??2

    //property int sceneHeight: 720
    //property int sceneWidth: 1280
    property string img: "../image/animate/mobile_effects/heal/"

    width: sceneWidth
    height: sceneHeight
    //Image {source:"/home/notify/Pictures/憯爾/2de96144883411ebb6edd017c2d2eca2.png"}
    Rectangle {
        id: mask
        color: "black"
        anchors.fill: parent
        opacity: 0.7
    }

    Image {
        id: leaf1
        anchors.centerIn: parent
        source: img + "leaves1.png"
        opacity: 0
        scale: 0.7
        rotation: 10
    }

    Image {
        id: leaf2
        anchors.centerIn: parent
        source: img + "leaves2.png"
        opacity: 0
        scale: 0.7
    }

    Image {
        id: bg
        anchors.centerIn: parent
        property int curr: 0
        // scale: 2.2   ; target
        source: img + "ginseng" + curr + ".png"
        opacity: 0
    }

    Image {
        id: text
        anchors.centerIn: parent
        source: img + "heal.png"
        opacity: 0
        scale: 0.8
    }

    // ?函??嚗?蝏?000ms
    // 1. ?隞?憭批??曉之??.2??          ; 400 ms
    // 2. 摮????隞??啣之嚗?.8?1?? ; 400 ms
    // 3. ?嗅??箏銝?蝐颱撮                  ; 400 ms
    // -----
    // ?箏?摮?憿箸???頧?5摨血??迫  ; 700 ms
    // ?嗅?2?曉之銝摰??甇Ｕ?           ; 700 ms
    // -----
    // ?迫900ms

    ParallelAnimation {
        id: step1
        running: false
        // bg
        PropertyAnimation {
            target: bg
            property: "scale"
            to: 2.2
            duration: 400
            easing.type: Easing.InQuad
        }
        PropertyAnimation {
            target: bg
            property: "opacity"
            to: 1
            duration: 400
            easing.type: Easing.InQuad
        }

        // text
        PropertyAnimation {
            target: text
            property: "scale"
            to: 1
            duration: 400
            easing.type: Easing.InQuad
        }
        PropertyAnimation {
            target: text
            property: "opacity"
            to: 1
            duration: 400
            easing.type: Easing.InQuad
        }

        // leaf
        PropertyAnimation {
            target: leaf1
            property: "scale"
            to: 1
            duration: 400
            easing.type: Easing.InQuad
        }
        PropertyAnimation {
            target: leaf1
            property: "opacity"
            to: 1
            duration: 400
            easing.type: Easing.InQuad
        }

        PropertyAnimation {
            target: leaf2
            property: "scale"
            to: 1
            duration: 400
            easing.type: Easing.InQuad
        }
        PropertyAnimation {
            target: leaf2
            property: "opacity"
            to: 1
            duration: 400
            easing.type: Easing.InQuad
        }

        onStopped: {
            step2.start();
        }
    }

    ParallelAnimation {
        id: step2
        running: false

        PropertyAnimation {
            target: leaf1
            property: "rotation"
            to: 25
            duration: 700
        }

        PropertyAnimation {
            target: leaf1
            property: "scale"
            to: 1.1
            duration: 700
        }

        PropertyAnimation {
            target: leaf2
            property: "scale"
            to: 1.2
            duration: 700
        }

        PropertyAnimation {
            target: bg
            property: "curr"
            to: 9
            duration: 800
        }

        SequentialAnimation {
            PauseAnimation {
                duration: 800
            }

            PropertyAnimation {
                target: root
                property: "opacity"
                to: 0
                duration: 300
            }
        }

        onStopped: {
            container.visible = false
            container.animationCompleted()
        }
    }

    Component.onCompleted: {
        step1.running = true;
    }
}
