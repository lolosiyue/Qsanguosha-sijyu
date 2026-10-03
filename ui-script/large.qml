import QtQuick 2.12

Item {
    id: root
    width: sceneWidth
    height: sceneHeight
    focus: true

    Image {
        id: heroCard
        source: "../image/large/"+hero.split(":")[1]+".png"

        // Zoom controls.
        property real manualScale: 1.0
        property real minScale: 0.2
        property real maxScale: 2.0
        property real baseScaleFactor: 1.0

        // Transform properties.
        property real rotationAngle: 0
        property real mirrorFactor: 1

        // Dynamically calculated properties.
        property real totalScale: baseScaleFactor * manualScale * mirrorFactor
        property real scaledWidth: implicitWidth * Math.abs(totalScale)
        property real scaledHeight: implicitHeight * Math.abs(totalScale)

        // Initial centered position.
        width: implicitWidth
        height: implicitHeight
        x: (root.width - scaledWidth) / 2
        y: (root.height - scaledHeight) / 2

        transform: [
            Scale {
                origin.x: heroCard.width / 2
                origin.y: heroCard.height / 2
                xScale: {
                    // Calculate the base scale automatically.
                    var base = Math.min(
                        (root.width * 0.9) / heroCard.implicitWidth,
                        (root.height * 0.9) / heroCard.implicitHeight
                    )
                    heroCard.baseScaleFactor = base
                    return heroCard.totalScale
                }
                yScale: heroCard.baseScaleFactor * heroCard.manualScale
            },
            Rotation {
                origin.x: heroCard.width / 2
                origin.y: heroCard.height / 2
                angle: heroCard.rotationAngle
            }
        ]

        // Left-button drag area.
        MouseArea {
            id: dragArea
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            drag.target: heroCard
            drag.axis: Drag.XAndYAxis

            onDoubleClicked: {
                // Keep the visual center fixed when mirrored.
                var visualCenterX = heroCard.x + heroCard.scaledWidth/2
                heroCard.mirrorFactor *= -1
                heroCard.x = visualCenterX - heroCard.scaledWidth/2
            }
        }

        // Middle-button zoom area.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.MiddleButton

            property real startY: 0
            property real initialScale: 1.0
            property real initialX: 0
            property real initialY: 0
            property real mouseXInItem: 0
            property real mouseYInItem: 0

            onPressed: {
                startY = mouseY
                initialScale = heroCard.manualScale
                initialX = heroCard.x
                initialY = heroCard.y
                mouseXInItem = mouseX - heroCard.width/2
                mouseYInItem = mouseY - heroCard.height/2
            }

            onPositionChanged: {
                if (pressed) {
                    // Calculate the zoom delta.
                    var delta = (mouseY - startY) * 0.004
                    var targetScale = initialScale + delta

                    // Clamp the zoom range.
                    var clampedScale = Math.max(heroCard.minScale,
                                              Math.min(heroCard.maxScale,
                                              targetScale))

                    // Stop at the limit.
                    if (clampedScale !== targetScale) return

                    // Calculate the scale.
                    var scaleRatio = clampedScale / initialScale

                    // Update position and scale.
                    heroCard.x = initialX + mouseXInItem * (1 - scaleRatio)
                    heroCard.y = initialY + mouseYInItem * (1 - scaleRatio)
                    heroCard.manualScale = clampedScale
                }
            }
        }

        // Right-button rotation area.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton

            property real startAngle: 0
            property real centerX: heroCard.x + heroCard.width/2
            property real centerY: heroCard.y + heroCard.height/2

            onPressed: {
                var scenePos = mapToItem(root, mouseX, mouseY)
                var dx = scenePos.x - centerX
                var dy = scenePos.y - centerY
                startAngle = Math.atan2(dy, dx) * 180 / Math.PI
            }

            onPositionChanged: {
                if(pressed) {
                    var scenePos = mapToItem(root, mouseX, mouseY)
                    var dx = scenePos.x - centerX
                    var dy = scenePos.y - centerY
                    var currentAngle = Math.atan2(dy, dx) * 180 / Math.PI

                    heroCard.rotationAngle += (currentAngle - startAngle) * heroCard.mirrorFactor
                    startAngle = currentAngle
                }
            }
            onDoubleClicked: {
                container.visible = false
                container.animationCompleted()
            }
        }
    }
}
