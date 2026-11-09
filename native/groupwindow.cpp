#include "groupwindow.h"

#include "viewporter-client.h"
#include "xdg-decoration-unstable-v1-client.h"
#include "xdg-shell-client.h"
#include "xdg-toplevel-icon-v1-client.h"

#include <config-kwin.h>
#include <internalwindow.h>
#include <scene/windowitem.h>
#include <wayland/clientconnection.h>
#include <wayland/plasmawindowmanagement.h>
#include <wayland/surface.h>
#include <wayland_server.h>
#include <window.h>
#include <workspace.h>

#include <QGuiApplication>
#include <QPalette>
#include <QSocketNotifier>
#include <QTimer>
#include <QUuid>
#include <QWindow>
#include <QtMath>
#include <cerrno>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

class GroupWindow::Surface
{
public:
    Surface(GroupWindow *owner, const QSize &size, const QString &title)
        : m_owner(owner)
        , m_size(size)
        , m_title(title)
    {
        const auto connection = KWin::waylandServer()->createConnection();
        if (!connection.connection) {
            return;
        }
        m_client = connection.connection;
        m_display = wl_display_connect_to_fd(connection.fd);
        if (!m_display) {
            m_client->destroy();
            return;
        }

        const int fd = wl_display_get_fd(m_display);
        m_read = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
        m_write = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Write);
        m_write->setEnabled(false);
        QObject::connect(m_read.get(), &QSocketNotifier::activated, m_owner, [this] {
            if (wl_display_dispatch(m_display) < 0) {
                connectionFailed();
                return;
            }
            flush();
        });
        QObject::connect(m_write.get(), &QSocketNotifier::activated, m_owner, [this] { flush(); });

        m_registry = wl_display_get_registry(m_display);
        static constexpr wl_registry_listener registryListener = {
            [](void *data, wl_registry *, uint32_t name, const char *interface, uint32_t) {
                static_cast<Surface *>(data)->bindGlobal(name, interface);
            },
            [](void *, wl_registry *, uint32_t) {},
        };
        wl_registry_add_listener(m_registry, &registryListener, this);
        m_sync = wl_display_sync(m_display);
        static constexpr wl_callback_listener syncListener = {
            [](void *data, wl_callback *callback, uint32_t) {
                auto *self = static_cast<Surface *>(data);
                wl_callback_destroy(callback);
                self->m_sync = nullptr;
                if (!self->createWindow()) {
                    self->m_owner->fail();
                }
            },
        };
        wl_callback_add_listener(m_sync, &syncListener, this);
        flush();
    }

    ~Surface()
    {
        if (!m_display) {
            return;
        }
        m_read.reset();
        m_write.reset();
        destroyProxy(m_iconManager);
        destroyProxy(m_decoration);
        destroyProxy(m_decorationManager);
        destroyProxy(m_toplevel);
        destroyProxy(m_xdgSurface);
        destroyProxy(m_shell);
        destroyProxy(m_viewport);
        destroyProxy(m_viewporter);
        destroyProxy(m_buffer);
        destroyProxy(m_shm);
        destroyProxy(m_surface);
        destroyProxy(m_compositor);
        destroyProxy(m_sync);
        destroyProxy(m_registry);
        wl_display_disconnect(m_display);
    }

    bool owns(const KWin::Window *window) const
    {
        return m_client && window && window->surface() && window->surface()->client() == m_client;
    }

    void setTitle(const QString &title)
    {
        if (m_title == title) {
            return;
        }
        m_title = title;
        if (m_toplevel) {
            xdg_toplevel_set_title(m_toplevel, m_title.toUtf8().constData());
            flush();
        }
    }

    void setMinimumSize(const QSize &size)
    {
        if (m_toplevel && m_minimumSize != size) {
            m_minimumSize = size;
            xdg_toplevel_set_min_size(m_toplevel, size.width(), size.height());
            wl_surface_commit(m_surface);
            flush();
        }
    }

private:
    template<typename T>
    static void destroyProxy(T *proxy)
    {
        if (proxy) {
            wl_proxy_destroy(reinterpret_cast<wl_proxy *>(proxy));
        }
    }

    void bindGlobal(uint32_t name, const char *interface)
    {
        if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
            m_compositor = static_cast<wl_compositor *>(wl_registry_bind(m_registry, name, &wl_compositor_interface, 1));
        } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
            m_shm = static_cast<wl_shm *>(wl_registry_bind(m_registry, name, &wl_shm_interface, 1));
            static constexpr wl_shm_listener listener = {[](void *, wl_shm *, uint32_t) {}};
            wl_shm_add_listener(m_shm, &listener, this);
        } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
            m_shell = static_cast<xdg_wm_base *>(wl_registry_bind(m_registry, name, &xdg_wm_base_interface, 1));
            static constexpr xdg_wm_base_listener listener = {
                [](void *, xdg_wm_base *shell, uint32_t serial) { xdg_wm_base_pong(shell, serial); },
            };
            xdg_wm_base_add_listener(m_shell, &listener, this);
        } else if (std::strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
            m_decorationManager = static_cast<zxdg_decoration_manager_v1 *>(
                wl_registry_bind(m_registry, name, &zxdg_decoration_manager_v1_interface, 1));
        } else if (std::strcmp(interface, wp_viewporter_interface.name) == 0) {
            m_viewporter = static_cast<wp_viewporter *>(wl_registry_bind(m_registry, name, &wp_viewporter_interface, 1));
        } else if (std::strcmp(interface, xdg_toplevel_icon_manager_v1_interface.name) == 0) {
            m_iconManager = static_cast<xdg_toplevel_icon_manager_v1 *>(
                wl_registry_bind(m_registry, name, &xdg_toplevel_icon_manager_v1_interface, 1));
            static constexpr xdg_toplevel_icon_manager_v1_listener listener = {
                [](void *, xdg_toplevel_icon_manager_v1 *, int32_t) {},
                [](void *, xdg_toplevel_icon_manager_v1 *) {},
            };
            xdg_toplevel_icon_manager_v1_add_listener(m_iconManager, &listener, this);
        }
    }

    bool createWindow()
    {
        if (!m_compositor || !m_shm || !m_shell || !m_decorationManager || !m_viewporter) {
            return false;
        }
        const int fd = memfd_create("kwin-window-tabs-window", MFD_CLOEXEC);
        if (fd < 0) {
            return false;
        }
        const QRgb color = QGuiApplication::palette().color(QPalette::Window).rgb();
        if (write(fd, &color, sizeof(color)) != sizeof(color)) {
            close(fd);
            return false;
        }
        auto *pool = wl_shm_create_pool(m_shm, fd, sizeof(color));
        m_buffer = wl_shm_pool_create_buffer(pool, 0, 1, 1, sizeof(color), WL_SHM_FORMAT_XRGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);
        static constexpr wl_buffer_listener bufferListener = {[](void *, wl_buffer *) {}};
        wl_buffer_add_listener(m_buffer, &bufferListener, this);

        m_surface = wl_compositor_create_surface(m_compositor);
        m_viewport = wp_viewporter_get_viewport(m_viewporter, m_surface);
        m_xdgSurface = xdg_wm_base_get_xdg_surface(m_shell, m_surface);
        static constexpr xdg_surface_listener surfaceListener = {
            [](void *data, xdg_surface *surface, uint32_t serial) {
                auto *self = static_cast<Surface *>(data);
                xdg_surface_ack_configure(surface, serial);
                self->commit();
            },
        };
        xdg_surface_add_listener(m_xdgSurface, &surfaceListener, this);
        m_toplevel = xdg_surface_get_toplevel(m_xdgSurface);
        static constexpr auto toplevelListener = [] {
            xdg_toplevel_listener listener{};
            listener.configure = [](void *data, xdg_toplevel *, int32_t width, int32_t height, wl_array *) {
                auto *self = static_cast<Surface *>(data);
                if (width > 0) {
                    self->m_size.setWidth(width);
                }
                if (height > 0) {
                    self->m_size.setHeight(height);
                }
            };
            listener.close = [](void *data, xdg_toplevel *) {
                Q_EMIT static_cast<Surface *>(data)->m_owner->closeRequested();
            };
            return listener;
        }();
        xdg_toplevel_add_listener(m_toplevel, &toplevelListener, this);

        const auto appId = "org.kde.kwinwindowtabs." + QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
        xdg_toplevel_set_app_id(m_toplevel, appId.constData());
        xdg_toplevel_set_title(m_toplevel, m_title.toUtf8().constData());
        if (m_iconManager) {
            auto *icon = xdg_toplevel_icon_manager_v1_create_icon(m_iconManager);
            xdg_toplevel_icon_v1_set_name(icon, "preferences-system-windows");
            xdg_toplevel_icon_manager_v1_set_icon(m_iconManager, m_toplevel, icon);
            xdg_toplevel_icon_v1_destroy(icon);
        }

        m_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(m_decorationManager, m_toplevel);
        static constexpr zxdg_toplevel_decoration_v1_listener decorationListener = {
            [](void *data, zxdg_toplevel_decoration_v1 *, uint32_t mode) {
                if (mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE) {
                    static_cast<Surface *>(data)->m_owner->fail();
                }
            },
        };
        zxdg_toplevel_decoration_v1_add_listener(m_decoration, &decorationListener, this);
        zxdg_toplevel_decoration_v1_set_mode(m_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        wl_surface_commit(m_surface);
        return true;
    }

    void commit()
    {
        wp_viewport_set_destination(m_viewport, m_size.width(), m_size.height());
        xdg_surface_set_window_geometry(m_xdgSurface, 0, 0, m_size.width(), m_size.height());
        wl_surface_attach(m_surface, m_buffer, 0, 0);
        wl_surface_damage(m_surface, 0, 0, m_size.width(), m_size.height());
        wl_surface_commit(m_surface);
    }

    void flush()
    {
        if (wl_display_flush(m_display) >= 0) {
            m_write->setEnabled(false);
        } else if (errno == EAGAIN) {
            m_write->setEnabled(true);
        } else {
            connectionFailed();
        }
    }

    void connectionFailed()
    {
        m_read->setEnabled(false);
        m_write->setEnabled(false);
        m_owner->fail();
    }

    GroupWindow *const m_owner;
    QSize m_size;
    QSize m_minimumSize;
    QString m_title;
    QPointer<KWin::ClientConnection> m_client;
    std::unique_ptr<QSocketNotifier> m_read;
    std::unique_ptr<QSocketNotifier> m_write;
    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    wl_callback *m_sync = nullptr;
    wl_compositor *m_compositor = nullptr;
    wl_shm *m_shm = nullptr;
    wl_buffer *m_buffer = nullptr;
    wl_surface *m_surface = nullptr;
    wp_viewporter *m_viewporter = nullptr;
    wp_viewport *m_viewport = nullptr;
    xdg_wm_base *m_shell = nullptr;
    xdg_surface *m_xdgSurface = nullptr;
    xdg_toplevel *m_toplevel = nullptr;
    zxdg_decoration_manager_v1 *m_decorationManager = nullptr;
    zxdg_toplevel_decoration_v1 *m_decoration = nullptr;
    xdg_toplevel_icon_manager_v1 *m_iconManager = nullptr;
};

class GroupWindow::SceneMember
{
public:
    SceneMember(KWin::WindowItem *item, KWin::Item *container, int z)
        : m_item(item)
        , m_originalParent(item->parentItem())
        , m_placement(container)
    {
        m_placement.setZ(z);
        item->setParentItem(&m_placement);
        item->refVisible(visibilityReasons);
    }

    ~SceneMember()
    {
        if (m_item) {
            m_item->unrefVisible(visibilityReasons);
            m_item->setParentItem(m_originalParent);
        }
    }

    void place(const QPointF &position)
    {
        if (m_item) {
            m_placement.setPosition(position - m_item->position());
        }
    }

private:
    static constexpr int visibilityReasons = KWin::WindowItem::PAINT_DISABLED_BY_HIDDEN
        | KWin::WindowItem::PAINT_DISABLED_BY_MINIMIZE;

    QPointer<KWin::WindowItem> m_item;
    QPointer<KWin::Item> m_originalParent;
    KWin::Item m_placement;
};

static KWin::RectF contentFrame(const KWin::Window *host, const KWin::Window *tab, int stripHeight)
{
    auto frame = host->clientGeometry();
    frame.setTop(frame.top() + stripHeight);
    frame.setSize(frame.size().boundedTo(tab->maxSize()).expandedTo(tab->minSize()));
    return frame;
}

GroupWindow::GroupWindow(KWin::Window *anchor, int stripHeight, const QString &title, QObject *parent)
    : QObject(parent)
    , m_anchor(anchor)
    , m_stripHeight(stripHeight)
    , m_anchorMaximizeMode(anchor->maximizeMode())
    , m_anchorRestoreFrame(anchor->geometryRestore())
{
    if (m_anchorMaximizeMode != KWin::MaximizeRestore) {
        const auto frame = anchor->frameGeometry();
        if (!m_anchorRestoreFrame.isValid()) {
            m_anchorRestoreFrame = frame;
        } else {
            if (!(m_anchorMaximizeMode & KWin::MaximizeHorizontal)) {
                m_anchorRestoreFrame.setLeft(frame.left());
                m_anchorRestoreFrame.setRight(frame.right());
            }
            if (!(m_anchorMaximizeMode & KWin::MaximizeVertical)) {
                m_anchorRestoreFrame.setTop(frame.top());
                m_anchorRestoreFrame.setBottom(frame.bottom());
            }
        }
    }
    connect(KWin::workspace(), &KWin::Workspace::windowAdded, this, &GroupWindow::adoptWindow);
    connect(KWin::workspace(), &KWin::Workspace::stackingOrderChanged, this, [this] {
        if (!m_disposed && m_window) {
            adoptTabBar();
            Q_EMIT changed();
        }
    });
    m_surface = std::make_unique<Surface>(this, anchor->clientSize().toSize() + QSize(0, stripHeight), title);
    QTimer::singleShot(3000, this, [this] {
        if (!m_ready) {
            fail();
        }
    });
}

GroupWindow::~GroupWindow()
{
    m_disposed = true;
    setTabBar(nullptr);
    selectTab(nullptr);
    const auto tabs = m_tabs.keys();
    for (auto *tab : tabs) {
        detachTab(tab);
    }
    restoreAnchorMaximization();
}

bool GroupWindow::canHostTab(const KWin::Window *window, bool allowMaximized)
{
    if (!window || window->isDeleted() || window->isHidden() || window->isMinimized() || !window->surface()
        || !window->windowItem() || !window->windowManagementInterface()) {
        return false;
    }
    return (allowMaximized || (window->maximizeMode() == KWin::MaximizeRestore
        && window->requestedMaximizeMode() == KWin::MaximizeRestore))
        && (!window->nextDecoration() || window->userCanSetNoBorder());
}

bool GroupWindow::ownsWindow(const KWin::Window *window) const
{
    return m_surface && m_surface->owns(window);
}

QObject *GroupWindow::window() const
{
    return m_ready ? m_window.data() : nullptr;
}

bool GroupWindow::failed() const
{
    return m_failed;
}

void GroupWindow::setTitle(const QString &title)
{
    m_surface->setTitle(title);
}

void GroupWindow::adoptWindow(KWin::Window *window)
{
    if (m_disposed || m_failed || m_window || !ownsWindow(window)) {
        return;
    }
    if (!m_anchor || m_anchor->isDeleted() || !m_anchor->output() || !window->windowItem()
        || !window->windowManagementInterface() || !window->isDecorated()) {
        fail();
        return;
    }

    m_window = window;
    window->setSkipCloseAnimation(true);
    window->setDesktops(m_anchor->desktops());
    window->setOnActivities(m_anchor->activities());
    const auto area = KWin::workspace()->clientArea(KWin::MaximizeArea, m_anchor.data());
    auto frame = window->frameGeometry();
    frame.moveTopLeft(m_anchor->frameGeometry().topLeft());
    if (m_anchorMaximizeMode != KWin::MaximizeRestore && m_anchorRestoreFrame.isValid()) {
        frame = m_anchorRestoreFrame;
        frame.setHeight(frame.height() + m_stripHeight);
    }
    window->moveResize(window->keepInArea(frame, area));

    connect(window, &KWin::Window::frameGeometryChanged, this, &GroupWindow::changed);
    connect(window, &KWin::Window::clientGeometryChanged, this, &GroupWindow::changed);
    connect(window, &KWin::Window::frameGeometryChanged, this, &GroupWindow::updateScene);
    connect(window, &KWin::Window::clientGeometryChanged, this, &GroupWindow::updateScene);
    connect(window, &KWin::Window::minimizedChanged, this, &GroupWindow::changed);
    const auto updateBarInput = [this] { setTabBarInteractive(m_barInteractive); };
    connect(window, &KWin::Window::minimizedChanged, this, updateBarInput);
    connect(window, &KWin::Window::hiddenByShowDesktopChanged, this, updateBarInput);
    connect(window, &KWin::Window::desktopsChanged, this, &GroupWindow::changed);
    connect(window, &KWin::Window::activitiesChanged, this, &GroupWindow::changed);
    connect(window, &KWin::Window::interactiveMoveResizeFinished, this, &GroupWindow::changed);
    connect(window, &KWin::Window::excludeFromCaptureChanged, this, &GroupWindow::protectCapture);
    connect(window, &KWin::Window::closed, this, [this] {
        setTabBar(nullptr);
        selectTab(nullptr);
        m_window.clear();
        fail();
    });
    if (m_anchorMaximizeMode == KWin::MaximizeRestore) {
        m_anchor.clear();
        m_ready = true;
        Q_EMIT changed();
        return;
    }

    window->setHidden(true);
    connect(window, &KWin::Window::maximizedChanged, this, &GroupWindow::finishMaximizedSetup, Qt::QueuedConnection);
    connect(m_anchor.data(), &KWin::Window::maximizedChanged, this, &GroupWindow::finishMaximizedSetup, Qt::QueuedConnection);
    window->maximize(m_anchorMaximizeMode, window->moveResizeGeometry());
    finishMaximizedSetup();
}

void GroupWindow::finishMaximizedSetup()
{
    if (m_ready || m_disposed || m_failed) {
        return;
    }
    if (!m_window || m_window->requestedMaximizeMode() != m_anchorMaximizeMode
        || !canHostTab(m_anchor.data(), true) || m_anchor->isRequestedFullScreen()
        || m_anchor->isInteractiveMove() || m_anchor->isInteractiveResize() || m_anchor->requestedTile()) {
        fail();
        return;
    }
    if (m_window->maximizeMode() != m_anchorMaximizeMode) {
        return;
    }
    if (!m_anchorUnmaximized) {
        if (m_anchor->requestedMaximizeMode() != m_anchorMaximizeMode) {
            fail();
            return;
        }
        m_anchor->maximize(KWin::MaximizeRestore);
        m_anchorUnmaximized = m_anchor->requestedMaximizeMode() == KWin::MaximizeRestore;
    }
    if (!m_anchorUnmaximized || m_anchor->requestedMaximizeMode() != KWin::MaximizeRestore) {
        fail();
        return;
    }
    if (m_anchor->maximizeMode() == KWin::MaximizeRestore) {
        m_ready = true;
        m_window->setHidden(false);
        Q_EMIT changed();
    }
}

bool GroupWindow::canRestoreAnchorMaximization() const
{
    return m_anchorUnmaximized && m_anchor && !m_anchor->isDeleted() && m_anchor->output()
        && m_anchor->requestedMaximizeMode() == KWin::MaximizeRestore && !m_anchor->isRequestedFullScreen()
        && !m_anchor->requestedTile() && !m_anchor->isInteractiveMove() && !m_anchor->isInteractiveResize();
}

bool GroupWindow::restoreAnchorMaximization()
{
    const bool restore = canRestoreAnchorMaximization();
    auto *anchor = m_anchor.data();
    m_anchor.clear();
    if (!m_anchorUnmaximized || !anchor || anchor->isDeleted()) {
        return false;
    }
    if (restore) {
        auto frame = m_anchorRestoreFrame;
        if (frame.isValid()) {
            const auto area = KWin::workspace()->clientArea(KWin::MaximizeArea, anchor);
            frame = anchor->keepInArea(frame, area);
            anchor->moveResize(frame);
        }
        anchor->maximize(m_anchorMaximizeMode, frame);
    }
    return true;
}

void GroupWindow::setTabBar(QObject *object)
{
    auto *handle = qobject_cast<QWindow *>(object);
    if (handle && m_barHandle == handle) {
        return;
    }
    disconnect(m_barGeometry);
    disconnect(m_barClosed);
    m_barScene.reset();
    if (m_barWindow && !m_barWindow->isDeleted()) {
        m_barWindow->setHidden(true);
    }
    m_barWindow.clear();
    m_barHandle = handle;
    adoptTabBar();
}

void GroupWindow::adoptTabBar()
{
    if (!m_window || !m_barHandle || m_barWindow || m_disposed) {
        return;
    }
    for (auto *window : KWin::workspace()->stackingOrder()) {
        auto *bar = qobject_cast<KWin::InternalWindow *>(window);
        if (!bar || bar->isDeleted() || bar->handle() != m_barHandle || !bar->windowItem()) {
            continue;
        }
        m_barWindow = bar;
        bar->setSkipCloseAnimation(true);
        m_barScene = std::make_unique<SceneMember>(bar->windowItem(), m_window->windowItem()->windowContainer(), 2);
        m_barGeometry = connect(bar, &KWin::Window::frameGeometryChanged, this, &GroupWindow::updateScene);
        m_barClosed = connect(bar, &KWin::Window::closed, this, [this] {
            m_barScene.reset();
            m_barWindow.clear();
        });
        updateScene();
        setTabBarInteractive(m_barInteractive);
        return;
    }
}

void GroupWindow::setTabBarInteractive(bool interactive)
{
    m_barInteractive = interactive;
    if (m_barWindow && !m_barWindow->isDeleted()) {
        m_barWindow->setHidden(!interactive || !m_window || !m_window->isShown());
    }
}

void GroupWindow::selectTab(KWin::Window *tab)
{
    if (tab && m_activeTab == tab) {
        return;
    }
    disconnect(m_activeGeometry);
    m_tabScene.reset();
    m_activeTab = tab;
    if (tab && m_window) {
        m_tabScene = std::make_unique<SceneMember>(tab->windowItem(), m_window->windowItem()->windowContainer(), 1);
        m_activeGeometry = connect(tab, &KWin::Window::frameGeometryChanged, this, &GroupWindow::updateScene);
        updateScene();
    }
}

void GroupWindow::updateScene()
{
    if (!m_window) {
        return;
    }
    const QPointF clientOffset = m_window->clientGeometry().topLeft() - m_window->pos();
    if (m_tabScene) {
        m_tabScene->place(clientOffset + QPointF(0, m_stripHeight));
    }
    if (m_barScene) {
        m_barScene->place(clientOffset);
    }
}

bool GroupWindow::ensureCaptureProtection()
{
    if (!m_window->excludeFromCapture()) {
        m_window->setExcludeFromCapture(true);
    }
    return m_window->excludeFromCapture();
}

void GroupWindow::protectCapture()
{
    if (m_window && m_captureProtected && !ensureCaptureProtection()) {
        selectTab(nullptr);
        m_window->setHidden(true);
        setTabBarInteractive(false);
        fail();
    }
}

void GroupWindow::fail()
{
    if (!m_failed && !m_disposed) {
        m_failed = true;
        Q_EMIT changed();
    }
}

bool GroupWindow::attachTab(QObject *object)
{
    auto *tab = qobject_cast<KWin::Window *>(object);
    if (!m_ready || !m_window || m_failed || !canHostTab(tab)) {
        return false;
    }
    std::optional<KWin::DecorationPolicy> originalDecoration;
    if (tab->nextDecoration()) {
        originalDecoration = tab->decorationPolicy();
        tab->setNoBorder(true);
        if (!tab->noBorder()) {
            tab->setDecorationPolicy(*originalDecoration);
            return false;
        }
    }
    const bool skipTaskbar = tab->skipTaskbar();
    std::optional<bool> originalSkipTaskbar;
#if KWIN_BUILD_X11
    if (tab->surface()->client() == KWin::waylandServer()->xWaylandConnection()) {
        originalSkipTaskbar = tab->originalSkipTaskbar();
        tab->setOriginalSkipTaskbar(true);
    }
#endif
    if ((originalSkipTaskbar.has_value() && !tab->originalSkipTaskbar())
        || (tab->excludeFromCapture() && !ensureCaptureProtection())) {
        if (originalSkipTaskbar.has_value()) {
            tab->setOriginalSkipTaskbar(*originalSkipTaskbar);
        }
        if (originalDecoration) {
            tab->setDecorationPolicy(*originalDecoration);
        }
        return false;
    }
    const auto protectTab = [this, tab] {
        m_captureProtected = m_captureProtected || tab->excludeFromCapture();
        protectCapture();
    };
    m_tabs.insert(tab, {originalDecoration, originalSkipTaskbar, skipTaskbar, tab->skipSwitcher(),
        tab->skipsCloseAnimation(), false,
        connect(tab, &KWin::Window::closed, this, [this, tab] {
            if (m_activeTab == tab) {
                selectTab(nullptr);
            }
            if (m_tabs.take(tab).offscreenRendering) {
                tab->unrefOffscreenRendering();
            }
            updateMinimumSize();
        }), connect(tab, &KWin::Window::excludeFromCaptureChanged, this, protectTab)});
    tab->setSkipTaskbar(true);
    tab->setSkipSwitcher(true);
    tab->setSkipCloseAnimation(true);
    tab->windowManagementInterface()->setParentWindow(m_window->windowManagementInterface());
    KWin::workspace()->constrain(m_window, tab);
    protectTab();
    if (m_failed) {
        detachTab(tab);
        return false;
    }
    updateMinimumSize();
    return true;
}

bool GroupWindow::canKeepTab(QObject *object) const
{
    auto *tab = qobject_cast<KWin::Window *>(object);
    return m_tabs.contains(tab) && !tab->nextDecoration();
}

bool GroupWindow::deferAnchorRelease(QObject *object, bool activate)
{
    auto *tab = qobject_cast<KWin::Window *>(object);
    const auto it = m_tabs.find(tab);
    if (m_disposed || m_failed || it == m_tabs.end() || tab != m_anchor || !canRestoreAnchorMaximization()
        || !m_window || !m_window->isShown() || !m_window->isOnCurrentDesktop() || !m_window->isOnCurrentActivity()) {
        return false;
    }

    const auto decoration = it->originalDecoration;
    it->originalDecoration.reset();
    it->offscreenRendering = true;
    tab->refOffscreenRendering();

    auto *workspace = KWin::workspace();
    const bool transferFocus = activate || workspace->activeWindow() == tab;
    if (transferFocus && !KWin::waylandServer()->isScreenLocked()) {
        workspace->activateWindow(m_window, true);
    }
    setTabBar(nullptr);
    selectTab(nullptr);
    tab->setHidden(true);
    restoreTabDecoration(tab, decoration);
    restoreAnchorMaximization();

    const QPointer<KWin::Window> survivor(tab);
    const auto finish = [this, survivor, workspace] {
        if (m_disposed) {
            return;
        }
        KWin::StackingUpdatesBlocker blocker(workspace);
        const bool hostFocused = m_window && workspace->activeWindow() == m_window;
        if (survivor && !survivor->isDeleted()) {
            detachTab(survivor);
            if (hostFocused && survivor->isShown() && survivor->isOnCurrentDesktop() && survivor->isOnCurrentActivity()
                && !KWin::waylandServer()->isScreenLocked()) {
                workspace->activateWindow(survivor, true);
            }
        }
        dispose();
    };
    const auto check = [this, survivor, finish] {
        if (m_failed || !m_window || !m_window->isShown() || !survivor || survivor->isDeleted()
            || survivor->requestedMaximizeMode() != m_anchorMaximizeMode || survivor->maximizeMode() == m_anchorMaximizeMode) {
            finish();
        }
    };
    connect(tab, &KWin::Window::maximizedChanged, this, check, Qt::QueuedConnection);
    connect(tab, &KWin::Window::closed, this, finish, Qt::QueuedConnection);
    connect(this, &GroupWindow::closeRequested, this, [survivor] {
        if (survivor && !survivor->isDeleted() && survivor->isCloseable()) {
            survivor->closeWindow();
        }
    });
    connect(this, &GroupWindow::changed, this, check, Qt::QueuedConnection);
    QTimer::singleShot(0, this, check);
    QTimer::singleShot(500, this, finish);
    return true;
}

void GroupWindow::restoreTabDecoration(KWin::Window *tab, std::optional<KWin::DecorationPolicy> policy)
{
    if (policy && tab->noBorder()) {
        tab->setDecorationPolicy(*policy);
    }
}

bool GroupWindow::detachTab(QObject *object)
{
    auto *tab = qobject_cast<KWin::Window *>(object);
    const auto it = m_tabs.find(tab);
    if (it == m_tabs.end()) {
        return false;
    }
    const Tab state = it.value();
    m_tabs.erase(it);
    disconnect(state.closed);
    disconnect(state.captureChanged);
    if (m_activeTab == tab) {
        selectTab(nullptr);
    }
    updateMinimumSize();
    if (state.offscreenRendering) {
        tab->unrefOffscreenRendering();
    }
    if (tab->isDeleted()) {
        return false;
    }
    if (m_window && !m_window->isDeleted()) {
        KWin::workspace()->unconstrain(m_window, tab);
    }
    if (auto *task = tab->windowManagementInterface()) {
        task->setParentWindow(nullptr);
    }
    restoreTabDecoration(tab, state.originalDecoration);
    if (state.originalSkipTaskbar.has_value()) {
        tab->setOriginalSkipTaskbar(*state.originalSkipTaskbar);
    } else if (!state.skipTaskbar) {
        tab->setSkipTaskbar(false);
    }
    if (!state.skipSwitcher) {
        tab->setSkipSwitcher(false);
    }
    if (!state.skipCloseAnimation) {
        tab->setSkipCloseAnimation(false);
    }
    if (state.minimizedByGroup && tab->isMinimized()) {
        tab->setMinimized(false);
    }
    tab->setHidden(false);
    return tab == m_anchor && restoreAnchorMaximization();
}

void GroupWindow::updateMinimumSize()
{
    if (m_disposed || !m_window) {
        return;
    }
    QSize minimum(1, 1);
    for (auto *tab : m_tabs.keys()) {
        const QSizeF size = tab->minSize();
        minimum = minimum.expandedTo(QSize(qCeil(size.width()), qCeil(size.height())));
    }
    m_surface->setMinimumSize(minimum + QSize(0, m_stripHeight));
}

void GroupWindow::placeTab(QObject *object, bool active, bool visible)
{
    auto *tab = qobject_cast<KWin::Window *>(object);
    const auto it = m_tabs.find(tab);
    if (!m_window || it == m_tabs.end() || tab->isDeleted()) {
        return;
    }
    if (active) {
        selectTab(tab);
    }
    if (tab->desktops() != m_window->desktops()) {
        tab->setDesktops(m_window->desktops());
    }
    if (tab->activities() != m_window->activities()) {
        tab->setOnActivities(m_window->activities());
    }
    if (m_window->isMinimized()) {
        tab->setHidden(true);
        if (!tab->isMinimized()) {
            tab->setMinimized(true);
            it->minimizedByGroup = tab->isMinimized();
        }
        return;
    }
    const bool restoreMinimized = it->minimizedByGroup;
    it->minimizedByGroup = false;
    if (visible) {
        const auto frame = contentFrame(m_window, tab, m_stripHeight);
        if (frame.isValid() && frame != tab->moveResizeGeometry()
            && !tab->isInteractiveMove() && !tab->isInteractiveResize()) {
            tab->moveResize(frame);
        }
    }
    if ((visible || restoreMinimized) && tab->isMinimized()) {
        tab->setMinimized(false);
    }
    tab->setHidden(!visible);
}

void GroupWindow::focusTab(QObject *object, bool activate)
{
    auto *tab = qobject_cast<KWin::Window *>(object);
    if (!m_window || !m_tabs.contains(tab) || !tab->isShown() || KWin::waylandServer()->isScreenLocked()) {
        return;
    }
    auto *workspace = KWin::workspace();
    KWin::StackingUpdatesBlocker blocker(workspace);
    workspace->raiseWindow(m_window);
    if (activate) {
        workspace->activateWindow(tab, true);
    } else if (auto *focused = workspace->activeWindow()) {
        workspace->raiseWindow(tab);
        workspace->raiseWindow(focused);
    }
}

void GroupWindow::dispose()
{
    if (m_disposed) {
        return;
    }
    m_disposed = true;
    setTabBar(nullptr);
    selectTab(nullptr);
    if (m_window && !m_window->isDeleted()) {
        m_window->setSkipTaskbar(true);
        m_window->setHidden(true);
    }
    restoreAnchorMaximization();
    deleteLater();
}
