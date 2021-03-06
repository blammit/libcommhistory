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

#include <QMutex>
#include <QDebug>

#include <QSparqlResultRow>
#include <QSparqlConnection>
#include <QSparqlError>

#include "queryrunner.h"
#include "queryresult.h"
#include "event.h"
#include "messagepart.h"
#include "trackerio.h"
#include "trackerio_p.h"

using namespace CommHistory;

QueryRunner::QueryRunner(TrackerIO *trackerIO, QObject *parent)
        : QObject(parent)
        , m_streamedMode(false)
        , m_chunkSize(0)
        , m_firstChunkSize(0)
        , m_enableQueue(false)
        , m_canFetchMore(false)
        , m_pTracker(trackerIO)
{
    qDebug() << __PRETTY_FUNCTION__;
}

QueryRunner::~QueryRunner()
{
    qDebug() << __PRETTY_FUNCTION__ << this << this->thread();

    endActiveQuery();
}

void QueryRunner::setStreamedMode(bool mode)
{
    m_streamedMode = mode;
}

void QueryRunner::setChunkSize(int size)
{
    m_chunkSize = size;
}

void QueryRunner::setFirstChunkSize(int size)
{
    m_firstChunkSize = size;
}

void QueryRunner::enableQueue(bool enable)
{
    m_enableQueue = enable;
}

void QueryRunner::addQueryToQueue(QueryType type,
                                  const QSparqlQuery &query,
                                  const QList<Event::Property> &properties)
{
    qDebug() << Q_FUNC_INFO << QThread::currentThread()  << this << "->";

    QMutexLocker locker(&m_mutex);

    QueryResult result;
    result.query = query;
    result.queryType = type;
    result.properties = properties;
    result.eventId = 0;

    m_queries.append(result);

#ifdef DEBUG
    m_timer.start();
#endif

    if (!m_enableQueue)
        QMetaObject::invokeMethod(this, "nextSlot", Qt::QueuedConnection);

    qDebug() << Q_FUNC_INFO << QThread::currentThread()  << this << "<-";
}

void QueryRunner::runEventsQuery(const QString &query, const QList<Event::Property> &properties)
{
    qDebug() << Q_FUNC_INFO << QThread::currentThread()  << this << "->";

    QSparqlQuery sparqlQuery(query);
    addQueryToQueue(EventQuery, sparqlQuery, properties);
}

void QueryRunner::runGroupQuery(const QString &query)
{
    qDebug() << Q_FUNC_INFO << QThread::currentThread()  << this << "->";

    QSparqlQuery sparqlQuery(query);
    addQueryToQueue(GroupQuery, sparqlQuery);
}

void QueryRunner::runGroupedCallQuery(const QString &query)
{
    qDebug() << Q_FUNC_INFO << QThread::currentThread()  << this << "->";

    QSparqlQuery sparqlQuery(query);
    addQueryToQueue(GroupedCallQuery, sparqlQuery);
}

void QueryRunner::runMessagePartQuery(const QString &query)
{
    qDebug() << Q_FUNC_INFO << QThread::currentThread()  << this << "->";

    QSparqlQuery sparqlQuery(query);
    addQueryToQueue(MessagePartQuery, sparqlQuery);
}

void QueryRunner::runQuery(const QSparqlQuery &query)
{
    qDebug() << Q_FUNC_INFO << QThread::currentThread()  << this << "->";

    addQueryToQueue(GenericQuery, query);
}

void QueryRunner::startQueue()
{
    QMetaObject::invokeMethod(this, "nextSlot", Qt::QueuedConnection);
}

void QueryRunner::fetchMore()
{
    qDebug() << Q_FUNC_INFO << QThread::currentThread() << this;

    QMetaObject::invokeMethod(this, "fetchMoreSlot", Qt::QueuedConnection);
}

void QueryRunner::startNextQueryIfReady()
{
    if (m_enableQueue && m_activeQuery.result)
        return;  // ongoing query and queue mode enabled

    endActiveQuery();

    m_mutex.lock();
    if (!m_queries.isEmpty()) {
        // start new query
        m_activeQuery = m_queries.takeFirst();

        qDebug() << &(m_pTracker->d->connection()) << QThread::currentThread();
    }
    m_mutex.unlock();

    if (!m_activeQuery.query.query().isEmpty() && m_activeQuery.result.isNull()) {
        // TODO: try to put query execution to trackerIOPrivate
        lastReadPos = QSparql::BeforeFirstRow;
        m_syncMode = m_streamedMode && m_pTracker->d->connection().hasFeature(QSparqlConnection::SyncExec);

        if (m_syncMode) {
            m_activeQuery.result = m_pTracker->d->connection().syncExec(m_activeQuery.query);
            if (m_activeQuery.result->hasError())
                finished();
            else
                readData();
        } else {
            m_activeQuery.result = m_pTracker->d->connection().exec(m_activeQuery.query);

            if (m_activeQuery.result->hasError()) {
                finished();
            } else {
                connect(m_activeQuery.result.data(),
                        SIGNAL(dataReady(int)),
                        this, SLOT(dataReady(int)));
                connect(m_activeQuery.result.data(),
                        SIGNAL(finished()),
                        this, SLOT(finished()));
            }
        }
    }
}

void QueryRunner::nextSlot()
{
    qDebug() << Q_FUNC_INFO << QThread::currentThread() << this;

    startNextQueryIfReady();
}

void QueryRunner::fetchMoreSlot()
{
    qDebug() << Q_FUNC_INFO << QThread::currentThread();

    if (m_activeQuery.result) {
        readData();
    }
}

bool QueryRunner::reallyFetchMore(int pos)
{
    if (m_streamedMode && m_activeQuery.result) {
        if (pos == QSparql::BeforeFirstRow)
            return true;
        if (pos == QSparql::AfterLastRow)
            return false;

        qDebug() << Q_FUNC_INFO << pos << m_firstChunkSize << m_chunkSize;

        if (pos == m_firstChunkSize - 1)
            return false;

        if (pos >= m_firstChunkSize) {
            pos = (pos - m_firstChunkSize) % m_chunkSize;
            if (pos == m_chunkSize - 1)
                return false;
        }
    }
    return true;
}

void QueryRunner::dataReady(int totalCount)
{
    qDebug() << Q_FUNC_INFO << totalCount;

    if (!m_streamedMode || reallyFetchMore(lastReadPos))
        readData();
}

void QueryRunner::readData()
{
    int start = lastReadPos;
    if (start == QSparql::BeforeFirstRow)
        start = 0;
    else
        ++start;
    int added = 0;

    qDebug() << Q_FUNC_INFO << "read from:" << start;

    m_activeQuery.result->setPos(lastReadPos);

    if (m_activeQuery.queryType == EventQuery) {
        QList<Event> events;
        QVariantList extra;

        while (m_activeQuery.result->next()) {
            Event event;

            m_activeQuery.fillEventFromModel(event);
            events.append(event);

            int max = m_activeQuery.result->current().count();
            for(int i = m_activeQuery.properties.size(); i < max; i++) {
                extra.append(m_activeQuery.result->value(i));
            }

            ++added;
            lastReadPos = m_activeQuery.result->pos();
            if (!reallyFetchMore(lastReadPos))
                break;
        }
        checkCanFetchMoreChange();
        if (added) {
            emit eventsReceived(start, start + added - 1, events);
            // TODO: add extra to eventsReceived on next break
            if (!extra.isEmpty())
                emit eventsReceivedExtra(events, extra);
        }
    } else if (m_activeQuery.queryType == GroupQuery) {
        QList<Group> groups;

        while (m_activeQuery.result->next()) {
            Group group;
            m_activeQuery.fillGroupFromModel(group);
            groups.append(group);
            ++added;
            lastReadPos = m_activeQuery.result->pos();
            if (!reallyFetchMore(lastReadPos))
                break;
        }

        checkCanFetchMoreChange();
        if (added)
            emit groupsReceived(start, start + added - 1, groups);
    } else if (m_activeQuery.queryType == MessagePartQuery) {
        QList<MessagePart> parts;
        while (m_activeQuery.result->next()) {
            MessagePart part;
            m_activeQuery.fillMessagePartFromModel(part);
            parts.append(part);
            ++added;
            lastReadPos = m_activeQuery.result->pos();
            if (!reallyFetchMore(lastReadPos))
                break;
        }

        checkCanFetchMoreChange();
        if (added)
            emit messagePartsReceived(m_activeQuery.eventId, parts);
    } else if (m_activeQuery.queryType == GroupedCallQuery) {
        QList<Event> events;

        while (m_activeQuery.result->next()) {
            Event event;
            m_activeQuery.fillCallGroupFromModel(event);
            events.append(event);
            ++added;
            lastReadPos = m_activeQuery.result->pos();
            if (!reallyFetchMore(lastReadPos))
                break;
        }

        checkCanFetchMoreChange();
        if (added)
            emit eventsReceived(start, start + added - 1, events);
    }

    // really finish current query in case more date than chunk size were read
    if (m_streamedMode && !m_canFetchMore)
        finished();

#ifdef DEBUG
    qDebug() << "*** TIMER" << m_timer.elapsed();
#endif
}

void QueryRunner::checkCanFetchMoreChange()
{
    bool newFetchMore = !(m_activeQuery.result->isFinished()
                          && m_activeQuery.result->pos() == QSparql::AfterLastRow);
    if (newFetchMore != m_canFetchMore) {
        m_canFetchMore = newFetchMore;
        emit canFetchMoreChanged(m_canFetchMore);
    }
}

void QueryRunner::finished()
{
    qDebug() << Q_FUNC_INFO;

    bool continueNext = false;

    if (m_activeQuery.queryType == GenericQuery) {
        emit resultsReceived(m_activeQuery.result);
        continueNext = m_enableQueue && !m_queries.isEmpty();
    } else {
        // ignore if there is no query or not all data were sent yet
        if (m_activeQuery.result.isNull()
            || (!m_activeQuery.result->hasError()
                && (!m_activeQuery.result->isFinished()
                    || (!m_syncMode && lastReadPos < m_activeQuery.result->size() - 1))))
            return;

        bool abort = m_activeQuery.result->hasError();
        if (abort) {
            qCritical() << m_activeQuery.result->lastError().message();
        } else {
            checkCanFetchMoreChange();
            {
                QMutexLocker locker(&m_mutex);
                continueNext = m_enableQueue && !m_queries.isEmpty();
            }
        }

        if (!continueNext)
            emit modelUpdated(!abort);
    }

    endActiveQuery();

    if (continueNext) {
        // start next query from queue
        nextSlot();
    }
}

void QueryRunner::endActiveQuery()
{
    if (m_activeQuery.result) {
        m_activeQuery.result->disconnect(this);
        if (m_activeQuery.queryType != GenericQuery)
            // workaround qsparql memory problems
            QMetaObject::invokeMethod(m_activeQuery.result, "deleteLater", Qt::QueuedConnection);
        m_activeQuery.result = 0;
    }
    m_activeQuery.query.setQuery(QString());
}
