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

#include <QtDBus/QtDBus>
#include <QDebug>

#include "trackerio.h"
#include "trackerio_p.h"
#include "eventmodel.h"
#include "eventmodel_p.h"
#include "callmodel.h"
#include "callmodel_p.h"
#include "event.h"
#include "commonutils.h"
#include "contactlistener.h"
#include "eventsquery.h"
#include "queryrunner.h"
#include "updatequery.h"
#include "committingtransaction.h"

namespace {
    static CommHistory::Event::PropertySet unusedProperties = CommHistory::Event::PropertySet()
        << CommHistory::Event::IsDraft
        << CommHistory::Event::FreeText
        << CommHistory::Event::MessageToken
        << CommHistory::Event::ReportDelivery
        << CommHistory::Event::ValidityPeriod
        << CommHistory::Event::ContentLocation
        << CommHistory::Event::FromVCardFileName
        << CommHistory::Event::FromVCardLabel
        << CommHistory::Event::MessageParts
        << CommHistory::Event::Cc
        << CommHistory::Event::Bcc
        << CommHistory::Event::ReadStatus
        << CommHistory::Event::ReportRead
        << CommHistory::Event::ReportReadRequested
        << CommHistory::Event::MmsId
        << CommHistory::Event::To;
}

namespace CommHistory
{

using namespace CommHistory;

/* ************************************************************************** *
 * ******** P R I V A T E   C L A S S   I M P L E M E N T A T I O N ********* *
 * ************************************************************************** */

CallModelPrivate::CallModelPrivate( EventModel *model )
        : EventModelPrivate( model )
        , sortBy( CallModel::SortByContact )
        , eventType( CallEvent::UnknownCallType )
        , referenceTime( QDateTime() )
        , hasBeenFetched( false )
{
    contactChangesEnabled = true;
    propertyMask -= unusedProperties;
}

void CallModelPrivate::executeGroupedQuery(const QString &query)
{
    qDebug() << __PRETTY_FUNCTION__;

    startContactListening();

    isReady = false;
    if (queryMode == EventModel::StreamedAsyncQuery) {
        queryRunner->setStreamedMode(true);
        queryRunner->setChunkSize(chunkSize);
        queryRunner->setFirstChunkSize(firstChunkSize);
    }
    queryRunner->runGroupedCallQuery(query);
    if (queryMode == EventModel::SyncQuery) {
        QEventLoop loop;
        while (!isReady || !messagePartsReady) {
            loop.processEvents(QEventLoop::WaitForMoreEvents);
        }
    }
}

bool CallModelPrivate::eventMatchesFilter( const Event &event ) const
{
    bool match = true;

    switch (eventType) {
    case CallEvent::MissedCallType:
        if (event.direction() != Event::Inbound || !event.isMissedCall())
            match = false;
        break;
    case CallEvent::DialedCallType:
        if (event.direction() != Event::Outbound)
            match = false;
        break;
    case CallEvent::ReceivedCallType:
        if (event.direction() != Event::Inbound || event.isMissedCall())
            match = false;
        break;
    default:
        break;
    }

    return match;
}

bool CallModelPrivate::acceptsEvent( const Event &event ) const
{
    qDebug() << __PRETTY_FUNCTION__ << event.id();
    if ( event.type() != Event::CallEvent )
    {
        return false;
    }

    if(!referenceTime.isNull() && (event.startTime() < referenceTime)) // a reference Time is already set, so any further event addition should be beyond that
    {
        return false;
    }

    if (!isInTreeMode && eventType != CallEvent::UnknownCallType && !eventMatchesFilter(event))
        return false;

    return true;
}

void CallModelPrivate::eventsReceivedSlot(int start, int end, QList<CommHistory::Event> events)
{
    Q_Q( CallModel );

    qDebug() << Q_FUNC_INFO << start << end << events.count();

    if ((sortBy != CallModel::SortByContact && sortBy != CallModel::SortByContactAndType)
            || updatedGroups.isEmpty())
        return EventModelPrivate::eventsReceivedSlot(start, end, events);

    // reimp from EventModelPrivate, for video calls

    // Here we should usually get one or two result rows, one for the
    // video call group and one for the corresponding audio call group.
    QMutableListIterator<Event> i(events);
    while (i.hasNext()) {
        Event event = i.next();

        bool replaced = false;
        QModelIndex index;
        for (int row = 0; row < eventRootItem->childCount(); row++) {
            if (belongToSameGroup(eventRootItem->eventAt(row), event)
                || eventRootItem->eventAt(row).id() == event.id()) {
                qDebug() << "replacing row" << row;
                replaced = true;
                index = q->createIndex(row, 0, eventRootItem->child(row));

                eventRootItem->child(row)->setEvent(event);
                QModelIndex bottom = q->createIndex(row,
                                                    EventModel::NumberOfColumns - 1,
                                                    eventRootItem->child(row));
                emit q->dataChanged(index, bottom);
                updatedGroups.remove(TrackerIOPrivate::makeCallGroupURI(event));

                // if we had an audio and video call group for the same
                // contact and the latest audio call gets upgraded (or
                // vice versa), there may now be two rows for the same
                // group, so we have to remove the other one.
                for (int dupe = index.row() + 1; dupe < eventRootItem->childCount(); dupe++) {
                    Event e = eventRootItem->eventAt(dupe);
                    if (belongToSameGroup(e, event)) {
                        qDebug() << Q_FUNC_INFO << "remove" << dupe << e.toString();
                        emit q->beginRemoveRows(QModelIndex(), dupe, dupe);
                        eventRootItem->removeAt(dupe);
                        emit q->endRemoveRows();
                        break;
                    }
                }
                break;
            }
        }

        if (!replaced) {
            // didn't find an old row to overwrite -> insert new row in the appropriate spot
            if (!event.contacts().isEmpty()) {
                contactCache.insert(qMakePair(event.localUid(), event.remoteUid()), event.contacts());
            }

            int row;
            for (row = 0; row < eventRootItem->childCount(); row++) {
                if (eventRootItem->child(row)->event().startTime() <= event.startTime())
                    break;
            }

            q->beginInsertRows(QModelIndex(), row, row);
            eventRootItem->insertChildAt(row, new EventTreeItem(event, eventRootItem));
            q->endInsertRows();

            updatedGroups.remove(TrackerIOPrivate::makeCallGroupURI(event));
        }
    }

    if (!updatedGroups.isEmpty()) {
        qDebug() << Q_FUNC_INFO << "remaining call groups:" << updatedGroups;
        // no results for call group means it has been emptied, remove from list
        foreach (QString group, updatedGroups.values()) {
            for (int row = 0; row < eventRootItem->childCount(); row++) {
                if (TrackerIOPrivate::makeCallGroupURI(eventRootItem->eventAt(row)) == group) {
                    qDebug() << Q_FUNC_INFO << "remove" << row << eventRootItem->eventAt(row).toString();
                    emit q->beginRemoveRows(QModelIndex(), row, row);
                    eventRootItem->removeAt(row);
                    emit q->endRemoveRows();
                    break;
                }
            }
        }
    }
}

void CallModelPrivate::modelUpdatedSlot( bool successful )
{
    EventModelPrivate::modelUpdatedSlot(successful);
    countedUids.clear();
    updatedGroups.clear();

    if (contactChangesEnabled && contactListener) {
        connect(contactListener.data(),
                SIGNAL(contactSettingsChanged(const QHash<QString, QVariant> &)),
                this,
                SLOT(contactSettingsChangedSlot(const QHash<QString, QVariant> &)),
                Qt::UniqueConnection);
    }
}

bool CallModelPrivate::belongToSameGroup( const Event &e1, const Event &e2 )
{
    if (sortBy == CallModel::SortByContact
        && remoteAddressMatch(e1.remoteUid(), e2.remoteUid(), NormalizeFlagKeepDialString)
        && e1.localUid() == e2.localUid()
        && e1.isVideoCall() == e2.isVideoCall())
    {
        return true;
    }
    else if ((sortBy == CallModel::SortByTime || sortBy == CallModel::SortByContactAndType)
             && (remoteAddressMatch(e1.remoteUid(), e2.remoteUid(), NormalizeFlagKeepDialString)
                 && e1.localUid() == e2.localUid()
                 && e1.direction() == e2.direction()
                 && e1.isMissedCall() == e2.isMissedCall()
                 && e1.isVideoCall() == e2.isVideoCall()))
    {
        return true;
    }
    return false;
}

int CallModelPrivate::calculateEventCount( EventTreeItem *item )
{
    int count = -1;

    switch ( sortBy )
    {
        case CallModel::SortByContact :
        case CallModel::SortByContactAndType:
        {
            // set event count for missed calls only,
            // leave the default value for non-missed ones
            if ( item->event().isMissedCall() )
            {
                count = 1;
                // start looping the list from index number 1, because
                // the index number 0 is the same item as the top level
                // one
                for ( int i = 1; i < item->childCount(); i++ )
                {
                    if ( item->child( i - 1 )->event().isMissedCall() &&
                         item->child( i )->event().isMissedCall() )
                    {
                        count++;
                    }
                    else
                    {
                        break;
                    }
                }
            }
            break;
        }
        case CallModel::SortByTime :
        {
            count = item->childCount();
            break;
        }
        default:
            break;
    }

    if (count < 1)
        count = 1;

    return count;
}

bool CallModelPrivate::fillModel( int start, int end, QList<CommHistory::Event> events )
{
    Q_UNUSED( start );
    Q_UNUSED( end );
    Q_Q( CallModel );

    //for flat mode EventModelPrivate::fillModel is sufficient as all the events will be stored at the top level
    if(!isInTreeMode)
    {
        return EventModelPrivate::fillModel(start, end, events);
    }

    if ( events.count() > 0 )
    {
        /*
         * call events are grouped as follows:
         *
         * [event 1] - (event 1)
         *             (event 2)
         *             (event 3)
         * [event 4] - (event 4)
         *             (event 5)
         * ...         ...
         *
         * NOTE:
         * on the top level, there are only the representatives of each group
         * on second level, there are all call events listed (also group reps)
         */

        switch ( sortBy )
        {
            /*
             * if sorted by contact,
             * then event count is meaningful only for missed calls.
             * it shows how many missed calls there are under the top
             * level one without breaking the continuity of the missed
             * calls.
             *
             * John, 3 missed
             * Mary, 1 received
             * John, 1 dialed
             * John, 1 missed
             * Mary, 2 dialed
             *
             *      ||
             *      \/
             *
             * John, 3 missed
             * Mary, received
             *
             * NOTE 1:
             * there are actually 4 missed calls from John, but there was
             * 1 dialed in between, that is why the event count is 3.
             *
             * NOTE 2:
             * there is no number for the received calls, since only the
             * missed calls have valid even count. (But -1 will be returned.)
             */
            case CallModel::SortByContact :
            case CallModel::SortByContactAndType:
            {
                QList<EventTreeItem *> topLevelItems;
                // get the first event and save it as top level item
                Event event = events.first();
                topLevelItems.append(new EventTreeItem(event));
                if (!event.contacts().isEmpty())
                    contactCache.insert(qMakePair(event.localUid(), event.remoteUid()), event.contacts());

                for (int i = 1; i < events.count(); i++) {
                    Event event = events.at(i);
                    if (!event.contacts().isEmpty())
                        contactCache.insert(qMakePair(event.localUid(), event.remoteUid()), event.contacts());

                    bool found = false;
                    for (int i = 0; i < topLevelItems.count() && !found; ++i) {
                        // ignore matching events because the already existing
                        // entry has to be more recent
                        if (belongToSameGroup(topLevelItems.at(i)->event(), event))
                            found = true;
                    }

                    if (!found)
                        topLevelItems.append(new EventTreeItem(event));
                }

                // save top level items into the model
                q->beginInsertRows( QModelIndex(), 0, topLevelItems.count() - 1);
                foreach ( EventTreeItem *item, topLevelItems )
                {
                    eventRootItem->appendChild( item );
                }
                q->endInsertRows();

                break;
            }
            /*
             * if sorted by time,
             * then event count is the number of events grouped under the
             * top level one
             *
             * John, 3 missed
             * Mary, 1 received
             * John, 1 dialed
             * John, 1 missed
             * Mary, 2 dialed
             */
            case CallModel::SortByTime  :
            {
                int previousLastRow = eventRootItem->childCount() - 1;
                EventTreeItem *previousLastItem = 0;

                EventTreeItem *last = 0;
                if (eventRootItem->childCount()) {
                    last = eventRootItem->child(previousLastRow);
                    previousLastItem = last;
                }

                QList<EventTreeItem *> newItems;

                foreach (Event event, events) {
                    if (!event.contacts().isEmpty())
                        contactCache.insert(qMakePair(event.localUid(), event.remoteUid()), event.contacts());

                    if (last && last->event().eventCount() == -1
                        && belongToSameGroup(event, last->event())) {
                        // still filling last row with matching events
                        last->appendChild(new EventTreeItem(event, last));
                    } else {
                        // no match to previous event -> update count
                        // for last row and add a new row if event is
                        // acceptable
                        if (last) {
                            if (!countedUids.contains(last->event().remoteUid()))
                                last->event().setEventCount(calculateEventCount(last));
                            countedUids.insert(last->event().remoteUid());

                            if (last != previousLastItem && eventMatchesFilter(last->event()))
                                newItems.append(last);
                            else if (last != previousLastItem) {
                                delete last;
                                last = 0;
                            }
                        }

                        event.setEventCount(-1);
                        last = new EventTreeItem(event);
                        last->appendChild(new EventTreeItem(event, last));
                    }
                }

                if (last && last != previousLastItem && eventMatchesFilter(last->event())) {
                    if (!countedUids.contains(last->event().remoteUid()))
                        last->event().setEventCount(calculateEventCount(last));
                    countedUids.insert(last->event().remoteUid());
                    newItems.append(last);
                } else  if (last != previousLastItem) {
                    delete last;
                }

                // update count for last item in the previous batch
                if (!newItems.isEmpty()) {
                    if (previousLastRow != -1)
                        emit q->dataChanged(q->createIndex(previousLastRow, 0, eventRootItem->child(previousLastRow)),
                                            q->createIndex(previousLastRow, CallModel::NumberOfColumns - 1,
                                                           eventRootItem->child(previousLastRow)));

                    // insert the rest
                    q->beginInsertRows(QModelIndex(), previousLastRow + 1, previousLastRow + newItems.count());
                    foreach (EventTreeItem *item, newItems)
                        eventRootItem->appendChild(item);
                    q->endInsertRows();
                }

                break;
            }

            default:
                break;
        }
    }

    return true;
}

void CallModelPrivate::addToModel( Event &event )
{
    Q_Q(CallModel);
    qDebug() << __PRETTY_FUNCTION__ << event.toString();

    if(!isInTreeMode)
    {
            return EventModelPrivate::addToModel(event);
    }

    if (!event.contacts().isEmpty()) {
        contactCache.insert(qMakePair(event.localUid(), event.remoteUid()), event.contacts());
    } else {
        if (!setContactFromCache(event)) {
            startContactListening();
            if (contactListener)
                contactListener->resolveContact(event.localUid(), event.remoteUid());
        }
    }

    switch ( sortBy )
    {
        case CallModel::SortByContact :
        case CallModel::SortByContactAndType:
        {
            // find match, update count if needed, move to top
            int matchingRow = -1;
            for (int i = 0; i < eventRootItem->childCount(); i++) {
                if (belongToSameGroup(eventRootItem->child(i)->event(), event)) {
                    matchingRow = i;
                    break;
                }
            }

            if (matchingRow != -1) {
                EventTreeItem *matchingItem = eventRootItem->child(matchingRow);

                if (matchingItem->event().direction() == event.direction()
                    && matchingItem->event().isMissedCall() == event.isMissedCall())
                    event.setEventCount(matchingItem->event().eventCount() + 1);
                else
                    event.setEventCount(1);

                matchingItem->setEvent(event);

                if (matchingRow == 0) {
                    // already at the top, update row
                    emit q->dataChanged(q->createIndex(0, 0, eventRootItem->child(0)),
                                        q->createIndex(0, CallModel::NumberOfColumns - 1,
                                                       eventRootItem->child(0)));
                } else {
                    // move to top
                    emit q->layoutAboutToBeChanged();
                    eventRootItem->moveChild(matchingRow, 0);
                    emit q->layoutChanged();
                }
            } else {
                // no match, insert new row at top
                emit q->beginInsertRows(QModelIndex(), 0, 0);
                event.setEventCount(1);
                eventRootItem->prependChild(new EventTreeItem(event));
                emit q->endInsertRows();
            }

            break;
        }
        case CallModel::SortByTime :
        {
            // reset event count if type doesn't match top event
            if (!eventMatchesFilter(event) && eventRootItem->childCount()) {
                EventTreeItem *topItem = eventRootItem->child(0);
                if (remoteAddressMatch(topItem->event().remoteUid(),
                                       event.remoteUid(), NormalizeFlagKeepDialString)
                    && topItem->event().localUid() == event.localUid()) {
                    EventTreeItem *newTopItem = new EventTreeItem(topItem->event());
                    newTopItem->event().setEventCount(1);

                    eventRootItem->removeAt(0);
                    eventRootItem->prependChild(newTopItem);

                    emit q->dataChanged(q->createIndex(0, 0, eventRootItem->child(0)),
                                        q->createIndex(0, CallModel::NumberOfColumns - 1,
                                                       eventRootItem->child(0)));
                    return;
                }
            }

            if (!eventMatchesFilter(event))
                return;

            // if new item is groupable with the first one in the list
            // NOTE: assumption is that time value is ok
            if (eventRootItem->childCount() && belongToSameGroup(event, eventRootItem->child(0)->event())
                && eventRootItem->child(0)->event().eventCount() != -1)
            {
                // alias
                EventTreeItem *firstTopLevelItem = eventRootItem->child( 0 );
                // add event to the group, set it as top level item and refresh event count
                firstTopLevelItem->prependChild( new EventTreeItem( event, firstTopLevelItem ) );
                firstTopLevelItem->setEvent( event );
                firstTopLevelItem->event().setEventCount( calculateEventCount( firstTopLevelItem ) );
                // only counter and timestamp of first must be updated
                emit q->dataChanged( q->createIndex( 0, 0, eventRootItem->child( 0 ) ),
                                     q->createIndex( 0, CallModel::NumberOfColumns - 1, eventRootItem->child( 0 ) ) );
            }
            // create a new group, otherwise
            else
            {
                // a new row must be inserted
                q->beginInsertRows( QModelIndex(), 0, 0 );
                // add new item as first on the list
                eventRootItem->prependChild( new EventTreeItem( event ) );
                // alias
                EventTreeItem *firstTopLevelItem = eventRootItem->child( 0 );
                // add the copy of the event to its local list and refresh event count
                firstTopLevelItem->prependChild( new EventTreeItem( event, firstTopLevelItem ) );
                firstTopLevelItem->event().setEventCount( calculateEventCount( firstTopLevelItem ) );
                q->endInsertRows();
            }
            break;
        }
        default :
        {
            qWarning() << __PRETTY_FUNCTION__ << "Adding call events to model sorted by type or by service has not been implemented yet.";
            return;
        }
    }
}

void CallModelPrivate::eventsAddedSlot( const QList<Event> &events )
{
    qDebug() << __PRETTY_FUNCTION__ << events.count();
    // TODO: sorting?
    EventModelPrivate::eventsAddedSlot(events);
}

void CallModelPrivate::eventsUpdatedSlot( const QList<Event> &events )
{
    Q_Q(CallModel);

    // TODO regrouping of events might occur =(

    // reimp from EventModelPrivate, plus additional isVideoCall processing
    foreach (const Event &event, events) {
        qDebug() << Q_FUNC_INFO << "updated" << event.toString();
        QModelIndex index = findEvent(event.id());
        Event e = event;

        if (!index.isValid()) {
            if (acceptsEvent(e))
                addToModel(e);

            continue;
        }

        EventTreeItem *item = static_cast<EventTreeItem *>(index.internalPointer());
        if (item) {
            Event oldEvent = item->event();
            if (oldEvent.isVideoCall() != event.isVideoCall()) {
                // Video call status up/downgraded; refetch both video-
                // and non-video-versions for the call group and process
                // the results in eventsReceived
                updatedGroups.insert(TrackerIOPrivate::makeCallGroupURI(oldEvent));
                updatedGroups.insert(TrackerIOPrivate::makeCallGroupURI(event));
            } else {
                modifyInModel(e);
            }
        }
    }

    qDebug() << Q_FUNC_INFO << "updatedGroups" << updatedGroups;

    if (!updatedGroups.isEmpty()) {
        if (sortBy == CallModel::SortByTime) {
            /*
             * *** TODO ***
             * Optimizing this would require a lot of tweaking to handle
             * split/merged/added/deleted rows. No time to do this right
             * now, so just force a refetch.
             */
            if (hasBeenFetched) {
                q->getEvents();
                return;
            }
        }

        QString query = TrackerIOPrivate::prepareGroupedCallQuery(updatedGroups.toList());
        executeGroupedQuery(query);
    }
}

QModelIndex CallModelPrivate::findEvent( int id ) const
{
    Q_Q( const CallModel );

    if(!isInTreeMode)
    {
        return EventModelPrivate::findEvent(id);
    }

    for ( int row = 0; row < eventRootItem->childCount(); row++ )
    {
        // check top level item
        if ( eventRootItem->child( row )->event().id() == id )
        {
            return q->createIndex( row, 0, eventRootItem->child( row ) );
        }
        // loop through all grouped events
        EventTreeItem *currentGroup = eventRootItem->child( row );
        for ( int column = 0; column < currentGroup->childCount(); column++ )
        {
            if ( currentGroup->child( column )->event().id() == id )
            {
                return q->createIndex( row, column, currentGroup->child( column ) );
            }
        }
    }

    // id was not found, return invalid index
    return QModelIndex();
}

void CallModelPrivate::deleteFromModel( int id )
{
    Q_Q(CallModel);

    if(!isInTreeMode)
    {
        return EventModelPrivate::deleteFromModel(id);
    }

    // TODO : what if an event is deleted from the db through commhistory-tool?

    // seek for the top level item which was deleted
    QModelIndex index = findEvent( id );

    // if id was not found, do nothing
    if ( !index.isValid() )
    {
        qDebug() << __PRETTY_FUNCTION__ << "*** Invalid";
        return;
    }

    // TODO : it works only when sorting is time based

    // if event is a top level item ( i.e. the whole group ), then delete it
    if ( index.column() == 0 )
    {
        int row = index.row();
        bool isRegroupingNeeded = false;
        // regrouping is needed/possible only if sorting is SortByTime...
        // ...and there is a previous row and a following row to group together
        if ( sortBy == CallModel::SortByTime &&
             row - 1 >= 0 && row + 1 < eventRootItem->childCount() )
        {
            EventTreeItem *prev = eventRootItem->child( row - 1 );
            EventTreeItem *next = eventRootItem->child( row + 1 );

            if ( belongToSameGroup( prev->event(), next->event() ) )
            {
                for ( int i = 0; i < next->childCount(); i++ )
                {
                    prev->appendChild( new EventTreeItem( next->child( i )->event() ) );
                }
                prev->event().setEventCount( calculateEventCount( prev ) );
                isRegroupingNeeded = true;
            }
        }

        qDebug() << __PRETTY_FUNCTION__ << "*** Top level" << row;
        // if there is no need to regroup the previous and following items,
        // then delete only one row
        if ( !isRegroupingNeeded )
        {
            q->beginRemoveRows( index.parent(), row, row );
            eventRootItem->removeAt( row );
        }
        // otherwise delete the current and the following one
        // (since we added content of the following to the previous)
        else
        {
            q->beginRemoveRows( index.parent(), row, row + 1 );
            eventRootItem->removeAt( row + 1 );
            eventRootItem->removeAt( row );
            emit q->dataChanged( q->createIndex( row - 1, 0, eventRootItem->child( row - 1 ) ),
                                 q->createIndex( row - 1, 0, eventRootItem->child( row - 1 ) ) );
        }
        q->endRemoveRows();
    }
    // otherwise item is a grouped event
    else
    {
        qDebug() << __PRETTY_FUNCTION__ << "*** Sth else";
        // TODO :
        // delete it from the model
        // update top level item
        // emit dataChanged()
    }
}

void CallModelPrivate::deleteCallGroup( const Event &event, bool typed )
{
    qDebug() << Q_FUNC_INFO << event.id();

    // the calls could be deleted simply with "delete ?call where ?call
    // belongs to ?channel", but then we wouldn't be able to send
    // separate eventDeleted signals :(
    QString queryString;

    if (!typed) {
        queryString = QLatin1String("SELECT ?call WHERE { "
                                    "?call nmo:communicationChannel ?:channel . "
                                    "}");
    } else if (event.direction() == Event::Outbound) {
        queryString = QLatin1String("SELECT ?call WHERE { "
                                    "?call nmo:communicationChannel ?:channel . "
                                    "?call nmo:isSent true . "
                                    "}");
    } else if (event.isMissedCall()) {
        queryString = QLatin1String("SELECT ?call WHERE { "
                                    "?call nmo:communicationChannel ?:channel . "
                                    "?call nmo:isSent false . "
                                    "?call nmo:isAnswered false . "
                                    "}");
    } else {
        queryString = QLatin1String("SELECT ?call WHERE { "
                                    "?call nmo:communicationChannel ?:channel . "
                                    "?call nmo:isSent false . "
                                    "?call nmo:isAnswered true . "
                                    "}");
    }

    QSparqlQuery query(queryString);

    QUrl channelUri(TrackerIOPrivate::makeCallGroupURI(event));

    query.bindValue(QLatin1String("channel"), channelUri);

    connect(partQueryRunner, SIGNAL(resultsReceived(QSparqlResult *)),
            this, SLOT(doDeleteCallGroup(QSparqlResult *)),
            Qt::UniqueConnection);
    partQueryRunner->runQuery(query);
    partQueryRunner->startQueue();
}

void CallModelPrivate::doDeleteCallGroup(QSparqlResult *result)
{
    qDebug() << Q_FUNC_INFO;

    QList<int> eventIds;
    tracker()->transaction();
    while (result->next()) {
        QString eventUri = result->value(0).toString();
        int id = Event::urlToId(eventUri);

        Event event;
        event.setType(Event::CallEvent);
        event.setId(id);
        tracker()->deleteEvent(event);
        eventIds << id;
    }

    if (eventIds.size()) {
        CommittingTransaction *t = tracker()->commit();
        if (t) {
            foreach (int id, eventIds) {
                t->addSignal(false, this,
                             "eventDeleted",
                             Q_ARG(int, id));
            }
        }
    } else {
        tracker()->rollback();
    }

    result->deleteLater();
}

void CallModelPrivate::slotAllCallsDeleted(int unused)
{
    Q_UNUSED(unused);
    Q_Q(CallModel);

    qWarning() << __PRETTY_FUNCTION__ << "clearing model";

    q->beginResetModel();
    clearEvents();
    q->endResetModel();
}

void CallModelPrivate::contactSettingsChangedSlot(const QHash<QString, QVariant> &changedSettings)
{
    Q_UNUSED(changedSettings);
    Q_Q(CallModel);

    if (hasBeenFetched)
        q->getEvents();
}

/* ************************************************************************** *
 * ********* P U B L I C   C L A S S   I M P L E M E N T A T I O N ********** *
 * ************************************************************************** */

CallModel::CallModel(QObject *parent)
        : EventModel(*new CallModelPrivate(this), parent)
{
    Q_D(CallModel);
    d->isInTreeMode = true;
}

CallModel::CallModel(CallModel::Sorting sorting, QObject* parent = 0)
        : EventModel(*new CallModelPrivate(this), parent)
{
    Q_D( CallModel );
    d->isInTreeMode = true;

    setFilter( sorting );
}

CallModel::~CallModel()
{
}


void CallModel::setQueryMode( EventModel::QueryMode mode )
{
    EventModel::setQueryMode(mode);
}

bool CallModel::setFilter(CallModel::Sorting sortBy,
                          CallEvent::CallType type,
                          const QDateTime &referenceTime)
{
    Q_D(CallModel);

    // save sorting, reference Time and call event Type for filtering call events
    d->sortBy = sortBy;
    d->eventType = type;
    d->referenceTime = referenceTime;

    if ( d->hasBeenFetched )
    {
        return getEvents();
    }
    return true;
}

bool CallModel::getEvents()
{
    Q_D(CallModel);

    d->hasBeenFetched = true;

    beginResetModel();
    d->clearEvents();
    endResetModel();
    d->countedUids.clear();
    d->updatedGroups.clear();

    if (d->sortBy == SortByContact) {
        QString query = TrackerIOPrivate::prepareGroupedCallQuery();
        d->executeGroupedQuery(query);
        return true;
    }

    EventsQuery query(d->propertyMask);
    query.addPattern(QLatin1String("%1 a nmo:Call .")).variable(Event::Id);

    if (d->eventType != CallEvent::UnknownCallType) {
        if (!d->isInTreeMode) {
            if (d->eventType == CallEvent::ReceivedCallType) {
                query.addPattern(QLatin1String("%1 nmo:isSent \"false\" . "
                                               "%1 nmo:isAnswered \"true\". "))
                    .variable(Event::Id);
            } else if (d->eventType == CallEvent::MissedCallType) {
                query.addPattern(QLatin1String("%1 nmo:isSent \"false\" . "
                                               "%1 nmo:isAnswered \"false\". "))
                    .variable(Event::Id);
            } else if (d->eventType == CallEvent::DialedCallType) {
                query.addPattern(QLatin1String("%1 nmo:isSent \"true\" ."))
                    .variable(Event::Id);
            }
        }

        if (!d->referenceTime.isNull()) {
            query.addPattern(QString(QLatin1String("FILTER (nmo:sentDate(%2) >= \"%1\"^^xsd:dateTime)"))
                             .arg(d->referenceTime.toUTC().toString(Qt::ISODate)))
                .variable(Event::Id);
        }
    }

    query.addModifier("ORDER BY DESC(%1) DESC(tracker:id(%2))")
                     .variable(Event::StartTime)
                     .variable(Event::Id);

    return d->executeQuery(query);
}

bool CallModel::getEvents(CallModel::Sorting sortBy,
                          CallEvent::CallType type,
                          const QDateTime &referenceTime)
{
    Q_D(CallModel);

    d->hasBeenFetched = true;

    return setFilter( sortBy, type, referenceTime );
}

bool CallModel::deleteAll()
{
    Q_D(CallModel);

    d->tracker()->transaction();

    bool deleted;
    deleted = d->tracker()->deleteAllEvents(Event::CallEvent);
    if (!deleted) {
        qWarning() << __PRETTY_FUNCTION__ << "Failed to delete events";
        d->tracker()->rollback();
        return false;
    }

    CommittingTransaction *t = d->commitTransaction(QList<Event>());
    if (t != 0)
        t->addSignal(false, d, "slotAllCallsDeleted", Q_ARG(int, -1));

    return true;
}

bool CallModel::markAllRead()
{
    Q_D(CallModel);

    d->tracker()->transaction();

    bool marked;
    marked = d->tracker()->markAsReadAll(Event::CallEvent);
    if (!marked) {
        qWarning() << __PRETTY_FUNCTION__ << "Failed to delete events";
        d->tracker()->rollback();
        return false;
    }

    d->commitTransaction(QList<Event>());

    return true;
}

bool CallModel::addEvent( Event &event )
{
    return EventModel::addEvent(event);
}

bool CallModel::modifyEvent( Event &event )
{
    Q_D(CallModel);

    if (!d->isInTreeMode || !event.modifiedProperties().contains(Event::IsRead)) {
        return EventModel::modifyEvent(event);
    }

    if (event.id() == -1) {
        qWarning() << Q_FUNC_INFO << "Event id not set";
        return false;
    }

    qDebug() << Q_FUNC_INFO << "setting isRead for call group";
    // isRead has changed, modify the event and set isRead for nested events
    bool isRead = event.isRead();

    d->tracker()->transaction(d->syncOnCommit);

    if (event.lastModified() == QDateTime::fromTime_t(0)) {
         event.setLastModified(QDateTime::currentDateTime());
    }

    if (!d->tracker()->modifyEvent(event)) {
        d->tracker()->rollback();
        return false;
    }

    QList<Event> events;
    events << event;

    if (d->sortBy == SortByContact) {
        d->tracker()->markAsReadCallGroup(event);
    } else {
        QModelIndex index = d->findEvent(event.id());
        if (index.isValid()) {
            EventTreeItem *item = static_cast<EventTreeItem *>(index.internalPointer());
            if (item) {
                // child 0 = event
                for (int i = 1; i < item->childCount(); i++) {
                    item->child(i)->event().setIsRead(isRead);
                    if (!d->tracker()->modifyEvent(item->child(i)->event())) {
                        d->tracker()->rollback();
                        return false;
                    }
                    events << item->child(i)->event();
                }
            }
        }
    }

    CommittingTransaction *t = d->commitTransaction(events);
    if (t) {
        t->addSignal(false,
                     d,
                     "eventsUpdated",
                    Q_ARG(QList<CommHistory::Event>, events));
    }

    return t != 0;
}

bool CallModel::deleteEvent( int id )
{
    Q_D(CallModel);

    if(!d->isInTreeMode)
    {
        return EventModel::deleteEvent(id);
    }

    qDebug() << Q_FUNC_INFO << id;
    QModelIndex index = d->findEvent(id);
    if (!index.isValid())
        return false;

    switch ( d->sortBy )
    {
        case SortByContact :
        case SortByContactAndType:
        {
            EventTreeItem *item = d->eventRootItem->child(index.row());
            d->deleteCallGroup(item->event(), d->sortBy == SortByContactAndType);
            return true;
        }

        case SortByTime :
        {
            EventTreeItem *item = d->eventRootItem->child( index.row() );

            d->tracker()->transaction( d->syncOnCommit );

            QList<Event> deletedEvents;

            // get all events stored in the item and delete them one by one
            for ( int i = 0; i < item->childCount(); i++ )
            {
                // NOTE: when events are sorted by time, the tree hierarchy is only 2 levels deep
                if (!d->tracker()->deleteEvent(item->child(i)->event())) {
                    d->tracker()->rollback();
                    return false;
                }
                deletedEvents << item->child( i )->event();
            }

            d->commitTransaction(deletedEvents);
            // delete event from model (not only from db)
            d->deleteFromModel( id );
            // signal delete in case someone else needs to know it
            emit d->eventDeleted( id );

            return true;
        }
        default :
        {
            qWarning() << __PRETTY_FUNCTION__ << "Deleting of call events from model sorted by type or by service has not been implemented yet.";
            return false;
        }
    }
}

bool CallModel::deleteEvent( Event &event )
{
    return deleteEvent( event.id() );
}

}
