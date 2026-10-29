import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Настройки Моей волны: характер, настроение, язык. Варианты с готовыми сидами
// присылает Яндекс (MorphServices.waveSettingsReady), выбор хранит MorphSettings
Popup {
    id: root

    // [{key, name, values: [{name, seed, unspecified}]}]
    property var options: []
    // ключ настройки: сид выбранного значения, пусто значит «любое»
    property var selected: ({})
    property color backgroundColor: "#141414"
    property color textColor: "white"
    property color subtextColor: "#888888"
    property color chipColor: "#1a1a1a"
    property color borderColor: "#333333"
    property color accentColor: "#b57339"
    property string fontFamily: ""

    signal settingPicked(string key, string seed)

    modal: true
    focus: true
    width: 440
    padding: 24
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: root.backgroundColor
        radius: 16
        border.color: root.borderColor
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 18

        Text {
            text: "TUNE MY VIBE"
            color: root.textColor
            font.family: root.fontFamily
            font.pixelSize: 16
            font.weight: Font.Black
        }

        Text {
            visible: root.options.length === 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Yandex hasn't sent the settings yet. Check the token and try again in a moment."
            color: root.subtextColor
            font.family: root.fontFamily
            font.pixelSize: 12
        }

        Repeater {
            model: root.options

            ColumnLayout {
                id: optionColumn
                required property var modelData
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: optionColumn.modelData.name.toUpperCase()
                    color: root.subtextColor
                    font.family: root.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.Black
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 8

                    Repeater {
                        model: optionColumn.modelData.values

                        Rectangle {
                            id: chip
                            required property var modelData
                            property string seed: modelData.unspecified ? "" : modelData.seed
                            property bool active: (root.selected[optionColumn.modelData.key] || "") === seed

                            width: chipText.implicitWidth + 24
                            height: 30
                            radius: 15
                            color: active ? root.accentColor : root.chipColor
                            border.color: active ? root.accentColor : root.borderColor
                            border.width: 1

                            Text {
                                id: chipText
                                anchors.centerIn: parent
                                text: chip.modelData.name
                                color: chip.active ? "black" : root.textColor
                                font.family: root.fontFamily
                                font.pixelSize: 12
                                font.weight: Font.Bold
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.settingPicked(optionColumn.modelData.key, chip.seed)
                            }
                        }
                    }
                }
            }
        }
    }
}
