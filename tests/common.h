/******************************************************************************
**
** This file is part of libcommhistory.
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** Contact: Reto Zingg <reto.zingg@nokia.com>
**
** This library is free software; you can redistribute it and/or modify it
** under the terms of the GNU Lesser General Public License version 2.1 as
** published by the Free Software Foundation.
**
** This library is distributed in the hope that it will be useful, but
** WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
** or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
** License for more details.
**
** You should have received a copy of the GNU Lesser General Public License
** along with this library; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
**
******************************************************************************/

#ifndef COMMON_H
#define COMMON_H

#include "eventmodel.h"
#include "event.h"
#include "group.h"

#include <QSignalSpy>

using namespace CommHistory;

const QString ACCOUNT1 = "/org/freedesktop/Telepathy/Account/gabble/jabber/dut_40localhost0";
const QString ACCOUNT2 = "/org/freedesktop/Telepathy/Account/gabble/jabber/dut2_40localhost0";
const QString RING_ACCOUNT = "ring/tel/ring";

/* The default load polling interval when waiting system to become idle */
const int IDLE_POLL_INTERVAL = 2000;

/* System is considered idle when system load drops below this value */
const double IDLE_TRESHOLD = 0.05; // 5%

const int WAIT_SIGNAL_TIMEOUT = 5000;

int addTestEvent(EventModel &model,
                 Event::EventType type,
                 Event::EventDirection direction,
                 const QString &account,
                 int groupId,
                 const QString &text = QString("test event"),
                 bool isDraft = false,
                 bool isMissedCall = false,
                 const QDateTime &when = QDateTime::currentDateTime(),
                 const QString &remoteUid = QString(),
                 bool toModelOnly = false,
                 const QString messageToken = QString());

void addTestGroups(Group &group1, Group &group2);
void addTestGroup(Group& grp, QString localUid, QString remoteUid);
int addTestContact(const QString &name, const QString &remoteUid, const QString &localUid=QString());
void modifyTestContact(int id, const QString &name);
void deleteTestContact(int id);
void cleanUpTestContacts();
bool compareEvents(Event &e1, Event &e2);
void deleteAll();
void deleteSmsMsgs();
QString randomMessage(int words);
double getSystemLoad();
void waitForIdle(int pollInterval = IDLE_POLL_INTERVAL);
bool waitSignal(QSignalSpy &spy, int msec = WAIT_SIGNAL_TIMEOUT);
// wait and allow deferred deletes to be processed (http://bugreports.qt.nokia.com/browse/QTBUG-12575)
void waitWithDeletes(int msec = WAIT_SIGNAL_TIMEOUT);

#endif
