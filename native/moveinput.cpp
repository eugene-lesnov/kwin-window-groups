#include "moveinput.h"

#include <config-kwin.h>
#include <input_event.h>
#include <input_event_spy.h>
#include <pointer_input.h>
#include <wayland_server.h>
#include <window.h>
#include <workspace.h>

#include <KApplicationTrader>
#include <KLocalizedString>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>

class MoveInput::Spy final : public KWin::InputEventSpy
{
public:
    Spy()
    {
        m_resetTimer.setSingleShot(true);
        QObject::connect(&m_resetTimer, &QTimer::timeout, &m_resetTimer, [this] {
            clearButtonEvent();
        });
        KWin::input()->installInputEventSpy(this);
    }

    void pointerButton(KWin::PointerButtonEvent *event) override
    {
        clearButtonEvent();
        if (event->button != Qt::LeftButton) {
            return;
        }

        if (event->state == KWin::PointerButtonState::Pressed && event->buttons == Qt::LeftButton
            && event->modifiersRelevantForShortcuts == Qt::MetaModifier) {
            m_pressedWindow = KWin::input()->pointer()->hover();
        } else if (event->state == KWin::PointerButtonState::Released && event->buttons == Qt::NoButton) {
            if (auto *workspace = KWin::workspace()) {
                m_releasedWindow = workspace->moveResizeWindow();
            }
        }

        if (m_pressedWindow || m_releasedWindow) {
            m_resetTimer.start(0);
        }
    }

    void pointerMotion(KWin::PointerMotionEvent *) override
    {
        clearButtonEvent();
    }

    void keyboardKey(KWin::KeyboardKeyEvent *) override
    {
        clearButtonEvent();
    }

    QPointer<QObject> m_pressedWindow;
    QPointer<QObject> m_releasedWindow;

private:
    void clearButtonEvent()
    {
        m_pressedWindow.clear();
        m_releasedWindow.clear();
    }

    QTimer m_resetTimer;
};

MoveInput::MoveInput(QObject *parent)
    : QObject(parent)
{
    if (QCoreApplication::applicationVersion() == KWIN_VERSION_STRING
        && QGuiApplication::platformName() == QStringLiteral("wayland-org.kde.kwin.qpa")
        && KWin::input()) {
        m_spy = std::make_unique<Spy>();
    }
}

MoveInput::~MoveInput() = default;

QString MoveInput::registerTranslationCatalog(const QUrl &localeDirectory) const
{
    constexpr auto domain = "kwin-window-tabs";
    const QString path = QFileInfo(localeDirectory.toLocalFile()).canonicalFilePath();
    if (!path.isEmpty()) {
        KLocalizedString::addDomainLocaleDir(domain, path);
    }
    return QString::fromLatin1(domain);
}

bool MoveInput::isMetaLeftPressFor(QObject *window) const
{
    return window && m_spy && m_spy->m_pressedWindow.data() == window;
}

bool MoveInput::isLeftReleaseFor(QObject *window) const
{
    return window && m_spy && m_spy->m_releasedWindow.data() == window;
}

bool MoveInput::canHostTab(QObject *window, bool allowMaximized) const
{
    return m_spy && KWin::waylandServer()
        && GroupWindow::canHostTab(qobject_cast<KWin::Window *>(window), allowMaximized);
}

QRectF MoveInput::restoredFrame(QObject *object) const
{
    const auto *window = qobject_cast<KWin::Window *>(object);
    if (!window) {
        return {};
    }
    const auto frame = window->geometryRestore();
    return frame.isValid() ? frame : window->frameGeometry();
}

QIcon MoveInput::tabIcon(QObject *object) const
{
    const auto *window = qobject_cast<KWin::Window *>(object);
    if (!window) {
        return {};
    }
    const QIcon icon = window->icon();
    if (!icon.isNull() && icon.name() != QStringLiteral("wayland")) {
        return icon;
    }

    const auto loadIcon = [](const QString &name) {
        const QFileInfo file(name);
        if (file.isAbsolute()) {
            return file.isFile() ? QIcon(name) : QIcon();
        }
        return QIcon::fromTheme(name);
    };
    const QString desktopId = window->desktopFileName();
    const QIcon desktopIcon = loadIcon(KWin::Window::iconFromDesktopFile(desktopId));
    if (!desktopIcon.isNull()) {
        return desktopIcon;
    }

    const QString windowClass = window->resourceClass();
    const auto matchesWindow = [&](const QString &id) {
        return !id.isEmpty() && (id.compare(desktopId, Qt::CaseInsensitive) == 0
            || id.compare(windowClass, Qt::CaseInsensitive) == 0);
    };
    if (!desktopId.isEmpty() || !windowClass.isEmpty()) {
        const auto services = KApplicationTrader::query([&](const KService::Ptr &service) {
            return matchesWindow(service->desktopEntryName())
                || matchesWindow(service->property<QString>(QStringLiteral("StartupWMClass")));
        });
        for (const auto &service : services) {
            const QIcon serviceIcon = loadIcon(service->icon());
            if (!serviceIcon.isNull()) {
                return serviceIcon;
            }
        }
    }
    return icon;
}

bool MoveInput::isGroupWindow(QObject *window) const
{
    const auto *candidate = qobject_cast<KWin::Window *>(window);
    for (auto *group : findChildren<GroupWindow *>(Qt::FindDirectChildrenOnly)) {
        if (group->ownsWindow(candidate)) {
            return true;
        }
    }
    return false;
}

GroupWindow *MoveInput::createGroupWindow(QObject *anchor, int stripHeight, const QString &title)
{
    auto *window = qobject_cast<KWin::Window *>(anchor);
    if (!canHostTab(anchor, true) || window->requestedMaximizeMode() != window->maximizeMode()) {
        return nullptr;
    }
    return new GroupWindow(window, stripHeight, title, this);
}
