import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Window
import org.kde.kirigami as Kirigami

Window {
    id: hint

    required property string translationDomain
    property var view: null
    property bool suppressed: false
    readonly property bool detaching: view?.detaching === true
    readonly property bool accentFill: !detaching && view?.armed === true
    readonly property int horizontalMargin: Kirigami.Units.smallSpacing * 2
    readonly property int preferredDetachWidth: Math.ceil(Math.max(titleMetrics.advanceWidth, titleMetrics.width)
        + label.leftPadding + label.rightPadding + 2 * horizontalMargin)
    readonly property rect hintRect: {
        if (!view) {
            return Qt.rect(0, 0, 1, 1);
        }
        if (view.detaching !== true) {
            return view.rect;
        }

        const { position, area, stripHeight } = view;
        const hintWidth = Math.min(preferredDetachWidth, Kirigami.Units.gridUnit * 28, area.width);
        const hintHeight = Math.min(stripHeight, area.height);
        const gap = Kirigami.Units.largeSpacing;
        const below = position.y + gap;
        const top = below + hintHeight <= area.y + area.height ? below : position.y - gap - hintHeight;
        return Qt.rect(
            Math.max(area.x, Math.min(position.x - hintWidth / 2, area.x + area.width - hintWidth)),
            Math.max(area.y, Math.min(top, area.y + area.height - hintHeight)),
            hintWidth, hintHeight);
    }

    title: "org.kde.kwin-window-tabs.drop-hint"
    flags: Qt.X11BypassWindowManagerHint | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
    visible: view !== null && !suppressed
    color: "transparent"
    x: Math.round(hintRect.x)
    y: Math.round(hintRect.y)
    width: Math.max(1, Math.round(hintRect.width))
    height: Math.max(1, Math.round(hintRect.height))

    TextMetrics {
        id: titleMetrics

        text: hint.detaching ? label.text : ""
        font: label.font
        renderType: label.renderType
    }

    Rectangle {
        anchors.fill: parent
        Kirigami.Theme.colorSet: hint.detaching ? Kirigami.Theme.Window : Kirigami.Theme.Header
        Kirigami.Theme.inherit: false
        color: hint.accentFill ? Kirigami.Theme.highlightColor : Kirigami.Theme.backgroundColor
        border.color: Kirigami.Theme.highlightColor
        border.width: hint.detaching ? 1 : 2
        radius: Kirigami.Units.smallSpacing

        Controls.Label {
            id: label

            anchors.fill: parent
            anchors.leftMargin: hint.horizontalMargin
            anchors.rightMargin: hint.horizontalMargin
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            textFormat: Text.PlainText
            maximumLineCount: 1
            elide: Text.ElideRight
            color: hint.accentFill ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
            text: {
                if (hint.detaching) {
                    const window = hint.view.window;
                    const caption = window && !window.deleted && !window.excludeFromCapture ? window.caption.trim() : "";
                    return caption || i18nd(hint.translationDomain, "Detach Window");
                }
                if (hint.view?.marked) {
                    return i18nd(hint.translationDomain, "Select another window and press the group shortcut again");
                }
                return hint.view?.armed ? i18nd(hint.translationDomain, "Release to group windows")
                    : i18nd(hint.translationDomain, "Drop here to group windows");
            }
        }
    }
}
