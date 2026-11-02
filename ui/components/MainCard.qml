import QtQuick
import Qt5Compat.GraphicalEffects

// Шапка библиотеки: MAIN крупно, мозаика из последних лайков и сколько
// песен в нём с каждой площадки. Клик открывает, кнопка сразу включает
Rectangle {
    id: root

    property int count: 0
    // до четырёх обложек последних лайков
    property var covers: []
    // {"Yandex": 12, "SoundCloud": 3, "YouTube": 1}
    property var sources: ({})
    property color textColor: "white"
    property color subtextColor: "#888888"
    property color cardColor: "#1a1a1a"
    property color chipColor: "#262626"
    property color borderColor: "#333333"
    property color accentColor: "#b57339"
    property color accentTextColor: "black"
    property string fontFamily: ""

    signal opened
    signal played

    readonly property var services: [
        { name: "Yandex", icon: "qrc:/assets/yandex_music_icon.svg" },
        { name: "SoundCloud", icon: "qrc:/assets/soundcloud_icon.svg" },
        { name: "YouTube", icon: "qrc:/assets/youtube_music_icon.svg" }
    ]

    implicitHeight: 176
    radius: 20
    color: root.cardColor
    border.color: cardMouse.containsMouse ? root.accentColor : root.borderColor
    border.width: cardMouse.containsMouse ? 1.5 : 1

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: root.radius
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.4; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.rgba(root.accentColor.r, root.accentColor.g, root.accentColor.b, 0.16) }
        }
    }

    MouseArea {
        id: cardMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.opened()

        Item {
            id: mosaic
            x: 20
            anchors.verticalCenter: parent.verticalCenter
            width: 136
            height: 136

            // пока лайков мало, вместо мозаики одно сердце
            CoverArt {
                anchors.fill: parent
                visible: root.covers.length < 4
                radius: 14
                name: "MAIN"
                icon: "qrc:/assets/heart.svg"
                tint: root.accentColor
            }

            Grid {
                visible: root.covers.length >= 4
                columns: 2
                spacing: 4

                Repeater {
                    model: root.covers.length >= 4 ? root.covers.slice(0, 4) : []

                    CoverArt {
                        width: 66
                        height: 66
                        radius: 10
                        source: modelData
                        name: "MAIN"
                        fontFamily: root.fontFamily
                    }
                }
            }
        }

        Column {
            anchors.left: mosaic.right
            anchors.leftMargin: 24
            anchors.right: playButton.left
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Text {
                text: root.count === 1 ? "1 song" : root.count + " songs"
                color: root.accentColor
                font.family: root.fontFamily
                font.pixelSize: 11
                font.weight: Font.Black
                font.letterSpacing: 1
            }

            Text {
                text: "MAIN"
                color: root.textColor
                font.family: root.fontFamily
                font.pixelSize: 30
                font.weight: Font.Black
            }

            Text {
                width: parent.width
                text: "Your likes from every service, each song once"
                color: root.subtextColor
                elide: Text.ElideRight
                font.family: root.fontFamily
                font.pixelSize: 13
            }

            Row {
                topPadding: 6
                spacing: 8

                Repeater {
                    model: root.services

                    Rectangle {
                        visible: (root.sources[modelData.name] || 0) > 0
                        width: chipRow.implicitWidth + 20
                        height: 26
                        radius: 13
                        color: root.chipColor

                        Row {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: 6

                            Image {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 14
                                height: 14
                                source: modelData.icon
                                sourceSize: Qt.size(48, 48)
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.sources[modelData.name] || 0
                                color: root.textColor
                                font.family: root.fontFamily
                                font.pixelSize: 11
                                font.weight: Font.Bold
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: playButton
            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            width: 56
            height: 56
            radius: 28
            visible: root.count > 0
            color: root.accentColor
            scale: playMouse.containsMouse ? 1.06 : 1
            Behavior on scale { NumberAnimation { duration: 100 } }

            Image {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: 2
                width: 24
                height: 24
                source: "qrc:/assets/play.svg"
                sourceSize: Qt.size(64, 64)
                layer.enabled: true
                layer.effect: ColorOverlay { color: root.accentTextColor }
            }

            MouseArea {
                id: playMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.played()
            }
        }
    }
}
