import QtQuick
import Qt5Compat.GraphicalEffects

// Пустое место с объяснением: что здесь появится и откуда это взять.
// Пустая рамка выглядит как поломка, а не как «ещё ничего нет»
Item {
    id: root

    property string icon: ""
    property string title: ""
    property string hint: ""
    property string actionText: ""
    property color textColor: "white"
    property color subtextColor: "#888888"
    property color bubbleColor: "#1a1a1a"
    property color accentColor: "#b57339"
    property color accentTextColor: "black"
    property string fontFamily: ""

    signal action

    implicitHeight: column.implicitHeight

    Column {
        id: column
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(380, root.width)
        spacing: 10

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 56
            height: 56
            radius: 28
            color: root.bubbleColor
            visible: root.icon !== ""

            Image {
                anchors.centerIn: parent
                width: 24
                height: 24
                source: root.icon
                sourceSize: Qt.size(64, 64)
                layer.enabled: true
                layer.effect: ColorOverlay { color: root.accentColor }
            }
        }

        Text {
            width: parent.width
            text: root.title
            color: root.textColor
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.family: root.fontFamily
            font.pixelSize: 15
            font.weight: Font.Bold
        }

        Text {
            width: parent.width
            visible: root.hint !== ""
            text: root.hint
            color: root.subtextColor
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            lineHeight: 1.2
            font.family: root.fontFamily
            font.pixelSize: 13
        }

        // отступ над кнопкой
        Item {
            width: 1
            height: 2
            visible: root.actionText !== ""
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: actionLabel.implicitWidth + 32
            height: 34
            radius: 17
            visible: root.actionText !== ""
            color: root.accentColor
            opacity: actionMouse.containsMouse ? 0.85 : 1

            Text {
                id: actionLabel
                anchors.centerIn: parent
                text: root.actionText
                color: root.accentTextColor
                font.family: root.fontFamily
                font.pixelSize: 11
                font.weight: Font.Black
                font.letterSpacing: 1
            }

            MouseArea {
                id: actionMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.action()
            }
        }
    }
}
