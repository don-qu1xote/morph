import QtQuick

// Заглушка, пока список грузится: те же строки, что будут, только пустые.
// Мигает вся целиком одной анимацией, а не каждая строка своей
Item {
    id: root

    property int rows: 5
    property int columns: 1
    property color baseColor: "#262626"
    property real rowHeight: 54

    implicitHeight: rows * rowHeight

    SequentialAnimation on opacity {
        running: root.visible
        loops: Animation.Infinite
        NumberAnimation { to: 0.45; duration: 700; easing.type: Easing.InOutQuad }
        NumberAnimation { to: 1; duration: 700; easing.type: Easing.InOutQuad }
    }

    Grid {
        anchors.fill: parent
        columns: root.columns
        columnSpacing: 20

        Repeater {
            model: root.rows * root.columns

            Item {
                width: (root.width - (root.columns - 1) * 20) / root.columns
                height: root.rowHeight

                Rectangle {
                    id: square
                    x: 45
                    anchors.verticalCenter: parent.verticalCenter
                    width: 36
                    height: 36
                    radius: 10
                    color: root.baseColor
                }

                Column {
                    anchors.left: square.right
                    anchors.leftMargin: 15
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7

                    Rectangle {
                        width: (index * 37 % 40 + 90)
                        height: 10
                        radius: 5
                        color: root.baseColor
                    }

                    Rectangle {
                        width: (index * 23 % 30 + 50)
                        height: 8
                        radius: 4
                        color: root.baseColor
                    }
                }
            }
        }
    }
}
