/*
 * Copyright 2019 Kai Uwe Broulik <kde@privat.broulik.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "utils_p.h"

#include <QAbstractItemModel>
#include <QAbstractProxyModel>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFile>
#include <QTextStream>

#include <KConcatenateRowsProxyModel>

#include <KProcessList>

using namespace NotificationManager;

QString Utils::processNameFromPid(uint pid)
{
    auto processInfo = KProcessList::processInfo(pid);

    if (!processInfo.isValid()) {
        return QString();
    }

    return processInfo.name();
}

QString Utils::desktopEntryFromPid(uint pid)
{
    QFile environFile(QStringLiteral("/proc/%1/environ").arg(QString::number(pid)));
    if (!environFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    const QByteArray bamfDesktopFileHint = QByteArrayLiteral("BAMF_DESKTOP_FILE_HINT");

    const auto lines = environFile.readAll().split('\0');
    for (const QByteArray &line : lines) {
        const int equalsIdx = line.indexOf('=');
        if (equalsIdx <= 0) {
            continue;
        }

        const QByteArray key = line.left(equalsIdx);
        const QByteArray value = line.mid(equalsIdx + 1);
        if (key == bamfDesktopFileHint) {
            return value;
        }
    }

    return QString();
}

QModelIndex Utils::mapToModel(const QModelIndex &idx, const QAbstractItemModel *sourceModel)
{
    // KModelIndexProxyMapper can only map different indices to a single source
    // but we have the other way round, a single index that splits into different source models
    QModelIndex resolvedIdx = idx;
    while (resolvedIdx.isValid() && resolvedIdx.model() != sourceModel) {
        if (auto *proxyModel = qobject_cast<const QAbstractProxyModel *>(resolvedIdx.model())) {
            resolvedIdx = proxyModel->mapToSource(resolvedIdx);
        // KConcatenateRowsProxyModel isn't a "real" proxy model, so we need to special case for it :(
        } else if (auto *concatenateModel = qobject_cast<const KConcatenateRowsProxyModel *>(resolvedIdx.model())) {
            resolvedIdx = concatenateModel->mapToSource(resolvedIdx);
        } else {
            if (resolvedIdx.model() != sourceModel) {
                resolvedIdx = QModelIndex(); // give up
            }
        }
    }
    return resolvedIdx;
}

bool Utils::isDBusMaster()
{
    return qApp->property("_plasma_dbus_master").toBool();
}
