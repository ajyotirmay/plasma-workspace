/***************************************************************************
 *   Copyright (C) 2015 Marco Martin <mart@kde.org>                        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA .        *
 ***************************************************************************/

#include "systemtray.h"
#include "debug.h"

#include <QDebug>
#include <QTimer>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QMenu>
#include <QQuickItem>
#include <QRegExp>
#include <QScreen>
#include <QStandardItemModel>
#include <QQuickWindow>

#include <Plasma/PluginLoader>
#include <Plasma/ServiceJob>

#include <KActionCollection>
#include <KAcceleratorManager>
#include <KLocalizedString>

#include <plasma_version.h>

class PlasmoidModel: public QStandardItemModel
{
public:
    explicit PlasmoidModel(QObject *parent = nullptr)
        : QStandardItemModel(parent)
    {
    }

    QHash<int, QByteArray> roleNames() const override {
        QHash<int, QByteArray> roles = QStandardItemModel::roleNames();
        roles[Qt::UserRole+1] = "plugin";
        return roles;
    }
};

SystemTray::SystemTray(QObject *parent, const QVariantList &args)
    : Plasma::Containment(parent, args),
      m_availablePlasmoidsModel(nullptr)
{
    setHasConfigurationInterface(true);
    setContainmentType(Plasma::Types::CustomEmbeddedContainment);
}

SystemTray::~SystemTray()
{
}

void SystemTray::init()
{
    Containment::init();

    for (const auto &info: Plasma::PluginLoader::self()->listAppletMetaData(QString())) {
        if (!info.isValid() || info.value(QStringLiteral("X-Plasma-NotificationArea")) != "true") {
            continue;
        }
        m_systrayApplets[info.pluginId()] = info;

        if (info.isEnabledByDefault()) {
            m_defaultPlasmoids += info.pluginId();
        }
        const QString dbusactivation = info.value(QStringLiteral("X-Plasma-DBusActivationService"));
        if (!dbusactivation.isEmpty()) {
            qCDebug(SYSTEM_TRAY) << "ST Found DBus-able Applet: " << info.pluginId() << dbusactivation;
            QRegExp rx(dbusactivation);
            rx.setPatternSyntax(QRegExp::Wildcard);
            m_dbusActivatableTasks[info.pluginId()] = rx;
        }
    }
}

void SystemTray::newTask(const QString &task)
{
    const auto appletsList = applets();
    for (Plasma::Applet *applet : appletsList) {
        if (!applet->pluginMetaData().isValid()) {
            continue;
        }

        //only allow one instance per applet
        if (task == applet->pluginMetaData().pluginId()) {
            //Applet::destroy doesn't delete the applet from Containment::applets in the same event
            //potentially a dbus activated service being restarted can be added in this time.
            if (!applet->destroyed()) {
                return;
            }
        }
    }

    //known one, recycle the id to reuse old config
    if (m_knownPlugins.contains(task)) {
        Applet *applet = Plasma::PluginLoader::self()->loadApplet(task, m_knownPlugins.value(task), QVariantList());
        //this should never happen unless explicitly wrong config is hand-written or
        //(more likely) a previously added applet is uninstalled
        if (!applet) {
            qWarning() << "Unable to find applet" << task;
            return;
        }
        applet->setProperty("org.kde.plasma:force-create", true);
        addApplet(applet);
    //create a new one automatic id, new config group
    } else {
        Applet * applet = createApplet(task, QVariantList() << "org.kde.plasma:force-create");
        if (applet) {
            m_knownPlugins[task] = applet->id();
        }
    }
}

void SystemTray::cleanupTask(const QString &task)
{
    const auto appletsList = applets();
    for (Plasma::Applet *applet : appletsList) {
        if (applet->pluginMetaData().isValid() && task == applet->pluginMetaData().pluginId()) {
            //we are *not* cleaning the config here, because since is one
            //of those automatically loaded/unloaded by dbus, we want to recycle
            //the config the next time it's loaded, in case the user configured something here
            applet->deleteLater();
            //HACK: we need to remove the applet from Containment::applets() as soon as possible
            //otherwise we may have disappearing applets for restarting dbus services
            //this may be removed when we depend from a frameworks version in which appletDeleted is emitted as soon as deleteLater() is called
            emit appletDeleted(applet);
        }
    }
}

void SystemTray::showPlasmoidMenu(QQuickItem *appletInterface, int x, int y)
{
    if (!appletInterface) {
        return;
    }

    Plasma::Applet *applet = appletInterface->property("_plasma_applet").value<Plasma::Applet*>();

    QPointF pos = appletInterface->mapToScene(QPointF(x, y));

    if (appletInterface->window() && appletInterface->window()->screen()) {
        pos = appletInterface->window()->mapToGlobal(pos.toPoint());
    } else {
        pos = QPoint();
    }

    QMenu *desktopMenu = new QMenu;
    connect(this, &QObject::destroyed, desktopMenu, &QMenu::close);
    desktopMenu->setAttribute(Qt::WA_DeleteOnClose);

    //this is a workaround where Qt will fail to realize a mouse has been released

    // this happens if a window which does not accept focus spawns a new window that takes focus and X grab
    // whilst the mouse is depressed
    // https://bugreports.qt.io/browse/QTBUG-59044
    // this causes the next click to go missing

    //by releasing manually we avoid that situation
    auto ungrabMouseHack = [appletInterface]() {
        if (appletInterface->window() && appletInterface->window()->mouseGrabberItem()) {
            appletInterface->window()->mouseGrabberItem()->ungrabMouse();
        }
    };

    QTimer::singleShot(0, appletInterface, ungrabMouseHack);
    //end workaround


    emit applet->contextualActionsAboutToShow();
    const auto contextActions = applet->contextualActions();
    for (QAction *action : contextActions) {
        if (action) {
            desktopMenu->addAction(action);
        }
    }

    QAction *runAssociatedApplication = applet->actions()->action(QStringLiteral("run associated application"));
    if (runAssociatedApplication && runAssociatedApplication->isEnabled()) {
        desktopMenu->addAction(runAssociatedApplication);
    }

    if (applet->actions()->action(QStringLiteral("configure"))) {
        desktopMenu->addAction(applet->actions()->action(QStringLiteral("configure")));
    }

    if (desktopMenu->isEmpty()) {
        delete desktopMenu;
        return;
    }

    desktopMenu->adjustSize();

    if (QScreen *screen = appletInterface->window()->screen()) {
        const QRect geo = screen->availableGeometry();

        pos = QPoint(qBound(geo.left(), (int)pos.x(), geo.right() - desktopMenu->width()),
                        qBound(geo.top(), (int)pos.y(), geo.bottom() - desktopMenu->height()));
    }

    KAcceleratorManager::manage(desktopMenu);
    desktopMenu->winId();
    desktopMenu->windowHandle()->setTransientParent(appletInterface->window());
    desktopMenu->popup(pos.toPoint());
}

QString SystemTray::plasmoidCategory(QQuickItem *appletInterface) const
{
    if (!appletInterface) {
        return QStringLiteral("UnknownCategory");
    }

    Plasma::Applet *applet = appletInterface->property("_plasma_applet").value<Plasma::Applet*>();
    if (!applet || !applet->pluginMetaData().isValid()) {
        return QStringLiteral("UnknownCategory");
    }

    const QString cat = applet->pluginMetaData().value(QStringLiteral("X-Plasma-NotificationAreaCategory"));

    if (cat.isEmpty()) {
        return QStringLiteral("UnknownCategory");
    }
    return cat;
}

void SystemTray::showStatusNotifierContextMenu(KJob *job, QQuickItem *statusNotifierIcon)
{
    if (QCoreApplication::closingDown() || !statusNotifierIcon) {
        // apparently an edge case can be triggered due to the async nature of all this
        // see: https://bugs.kde.org/show_bug.cgi?id=251977
        return;
    }

    Plasma::ServiceJob *sjob = qobject_cast<Plasma::ServiceJob *>(job);
    if (!sjob) {
        return;
    }

    QMenu *menu = qobject_cast<QMenu *>(sjob->result().value<QObject *>());

    if (menu) {
        menu->adjustSize();
        const auto parameters = sjob->parameters();
        int x = parameters[QStringLiteral("x")].toInt();
        int y = parameters[QStringLiteral("y")].toInt();

        //try tofind the icon screen coordinates, and adjust the position as a poor
        //man's popupPosition

        QRect screenItemRect(statusNotifierIcon->mapToScene(QPointF(0, 0)).toPoint(), QSize(statusNotifierIcon->width(), statusNotifierIcon->height()));

        if (statusNotifierIcon->window()) {
            screenItemRect.moveTopLeft(statusNotifierIcon->window()->mapToGlobal(screenItemRect.topLeft()));
        }

        switch (location()) {
        case Plasma::Types::LeftEdge:
            x = screenItemRect.right();
            y = screenItemRect.top();
            break;
        case Plasma::Types::RightEdge:
            x = screenItemRect.left() - menu->width();
            y = screenItemRect.top();
            break;
        case Plasma::Types::TopEdge:
            x = screenItemRect.left();
            y = screenItemRect.bottom();
            break;
        case Plasma::Types::BottomEdge:
            x = screenItemRect.left();
            y = screenItemRect.top() - menu->height();
            break;
        default:
            x = screenItemRect.left();
            if (screenItemRect.top() - menu->height() >= statusNotifierIcon->window()->screen()->geometry().top()) {
                y = screenItemRect.top() - menu->height();
            } else {
                y = screenItemRect.bottom();
            }
        }

        KAcceleratorManager::manage(menu);
        menu->winId();
        menu->windowHandle()->setTransientParent(statusNotifierIcon->window());
        menu->popup(QPoint(x, y));
    }
}

QPointF SystemTray::popupPosition(QQuickItem* visualParent, int x, int y)
{
    if (!visualParent) {
        return QPointF(0, 0);
    }

    QPointF pos = visualParent->mapToScene(QPointF(x, y));

    if (visualParent->window() && visualParent->window()->screen()) {
        pos = visualParent->window()->mapToGlobal(pos.toPoint());
    } else {
        return QPoint();
    }
    return pos;
}

void SystemTray::reorderItemBefore(QQuickItem* before, QQuickItem* after)
{
    if (!before || !after) {
        return;
    }

    before->setVisible(false);
    before->setParentItem(after->parentItem());
    before->stackBefore(after);
    before->setVisible(true);
}

void SystemTray::reorderItemAfter(QQuickItem* after, QQuickItem* before)
{
    if (!before || !after) {
        return;
    }

    after->setVisible(false);
    after->setParentItem(before->parentItem());
    after->stackAfter(before);
    after->setVisible(true);
}

bool SystemTray::isSystemTrayApplet(const QString &appletId)
{
    return m_systrayApplets.contains(appletId);
}

void SystemTray::restoreContents(KConfigGroup &group)
{
    Q_UNUSED(group);
    //NOTE: RestoreContents shouldn't do anything here because is too soon, so have an empty reimplementation
}

void SystemTray::restorePlasmoids()
{
    if (!isContainment()) {
        qCWarning(SYSTEM_TRAY) << "Loaded as an applet, this shouldn't have happened";
        return;
    }

    //First: remove all that are not allowed anymore
    const auto appletsList = applets();
    for (Plasma::Applet *applet : appletsList) {
        //Here it should always be valid.
        //for some reason it not always is.
        if (!applet->pluginMetaData().isValid()) {
            applet->config().parent().deleteGroup();
            applet->deleteLater();
        } else {
            const QString task = applet->pluginMetaData().pluginId();
            if (!m_allowedPlasmoids.contains(task)) {
                //in those cases we do delete the applet config completely
                //as they were explicitly disabled by the user
                applet->config().parent().deleteGroup();
                applet->deleteLater();
            }
        }
    }

    KConfigGroup cg = config();

    cg = KConfigGroup(&cg, "Applets");

    const auto groups = cg.groupList();
    for (const QString &group : groups) {
        KConfigGroup appletConfig(&cg, group);
        QString plugin = appletConfig.readEntry("plugin");
        if (!plugin.isEmpty()) {
            m_knownPlugins[plugin] = group.toInt();
        }
    }

    QStringList ownApplets;

    QMap<QString, KPluginMetaData> sortedApplets;
    for (auto it = m_systrayApplets.constBegin(); it != m_systrayApplets.constEnd(); ++it) {
        const KPluginMetaData &info = it.value();
        if (m_allowedPlasmoids.contains(info.pluginId()) && !
            m_dbusActivatableTasks.contains(info.pluginId())) {
            //FIXME
            // if we already have a plugin with this exact name in it, then check if it is the
            // same plugin and skip it if it is indeed already listed
            if (sortedApplets.contains(info.name())) {
                bool dupe = false;
                // it is possible (though poor form) to have multiple applets
                // with the same visible name but different plugins, so we hve to check all values
                const auto infos = sortedApplets.values(info.name());
                for (const KPluginMetaData &existingInfo : infos) {
                    if (existingInfo.pluginId() == info.pluginId()) {
                        dupe = true;
                        break;
                    }
                }

                if (dupe) {
                    continue;
                }
            }

            // insertMulti because it is possible (though poor form) to have multiple applets
            // with the same visible name but different plugins
            sortedApplets.insertMulti(info.name(), info);
        }
    }

    for (const KPluginMetaData &info : qAsConst(sortedApplets)) {
        qCDebug(SYSTEM_TRAY) << " Adding applet: " << info.name();
        if (m_allowedPlasmoids.contains(info.pluginId())) {
            newTask(info.pluginId());
        }
    }

    initDBusActivatables();
}

QStringList SystemTray::defaultPlasmoids() const
{
    return m_defaultPlasmoids;
}

QAbstractItemModel* SystemTray::availablePlasmoids()
{
    if (!m_availablePlasmoidsModel) {
        m_availablePlasmoidsModel = new PlasmoidModel(this);

        for (const KPluginMetaData &info : qAsConst(m_systrayApplets)) {
            QString name = info.name();
            const QString dbusactivation = info.rawData().value(QStringLiteral("X-Plasma-DBusActivationService")).toString();

            if (!dbusactivation.isEmpty()) {
                name += i18n(" (Automatic load)");
            }
            QStandardItem *item = new QStandardItem(QIcon::fromTheme(info.iconName()), name);
            item->setData(info.pluginId());
            m_availablePlasmoidsModel->appendRow(item);
        }
        m_availablePlasmoidsModel->sort(0 /*column*/);
    }
    return m_availablePlasmoidsModel;
}

QStringList SystemTray::allowedPlasmoids() const
{
    return m_allowedPlasmoids;
}

void SystemTray::setAllowedPlasmoids(const QStringList &allowed)
{
    if (allowed == m_allowedPlasmoids) {
        return;
    }

    m_allowedPlasmoids = allowed;

    restorePlasmoids();
    emit allowedPlasmoidsChanged();
}

void SystemTray::initDBusActivatables()
{
    /* Loading and unloading Plasmoids when dbus services come and go
     *
     * This works as follows:
     * - we collect a list of plugins and related services in m_dbusActivatableTasks
     * - we query DBus for the list of services, async (initDBusActivatables())
     * - we go over that list, adding tasks when a service and plugin match (serviceNameFetchFinished())
     * - we start watching for new services, and do the same (serviceNameFetchFinished())
     * - whenever a service is gone, we check whether to unload a Plasmoid (serviceUnregistered())
     */
    QDBusPendingCall async = QDBusConnection::sessionBus().interface()->asyncCall(QStringLiteral("ListNames"));
    QDBusPendingCallWatcher *callWatcher = new QDBusPendingCallWatcher(async, this);
    connect(callWatcher, &QDBusPendingCallWatcher::finished,
            [=](QDBusPendingCallWatcher *callWatcher){
                SystemTray::serviceNameFetchFinished(callWatcher, QDBusConnection::sessionBus());
            });

    QDBusPendingCall systemAsync = QDBusConnection::systemBus().interface()->asyncCall(QStringLiteral("ListNames"));
    QDBusPendingCallWatcher *systemCallWatcher = new QDBusPendingCallWatcher(systemAsync, this);
    connect(systemCallWatcher, &QDBusPendingCallWatcher::finished,
            [=](QDBusPendingCallWatcher *callWatcher){
                SystemTray::serviceNameFetchFinished(callWatcher, QDBusConnection::systemBus());
            });
}

void SystemTray::serviceNameFetchFinished(QDBusPendingCallWatcher* watcher, const QDBusConnection &connection)
{
    QDBusPendingReply<QStringList> propsReply = *watcher;
    watcher->deleteLater();

    if (propsReply.isError()) {
        qCWarning(SYSTEM_TRAY) << "Could not get list of available D-Bus services";
    } else {
        const auto propsReplyValue = propsReply.value();
        for (const QString& serviceName : propsReplyValue) {
            serviceRegistered(serviceName);
        }
    }

    // Watch for new services
    // We need to watch for all of new services here, since we want to "match" the names,
    // not just compare them
    // This makes mpris work, since it wants to match org.mpris.MediaPlayer2.dragonplayer
    // against org.mpris.MediaPlayer2
    // QDBusServiceWatcher is not capable for watching wildcard service right now
    // See:
    // https://bugreports.qt.io/browse/QTBUG-51683
    // https://bugreports.qt.io/browse/QTBUG-33829
    connect(connection.interface(), &QDBusConnectionInterface::serviceOwnerChanged, this, &SystemTray::serviceOwnerChanged);
}

void SystemTray::serviceOwnerChanged(const QString &serviceName, const QString &oldOwner, const QString &newOwner)
{
    if (oldOwner.isEmpty()) {
        serviceRegistered(serviceName);
    } else if (newOwner.isEmpty()) {
        serviceUnregistered(serviceName);
    }
}

void SystemTray::serviceRegistered(const QString &service)
{
    if (service.startsWith(QLatin1Char(':'))) {
        return;
    }

    //qCDebug(SYSTEM_TRAY) << "DBus service appeared:" << service;
    for (auto it = m_dbusActivatableTasks.constBegin(), end = m_dbusActivatableTasks.constEnd(); it != end; ++it) {
        const QString &plugin = it.key();
        if (!m_allowedPlasmoids.contains(plugin)) {
            continue;
        }

        const auto &rx = it.value();
        if (rx.exactMatch(service)) {
            //qCDebug(SYSTEM_TRAY) << "ST : DBus service " << m_dbusActivatableTasks[plugin] << "appeared. Loading " << plugin;
            newTask(plugin);
            m_dbusServiceCounts[plugin]++;
        }
    }
}

void SystemTray::serviceUnregistered(const QString &service)
{
    //qCDebug(SYSTEM_TRAY) << "DBus service disappeared:" << service;

    for (auto it = m_dbusActivatableTasks.constBegin(), end = m_dbusActivatableTasks.constEnd(); it != end; ++it) {
        const QString &plugin = it.key();
        if (!m_allowedPlasmoids.contains(plugin)) {
            continue;
        }

        const auto &rx = it.value();
        if (rx.exactMatch(service)) {
            m_dbusServiceCounts[plugin]--;
            Q_ASSERT(m_dbusServiceCounts[plugin] >= 0);
            if (m_dbusServiceCounts[plugin] == 0) {
                //qCDebug(SYSTEM_TRAY) << "ST : DBus service " << m_dbusActivatableTasks[plugin] << " disappeared. Unloading " << plugin;
                cleanupTask(plugin);
            }
        }
    }
}

K_EXPORT_PLASMA_APPLET_WITH_JSON(systemtray, SystemTray, "metadata.json")

#include "systemtray.moc"
