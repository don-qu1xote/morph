import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

// Страница артиста: шапка с фото, жанрами и слушателями, твоя статистика,
// популярные треки, твои песни из MAIN, дискография и похожие артисты.
// Данные приходят из MorphServices.artistReady, что делать по клику, решает тема
Popup {
    id: root

    // name, imageUrl, genres, listeners, popular, albums, similar, yours, inMain, plays
    property var info: ({})
    property bool loading: false
    property string currentTrackId: ""
    property color backgroundColor: "#141414"
    property color cardColor: "#1a1a1a"
    property color textColor: "white"
    property color subtextColor: "#888888"
    property color hoverColor: "#222222"
    property color borderColor: "#333333"
    property color accentColor: "#b57339"
    property color accentTextColor: "black"
    property string fontFamily: ""

    signal playTracks(var tracks, int index)
    signal waveWanted(string name)
    signal albumWanted(var album)
    signal artistWanted(string name)

    readonly property var popular: info.popular || []
    readonly property var yours: info.yours || []
    readonly property var albums: info.albums || []
    readonly property var similar: info.similar || []
    property bool showAllPopular: false

    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onInfoChanged: {
        showAllPopular = false
        flick.contentY = 0
    }

    background: Rectangle {
        color: root.backgroundColor
        radius: 20
        border.color: root.borderColor
        border.width: 1
    }

    function duration(ms) {
        var s = Math.round((ms || 0) / 1000)
        var m = Math.floor(s / 60)
        s = s % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }

    function listeners(n) {
        if (n >= 1000000) return (n / 1000000).toFixed(1).replace(".0", "") + "M"
        if (n >= 1000) return Math.round(n / 1000) + "K"
        return "" + n
    }

    // строка про тебя: сколько песен в MAIN и сколько раз играл
    function personalLine() {
        var parts = []
        if (info.inMain > 0) parts.push(info.inMain + (info.inMain === 1 ? " song" : " songs") + " in your MAIN")
        if (info.plays > 0) parts.push("played " + info.plays + (info.plays === 1 ? " time" : " times"))
        return parts.join(" · ")
    }

    component Pill: Rectangle {
        id: pill
        property string label: ""
        property bool primary: false
        signal clicked

        implicitWidth: pillText.implicitWidth + 32
        implicitHeight: 34
        radius: 17
        color: primary ? root.accentColor : (pillMouse.containsMouse ? root.hoverColor : "transparent")
        border.color: primary ? root.accentColor : root.borderColor
        border.width: 1

        Text {
            id: pillText
            anchors.centerIn: parent
            text: pill.label
            color: pill.primary ? root.accentTextColor : root.textColor
            font.family: root.fontFamily
            font.pixelSize: 12
            font.weight: Font.Black
        }

        MouseArea {
            id: pillMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pill.clicked()
        }
    }

    component SectionTitle: Text {
        color: root.textColor
        font.family: root.fontFamily
        font.pixelSize: 15
        font.weight: Font.Black
        Layout.topMargin: 6
    }

    component TrackRow: Rectangle {
        id: row
        property var track: ({})
        property int number: 0
        property bool current: track.id !== undefined && String(track.id) === root.currentTrackId
        signal picked

        Layout.fillWidth: true
        implicitHeight: 52
        radius: 10
        color: rowMouse.containsMouse || current ? root.hoverColor : "transparent"

        MouseArea {
            id: rowMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: row.picked()
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 14
            spacing: 12

            Text {
                Layout.preferredWidth: 22
                text: row.number
                horizontalAlignment: Text.AlignRight
                color: row.current ? root.accentColor : root.subtextColor
                font.family: root.fontFamily
                font.pixelSize: 12
                font.weight: Font.Bold
            }

            Rectangle {
                Layout.preferredWidth: 38
                Layout.preferredHeight: 38
                radius: 8
                color: root.cardColor
                Image {
                    anchors.fill: parent
                    source: row.track.coverUrl || ""
                    sourceSize: Qt.size(76, 76)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    layer.enabled: true
                    layer.effect: OpacityMask { maskSource: Rectangle { width: 38; height: 38; radius: 8 } }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: row.track.title || ""
                    color: row.current ? root.accentColor : root.textColor
                    elide: Text.ElideRight
                    font.family: root.fontFamily
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: row.track.album || ""
                    color: root.subtextColor
                    elide: Text.ElideRight
                    font.family: root.fontFamily
                    font.pixelSize: 11
                }
            }

            Text {
                text: root.duration(row.track.durationMs)
                color: root.subtextColor
                font.family: root.fontFamily
                font.pixelSize: 11
            }
        }
    }

    contentItem: Flickable {
        id: flick
        clip: true
        contentHeight: column.implicitHeight + 56
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: column
            x: 28
            y: 28
            width: flick.width - 56
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 24

                Rectangle {
                    Layout.preferredWidth: 150
                    Layout.preferredHeight: 150
                    radius: 75
                    color: root.cardColor

                    Text {
                        anchors.centerIn: parent
                        visible: avatar.status !== Image.Ready
                        text: (root.info.name || "?").charAt(0).toUpperCase()
                        color: root.subtextColor
                        font.family: root.fontFamily
                        font.pixelSize: 56
                        font.weight: Font.Black
                    }

                    Image {
                        id: avatar
                        anchors.fill: parent
                        source: root.info.imageUrl || ""
                        sourceSize: Qt.size(300, 300)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        layer.enabled: true
                        layer.effect: OpacityMask { maskSource: Rectangle { width: 150; height: 150; radius: 75 } }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: "ARTIST"
                        color: root.subtextColor
                        font.family: root.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.Black
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.info.name || ""
                        color: root.textColor
                        elide: Text.ElideRight
                        font.family: root.fontFamily
                        font.pixelSize: 32
                        font.weight: Font.Black
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: [root.info.genres || "", root.info.listeners > 0 ? root.listeners(root.info.listeners) + " listeners a month" : ""]
                              .filter(function(s) { return s.length > 0 }).join(" · ")
                        color: root.subtextColor
                        elide: Text.ElideRight
                        font.family: root.fontFamily
                        font.pixelSize: 12
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: root.personalLine()
                        color: root.accentColor
                        elide: Text.ElideRight
                        font.family: root.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }

                    RowLayout {
                        Layout.topMargin: 8
                        spacing: 10

                        Pill {
                            label: "PLAY"
                            primary: true
                            visible: root.popular.length > 0
                            onClicked: root.playTracks(root.popular, 0)
                        }
                        Pill {
                            label: "ARTIST WAVE"
                            onClicked: root.waveWanted(root.info.name || "")
                        }
                        Pill {
                            label: "YOUR SONGS"
                            visible: root.yours.length > 0
                            onClicked: root.playTracks(root.yours, 0)
                        }
                    }
                }
            }

            Text {
                visible: root.loading || (root.popular.length === 0 && root.albums.length === 0)
                Layout.topMargin: 10
                text: root.loading ? "Loading..." : "Nothing found for this artist"
                color: root.subtextColor
                font.family: root.fontFamily
                font.pixelSize: 13
            }

            SectionTitle {
                visible: root.popular.length > 0
                text: "POPULAR"
            }

            Repeater {
                model: root.popular.slice(0, root.showAllPopular ? 20 : 5)
                TrackRow {
                    required property var modelData
                    required property int index
                    track: modelData
                    number: index + 1
                    onPicked: root.playTracks(root.popular, index)
                }
            }

            Text {
                visible: root.popular.length > 5
                text: root.showAllPopular ? "SHOW LESS" : "SHOW MORE"
                color: moreMouse.containsMouse ? root.textColor : root.subtextColor
                font.family: root.fontFamily
                font.pixelSize: 11
                font.weight: Font.Black
                MouseArea {
                    id: moreMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.showAllPopular = !root.showAllPopular
                }
            }

            SectionTitle {
                visible: root.yours.length > 0
                text: "IN YOUR MAIN"
            }

            Repeater {
                model: root.yours.slice(0, 5)
                TrackRow {
                    required property var modelData
                    required property int index
                    track: modelData
                    number: index + 1
                    onPicked: root.playTracks(root.yours, index)
                }
            }

            SectionTitle {
                visible: root.albums.length > 0
                text: "DISCOGRAPHY"
            }

            ListView {
                visible: root.albums.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: 196
                orientation: ListView.Horizontal
                spacing: 16
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.albums

                delegate: Item {
                    id: albumCard
                    required property var modelData
                    width: 140
                    height: 196

                    Rectangle {
                        id: albumCover
                        width: 140
                        height: 140
                        radius: 14
                        color: root.cardColor
                        border.color: albumMouse.containsMouse ? root.accentColor : "transparent"
                        border.width: 1.5

                        Image {
                            anchors.fill: parent
                            anchors.margins: 1
                            source: albumCard.modelData.coverUrl || ""
                            sourceSize: Qt.size(280, 280)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            layer.enabled: true
                            layer.effect: OpacityMask { maskSource: Rectangle { width: 138; height: 138; radius: 13 } }
                        }
                    }

                    Text {
                        anchors.top: albumCover.bottom
                        anchors.topMargin: 8
                        width: parent.width
                        text: albumCard.modelData.title || ""
                        color: root.textColor
                        elide: Text.ElideRight
                        font.family: root.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }

                    Text {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        text: [albumCard.modelData.year > 0 ? albumCard.modelData.year : "",
                               albumCard.modelData.type === "single" ? "Single" : (albumCard.modelData.trackCount > 0 ? albumCard.modelData.trackCount + " tracks" : "")]
                              .filter(function(s) { return ("" + s).length > 0 }).join(" · ")
                        color: root.subtextColor
                        elide: Text.ElideRight
                        font.family: root.fontFamily
                        font.pixelSize: 11
                    }

                    MouseArea {
                        id: albumMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.albumWanted(albumCard.modelData)
                    }
                }
            }

            SectionTitle {
                visible: root.similar.length > 0
                text: "FANS ALSO LIKE"
            }

            ListView {
                visible: root.similar.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: 140
                orientation: ListView.Horizontal
                spacing: 18
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.similar

                delegate: Item {
                    id: similarCard
                    required property var modelData
                    width: 104
                    height: 140

                    Rectangle {
                        id: similarAvatar
                        width: 104
                        height: 104
                        radius: 52
                        color: root.cardColor
                        border.color: similarMouse.containsMouse ? root.accentColor : "transparent"
                        border.width: 1.5

                        Text {
                            anchors.centerIn: parent
                            text: (similarCard.modelData.name || "?").charAt(0).toUpperCase()
                            color: root.subtextColor
                            font.family: root.fontFamily
                            font.pixelSize: 34
                            font.weight: Font.Black
                        }

                        Image {
                            anchors.fill: parent
                            anchors.margins: 1
                            source: similarCard.modelData.imageUrl || ""
                            sourceSize: Qt.size(208, 208)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            layer.enabled: true
                            layer.effect: OpacityMask { maskSource: Rectangle { width: 102; height: 102; radius: 51 } }
                        }
                    }

                    Text {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: similarCard.modelData.name || ""
                        color: root.textColor
                        elide: Text.ElideRight
                        font.family: root.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }

                    MouseArea {
                        id: similarMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.artistWanted(similarCard.modelData.name || "")
                    }
                }
            }
        }
    }
}
