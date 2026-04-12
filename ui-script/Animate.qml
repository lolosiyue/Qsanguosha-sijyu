/********************************************************************
    Copyright (c) 2013-2014 - QSanguosha-Rara

    This file is part of QSanguosha-Hegemony.

    This game is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 3.0
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    See the LICENSE file for more details.

    QSanguosha-Rara
    *********************************************************************/

import QtQuick 2.12

Item {
    function resolveAnimateImageSource(heroSpec) {
        var spec = heroSpec ? String(heroSpec) : ""
        var arr = spec.split(":")
        var token = arr.length > 1 ? arr.slice(1).join(":") : spec

        if (!token) return ""

        var lower = token.toLowerCase()
        var hasExt = lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".webp")
        if (token.indexOf("/") >= 0 || hasExt) {
            if (token.startsWith("../")) return token
            if (token.startsWith("image/")) return "../" + token
            return token
        }

        return "../image/animate/" + token + ".png"
    }

    Rectangle {
        id: mask
        x: 0
        width: sceneWidth
        height: 180
        y: sceneHeight * 0.6
        color: "black"
        opacity: 0
        z: 0
    }

    Image {
        id: heroPic
        x: sceneWidth
        y: sceneHeight*0.17
        fillMode: Image.PreserveAspectFit
        source: resolveAnimateImageSource(hero)
        scale: 0.7
        z: 2
    }

    Rectangle {
        id: heroPicBg
        color: "transparent"
        x: sceneWidth*0.25
        y: sceneHeight*0.2
        opacity: 0
        Image {
            fillMode: Image.PreserveAspectFit
			source: resolveAnimateImageSource(hero)
			scale: 2
            opacity: 0.5
			z: 1
        }
        Image {
            fillMode: Image.PreserveAspectFit
            source: "../image/animate/util/bluemask.png"
            scale: 2
            opacity: 0.5
			z: 2
        }
    }

    FontLoader {
        id: bwk
        source: "../font/FZBWKSK.TTF"
    }

    Text {
        id: text
        color: "white"
        text: skill
        font.family: "隶书"
        style: Text.Outline
        font.pointSize: 900
        opacity: 0
        z: 3
        x: sceneWidth*0.5
        y: sceneHeight*0.66
    }

    ParallelAnimation {
        id: step1
        running: false
        PropertyAnimation {
            target: heroPic
            property: "x"
            to: sceneWidth / 2 - 500
            duration: 800
            easing.type: Easing.OutQuad
			easing.overshoot: 3
        }
        PropertyAnimation{
            target: mask
            property: "opacity"
            to: 0.6
            duration: 0
        }
        onStopped: {
			step2.start()
        }
    }

    SequentialAnimation {
        id: step2
        onStopped: {
            container.visible = false
            container.animationCompleted()
        }
        ParallelAnimation {
			PropertyAnimation {
                target: heroPicBg
                property: "opacity"
                to: 1
                duration: 800
				easing.overshoot: 3
				easing.type: Easing.OutQuad
            }
            PropertyAnimation {
                target: text
                property: "opacity"
                to: 1.0
                duration: 800
            }
            PropertyAnimation {
                target: text
                property: "font.pointSize"
                to: 90
                duration: 800
            }
        }
        PauseAnimation { duration: 2500 }
    }

    Component.onCompleted: {
        step1.start()
    }
}
