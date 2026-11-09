#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <memory>

class QWindow;

namespace KWin
{
enum class DecorationPolicy;
class InternalWindow;
class Window;
}

class GroupWindow : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Group windows are created by MoveInput")
    Q_PROPERTY(QObject *window READ window NOTIFY changed)
    Q_PROPERTY(bool failed READ failed NOTIFY changed)

public:
    GroupWindow(KWin::Window *anchor, int stripHeight, const QString &title, QObject *parent);
    ~GroupWindow() override;

    static bool canHostTab(const KWin::Window *window, bool allowMaximized = false);
    bool ownsWindow(const KWin::Window *window) const;
    QObject *window() const;
    bool failed() const;

    Q_INVOKABLE void setTitle(const QString &title);
    Q_INVOKABLE void setTabBar(QObject *object);
    Q_INVOKABLE void setTabBarInteractive(bool interactive);
    Q_INVOKABLE bool attachTab(QObject *object);
    Q_INVOKABLE void detachTab(QObject *object);
    Q_INVOKABLE void placeTab(QObject *object, bool active, bool visible);
    Q_INVOKABLE void focusTab(QObject *object, bool activate);
    Q_INVOKABLE void dispose();

Q_SIGNALS:
    void changed();
    void closeRequested();

private:
    class Surface;
    class SceneMember;
    struct Tab {
        KWin::DecorationPolicy decoration;
        bool skipTaskbar;
        bool skipSwitcher;
        bool skipCloseAnimation;
        bool minimizedByGroup;
        QMetaObject::Connection closed;
        QMetaObject::Connection captureChanged;
    };

    void adoptWindow(KWin::Window *window);
    void adoptTabBar();
    void selectTab(KWin::Window *tab);
    void updateScene();
    void protectCapture();
    void updateMinimumSize();
    void fail();

    std::unique_ptr<Surface> m_surface;
    QPointer<KWin::Window> m_anchor;
    QPointer<KWin::Window> m_window;
    QPointer<KWin::Window> m_activeTab;
    QPointer<QWindow> m_barHandle;
    QPointer<KWin::InternalWindow> m_barWindow;
    std::unique_ptr<SceneMember> m_tabScene;
    std::unique_ptr<SceneMember> m_barScene;
    QMetaObject::Connection m_activeGeometry;
    QMetaObject::Connection m_barGeometry;
    QMetaObject::Connection m_barClosed;
    QHash<KWin::Window *, Tab> m_tabs;
    int m_stripHeight;
    bool m_barInteractive = false;
    bool m_captureProtected = false;
    bool m_failed = false;
    bool m_disposed = false;
};
