import QtQuick
import Qt5Compat.GraphicalEffects

// Быстрый вход на главной: MAIN и свои плейлисты плитками. Клик по плитке
// открывает плейлист, кнопка на ней сразу включает его с начала.
// Модель: name, title, caption, coverUrl и icon
Item {
    id: root

    property var model: []
    property color textColor: "white"
    property color subtextColor: "#888888"
    property color cardColor: "#1a1a1a"
    property color hoverColor: "#262626"
    property color borderColor: "#333333"
    property color accentColor: "#b57339"
    property color accentTextColor: "black"
    property string fontFamily: ""
    property real spacing: 10
    property real tileHeight: 64

    signal opened(string name)
    signal played(string name)

    readonly property int columns: width >= 900 ? 3 : 2
    readonly property real tileWidth: (width - (columns - 1) * spacing) / columns
    readonly property int rows: Math.ceil(model.length / columns)

    implicitHeight: rows > 0 ? rows * tileHeight + (rows - 1) * spacing : 0

    Grid {
        columns: root.columns
        spacing: root.spacing

        Repeater {
            model: root.model

            Rectangle {
                width: root.tileWidth
                height: root.tileHeight
                radius: 14
                color: tileMouse.containsMouse ? root.hoverColor : root.cardColor
                border.color: root.borderColor
                border.width: 1
                Behavior on color { ColorAnimation { duration: 120 } }

                MouseArea {
                    id: tileMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.opened(modelData.name)

                    CoverArt {
                        id: art
                        x: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 48
                        height: 48
                        radius: 10
                        source: modelData.coverUrl || ""
                        name: modelData.title || modelData.name
                        icon: modelData.icon || ""
                        tint: modelData.icon ? root.accentColor : "transparent"
                        fontFamily: root.fontFamily
                    }

                    Column {
                        anchors.left: art.right
                        anchors.leftMargin: 12
                        anchors.right: playButton.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        Text {
                            width: parent.width
                            text: modelData.title || modelData.name
                            color: root.textColor
                            elide: Text.ElideRight
                            font.family: root.fontFamily
                            font.pixelSize: 13
                            font.weight: Font.Bold
                        }

                        Text {
                            width: parent.width
                            text: modelData.caption || ""
                            color: root.subtextColor
                            elide: Text.ElideRight
                            font.family: root.fontFamily
                            font.pixelSize: 11
                        }
                    }

                    Rectangle {
                        id: playButton
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        width: 32
                        height: 32
                        radius: 16
                        color: root.accentColor
                        opacity: tileMouse.containsMouse ? 1 : 0
                        scale: playMouse.containsMouse ? 1.08 : 1
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                        Behavior on scale { NumberAnimation { duration: 100 } }

                        Image {
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: 1
                            width: 14
                            height: 14
                            source: "qrc:/assets/play.svg"
                            sourceSize: Qt.size(48, 48)
                            layer.enabled: true
                            layer.effect: ColorOverlay { color: root.accentTextColor }
                        }

                        MouseArea {
                            id: playMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.played(modelData.name)
                        }
                    }
                }
            }
        }
    }
}
