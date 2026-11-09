#include "moveinput.h"

#include <config-kwin.h>
#include <input_event.h>
#include <input_event_spy.h>
#include <pointer_input.h>
#include <wayland_server.h>
#include <window.h>
#include <workspace.h>

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
    if (!canHostTab(anchor, false)) {
        return nullptr;
    }
    return new GroupWindow(qobject_cast<KWin::Window *>(anchor), stripHeight, title, this);
}
