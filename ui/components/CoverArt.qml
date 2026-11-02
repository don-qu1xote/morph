import QtQuick
import Qt5Compat.GraphicalEffects

// Обложка с запасным видом: пока картинки нет или она не грузится, цвет и
// буква берутся из названия. Так у каждого плейлиста свой цвет, а не
// одинаковый серый квадрат с нотой
Item {
    id: root

    property string source: ""
    property string name: ""
    property real radius: 12
    property string fontFamily: ""
    // иконка вместо буквы, например сердце у MAIN
    property string icon: ""
    // свой цвет вместо цвета из названия: MAIN красится в акцент темы
    property color tint: "transparent"

    readonly property bool hasImage: image.status === Image.Ready
    readonly property real hue: {
        let h = 0
        for (let i = 0; i < name.length; ++i) h = (h * 31 + name.charCodeAt(i)) % 360
        return h / 360
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        visible: !root.hasImage
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.tint.a > 0 ? Qt.lighter(root.tint, 1.15) : Qt.hsla(root.hue, 0.45, 0.42, 1) }
            GradientStop { position: 1.0; color: root.tint.a > 0 ? Qt.darker(root.tint, 1.6) : Qt.hsla((root.hue + 0.08) % 1, 0.5, 0.2, 1) }
        }

        Text {
            anchors.centerIn: parent
            visible: root.icon === ""
            text: root.name.length > 0 ? root.name.charAt(0).toUpperCase() : ""
            color: "#e6ffffff"
            font.family: root.fontFamily
            font.pixelSize: Math.max(10, Math.round(root.height * 0.42))
            font.weight: Font.Black
        }

        Image {
            anchors.centerIn: parent
            visible: root.icon !== ""
            width: Math.round(root.height * 0.36)
            height: width
            source: root.icon
            sourceSize: Qt.size(96, 96)
            layer.enabled: visible
            layer.effect: ColorOverlay { color: "white" }
        }
    }

    Image {
        id: image
        anchors.fill: parent
        source: root.source ? MorphCache.getCachedCover(root.source) : ""
        // обложки в лентах мелкие, а памяти на эйре мало: оригинал не держим
        sourceSize.width: 300
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        visible: root.hasImage
        layer.enabled: visible
        layer.effect: OpacityMask {
            maskSource: Rectangle { width: image.width; height: image.height; radius: root.radius }
        }
        onStatusChanged: if (status === Image.Ready && source.toString().startsWith("http")) MorphCache.cacheCover(source)
    }
}
