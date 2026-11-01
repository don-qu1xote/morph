import QtQuick

// Подпись поверх обложки плейлиста: название и строка под ним на затемнении
// снизу. У миксов от morph обложка это обложка трека, без подписи их не отличить
Item {
    id: root

    property string title: ""
    property string subtitle: ""
    property string fontFamily: ""
    property real radius: 0

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * 0.6
        radius: root.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: "#d8000000" }
        }
    }

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        spacing: 1

        Text {
            width: parent.width
            text: root.title
            color: "white"
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            font.family: root.fontFamily
            font.pixelSize: 13
            font.weight: Font.Black
        }

        Text {
            visible: root.subtitle.length > 0
            width: parent.width
            text: root.subtitle
            color: "#cccccc"
            elide: Text.ElideRight
            font.family: root.fontFamily
            font.pixelSize: 10
            font.weight: Font.Bold
        }
    }
}
