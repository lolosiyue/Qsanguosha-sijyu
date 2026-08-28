import QtQuick
import "."

pragma ComponentBehavior: Bound

Item {
    id: root

    property var variants: []
    implicitHeight: variantColumn.height

    Column {
        id: variantColumn
        width: parent.width
        spacing: HomeTheme.cardVariantRowGap

        Repeater {
            model: root.variants || []

            Rectangle {
                id: variantRow
                required property int index
                required property var modelData

                width: variantColumn.width
                height: variantContent.implicitHeight + HomeTheme.cardVariantRowPadding * 2
                radius: HomeTheme.cardVariantRowRadius
                color: index % 2 === 0 ? HomeTheme.cardVariantFill
                                      : HomeTheme.cardVariantAlternateFill
                Accessible.role: Accessible.StaticText
                Accessible.name: modelData.suitDisplay + " " + modelData.numberDisplay
                                 + ", " + modelData.packageDisplay + ", "
                                 + homeController.qtTranslate("CardScene", "IDs %1")
                                   .arg(modelData.idDisplay)

                Column {
                    id: variantContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: HomeTheme.cardVariantRowPadding
                    spacing: HomeTheme.cardVariantRowGap

                    Row {
                        width: parent.width
                        spacing: HomeTheme.cardVariantRowGap

                        Row {
                            width: HomeTheme.cardVariantSuitWidth
                            spacing: HomeTheme.cardVariantRowGap
                            Image {
                                width: HomeTheme.cardSuitIconSize
                                height: HomeTheme.cardSuitIconSize
                                source: variantRow.modelData.suitIcon
                                fillMode: Image.PreserveAspectFit
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: variantRow.modelData.suitDisplay + " "
                                      + variantRow.modelData.numberDisplay
                                color: variantRow.modelData.suitKey === "heart"
                                       || variantRow.modelData.suitKey === "diamond"
                                       || variantRow.modelData.suitKey === "no_suit_red"
                                       ? HomeTheme.cardSuitRed : HomeTheme.cardSuitBlack
                                font.pixelSize: HomeTheme.cardBodyFontSize
                                font.bold: true
                            }
                        }

                        Text {
                            width: parent.width - HomeTheme.cardVariantSuitWidth
                                   - HomeTheme.cardVariantRowGap
                            text: variantRow.modelData.packageDisplay
                            color: HomeTheme.cardTextSecondary
                            font.pixelSize: HomeTheme.cardCaptionFontSize
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                        }
                    }

                    Text {
                        width: parent.width
                        text: homeController.qtTranslate("CardScene", "IDs %1")
                              .arg(variantRow.modelData.idDisplay)
                        color: HomeTheme.cardTextMuted
                        font.pixelSize: HomeTheme.cardMetaFontSize
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }
    }
}
