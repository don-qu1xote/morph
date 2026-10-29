import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

// Боковая панель с двумя вкладками. «Очередь»: текущий трек, очередь, которую
// собрали руками, и продолжение списка, который играет. «Текст»: слова песни,
// у синхронного текста текущая строка подсвечивается, по клику перемотка
Popup {
    id: root

    property string tab: "queue"
    property var currentTrack: null
    property var queued: []
    property var upcoming: []
    property string contextName: ""
    // [{timeMs, text}]; lyricsState: "loading", "ready" или "missing"
    property var lyricsLines: []
    property bool lyricsSynced: false
    property string lyricsSource: ""
    property string lyricsState: ""
    property real position: 0
    property color backgroundColor: "#141414"
    property color textColor: "white"
    property color subtextColor: "#888888"
    property color hoverColor: "#222222"
    property color borderColor: "#333333"
    property color accentColor: "#b57339"
    property string fontFamily: ""

    signal playQueued(int index)
    signal removeQueued(int index)
    signal clearQueue
    signal playUpcoming(int offset)
    // текст нужен: открыли вкладку или сменился трек, пока она открыта
    signal lyricsWanted
    signal seekRequested(real timeMs)

    width: 340
    padding: 16
    modal: false
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: root.backgroundColor
        radius: 16
        border.color: root.borderColor
        border.width: 1
    }

    // последняя строка, чьё время уже наступило; чуть заранее, чтобы глаз успевал
    readonly property int activeLine: {
        if (!lyricsSynced) return -1
        var active = -1
        for (var i = 0; i < lyricsLines.length; i++) {
            if (lyricsLines[i].timeMs > position + 300) break
            active = i
        }
        return active
    }

    onTabChanged: if (tab === "lyrics" && opened) lyricsWanted()
    onOpened: if (tab === "lyrics") lyricsWanted()
    onCurrentTrackChanged: if (tab === "lyrics" && opened) lyricsWanted()

    // строки списка по порядку: в какой секции, какой трек и откуда он
    readonly property var rows: {
        var out = []
        if (currentTrack) out.push({ section: "NOW PLAYING", track: currentTrack, kind: "current", position: 0 })
        for (var i = 0; i < queued.length; i++)
            out.push({ section: "NEXT IN QUEUE", track: queued[i], kind: "queued", position: i })
        for (var j = 0; j < upcoming.length; j++)
            out.push({ section: "NEXT FROM " + contextName, track: upcoming[j], kind: "upcoming", position: j })
        return out
    }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Repeater {
                model: [{ key: "queue", label: "QUEUE" }, { key: "lyrics", label: "LYRICS" }]

                Text {
                    required property var modelData
                    text: modelData.label
                    color: root.tab === modelData.key ? root.textColor : root.subtextColor
                    font.family: root.fontFamily
                    font.pixelSize: 16
                    font.weight: Font.Black

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.tab = parent.modelData.key
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                visible: root.tab === "queue" && root.queued.length > 0
                text: "CLEAR"
                color: clearMouse.containsMouse ? root.textColor : root.subtextColor
                font.family: root.fontFamily
                font.pixelSize: 11
                font.weight: Font.Black

                MouseArea {
                    id: clearMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.clearQueue()
                }
            }

            Image {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                source: "qrc:/assets/close.svg"
                sourceSize: Qt.size(32, 32)
                layer.enabled: true
                layer.effect: ColorOverlay { color: root.subtextColor }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.close()
                }
            }
        }

        Text {
            visible: root.tab === "queue" && root.rows.length === 0
            text: "Nothing is playing yet"
            color: root.subtextColor
            font.family: root.fontFamily
            font.pixelSize: 12
        }

        ListView {
            id: list
            visible: root.tab === "queue"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            model: root.rows

            delegate: Column {
                id: row
                required property var modelData
                required property int index
                width: list.width
                spacing: 4

                // заголовок секции у первой строки секции
                Text {
                    visible: row.index === 0 || root.rows[row.index - 1].section !== row.modelData.section
                    topPadding: row.index === 0 ? 0 : 12
                    text: row.modelData.section
                    color: root.subtextColor
                    elide: Text.ElideRight
                    width: parent.width
                    font.family: root.fontFamily
                    font.pixelSize: 10
                    font.weight: Font.Black
                }

                Rectangle {
                    width: parent.width
                    height: 52
                    radius: 8
                    color: rowMouse.containsMouse && row.modelData.kind !== "current" ? root.hoverColor : "transparent"

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: row.modelData.kind === "current" ? Qt.ArrowCursor : Qt.PointingHandCursor
                        onClicked: {
                            if (row.modelData.kind === "queued") root.playQueued(row.modelData.position)
                            else if (row.modelData.kind === "upcoming") root.playUpcoming(row.modelData.position)
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            radius: 6
                            color: root.hoverColor
                            clip: true

                            Image {
                                anchors.fill: parent
                                source: row.modelData.track.coverUrl || ""
                                sourceSize: Qt.size(80, 80)
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                Layout.fillWidth: true
                                text: row.modelData.track.title || ""
                                color: row.modelData.kind === "current" ? root.accentColor : root.textColor
                                elide: Text.ElideRight
                                font.family: root.fontFamily
                                font.pixelSize: 13
                                font.weight: Font.Bold
                            }

                            Text {
                                Layout.fillWidth: true
                                text: row.modelData.track.artist || ""
                                color: root.subtextColor
                                elide: Text.ElideRight
                                font.family: root.fontFamily
                                font.pixelSize: 11
                            }
                        }

                        Image {
                            visible: row.modelData.kind === "queued"
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                            source: "qrc:/assets/close.svg"
                            sourceSize: Qt.size(32, 32)
                            layer.enabled: true
                            layer.effect: ColorOverlay { color: root.subtextColor }

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -6
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.removeQueued(row.modelData.position)
                            }
                        }
                    }
                }
            }
        }

        Text {
            visible: root.tab === "lyrics" && root.lyricsState !== "ready"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: !root.currentTrack ? "Nothing is playing yet"
                : root.lyricsState === "missing" ? "No lyrics for this track" : "Looking for lyrics..."
            color: root.subtextColor
            font.family: root.fontFamily
            font.pixelSize: 12
        }

        ListView {
            id: lyricsList
            visible: root.tab === "lyrics" && root.lyricsState === "ready"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            boundsBehavior: Flickable.StopAtBounds
            model: root.lyricsLines
            // текущая строка держится в верхней трети, прокрутка плавная
            currentIndex: root.activeLine
            highlightRangeMode: root.lyricsSynced ? ListView.ApplyRange : ListView.NoHighlightRange
            preferredHighlightBegin: height * 0.3
            preferredHighlightEnd: height * 0.4
            highlightMoveDuration: 300

            delegate: Text {
                required property var modelData
                required property int index
                width: lyricsList.width
                wrapMode: Text.WordWrap
                text: modelData.text.length > 0 ? modelData.text : "♪"
                color: !root.lyricsSynced || index === root.activeLine ? root.textColor : root.subtextColor
                opacity: !root.lyricsSynced || index >= root.activeLine ? 1 : 0.6
                font.family: root.fontFamily
                font.pixelSize: index === root.activeLine ? 18 : 16
                font.weight: Font.Bold
                Behavior on font.pixelSize { NumberAnimation { duration: 150 } }

                MouseArea {
                    anchors.fill: parent
                    enabled: root.lyricsSynced
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.seekRequested(parent.modelData.timeMs)
                }
            }
        }

        Text {
            visible: root.tab === "lyrics" && root.lyricsState === "ready" && root.lyricsSource.length > 0
            text: "Lyrics: " + root.lyricsSource
            color: root.subtextColor
            font.family: root.fontFamily
            font.pixelSize: 10
        }
    }
}
