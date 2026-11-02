import QtQuick
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

// Что добавить в плейлист, под его треками, как у Spotify и Яндекса. Подбор
// приходит из MorphServices.playlistSuggestions, добавляет тема по added
ColumnLayout {
    id: root

    property var tracks: []
    // contents, title или taste: по чему подобрано
    property string basis: ""
    property bool loading: false
    // плейлист ещё пуст: заголовок зовёт начать
    property bool startingEmpty: false
    property color textColor: "white"
    property color subtextColor: "#888888"
    property color cardColor: "#1a1a1a"
    property color hoverColor: "#262626"
    property color borderColor: "#333333"
    property color accentColor: "#b57339"
    property color accentTextColor: "black"
    property color skeletonColor: "#262626"
    property string fontFamily: ""

    signal added(int index)
    signal refreshWanted

    spacing: 14

    function serviceIcon(name) {
        if (name === "SoundCloud") return "qrc:/assets/soundcloud_icon.svg"
        if (name === "YouTube") return "qrc:/assets/youtube_music_icon.svg"
        return "qrc:/assets/yandex_music_icon.svg"
    }

    function basisText() {
        if (root.basis === "title") return "Picked by the playlist name"
        if (root.basis === "taste") return "Picked from what you like"
        if (root.basis === "contents") return "Picked from what's in this playlist"
        return ""
    }

    SectionHeader {
        Layout.fillWidth: true
        title: root.startingEmpty ? "LET'S FIND SOMETHING FOR YOUR PLAYLIST" : "RECOMMENDED"
        caption: root.basisText()
        actionText: root.loading ? "" : "REFRESH"
        textColor: root.textColor
        subtextColor: root.subtextColor
        accentColor: root.accentColor
        fontFamily: root.fontFamily
        onAction: root.refreshWanted()
    }

    SkeletonRows {
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        visible: root.loading && root.tracks.length === 0
        rows: 4
        baseColor: root.skeletonColor
    }

    Text {
        Layout.fillWidth: true
        visible: !root.loading && root.tracks.length === 0
        text: root.startingEmpty
              ? "Name the playlist after a mood or a genre and morph will suggest tracks for it. Or add any track from Search: right click, ADD TO PLAYLIST"
              : "Nothing new to suggest right now. Try REFRESH in a moment"
        color: root.subtextColor
        wrapMode: Text.WordWrap
        font.family: root.fontFamily
        font.pixelSize: 13
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: rows.implicitHeight + 16
        visible: root.tracks.length > 0
        color: root.cardColor
        radius: 12
        border.color: root.borderColor
        opacity: root.loading ? 0.5 : 1

        Column {
            id: rows
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8

            Repeater {
                model: root.tracks

                Rectangle {
                    width: rows.width
                    height: 56
                    radius: 10
                    color: rowMouse.containsMouse ? root.hoverColor : "transparent"

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                    }

                    CoverArt {
                        id: art
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 40
                        height: 40
                        radius: 8
                        source: modelData.coverUrl || ""
                        name: modelData.title || ""
                        fontFamily: root.fontFamily
                    }

                    Column {
                        anchors.left: art.right
                        anchors.leftMargin: 14
                        anchors.right: addButton.left
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3

                        Text {
                            width: parent.width
                            text: modelData.title || ""
                            color: root.textColor
                            elide: Text.ElideRight
                            font.family: root.fontFamily
                            font.pixelSize: 13
                            font.weight: Font.Bold
                        }

                        Row {
                            width: parent.width
                            spacing: 6

                            Image {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 12
                                height: 12
                                source: root.serviceIcon(modelData.service)
                                sourceSize: Qt.size(24, 24)
                            }

                            Text {
                                width: parent.width - 18
                                text: modelData.artist || ""
                                color: root.subtextColor
                                elide: Text.ElideRight
                                font.family: root.fontFamily
                                font.pixelSize: 12
                            }
                        }
                    }

                    Rectangle {
                        id: addButton
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: addLabel.implicitWidth + 28
                        height: 30
                        radius: 15
                        color: addMouse.containsMouse ? root.accentColor : "transparent"
                        border.color: root.accentColor

                        Text {
                            id: addLabel
                            anchors.centerIn: parent
                            text: "ADD"
                            color: addMouse.containsMouse ? root.accentTextColor : root.accentColor
                            font.family: root.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.Black
                        }

                        MouseArea {
                            id: addMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.added(index)
                        }
                    }
                }
            }
        }
    }
}
