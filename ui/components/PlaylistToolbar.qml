import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

// Строка над треками плейлиста: волна по нему, поиск внутри и сортировка.
// Список по фильтру и сортировке собирает тема через MorphServices.arrangeTracks
Item {
    id: root

    property string filter: ""
    property string sort: "custom"
    property int count: 0
    property int total: 0
    property color textColor: "white"
    property color subtextColor: "#888888"
    property color fieldColor: "#1a1a1a"
    property color borderColor: "#333333"
    property color accentColor: "#b57339"
    property color accentTextColor: "black"
    property string fontFamily: ""

    signal waveWanted
    // фильтр или сортировка поменялись, список пора пересобрать
    signal arrangeChanged

    readonly property var sorts: [
        { key: "custom", label: "ORDER" },
        { key: "title", label: "TITLE" },
        { key: "artist", label: "ARTIST" },
        { key: "plays", label: "MOST PLAYED" },
        { key: "duration", label: "LENGTH" }
    ]

    function reset() {
        sort = "custom"
        searchField.text = ""
        filter = ""
    }

    function sortLabel() {
        for (var i = 0; i < sorts.length; i++) {
            if (sorts[i].key === sort) return sorts[i].label
        }
        return sorts[0].label
    }

    implicitHeight: 36

    // печатают быстро: список пересобирается, когда пауза
    Timer {
        id: typing
        interval: 200
        onTriggered: root.arrangeChanged()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 10

        Rectangle {
            implicitWidth: waveRow.implicitWidth + 28
            implicitHeight: 34
            radius: 17
            color: root.accentColor
            opacity: waveMouse.containsMouse ? 0.85 : 1

            RowLayout {
                id: waveRow
                anchors.centerIn: parent
                spacing: 6

                Image {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    source: "qrc:/assets/waveform.svg"
                    sourceSize: Qt.size(32, 32)
                    layer.enabled: true
                    layer.effect: ColorOverlay { color: root.accentTextColor }
                }

                Text {
                    text: "PLAYLIST WAVE"
                    color: root.accentTextColor
                    font.family: root.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.Black
                }
            }

            MouseArea {
                id: waveMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.waveWanted()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.maximumWidth: 320
            implicitHeight: 34
            radius: 17
            color: root.fieldColor
            border.color: searchField.activeFocus ? root.accentColor : root.borderColor
            border.width: 1

            Image {
                id: magnifier
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                height: 14
                source: "qrc:/assets/magnify.svg"
                sourceSize: Qt.size(28, 28)
                layer.enabled: true
                layer.effect: ColorOverlay { color: root.subtextColor }
            }

            TextField {
                id: searchField
                anchors.left: magnifier.right
                anchors.right: parent.right
                anchors.leftMargin: 6
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                background: Item {}
                placeholderText: "Search in playlist"
                placeholderTextColor: root.subtextColor
                color: root.textColor
                font.family: root.fontFamily
                font.pixelSize: 12
                selectByMouse: true
                onTextEdited: {
                    root.filter = text.trim()
                    typing.restart()
                }
            }
        }

        Item { Layout.fillWidth: true }

        Text {
            text: root.filter.length > 0 ? root.count + " of " + root.total : root.total + (root.total === 1 ? " track" : " tracks")
            color: root.subtextColor
            font.family: root.fontFamily
            font.pixelSize: 11
            font.weight: Font.Bold
        }

        // клик переключает на следующую сортировку по кругу
        Rectangle {
            implicitWidth: sortRow.implicitWidth + 24
            implicitHeight: 34
            radius: 17
            color: sortMouse.containsMouse ? root.fieldColor : "transparent"
            border.color: root.sort === "custom" ? root.borderColor : root.accentColor
            border.width: 1

            RowLayout {
                id: sortRow
                anchors.centerIn: parent
                spacing: 6

                Image {
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    source: "qrc:/assets/sort-variant.svg"
                    sourceSize: Qt.size(28, 28)
                    layer.enabled: true
                    layer.effect: ColorOverlay { color: root.textColor }
                }

                Text {
                    text: root.sortLabel()
                    color: root.textColor
                    font.family: root.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.Black
                }
            }

            MouseArea {
                id: sortMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    var next = 0
                    for (var i = 0; i < root.sorts.length; i++) {
                        if (root.sorts[i].key === root.sort) next = (i + 1) % root.sorts.length
                    }
                    root.sort = root.sorts[next].key
                    root.arrangeChanged()
                }
            }
        }
    }
}
