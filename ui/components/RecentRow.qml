import QtQuick
import Qt5Compat.GraphicalEffects

// Недавно игравшее на всех площадках, свежее слева. Треки приходят из
// MorphServices.recentTracks: своя история, а не яндексовская
ListView {
    id: root

    property var tracks: []
    property string currentId: ""
    property color textColor: "white"
    property color subtextColor: "#888888"
    property color accentColor: "#b57339"
    property color accentTextColor: "black"
    property string fontFamily: ""

    signal played(int index)

    model: tracks
    orientation: ListView.Horizontal
    spacing: 14
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    implicitHeight: 178

    function serviceIcon(name) {
        if (name === "SoundCloud") return "qrc:/assets/soundcloud_icon.svg"
        if (name === "YouTube") return "qrc:/assets/youtube_music_icon.svg"
        return "qrc:/assets/yandex_music_icon.svg"
    }

    delegate: Item {
        width: 128
        height: root.height

        readonly property bool isCurrent: root.currentId !== "" && root.currentId === modelData.id

        CoverArt {
            id: art
            width: 128
            height: 128
            radius: 16
            source: modelData.coverUrl || ""
            name: modelData.title || ""
            fontFamily: root.fontFamily
        }

        Rectangle {
            anchors.fill: art
            radius: 16
            color: Qt.rgba(root.accentColor.r, root.accentColor.g, root.accentColor.b, 0.65)
            visible: cardMouse.containsMouse

            Image {
                anchors.centerIn: parent
                width: 36
                height: 36
                source: "qrc:/assets/play.svg"
                sourceSize: Qt.size(72, 72)
                layer.enabled: true
                layer.effect: ColorOverlay { color: "white" }
            }
        }

        Rectangle {
            anchors.right: art.right
            anchors.bottom: art.bottom
            anchors.margins: 8
            width: 22
            height: 22
            radius: 11
            color: "#b3000000"

            Image {
                anchors.centerIn: parent
                width: 14
                height: 14
                source: root.serviceIcon(modelData.service)
                sourceSize: Qt.size(48, 48)
            }
        }

        Text {
            id: titleText
            anchors.top: art.bottom
            anchors.topMargin: 8
            width: parent.width
            text: modelData.title || ""
            color: isCurrent ? root.accentColor : root.textColor
            elide: Text.ElideRight
            font.family: root.fontFamily
            font.pixelSize: 12
            font.weight: Font.Bold
        }

        Text {
            anchors.top: titleText.bottom
            anchors.topMargin: 2
            width: parent.width
            text: modelData.artist || ""
            color: root.subtextColor
            elide: Text.ElideRight
            font.family: root.fontFamily
            font.pixelSize: 11
        }

        MouseArea {
            id: cardMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.played(index)
        }
    }
}
