import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Window
import org.kde.kirigami as Kirigami

Window {
    id: hint

    property var view: null
    property bool suppressed: false

    title: "org.kde.kwin-window-tabs.drop-hint"
    flags: Qt.X11BypassWindowManagerHint | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
    visible: view !== null && !suppressed
    color: "transparent"
    x: view ? Math.round(view.rect.x) : 0
    y: view ? Math.round(view.rect.y) : 0
    width: view ? Math.max(1, Math.round(view.rect.width)) : 1
    height: view ? Math.max(1, Math.round(view.rect.height)) : 1

    Rectangle {
        anchors.fill: parent
        Kirigami.Theme.colorSet: Kirigami.Theme.Header
        Kirigami.Theme.inherit: false
        color: hint.view?.armed ? Kirigami.Theme.highlightColor : Kirigami.Theme.backgroundColor
        border.color: Kirigami.Theme.highlightColor
        border.width: 2
        radius: Kirigami.Units.smallSpacing

        Controls.Label {
            anchors.fill: parent
            anchors.leftMargin: Kirigami.Units.smallSpacing * 2
            anchors.rightMargin: Kirigami.Units.smallSpacing * 2
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: hint.view?.armed ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
            text: {
                if (hint.view?.marked) {
                    return i18n("Select another window and press the group shortcut again");
                }
                return hint.view?.armed ? i18n("Release to group windows") : i18n("Drop here to group windows");
            }
        }
    }
}
