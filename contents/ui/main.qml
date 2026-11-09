pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kwin 3.0 as KWinApi
import org.kde.kirigami as Kirigami
import "../code/groups.mjs" as Groups

Item {
    id: root

    property var controller: null
    property var tabs: null
    property var hint: null
    property var groupingHint: null
    readonly property string translationDomain: dragInput.translationDomain
    readonly property var workspace: KWinApi.Workspace
    readonly property int stripHeight: Math.round(Kirigami.Units.gridUnit * 1.6)
    readonly property var fullscreenEffects: ["overview", "windowview", "desktopgrid", "cube", "tileseditor"]

    function overlaysBlocked() {
        return fullscreenEffects.some(effect => workspace.isEffectActive(effect));
    }

    function createTabBar(workAreaAt) {
        const bar = tabsComponent.createObject(null, {
            readCursorPosition: () => root.workspace.cursorPos,
            workAreaAt: workAreaAt,
            stripHeight: root.stripHeight
        }) as TabBarWindow;
        if (bar) {
            bar.selected.connect(id => controller.select(id));
            bar.closeRequested.connect(id => controller.closeTab(id));
            bar.reordered.connect((id, targetId, after) => controller.reorder(id, targetId, after));
            bar.detached.connect(id => controller.detach(id));
            bar.droppedOutside.connect((id, position) => controller.detachDragged(bar, id, position));
            bar.ungrouped.connect(() => controller.ungroup());
        }
        return bar;
    }

    Component {
        id: tabsComponent
        TabBarWindow { translationDomain: root.translationDomain }
    }

    Component {
        id: hintComponent
        DropHintWindow {
            translationDomain: root.translationDomain
            view: root.tabs?.detachHint ?? root.groupingHint
        }
    }

    DragInput {
        id: dragInput
        onEscapePressed: root.tabs?.cancelTabDrag()
    }

    Timer {
        id: dropTimer
        onTriggered: root.controller.refresh()
    }

    Timer {
        interval: 120
        repeat: true
        running: (root.tabs !== null && root.tabs.view !== null) || (root.hint !== null && root.hint.view !== null)
        onTriggered: if (root.hint.suppressed !== root.overlaysBlocked()) root.controller.refresh()
    }

    KWinApi.ShortcutHandler {
        name: "kwin-window-tabs-group"
        text: i18nd(root.translationDomain, "Window Groups: mark or group a window")
        sequence: "Meta+G"
        onActivated: root.controller?.markOrJoin()
    }

    KWinApi.ShortcutHandler {
        name: "kwin-window-tabs-previous"
        text: i18nd(root.translationDomain, "Window Groups: previous tab")
        sequence: "Meta+PageUp"
        onActivated: root.controller?.cycle(-1)
    }

    KWinApi.ShortcutHandler {
        name: "kwin-window-tabs-next"
        text: i18nd(root.translationDomain, "Window Groups: next tab")
        sequence: "Meta+PageDown"
        onActivated: root.controller?.cycle(1)
    }

    KWinApi.ShortcutHandler {
        name: "kwin-window-tabs-detach"
        text: i18nd(root.translationDomain, "Window Groups: detach the current tab")
        sequence: "Meta+Shift+G"
        onActivated: root.controller?.detach()
    }

    KWinApi.ShortcutHandler {
        name: "kwin-window-tabs-ungroup"
        text: i18nd(root.translationDomain, "Window Groups: ungroup windows")
        onActivated: root.controller?.ungroup()
    }

    Component.onCompleted: {
        hint = hintComponent.createObject(null);
        if (!hint) {
            throw new Error("Window Groups could not create its drop hint");
        }
        const maximizeArea = KWin.MaximizeArea ?? 2;
        const workArea = output => workspace.clientArea(maximizeArea, output, workspace.currentDesktop);
        controller = Groups.createController(workspace, {
            workArea: window => workArea(window.output),
            stripHeight: () => root.stripHeight,
            isOverlay: window => window.caption === "org.kde.kwin-window-groups.bar" || window.caption === "org.kde.kwin-window-groups.menu"
                || window.caption === hint.title,
            isGroupWindow: window => dragInput.isGroupWindow(window),
            canHostTab: (window, allowMaximized) => dragInput.canHostTab(window, allowMaximized),
            restoredFrame: window => dragInput.restoredFrame(window),
            tabIcon: window => dragInput.tabIcon(window),
            createGroupWindow: window => dragInput.createGroupWindow(window, root.stripHeight, i18nd(root.translationDomain, "Window Groups")),
            createTabBar: () => root.createTabBar(position => workArea(workspace.screenAt(position))),
            groupTitle: caption => i18nd(root.translationDomain, "Window Groups — %1", caption),
            blocked: overlaysBlocked,
            isMetaLeftPressFor: window => dragInput.isMetaLeftPressFor(window),
            isLeftReleaseFor: window => dragInput.isLeftReleaseFor(window),
            waitForDrop: delay => {
                dropTimer.interval = delay;
                dropTimer.restart();
            },
            update: (activeBar, hintView) => {
                tabs = activeBar;
                hint.suppressed = overlaysBlocked();
                groupingHint = hintView;
            }
        });
        controller.start();
    }

    Component.onDestruction: {
        if (controller) {
            controller.stop();
        }
        if (hint) {
            hint.destroy();
        }
    }
}
