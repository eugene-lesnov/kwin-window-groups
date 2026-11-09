const dropDelay = 160;

function frameOf(window) {
    const frame = window.frameGeometry;
    return Qt.rect(frame.x, frame.y, frame.width, frame.height);
}

function samePosition(first, second) {
    return Math.abs(first.x - second.x) < 2 && Math.abs(first.y - second.y) < 2;
}

function contains(rect, point) {
    return point.x >= rect.x && point.x < rect.x + rect.width
        && point.y >= rect.y && point.y < rect.y + rect.height;
}

function intersects(first, second) {
    return first.x < second.x + second.width && first.x + first.width > second.x
        && first.y < second.y + second.height && first.y + first.height > second.y;
}

function scopeOf(window) {
    return JSON.stringify([
        window.output?.name,
        Array.from(window.desktops, desktop => String(desktop.id)).sort(),
        Array.from(window.activities).sort()
    ]);
}

function isApplicationWindow(window) {
    return window && !window.deleted && window.managed && window.normalWindow;
}

function fits(window, width, height) {
    const min = window.minSize;
    return width >= Math.max(1, min.width) && height >= Math.max(1, min.height);
}

export function createController(workspace, ui) {
    const windows = new Map();
    const groups = new Set();
    const workspaceConnections = [];
    let drag = null;
    let marked = null;
    let stopped = false;

    function listen(signal, handler, connections) {
        signal.connect(handler);
        connections.push({ signal, handler });
    }

    function disconnect(connections) {
        for (const { signal, handler } of connections) {
            signal.disconnect(handler);
        }
    }

    function entryFor(window) {
        return window ? windows.get(String(window.internalId)) : null;
    }

    function focusedEntry() {
        return focusOwner() ?? groupForWindow(workspace.activeWindow)?.active;
    }

    function groupForWindow(window) {
        if (window) {
            for (const group of groups) {
                if (group.host.window === window) {
                    return group;
                }
            }
        }
        return null;
    }

    function focusOwner() {
        let window = workspace.activeWindow;
        while (window) {
            const entry = entryFor(window);
            if (entry?.group || !window.transientFor) {
                return entry;
            }
            window = window.transientFor;
        }
        return null;
    }

    function maximizeMode(window) {
        if (window.maximizeMode !== undefined) {
            return window.maximizeMode;
        }
        const area = ui.workArea(window);
        const frame = window.frameGeometry;
        const vertical = Math.abs(frame.y - area.y) < 2 && Math.abs(frame.height - area.height) < 2;
        const horizontal = Math.abs(frame.x - area.x) < 2 && Math.abs(frame.width - area.width) < 2;
        return (vertical ? 1 : 0) | (horizontal ? 2 : 0);
    }

    function canGroup(entry, allowMaximized = false) {
        const window = entry?.window;
        return isApplicationWindow(window) && window.output && window.wantsInput
            && window.moveable && window.resizeable && window.minimizable && window.closeable
            && !window.transient && !window.modal && !window.fullScreen
            && !window.shade && (allowMaximized || !maximizeMode(window))
            && (entry.attached ? !window.tile && entry.group.host.canKeepTab(window)
                : !window.skipTaskbar && ui.canHostTab(window, allowMaximized));
    }

    function isOnWorkspace(window) {
        const desktop = workspace.currentDesktop;
        return (window.onAllDesktops || Array.from(window.desktops).some(item => desktop && item.id === desktop.id))
            && (!window.activities.length || Array.from(window.activities).includes(workspace.currentActivity));
    }

    function isVisible(window) {
        return window && !window.deleted && !window.minimized && !window.hidden && isOnWorkspace(window);
    }

    function fitFrame(window, frame) {
        const area = ui.workArea(window);
        const width = Math.min(frame.width, area.width);
        const height = Math.min(frame.height, area.height);
        return Qt.rect(
            Math.max(area.x, Math.min(frame.x, area.x + area.width - width)),
            Math.max(area.y, Math.min(frame.y, area.y + area.height - height)),
            width, height
        );
    }

    function canPair(source, target) {
        if (!source || !target || source === target || source.group
            || !canGroup(source) || !canGroup(target, !target.group)
            || target.window.move || target.window.resize
            || !isVisible(source.window) || !isVisible(target.window)
            || scopeOf(source.window) !== scopeOf(target.window)) {
            return false;
        }
        const group = target.group;
        if (group && (group.active !== target || !group.host.window || group.host.window.minimized)) {
            return false;
        }
        const frame = (group?.host.window ?? target.window).clientGeometry;
        const stripUsesExistingHeight = group || (maximizeMode(target.window) & 1);
        return fits(source.window, frame.width, frame.height - (stripUsesExistingHeight ? ui.stripHeight() : 0));
    }

    function attachTab(group, entry) {
        const window = entry.window;
        if (window.move || window.resize) {
            return false;
        }
        const tile = window.tile;
        const frame = tile ? ui.restoredFrame(window) : null;
        if (tile && !tile.unmanage?.(window)) {
            return false;
        }
        if (window.tile || !group.host.attachTab(window)) {
            if (!window.deleted && !window.tile) {
                tile?.manage?.(window);
            }
            return false;
        }
        if (tile) {
            entry.originalTile = tile;
            entry.originalFrame = frame;
        }
        entry.attached = true;
        return true;
    }

    function release(entry, keepPosition, handledNatively = false) {
        const { window, group, attached } = entry;
        entry.group = null;
        entry.attached = false;
        if (!handledNatively && attached && window && !window.deleted) {
            const handledMaximization = group.host.detachTab(window);
            const tile = entry.originalTile;
            const restoreTile = tile?.manage && !tile.isLayout;
            if (!handledMaximization && (!keepPosition || restoreTile) && window.output
                && !window.fullScreen && !window.tile && !maximizeMode(window)) {
                window.frameGeometry = fitFrame(window, entry.originalFrame);
                if (restoreTile) {
                    tile.manage(window);
                } else if (entry.originalMaximize) {
                    window.setMaximize(!!(entry.originalMaximize & 1), !!(entry.originalMaximize & 2));
                }
            }
        }
        entry.originalFrame = null;
        entry.originalMaximize = 0;
        entry.originalTile = null;
    }

    function dissolve(group, keepPosition = null, activateSurvivor = null) {
        groups.delete(group);
        group.host.changed.disconnect(scheduleRefresh);
        group.host.closeRequested.disconnect(group.close);
        group.bar.view = null;
        group.bar.destroy();
        const deferred = activateSurvivor !== null && keepPosition?.attached === true
            && group.host.deferLastTabRelease?.(keepPosition.window, activateSurvivor) === true;
        for (const entry of group.members) {
            release(entry, entry === keepPosition, deferred && entry === keepPosition);
        }
        if (!deferred) {
            group.host.dispose();
        }
        return deferred;
    }

    function requestSelection(entry) {
        if (!entry?.group || !canGroup(entry) || ui.blocked()) {
            return;
        }
        entry.group.active = entry;
        entry.group.focusPending = true;
        refresh();
    }

    function reconcile() {
        for (const group of groups) {
            const window = group.host.window;
            const invalid = group.members.find(entry => !entry.window.move && !entry.window.resize
                && !canGroup(entry, !window && entry === group.pendingMaximizedAnchor));
            if (group.host.failed || invalid) {
                dissolve(group, invalid);
                continue;
            }
            if (!window) {
                continue;
            }
            if (group.pendingMaximizedAnchor) {
                const frame = window.clientGeometry;
                if (group.members.some(entry => !fits(entry.window, frame.width, frame.height - ui.stripHeight()))) {
                    dissolve(group);
                    continue;
                }
            }
            const unattached = group.members.find(entry => !entry.attached && !attachTab(group, entry));
            if (unattached) {
                dissolve(group, unattached);
                continue;
            }
            group.pendingMaximizedAnchor = null;

            const focused = focusOwner();
            const activeWindow = workspace.activeWindow;
            const ownsFocus = focused?.group === group;
            if (ownsFocus && (!group.focusPending || activeWindow !== focused.window)) {
                group.active = focused;
            }
            const visible = isVisible(window);
            group.host.setTitle(ui.groupTitle(group.active.window.caption));
            group.host.placeTab(group.active.window, true, visible);
            if (visible && !window.move && !window.resize
                && (activeWindow === window || (group.focusPending && (ownsFocus || !activeWindow)))) {
                group.host.focusTab(group.active.window, true);
            } else if (visible && ownsFocus && group.lastFocus !== String(activeWindow.internalId)) {
                group.host.focusTab(group.active.window, false);
            }
            group.lastFocus = ownsFocus ? String(activeWindow.internalId) : null;
            group.focusPending = false;
            for (const entry of group.members) {
                if (entry !== group.active) {
                    group.host.placeTab(entry.window, false, false);
                }
            }
        }
    }

    function join(source, target, originalFrame, originalMaximize = 0) {
        if (!canPair(source, target) || ui.blocked()) {
            return;
        }
        let group = target.group;
        if (group) {
            if (!attachTab(group, source)) {
                return;
            }
        } else {
            const host = ui.createGroupWindow(target.window);
            if (!host) {
                return;
            }
            const bar = ui.createTabBar();
            if (!bar || !host.setTabBar) {
                bar?.destroy();
                host.dispose();
                return;
            }
            host.setTabBar(bar);
            group = {
                host, bar, members: [target], active: target, focusPending: false, lastFocus: null,
                pendingMaximizedAnchor: maximizeMode(target.window) ? target : null
            };
            group.close = () => {
                if (group.host.window?.minimized) {
                    group.host.window.minimized = false;
                    refresh();
                }
                for (const entry of group.members.slice()) {
                    if (!entry.window.deleted && entry.window.closeable) {
                        entry.window.closeWindow();
                    }
                }
            };
            host.changed.connect(scheduleRefresh);
            host.closeRequested.connect(group.close);
            groups.add(group);
            target.group = group;
            target.originalFrame = frameOf(target.window);
        }
        source.originalFrame = source.originalFrame ?? originalFrame;
        source.originalMaximize = originalMaximize;
        source.group = group;
        group.members.push(source);
        group.tabs = null;
        requestSelection(source);
    }

    function removeFromGroup(entry, closing, dropPosition = null) {
        const group = entry.group;
        if (!group || !groups.has(group)) {
            return;
        }
        const dropped = dropPosition !== null;
        if (dropped) {
            const frame = entry.originalFrame;
            if (!entry.attached || !group.host.detachTabAt(entry.window, dropPosition, Qt.size(frame.width, frame.height))) {
                return;
            }
        }
        const index = group.members.indexOf(entry);
        const wasActive = entry === group.active;
        const shouldFocus = closing && wasActive && (group.lastFocus === entry.id || focusedEntry() === entry);
        group.members.splice(index, 1);
        group.tabs = null;
        if (closing) {
            entry.group = null;
        } else {
            release(entry, false, dropped);
        }
        if (wasActive) {
            group.active = group.members[Math.min(index, group.members.length - 1)];
            group.focusPending = shouldFocus;
        }
        if (group.members.length <= 1) {
            const deferred = dissolve(group, group.active, closing || dropped ? shouldFocus : null);
            if (!deferred && shouldFocus && group.active && isVisible(group.active.window)) {
                workspace.activeWindow = group.active.window;
            }
        }
    }

    function dropTarget(source, point) {
        if (source.window.tile) {
            return null;
        }
        for (const window of Array.from(workspace.windowAt(point, -1))) {
            if (entryFor(window) === source || ui.isOverlay(window)) {
                continue;
            }
            const target = entryFor(window) ?? groupForWindow(window)?.active;
            return canPair(source, target) ? target : null;
        }
        return null;
    }

    function dropZone(entry) {
        const frame = (entry.group?.host.window ?? entry.window).frameGeometry;
        return Qt.rect(frame.x, frame.y, frame.width, Math.min(frame.height, ui.stripHeight() * 2));
    }

    function startMove(entry) {
        if (entry.group || !entry.window.move || !ui.isMetaLeftPressFor(entry.window)
            || !canGroup(entry, true) || ui.blocked()) {
            return;
        }
        marked = null;
        drag = {
            source: entry, originalFrame: frameOf(entry.window), originalMaximize: maximizeMode(entry.window),
            tileRestoreFrame: entry.window.tile ? ui.restoredFrame(entry.window) : null,
            hovered: null, since: 0
        };
        scheduleRefresh();
    }

    function endMove(entry) {
        if (drag && drag.source === entry) {
            const moved = drag;
            drag = null;
            const point = workspace.cursorPos;
            const target = ui.isLeftReleaseFor(entry.window) ? dropTarget(entry, point) : null;
            if (target && target === moved.hovered && Date.now() - moved.since >= dropDelay
                && contains(dropZone(target), point)) {
                Qt.callLater(() => {
                    if (!stopped && !drag && windows.get(entry.id) === entry && windows.get(target.id) === target
                        && !entry.window.move && !entry.window.tile
                        && !samePosition(entry.window.frameGeometry, moved.originalFrame)) {
                        const originalFrame = moved.tileRestoreFrame
                            ?? (moved.originalMaximize ? frameOf(entry.window) : moved.originalFrame);
                        join(entry, target, originalFrame, moved.originalMaximize);
                    }
                });
            }
        }
        scheduleRefresh();
    }

    function dragHint() {
        const point = workspace.cursorPos;
        const target = samePosition(drag.source.window.frameGeometry, drag.originalFrame)
            ? null : dropTarget(drag.source, point);
        const zone = target ? dropZone(target) : null;
        const hovered = zone && contains(zone, point) ? target : null;
        const now = Date.now();
        if (hovered !== drag.hovered) {
            drag.hovered = hovered;
            drag.since = now;
        }
        const remaining = dropDelay - (now - drag.since);
        if (hovered && remaining > 0) {
            ui.waitForDrop(remaining);
        }
        return zone ? { rect: zone, armed: hovered !== null && remaining <= 0, marked: false } : null;
    }

    function covered(window, rect) {
        const order = Array.from(workspace.stackingOrder);
        const index = order.findIndex(item => String(item.internalId) === String(window.internalId));
        return index < 0 || order.slice(index + 1).some(item => !ui.isOverlay(item)
            && !item.outline && !item.desktopWindow && isVisible(item) && intersects(item.frameGeometry, rect));
    }

    function updateTabBar(group, focused, blocked) {
        const window = group.host.window;
        const frame = window.clientGeometry;
        const height = ui.stripHeight();
        const rect = Qt.rect(frame.x, frame.y, frame.width, height);
        const area = ui.workArea(window);
        const interactive = focused === group.active && isVisible(window) && !blocked && !covered(window, rect);
        if (!group.tabs) {
            group.tabs = group.members.map(entry => {
                entry.icon = entry.icon ?? ui.tabIcon(entry.window);
                return { id: entry.id, caption: entry.window.caption, icon: entry.icon };
            });
            group.bar.tabs = group.tabs;
        }
        group.bar.view = {
            rect, workArea: area, activeIndex: group.members.indexOf(group.active),
            windowForTab: id => windows.get(id)?.window
        };
        group.bar.interactive = interactive;
        group.host.setTabBarInteractive(interactive);
    }

    function refresh() {
        if (stopped) {
            return;
        }
        reconcile();
        const focused = focusedEntry();
        const blocked = ui.blocked();
        for (const group of groups) {
            if (group.host.window) {
                updateTabBar(group, focused, blocked);
            }
        }
        let hint = drag ? dragHint() : null;
        if (!drag && marked && focused === marked && canGroup(marked)) {
            hint = { rect: dropZone(marked), armed: false, marked: true };
        }
        ui.update(focused?.group?.bar ?? null, hint);
    }

    function scheduleRefresh() {
        if (!stopped) {
            Qt.callLater(refresh);
        }
    }

    function register(window) {
        if (!isApplicationWindow(window) || ui.isOverlay(window) || ui.isGroupWindow(window) || entryFor(window)) {
            return;
        }
        const entry = {
            id: String(window.internalId), window, group: null, originalFrame: null, originalMaximize: 0,
            originalTile: null, attached: false, icon: null, connections: []
        };
        windows.set(entry.id, entry);
        const listenTo = (signal, handler) => listen(signal, handler, entry.connections);
        const groupChanged = () => { if (entry.group || entry === marked || drag) scheduleRefresh(); };
        const tabChanged = () => {
            if (entry.group) {
                entry.group.tabs = null;
                scheduleRefresh();
            }
        };
        const iconChanged = () => {
            entry.icon = null;
            tabChanged();
        };
        listenTo(window.interactiveMoveResizeStarted, () => startMove(entry));
        listenTo(window.interactiveMoveResizeFinished, () => endMove(entry));
        listenTo(window.frameGeometryChanged, groupChanged);
        listenTo(window.minimizedChanged, () => {
            if (entry.attached && entry.group.active === entry && window.minimized && entry.group.host.window) {
                entry.group.host.window.minimized = true;
            }
            groupChanged();
        });
        listenTo(window.captionChanged, tabChanged);
        listenTo(window.iconChanged, iconChanged);
        listenTo(window.desktopFileNameChanged, iconChanged);
        listenTo(window.windowClassChanged, iconChanged);
        for (const signal of [window.outputChanged, window.desktopsChanged, window.activitiesChanged,
            window.fullScreenChanged, window.maximizedChanged, window.tileChanged, window.requestedTileChanged,
            window.transientChanged, window.modalChanged, window.skipTaskbarChanged, window.decorationPolicyChanged,
            window.decorationChanged, window.closeableChanged]) {
            listenTo(signal, groupChanged);
        }
        if (window.shadeChanged) {
            listenTo(window.shadeChanged, groupChanged);
        }
        listenTo(window.closed, () => unregister(entry));
    }

    function unregister(entry) {
        if (!entry || windows.get(entry.id) !== entry) {
            return;
        }
        disconnect(entry.connections);
        if (drag && drag.source === entry) {
            drag = null;
        }
        if (marked === entry) {
            marked = null;
        }
        removeFromGroup(entry, true);
        windows.delete(entry.id);
        scheduleRefresh();
    }

    function workspaceChanged() {
        drag = null;
        marked = null;
        for (const group of groups) {
            group.bar.cancelTabDrag();
        }
        scheduleRefresh();
    }

    return {
        start() {
            listen(workspace.windowAdded, window => { register(window); scheduleRefresh(); }, workspaceConnections);
            listen(workspace.windowRemoved, window => { unregister(entryFor(window)); scheduleRefresh(); }, workspaceConnections);
            listen(workspace.windowActivated, scheduleRefresh, workspaceConnections);
            listen(workspace.currentDesktopChanged, workspaceChanged, workspaceConnections);
            listen(workspace.currentActivityChanged, workspaceChanged, workspaceConnections);
            listen(workspace.screensChanged, workspaceChanged, workspaceConnections);
            listen(workspace.cursorPosChanged, () => { if (drag) scheduleRefresh(); }, workspaceConnections);
            for (const window of Array.from(workspace.stackingOrder)) {
                register(window);
            }
        },
        stop() {
            stopped = true;
            for (const group of groups) {
                dissolve(group);
            }
            disconnect(workspaceConnections);
            for (const entry of windows.values()) {
                disconnect(entry.connections);
            }
            windows.clear();
        },
        refresh,
        markOrJoin() {
            const focused = focusedEntry();
            if (!focused || !canGroup(focused) || !isVisible(focused.window) || ui.blocked()) {
                return;
            }
            const target = marked && (marked.group ? marked.group.active : marked);
            if (!target) {
                marked = focused;
            } else if (target === focused) {
                marked = null;
            } else if (canPair(focused, target)) {
                marked = null;
                join(focused, target, frameOf(focused.window));
            } else if (!canGroup(target) || !isVisible(target.window)) {
                marked = focused;
            }
            refresh();
        },
        select(id) {
            const group = focusedEntry()?.group;
            const entry = windows.get(id);
            if (group && entry && entry.group === group && entry !== group.active) {
                requestSelection(entry);
            }
        },
        closeTab(id) {
            const group = focusedEntry()?.group;
            const entry = windows.get(id);
            if (stopped || !group || entry?.group !== group || !group.bar.interactive || ui.blocked()) {
                return;
            }
            if (entry !== group.active) {
                requestSelection(entry);
            }
            const window = entry.window;
            if (entry.group === group && entry === group.active && window?.closeable && !window.deleted) {
                window.closeWindow();
            }
        },
        reorder(id, targetId, after) {
            const group = focusedEntry()?.group;
            const entry = windows.get(id);
            const target = windows.get(targetId);
            if (stopped || !group || entry?.group !== group || target?.group !== group || entry === target
                || !group.bar.interactive || ui.blocked()) {
                return;
            }

            const sourceIndex = group.members.indexOf(entry);
            const boundary = group.members.indexOf(target) + Number(after);
            const destination = boundary - Number(sourceIndex < boundary);
            if (sourceIndex !== destination) {
                group.members.splice(sourceIndex, 1);
                group.members.splice(destination, 0, entry);
                group.tabs = null;
                refresh();
            }
        },
        detach(id) {
            const focused = focusedEntry();
            const entry = id === undefined ? focused : windows.get(id);
            if (focused && focused.group && entry && focused.group === entry.group && !ui.blocked()) {
                removeFromGroup(entry, false);
                refresh();
            }
        },
        detachDragged(bar, id, position) {
            const entry = windows.get(id);
            if (stopped || entry?.group?.bar !== bar || ui.blocked() || !canGroup(entry)) {
                return;
            }
            removeFromGroup(entry, false, position);
            refresh();
        },
        ungroup() {
            const group = focusedEntry()?.group;
            if (group && !ui.blocked()) {
                dissolve(group);
                refresh();
            }
        }
    };
}
