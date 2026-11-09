#pragma once

#include "groupwindow.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>
#include <memory>

class MoveInput : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit MoveInput(QObject *parent = nullptr);
    ~MoveInput() override;

    Q_INVOKABLE bool isMetaLeftPressFor(QObject *window) const;
    Q_INVOKABLE bool isLeftReleaseFor(QObject *window) const;
    Q_INVOKABLE bool canHostTab(QObject *window, bool allowMaximized) const;
    Q_INVOKABLE bool isGroupWindow(QObject *window) const;
    Q_INVOKABLE GroupWindow *createGroupWindow(QObject *anchor, int stripHeight, const QString &title);

private:
    class Spy;
    std::unique_ptr<Spy> m_spy;
};
