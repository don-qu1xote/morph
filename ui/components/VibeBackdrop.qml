import QtQuick

// Живой фон карточки MY VIBE: отсвет акцентного цвета справа и полосы
// эквалайзера снизу. Полосы двигаются, только пока играет волна и окно на
// виду, иначе стоят: анимация впустую греет ноутбук
Item {
    id: root

    property color accentColor: "#b57339"
    property real radius: 25
    property bool playing: false
    property int bars: 56
    property real phase

    function level(i) {
        return 0.12 + 0.88 * Math.abs(Math.sin(i * 0.42 + phase)) * (0.55 + 0.45 * Math.sin(i * 0.17 - phase * 0.6))
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.35; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.rgba(root.accentColor.r, root.accentColor.g, root.accentColor.b, 0.2) }
        }
    }

    Row {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.radius
        anchors.rightMargin: root.radius
        // рамка карточки остаётся видна под полосами
        anchors.bottomMargin: 1
        height: parent.height * 0.38
        spacing: 3

        Repeater {
            model: root.bars

            Rectangle {
                anchors.bottom: parent.bottom
                width: (row.width - (root.bars - 1) * row.spacing) / root.bars
                height: row.height * root.level(index)
                radius: width / 2
                color: root.accentColor
                // слева, под названием, полосы почти не видны
                opacity: 0.06 + 0.3 * index / root.bars
            }
        }
    }

    NumberAnimation on phase {
        from: 0
        to: 2 * Math.PI
        duration: 3600
        loops: Animation.Infinite
        running: root.playing && root.visible && Qt.application.state === Qt.ApplicationActive
    }
}
