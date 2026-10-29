import QtQuick

// Станции Яндекса на главной: занятия, жанры, настроения. Модель с ролями
// name, seed, imageUrl, color; цвета и шрифт приходят из темы
ListView {
    id: root

    property color textColor: "white"
    property color cardColor: "#1a1a1a"
    property color borderColor: "#333333"
    property color accentColor: "#b57339"
    property string fontFamily: ""
    property string activeSeed: ""

    signal picked(string seed, string name)

    orientation: ListView.Horizontal
    spacing: 12
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    delegate: Item {
        width: 96
        height: root.height

        Rectangle {
            id: tile
            width: parent.width
            height: 72
            radius: 16
            color: model.color ? model.color : root.cardColor
            border.color: root.activeSeed === model.seed ? root.accentColor : root.borderColor
            border.width: root.activeSeed === model.seed ? 2 : 1
            scale: tileMouse.containsMouse ? 1.05 : 1
            Behavior on scale { NumberAnimation { duration: 120 } }

            Image {
                anchors.centerIn: parent
                width: 40
                height: 40
                source: model.imageUrl
                sourceSize: Qt.size(80, 80)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
        }

        Text {
            anchors.top: tile.bottom
            anchors.topMargin: 6
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: model.name
            color: root.textColor
            elide: Text.ElideRight
            font.family: root.fontFamily
            font.pixelSize: 11
            font.weight: Font.Bold
        }

        MouseArea {
            id: tileMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.picked(model.seed, model.name)
        }
    }
}
