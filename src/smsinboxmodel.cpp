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

#include <QDebug>

#include "eventmodel_p.h"
#include "smsinboxmodel.h"
#include "eventsquery.h"

namespace CommHistory {

using namespace CommHistory;

class SMSInboxModelPrivate : public EventModelPrivate {
public:
    Q_DECLARE_PUBLIC(SMSInboxModel);

    SMSInboxModelPrivate(EventModel *model)
        : EventModelPrivate(model) {
    }

    bool acceptsEvent(const Event &event) const {
        qDebug() << __PRETTY_FUNCTION__ << event.id();
        if (event.type() == Event::SMSEvent &&
            event.direction() == Event::Inbound) return true;

        return false;
    }
};

SMSInboxModel::SMSInboxModel(QObject *parent)
        : EventModel(*new SMSInboxModelPrivate(this), parent)
{
}

SMSInboxModel::~SMSInboxModel()
{
}

bool SMSInboxModel::getEvents()
{
    Q_D(SMSInboxModel);

    beginResetModel();
    d->clearEvents();
    endResetModel();

    EventsQuery query(d->propertyMask);

    query.addPattern(QLatin1String("%1 nmo:isSent \"false\"; "
                                      "nmo:isDraft \"false\"; "
                                      "nmo:isDeleted \"false\"; "
                                      "rdf:type nmo:SMSMessage ."))
            .variable(Event::Id);

    return d->executeQuery(query);
}

}
