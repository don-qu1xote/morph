import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

// Сервер для телефона в настройках: включить, что вписать в Amperfy и видно ли
// телефон. Пароль показан открыто, его всё равно переписывают в телефон руками.
// Высота раздела не зависит от ширины: пока настройки скрыты, они узкие, и
// Layout вокруг запоминал высоту от переноса строк в узкой колонке
Column {
    id: root

    property color textColor: "white"
    property color subtextColor: "#888888"
    property color cardColor: "#1a1a1a"
    property color borderColor: "#333333"
    property color accentColor: "#b57339"
    property color trackColor: "#222222"
    property string fontFamily: ""

    signal toast(string message)

    spacing: 10

    // часы для «заходил N минут назад»
    property double now: Date.now()
    Timer {
        interval: 30000; repeat: true
        running: root.visible && MorphPhone.running
        onTriggered: root.now = Date.now()
    }

    function ago(ms) {
        var minutes = Math.floor((root.now - ms) / 60000)
        if (minutes < 1) return "just now"
        if (minutes < 60) return minutes + " min ago"
        var hours = Math.floor(minutes / 60)
        if (hours < 24) return hours + " h ago"
        return Math.floor(hours / 24) + " d ago"
    }

    function setEnabled(on) {
        MorphPhone.setEnabled(on)
        if (!on) toast("Phone server is off")
        else toast(MorphPhone.running ? "Phone server is on" : MorphPhone.error)
    }

    function copy(label, value) {
        MorphSettings.copyToClipboard(value)
        toast(label + " copied")
    }

    component InfoRow: RowLayout {
        id: row
        property string label: ""
        property string value: ""
        property string extra: ""
        signal extraClicked

        spacing: 12

        Text {
            Layout.preferredWidth: 80
            text: row.label
            color: root.subtextColor
            font.family: root.fontFamily; font.pixelSize: 11
        }
        TextEdit {
            Layout.fillWidth: true
            text: row.value
            readOnly: true; selectByMouse: true
            color: root.textColor
            font.family: root.fontFamily; font.pixelSize: 14; font.weight: Font.Medium
        }
        Text {
            visible: row.extra !== ""
            text: row.extra
            color: extraMouse.containsMouse ? root.accentColor : root.subtextColor
            font.family: root.fontFamily; font.pixelSize: 11; font.weight: Font.Black; font.letterSpacing: 1
            MouseArea {
                id: extraMouse
                anchors.fill: parent; anchors.margins: -6
                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: row.extraClicked()
            }
        }
        Text {
            text: "COPY"
            color: copyMouse.containsMouse ? root.accentColor : root.subtextColor
            font.family: root.fontFamily; font.pixelSize: 11; font.weight: Font.Black; font.letterSpacing: 1
            MouseArea {
                id: copyMouse
                anchors.fill: parent; anchors.margins: -6
                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: root.copy(row.label, row.value)
            }
        }
    }

    Button {
        width: root.width; height: 50
        onClicked: root.setEnabled(!MorphPhone.running)
        background: Rectangle { color: root.cardColor; radius: 10; border.color: root.borderColor }
        contentItem: RowLayout {
            anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 15; anchors.rightMargin: 15; spacing: 10
            Image {
                source: "qrc:/assets/cellphone.svg"; Layout.preferredWidth: 20; Layout.preferredHeight: 20
                Layout.alignment: Qt.AlignVCenter
                sourceSize.width: 40; sourceSize.height: 40
                layer.enabled: true; layer.effect: ColorOverlay { color: root.textColor }
            }
            Text {
                text: "Phone Server"; color: root.textColor
                font.family: root.fontFamily; font.pixelSize: 14; font.weight: Font.Medium
                Layout.fillWidth: true; verticalAlignment: Text.AlignVCenter
            }
            Switch {
                id: phoneSwitch
                Layout.alignment: Qt.AlignVCenter; Layout.preferredHeight: 20; padding: 0
                checked: MorphPhone.running
                onToggled: {
                    root.setEnabled(checked)
                    // сервер мог не подняться (порт занят): переключатель показывает, как есть
                    checked = Qt.binding(function() { return MorphPhone.running })
                }
                indicator: Rectangle {
                    implicitWidth: 36; implicitHeight: 20; radius: 10
                    color: phoneSwitch.checked ? root.accentColor : root.trackColor
                    Behavior on color { ColorAnimation { duration: 150 } }
                    Rectangle {
                        x: phoneSwitch.checked ? parent.width - width - 2 : 2; y: 2
                        width: 16; height: 16; radius: 8; color: "white"
                        Behavior on x { NumberAnimation { duration: 150 } }
                    }
                }
            }
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; acceptedButtons: Qt.NoButton }
    }

    Text {
        width: root.width
        visible: MorphPhone.error !== "" && !MorphPhone.running
        text: MorphPhone.error
        color: root.accentColor
        wrapMode: Text.WordWrap
        font.family: root.fontFamily; font.pixelSize: 12
    }

    Rectangle {
        width: root.width
        visible: MorphPhone.running
        height: details.height + 30
        color: root.cardColor; radius: 10; border.color: root.borderColor

        Column {
            id: details
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 15
            spacing: 12

            InfoRow { width: details.width; label: "Server"; value: MorphPhone.address }
            InfoRow { width: details.width; label: "User"; value: MorphPhone.user }
            InfoRow {
                width: details.width
                label: "Password"; value: MorphPhone.password
                extra: "NEW"
                onExtraClicked: {
                    MorphPhone.newPassword()
                    root.toast("New password: enter it in Amperfy again")
                }
            }

            Rectangle { width: details.width; height: 1; color: root.borderColor }

            Text {
                width: details.width
                text: MorphPhone.lastSeen > 0
                      ? MorphPhone.lastClient + " connected " + root.ago(MorphPhone.lastSeen)
                      : "Waiting for your phone"
                color: root.textColor
                font.family: root.fontFamily; font.pixelSize: 13
            }
            Text {
                width: details.width
                text: MorphPhone.mixCount > 0
                      ? (MorphPhone.mixCount === 1 ? "1 mix" : MorphPhone.mixCount + " mixes") + " ready for the phone"
                      : "Daily mixes for the phone are built once a day"
                color: root.subtextColor
                font.family: root.fontFamily; font.pixelSize: 12
            }
            Text {
                width: details.width
                text: "In Amperfy pick Subsonic and enter these three lines. Then turn on Newest Songs under " +
                      "Auto Cache in its account settings: new likes and daily mixes download by themselves " +
                      "while the phone is on the same Wi-Fi as this computer."
                color: root.subtextColor
                height: 30
                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                font.family: root.fontFamily; font.pixelSize: 12
            }
        }
    }
}
