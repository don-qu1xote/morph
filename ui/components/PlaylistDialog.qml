import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Окно нового плейлиста, его правки и SAVE AS PLAYLIST. Имя уже подставлено и
// выделено: Enter сразу создаёт, а можно просто начать печатать своё. Занятое
// имя видно до нажатия кнопки, а не тостом после. Что делать с результатом,
// решает тема по accepted
Popup {
    id: root

    // create, edit или save
    property string mode: "create"
    property string originalName: ""
    property var takenNames: []
    // строка под полями: сколько треков сохранится или с какого начнётся
    property string trackLine: ""
    property bool pinterestReady: false
    property color textColor: "white"
    property color subtextColor: "#888888"
    property color cardColor: "#1a1a1a"
    property color fieldColor: "#111111"
    property color borderColor: "#333333"
    property color accentColor: "#b57339"
    property color accentTextColor: "black"
    property color errorColor: "#e5484d"
    property string fontFamily: ""

    signal accepted(string name, string coverUrl)

    readonly property string cleanName: nameField.text.trim()
    readonly property string problem: {
        if (cleanName === "") return "Enter a name"
        if (cleanName !== originalName && takenNames.indexOf(cleanName) !== -1)
            return "You already have a playlist with this name"
        return ""
    }

    function openWith(newMode, name, coverUrl, taken, line) {
        mode = newMode
        originalName = newMode === "edit" ? name : ""
        takenNames = taken
        trackLine = line
        nameField.text = name
        coverField.text = coverUrl
        open()
    }

    function accept() {
        if (problem !== "") return
        accepted(cleanName, coverField.text.trim())
        close()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(400, parent ? parent.width - 32 : 400)
    modal: true
    focus: true
    padding: 22
    background: Rectangle { color: root.cardColor; radius: 16; border.color: root.borderColor }

    onOpened: {
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    contentItem: ColumnLayout {
        spacing: 14

        Text {
            text: root.mode === "edit" ? "EDIT PLAYLIST" : (root.mode === "save" ? "SAVE AS PLAYLIST" : "NEW PLAYLIST")
            color: root.textColor
            font.family: root.fontFamily
            font.pixelSize: 14
            font.weight: Font.Black
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            // так обложка и будет выглядеть: своя картинка или цвет с буквой
            CoverArt {
                Layout.preferredWidth: 76
                Layout.preferredHeight: 76
                radius: 12
                source: coverField.text.trim()
                name: root.cleanName
                fontFamily: root.fontFamily
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: "NAME"
                    color: root.subtextColor
                    font.family: root.fontFamily
                    font.pixelSize: 10
                    font.weight: Font.Black
                    font.letterSpacing: 1
                }

                TextField {
                    id: nameField
                    Layout.fillWidth: true
                    color: root.textColor
                    font.family: root.fontFamily
                    font.pixelSize: 13
                    padding: 10
                    selectByMouse: true
                    selectionColor: root.accentColor
                    selectedTextColor: root.accentTextColor
                    background: Rectangle {
                        color: root.fieldColor
                        radius: 8
                        border.color: root.problem !== "" && nameField.text !== "" ? root.errorColor
                                    : (nameField.activeFocus ? root.accentColor : root.borderColor)
                    }
                    onAccepted: root.accept()
                }

                Text {
                    Layout.fillWidth: true
                    text: root.problem
                    visible: root.problem !== "" && nameField.text !== ""
                    color: root.errorColor
                    wrapMode: Text.WordWrap
                    font.family: root.fontFamily
                    font.pixelSize: 11
                }
            }
        }

        Text {
            text: "COVER"
            color: root.subtextColor
            font.family: root.fontFamily
            font.pixelSize: 10
            font.weight: Font.Black
            font.letterSpacing: 1
        }

        TextField {
            id: coverField
            Layout.fillWidth: true
            placeholderText: "Image link, optional"
            placeholderTextColor: root.subtextColor
            color: root.textColor
            font.family: root.fontFamily
            font.pixelSize: 12
            padding: 10
            selectByMouse: true
            selectionColor: root.accentColor
            selectedTextColor: root.accentTextColor
            background: Rectangle {
                color: root.fieldColor
                radius: 8
                border.color: coverField.activeFocus ? root.accentColor : root.borderColor
            }
            onAccepted: root.accept()
        }

        Text {
            Layout.fillWidth: true
            visible: coverField.text.trim() === ""
            text: root.pinterestReady ? "Leave it empty and morph takes a new picture from your Pinterest board"
                                      : "Leave it empty for a cover in the playlist's own colour"
            color: root.subtextColor
            wrapMode: Text.WordWrap
            font.family: root.fontFamily
            font.pixelSize: 11
        }

        Text {
            Layout.fillWidth: true
            visible: root.trackLine !== ""
            text: root.trackLine
            color: root.textColor
            elide: Text.ElideRight
            font.family: root.fontFamily
            font.pixelSize: 12
            font.weight: Font.Bold
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10

            Item { Layout.fillWidth: true }

            Rectangle {
                implicitWidth: cancelLabel.implicitWidth + 32
                implicitHeight: 36
                radius: 18
                color: cancelMouse.containsMouse ? root.fieldColor : "transparent"
                border.color: root.borderColor

                Text {
                    id: cancelLabel
                    anchors.centerIn: parent
                    text: "CANCEL"
                    color: root.textColor
                    font.family: root.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.Black
                }

                MouseArea {
                    id: cancelMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.close()
                }
            }

            Rectangle {
                implicitWidth: okLabel.implicitWidth + 36
                implicitHeight: 36
                radius: 18
                color: root.accentColor
                opacity: root.problem !== "" ? 0.4 : (okMouse.containsMouse ? 0.85 : 1)

                Text {
                    id: okLabel
                    anchors.centerIn: parent
                    text: root.mode === "create" ? "CREATE" : "SAVE"
                    color: root.accentTextColor
                    font.family: root.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.Black
                }

                MouseArea {
                    id: okMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: root.problem !== "" ? Qt.ArrowCursor : Qt.PointingHandCursor
                    onClicked: root.accept()
                }
            }
        }
    }
}
