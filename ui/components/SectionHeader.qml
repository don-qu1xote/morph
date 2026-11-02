import QtQuick
import QtQuick.Layouts

// Заголовок раздела, одинаковый на всех экранах. compact для настроек:
// мелкий и приглушённый, чтобы группы читались, но не спорили с полями
RowLayout {
    id: root

    property string title: ""
    property string caption: ""
    property string actionText: ""
    property bool compact: false
    property color textColor: "white"
    property color subtextColor: "#888888"
    property color accentColor: "#b57339"
    property string fontFamily: ""

    signal action

    spacing: 10

    Text {
        text: root.title
        color: root.compact ? root.subtextColor : root.textColor
        font.family: root.fontFamily
        font.pixelSize: root.compact ? 11 : 16
        font.weight: Font.Black
        font.letterSpacing: root.compact ? 1.5 : 0
    }

    Text {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignBaseline
        text: root.caption
        color: root.subtextColor
        elide: Text.ElideRight
        font.family: root.fontFamily
        font.pixelSize: 12
    }

    Text {
        visible: root.actionText !== ""
        text: root.actionText
        color: actionMouse.containsMouse ? root.accentColor : root.subtextColor
        font.family: root.fontFamily
        font.pixelSize: 11
        font.weight: Font.Black
        font.letterSpacing: 1

        MouseArea {
            id: actionMouse
            anchors.fill: parent
            anchors.margins: -6
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.action()
        }
    }
}
