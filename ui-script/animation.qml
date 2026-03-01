import QtQuick 2.12

Rectangle {
    id: container
	color: "transparent"
    signal animationCompleted()

	Loader {
		id: mainLoader
		anchors.fill: parent

		onLoaded: {
			if (item && item.animationCompleted) {
				item.animationCompleted.connect(container.animationCompleted)
			}
		}

		source: {
			if (!hero) return ""
			var arr = hero.split(":")
			return arr.length === 1 ? "Default.qml" : arr[0] + ".qml"
		}
	}
}
