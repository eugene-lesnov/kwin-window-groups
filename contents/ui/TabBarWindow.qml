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
    property var insertion: null
    readonly property string menuTitle: "org.kde.kwin-window-tabs.menu"
    readonly property int activeIndex: view ? view.activeIndex : -1

    signal selected(string id)
    signal closeRequested(string id)
    signal reordered(string id, string targetId, bool after)
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

    function insertionAt(source, point) {
        if (!interactive || !tabList.contains(point)) {
            return null;
        }

        const contentPoint = tabList.mapToItem(tabList.contentItem, point);
        const targetIndex = tabList.indexAt(contentPoint.x, contentPoint.y);
        const target = tabList.itemAtIndex(targetIndex);
        if (!target) {
            return null;
        }

        const rightHalf = contentPoint.x >= target.x + target.width / 2;
        const after = rightHalf !== (tabList.effectiveLayoutDirection === Qt.RightToLeft);
        const boundary = targetIndex + Number(after);
        if (boundary === source.index || boundary === source.index + 1) {
            return null;
        }

        return {
            targetId: view.tabs[targetIndex].id,
            after,
            x: target.x - tabList.contentX + (rightHalf ? target.width : 0)
        };
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
    onInteractiveChanged: {
        if (!interactive) {
            insertion = null;
            tabMenu.close();
        }
    }
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
            onModelChanged: bar.insertion = null
            onContentXChanged: bar.insertion = null

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
                    z: 1
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

                    Desktop.ToolButton {
                        id: closeButton

                        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                        Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                        enabled: bar.interactive
                        focusPolicy: Qt.NoFocus
                        icon.name: "window-close"
                        icon.width: Kirigami.Units.iconSizes.small
                        icon.height: Kirigami.Units.iconSizes.small
                        icon.color: tab.selected ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                        Accessible.name: i18n("Close Window")

                        Rectangle {
                            anchors.fill: closeButton
                            visible: tab.selected && closeButton.enabled && closeButton.hovered
                            color: "transparent"
                            radius: Kirigami.Units.cornerRadius
                            border.color: closeButton.icon.color
                        }

                        onClicked: {
                            tabMenu.close();
                            Qt.callLater(bar.closeRequested, tab.modelData.id);
                        }
                    }
                }

                MouseArea {
                    id: mouse

                    property point pressPosition
                    property bool dragging: false

                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: true
                    preventStealing: true

                    onPressed: event => {
                        if (event.button === Qt.LeftButton) {
                            pressPosition = mapToItem(tabList, event.x, event.y);
                            dragging = false;
                        }
                    }

                    onPositionChanged: event => {
                        if (!(pressedButtons & Qt.LeftButton) || !bar.interactive) {
                            return;
                        }
                        const point = mapToItem(tabList, event.x, event.y);
                        if (!dragging && Math.hypot(point.x - pressPosition.x, point.y - pressPosition.y) < drag.threshold) {
                            return;
                        }
                        dragging = true;
                        bar.insertion = bar.insertionAt(tab, point);
                    }

                    onReleased: event => {
                        if (event.button === Qt.LeftButton && dragging) {
                            const drop = bar.insertionAt(tab, mapToItem(tabList, event.x, event.y));
                            bar.insertion = null;
                            if (drop) {
                                Qt.callLater(bar.reordered, tab.modelData.id, drop.targetId, drop.after);
                            }
                        }
                    }

                    onCanceled: bar.insertion = null

                    onClicked: event => {
                        if (event.button === Qt.RightButton) {
                            const position = bar.readCursorPosition();
                            Qt.callLater(bar.openTabMenu, tab.modelData.id, position);
                        } else if (!dragging) {
                            tabMenu.close();
                            bar.selected(tab.modelData.id);
                        }
                    }
                }
            }
        }

        Rectangle {
            visible: bar.insertion !== null
            width: Kirigami.Units.smallSpacing
            height: parent.height - 2 * Kirigami.Units.smallSpacing
            x: bar.insertion ? Math.max(0, Math.min(parent.width - width, bar.insertion.x - width / 2)) : 0
            anchors.verticalCenter: parent.verticalCenter
            radius: width / 2
            color: Kirigami.Theme.textColor
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
