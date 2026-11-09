#pragma once

#include "groupwindow.h"

#include <QIcon>
#include <QObject>
#include <QRectF>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace KWIN_WINDOW_TABS_NAMESPACE
{

class MoveInput : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit MoveInput(QObject *parent = nullptr);
    ~MoveInput() override;

    Q_INVOKABLE QString registerTranslationCatalog(const QUrl &localeDirectory) const;
    Q_INVOKABLE bool isMetaLeftPressFor(QObject *window) const;
    Q_INVOKABLE bool isLeftReleaseFor(QObject *window) const;
    Q_INVOKABLE bool canHostTab(QObject *window, bool allowMaximized) const;
    Q_INVOKABLE QRectF restoredFrame(QObject *window) const;
    Q_INVOKABLE QIcon tabIcon(QObject *window) const;
    Q_INVOKABLE bool isGroupWindow(QObject *window) const;
    Q_INVOKABLE GroupWindow *createGroupWindow(QObject *anchor, int stripHeight, const QString &title);

#ifdef KWIN_WINDOW_TABS_BUILD_ID
    Q_INVOKABLE bool registerReloadStatus(const QUrl &entry);

public Q_SLOTS:
    Q_SCRIPTABLE QString reloadStatus() const;
#endif

private:
    class Spy;
    std::unique_ptr<Spy> m_spy;
#ifdef KWIN_WINDOW_TABS_BUILD_ID
    QString m_reloadEntry;
#endif
};

}
