pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.desktop as Desktop
import org.kde.kirigami as Kirigami

Window {
    id: bar

    required property var readCursorPosition
    required property int stripHeight
    property var view: null
    property bool interactive: false
    readonly property string menuTitle: "org.kde.kwin-window-tabs.menu"
    readonly property int activeIndex: view ? view.activeIndex : -1

    signal selected(string id)
    signal detached(string id)
    signal ungrouped()

    title: "org.kde.kwin-window-tabs.bar"
    flags: Qt.X11BypassWindowManagerHint | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    visible: view !== null
    color: "transparent"
    x: view ? Math.round(view.rect.x) : 0
    y: view ? Math.round(view.rect.y) : 0
    width: view ? Math.max(1, Math.round(view.rect.width)) : 1
    height: stripHeight

    function revealActiveTab() {
        if (activeIndex >= 0 && activeIndex < tabList.count) {
            tabList.positionViewAtIndex(activeIndex, ListView.Contain);
        }
    }

    function openTabMenu(tabId, position) {
        if (!visible || !interactive || !view?.tabs.some(tab => tab.id === tabId)) {
            return;
        }

        const area = view.workArea;
        tabMenu.tabId = tabId;
        tabMenu.currentIndex = -1;
        tabMenu.open();
        const popupWindow = tabMenu.contentItem.Window.window;
        if (popupWindow && popupWindow !== bar) {
            popupWindow.title = menuTitle;
            const width = popupWindow.width;
            const height = popupWindow.height;
            const x = Math.max(area.x, Math.min(position.x, area.x + area.width - width));
            const y = Math.max(area.y, Math.min(position.y, area.y + area.height - height));
            popupWindow.setGeometry(Math.round(x), Math.round(y), width, height);
        }
    }

    onActiveIndexChanged: Qt.callLater(revealActiveTab)
    onVisibleChanged: if (!visible) tabMenu.close()
    onInteractiveChanged: if (!interactive) tabMenu.close()
    onViewChanged: {
        if (tabMenu.visible && !view?.tabs.some(tab => tab.id === tabMenu.tabId)) {
            tabMenu.close();
        }
    }

    Rectangle {
        anchors.fill: parent
        Kirigami.Theme.colorSet: Kirigami.Theme.Header
        Kirigami.Theme.inherit: false
        color: Kirigami.Theme.backgroundColor
        border.color: Qt.alpha(Kirigami.Theme.textColor, 0.2)
        radius: Kirigami.Units.smallSpacing

        ListView {
            id: tabList

            width: parent.width
            height: bar.stripHeight
            clip: true
            orientation: ListView.Horizontal
            boundsBehavior: Flickable.StopAtBounds
            model: bar.view ? bar.view.tabs : []
            onCountChanged: Qt.callLater(bar.revealActiveTab)

            Controls.ScrollBar.horizontal: Controls.ScrollBar {
                policy: Controls.ScrollBar.AsNeeded
            }

            delegate: Rectangle {
                id: tab

                required property int index
                required property var modelData
                readonly property bool selected: index === bar.activeIndex

                width: Math.max(Kirigami.Units.gridUnit * 5, tabList.width / Math.max(1, tabList.count))
                height: bar.stripHeight
                radius: Kirigami.Units.smallSpacing
                color: selected ? Kirigami.Theme.highlightColor
                    : mouse.containsMouse ? Qt.alpha(Kirigami.Theme.highlightColor, 0.18) : "transparent"

                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.caption
                Accessible.selected: selected
                Accessible.onPressAction: bar.selected(modelData.id)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Kirigami.Units.smallSpacing * 2
                    anchors.rightMargin: Kirigami.Units.smallSpacing * 2
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        source: tab.modelData.icon
                    }

                    Controls.Label {
                        Layout.fillWidth: true
                        text: tab.modelData.caption
                        color: tab.selected ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: true

                    onClicked: event => {
                        if (event.button === Qt.RightButton) {
                            const position = bar.readCursorPosition();
                            Qt.callLater(bar.openTabMenu, tab.modelData.id, position);
                        } else {
                            tabMenu.close();
                            bar.selected(tab.modelData.id);
                        }
                    }
                }
            }
        }
    }

    Desktop.Menu {
        id: tabMenu

        property string tabId: ""

        parent: bar.contentItem
        popupType: Controls.Popup.Window
        closePolicy: Controls.Popup.CloseOnEscape

        Desktop.MenuItem {
            text: i18n("Detach Window")
            icon.name: "tab-detach"
            onTriggered: bar.detached(tabMenu.tabId)
        }

        Desktop.MenuItem {
            text: i18n("Ungroup Windows")
            icon.name: "object-ungroup"
            onTriggered: bar.ungrouped()
        }
    }
}
