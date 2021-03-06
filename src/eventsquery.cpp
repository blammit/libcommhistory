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
#include <QStringList>

#include "eventsquery.h"

namespace CommHistory
{

// keep in sync with Event::Property
const char * VARIABLE_NAMES[] = {"message",
                                 "type",
                                 "startTime",
                                 "endTime",
                                 "direction",
                                 "isDraft",
                                 "isRead",
                                 "isMissedCall",
                                 "isEmergencyCall",
                                 "status",
                                 "bytesReceived",
                                 "localUid",
                                 "remoteUid",
                                 "contactId",
                                 "contactName",
                                 "parentId",
                                 "subject",
                                 "freeText",
                                 "groupId",
                                 "messageToken",
                                 "lastModified",
                                 "eventCount",
                                 "fromVCardFileName",
                                 "fromVCardLabel",
                                 "encoding",
                                 "characterSet",
                                 "language",
                                 "isDeleted",
                                 "reportDelivery",
                                 "validityPeriod",
                                 "contentLocation",
                                 "messageParts",
                                 "cc",
                                 "bcc",
                                 "readStatus",
                                 "reportRead",
                                 "reportReadRequested",
                                 "mmsId",
                                 "mmsTo",
                                 "contacts",
                                 "isAction",
                                 "headers"};

const int VARIABLE_NAMES_SIZE = sizeof(VARIABLE_NAMES)/sizeof(VARIABLE_NAMES[0]);

QString eventPropertyName(Event::Property p)
{
    if (p >= VARIABLE_NAMES_SIZE) {
        qCritical() << "Event::Property variable names are out of date";
        Q_ASSERT(false);
    }

    return QString("?") + QString(QLatin1String(VARIABLE_NAMES[p]));
}
// functions to hardcode ontology at hands
QLatin1String ontologyProperty(Event::Property p)
{
    switch (p){
    case Event::Type:
        return QLatin1String("rdf:type");
    case Event::StartTime:
        return QLatin1String("nmo:sentDate");
    case Event::EndTime:
        return QLatin1String("nmo:receivedDate");
    case Event::Direction:
        return QLatin1String("nmo:isSent");
    case Event::IsDraft:
        return QLatin1String("nmo:isDraft");
    case Event::IsRead:
        return QLatin1String("nmo:isRead");
    case Event::IsMissedCall:
    case Event::IsAction: //TODO
        return QLatin1String("nmo:isAnswered");
    case Event::IsEmergencyCall:
        return QLatin1String("nmo:isEmergency");
    case Event::Status:
        return QLatin1String("nmo:deliveryStatus");
    case Event::BytesReceived:
        return QLatin1String("nie:contentSize");
    // LocalUid/RemoteUid map to to/from depending on the direction
    // and will be defined when results available
    // for the query building the arbitrary mapping is used cause
    // the properties are always requested togeather,
    // NOTE: the mapping should be same in queryresult.cpp !!!
    case Event::LocalUid:
        return QLatin1String("?from");
    case Event::RemoteUid:
        return QLatin1String("?to");
    case Event::ParentId:
        return QLatin1String("nmo:phoneMessageId");
    case Event::Subject:
        return QLatin1String("nmo:messageSubject");
    case Event::FreeText:
        return QLatin1String("nie:plainTextContent");
    case Event::GroupId:
        return QLatin1String("nmo:communicationChannel");
    case Event::MessageToken:
        return QLatin1String("nmo:messageId");
    case Event::LastModified:
        return QLatin1String("nie:contentLastModified");
    case Event::FromVCardFileName:
        return QLatin1String("nfo:fileName");
    case Event::FromVCardLabel:
        return QLatin1String("rdfs:label");
    case Event::Encoding:
        return QLatin1String("nmo:encoding");
    case Event::CharacterSet:
        return QLatin1String("nie:characterSet");
    case Event::Language:
        return QLatin1String("nie:language");
    case Event::IsDeleted:
        return QLatin1String("nmo:isDeleted");
    case Event::ReportDelivery:
        return QLatin1String("nmo:reportDelivery");
    case Event::ValidityPeriod:
        return QLatin1String("nmo:validityPeriod");
    case Event::ContentLocation:
        return QLatin1String("nie:generator");
    case Event::Cc:
        return QLatin1String("nmo:cc");
    case Event::Bcc:
        return QLatin1String("nmo:bcc");
    case Event::ReadStatus:
        return QLatin1String("nmo:reportReadStatus");
    case Event::ReportRead:
        return QLatin1String("nmo:sentWithReportRead");
    case Event::ReportReadRequested:
        return QLatin1String("nmo:mustAnswerReportRead");
    case Event::MmsId:
        return QLatin1String("nmo:mmsId");
    case Event::To:
    case Event::Headers:
        return QLatin1String("nmo:messageHeader");
    default:
        qCritical() << Q_FUNC_INFO << "Invalid ontology property for " << p;
        Q_ASSERT(false);
    }

    return QLatin1String("");
}

QString functionForProperty(Event::Property p)
{
    QStringList func;

    switch(p) {
    case Event::Id:
        break; //pattern only
    case Event::Type:
    case Event::StartTime:
    case Event::EndTime:
    case Event::Direction:
    case Event::IsDraft:
    case Event::IsRead:
    case Event::IsMissedCall:
    case Event::IsEmergencyCall:
    case Event::Status:
    case Event::BytesReceived:
    case Event::ParentId:
    case Event::Subject:
    case Event::FreeText:
    case Event::GroupId:
    case Event::MessageToken:
    case Event::LastModified:
    case Event::EventCount:
    case Event::Encoding:
    case Event::CharacterSet:
    case Event::Language:
    case Event::IsDeleted:
    case Event::ReportDelivery:
    case Event::ValidityPeriod:
    case Event::ContentLocation:
    case Event::ReportRead:
    case Event::ReportReadRequested:
    case Event::ReadStatus:
    case Event::MmsId:
    case Event::IsAction:
        func << ontologyProperty(p)
             << "("
             << eventPropertyName(Event::Id)
             << ")";
        break;
    case Event::LocalUid:
    case Event::RemoteUid:
        func << QLatin1String("(SELECT GROUP_CONCAT(")
             << QLatin1String("tracker:coalesce(nco:imID(?medium), nco:phoneNumber(?medium), ?medium), \"\\u001e\") ")
             << QLatin1String("WHERE { ")
             << ontologyProperty(p)
             << QLatin1String(" nco:hasContactMedium ?medium . })");
        break;
    case Event::ContactId:
        // join all contact matches
        func << QLatin1String(
            "  (SELECT GROUP_CONCAT(" \
            "    fn:concat(tracker:id(?contact), \"\\u001e\", " \
            "              tracker:coalesce(nco:nameGiven(?contact), \"\"), \"\\u001e\", " \
            "              tracker:coalesce(nco:nameFamily(?contact), \"\"), " \
            "              \"\\u001d\"," \
            "              tracker:coalesce(nco:nickname(?contact), \"\")," \
            "              \"\\u001d\"," \
            "              tracker:coalesce(?nicknames, \"\")), " \
            "    \"\\u001c\") " \
            "WHERE {" \
            "  SELECT ?contact " \
            "  (SELECT GROUP_CONCAT(fn:string-join((nco:imID(?addr), ?nickname), \"\\u001f\"), \"\\u001e\")" \
            "  WHERE {" \
            "    ?contact nco:hasAffiliation [ nco:hasIMAddress ?addr ] . " \
            "    ?addr nco:imNickname ?nickname . " \
            "  }) AS ?nicknames " \
            "  WHERE {" \
            "  {" \
            "    ?target nco:hasIMAddress ?address . " \
            "    ?contact nco:hasAffiliation [ nco:hasIMAddress ?address ] . " \
            "  } UNION {" \
            "    ?target nco:hasPhoneNumber [ maemo:localPhoneNumber ?number ] . " \
            "    ?contact nco:hasAffiliation [ nco:hasPhoneNumber [ maemo:localPhoneNumber ?number ] ] . " \
            "  }}" \
            "}) AS ?contacts "
            );
        break;
    case Event::ContactName:
        func << QLatin1String("rdf:nil");
        break;
    case Event::FromVCardFileName:
    case Event::FromVCardLabel:
        func << ontologyProperty(p)
             <<  QLatin1String("(nmo:fromVCard(")
             << eventPropertyName(Event::Id)
             << "))";
        break;
    case Event::Cc:
    case Event::Bcc:
        func << QString(QLatin1String(
                "(SELECT GROUP_CONCAT(%1, \'\\u001e\') {"
                "%2 %3 %1Contact . "
                "{%1Contact nco:hasIMAddress [nco:imID %1]}"
                "UNION"
                "{%1Contact nco:hasPhoneNumber [nco:phoneNumber %1]}})"))
             .arg(eventPropertyName(p))
             .arg(eventPropertyName(Event::Id))
             .arg(ontologyProperty(p));
        break;
    case Event::MessageParts:
        //return eventPropertyName(Event::Id);
        break;
    case Event::Contacts:
        break;
    case Event::To:
        break;
    case Event::Headers:
        func << QString(QLatin1String(
                "(SELECT GROUP_CONCAT(fn:string-join((nmo:headerName(?header), nmo:headerValue(?header)), \"\\u001d\"), \"\\u001f\") " \
                "WHERE { " \
                "%1 %2 ?header " \
                "})"))
                .arg(eventPropertyName(Event::Id))
                .arg(ontologyProperty(p));
        break;
    default:
        qCritical() << Q_FUNC_INFO << "Unhandled property for " << p;
        Q_ASSERT(false);
    }

    return func.join("");
}

QString patternForProperty(Event::Property p)
{
    QStringList pattern;

    switch(p) {
    case Event::Id:
        pattern << eventPropertyName(Event::Id)
                << QLatin1String("a nmo:Message .");
        break;
    case Event::Type:
    case Event::StartTime:
    case Event::EndTime:
    case Event::Direction:
    case Event::IsDraft:
    case Event::IsRead:
    case Event::IsMissedCall:
    case Event::IsEmergencyCall:
    case Event::Status:
    case Event::BytesReceived:
    case Event::ParentId:
    case Event::Subject:
    case Event::FreeText:
    case Event::GroupId:
    case Event::MessageToken:
    case Event::LastModified:
    case Event::EventCount:
    case Event::Encoding:
    case Event::CharacterSet:
    case Event::Language:
    case Event::IsDeleted:
    case Event::ReportDelivery:
    case Event::ValidityPeriod:
    case Event::IsAction:
        pattern << eventPropertyName(Event::Id)
                << ontologyProperty(p)
                << eventPropertyName(p)
                << ".";
        break;
    case Event::LocalUid:
    case Event::RemoteUid:
        break;
    case Event::FromVCardFileName:
    case Event::FromVCardLabel:
        pattern << eventPropertyName(Event::Id)
                <<  QLatin1String("nmo:fromVCard")
                << "["
                << ontologyProperty(p)
                << eventPropertyName(p)
                << "] .";
        break;
    case Event::ContactId:
    case Event::ContactName:
    case Event::ContentLocation:
    case Event::MessageParts:
    case Event::Cc:
    case Event::Bcc:
    case Event::ReadStatus:
    case Event::ReportRead:
    case Event::ReportReadRequested:
    case Event::MmsId:
    case Event::To:
        //return eventPropertyName(Event::Id);
        break;
    case Event::Contacts:
        break;
    case Event::Headers:
        break;
    default:
        qCritical() << Q_FUNC_INFO << "Unhandled property for " << p;
        Q_ASSERT(false);
    }

    return pattern.join(" ");
}

class EventsQueryPrivate {
public:
    EventsQueryPrivate(EventsQuery *parent,
                       const Event::PropertySet &propertySet) :
            q(parent),
            distinct(false),
            lastAdded(Patterns)
    {
        Event::PropertySet finalProperties(propertySet);

        if (!finalProperties.contains(Event::Id))
            finalProperties.insert(Event::Id);
        // no contactName without contactId
        if (finalProperties.contains(Event::ContactId) !=
            finalProperties.contains(Event::ContactName)) {
            finalProperties.insert(Event::ContactId);
            finalProperties.insert(Event::ContactName);
        }
        // localUid/remoteUid should be togeather forever
        if (finalProperties.contains(Event::LocalUid) !=
            finalProperties.contains(Event::RemoteUid)
            // no contactId withouth localUid/remoteUid
            || finalProperties.contains(Event::ContactId)) {
            finalProperties.insert(Event::LocalUid);
            finalProperties.insert(Event::RemoteUid);
        }

        // vcardFilename and label should be togeather
        if (finalProperties.contains(Event::FromVCardFileName) !=
            finalProperties.contains(Event::FromVCardLabel)) {
            finalProperties.insert(Event::FromVCardFileName);
            finalProperties.insert(Event::FromVCardLabel);
        }

        // direction is needed to set the following properties correctly
        if (!finalProperties.contains(Event::Direction)
            && (finalProperties.contains(Event::LocalUid)
                || finalProperties.contains(Event::RemoteUid)
                || finalProperties.contains(Event::Status)))
            finalProperties.insert(Event::Direction);

        // to handle missing status, type is needed as well
        if (finalProperties.contains(Event::Status))
            finalProperties.insert(Event::Type);

        // x-mms-to: is included in headers
        if (finalProperties.contains(Event::To)
            || finalProperties.contains(Event::Headers)) {
            finalProperties -= Event::To;
            finalProperties.insert(Event::Headers);
        }

        variables = finalProperties.toList();
    }

    EventsQuery *q;

    bool distinct;
    QList<Event::Property> variables;

    enum QueryPart {
        Patterns = 0,
        Modifiers,
        Projections,
        NumberOfParts
    };
    struct {
        QStringList patterns;
        Event::PropertySet variables;
    } parts[NumberOfParts];
    QueryPart lastAdded;

    void addToPart(QueryPart part, const QString &item)
    {
        parts[part].patterns.append(item);
        lastAdded = part;
    }

    void referenceVariable(Event::Property property)
    {
        parts[lastAdded].patterns.last() = parts[lastAdded].patterns.last().arg(eventPropertyName(property));
        parts[lastAdded].variables.insert(property);
        if (!variables.contains(property))
            variables.append(property);
    }
};

EventsQuery::EventsQuery(const Event::PropertySet &propertySet) :
        d(new EventsQueryPrivate(this, propertySet))
{
}

EventsQuery::~EventsQuery()
{
    delete d;
}

EventsQuery& EventsQuery::addPattern(const QString &pattern)
{
    d->addToPart(EventsQueryPrivate::Patterns, pattern);

    return *this;
}

EventsQuery& EventsQuery::addModifier(const QString &pattern)
{
    d->addToPart(EventsQueryPrivate::Modifiers, pattern);

    return *this;
}

EventsQuery& EventsQuery::addProjection(const QString &projection)
{
    d->addToPart(EventsQueryPrivate::Projections, projection);

    return *this;
}

EventsQuery& EventsQuery::variable(Event::Property property)
{
    d->referenceVariable(property);

    return *this;
}

void EventsQuery::setDistinct(bool distinct)
{
    d->distinct = distinct;
}

bool EventsQuery::distinct() const
{
    return d->distinct;
}

QList<Event::Property> EventsQuery::eventProperties() const
{
    qDebug() << Q_FUNC_INFO;
    return d->variables;
}

QString EventsQuery::query() const
{
    qDebug() << Q_FUNC_INFO;
    QStringList query;

    // generate variable names
    QStringList projections;
    QStringList subselectProjections;
    foreach(Event::Property p, d->variables) {

        if (p == Event::EventCount) { // runtime
            d->variables.removeOne(p);
            continue;
        }

        if (d->parts[EventsQueryPrivate::Modifiers].variables.contains(p)) {
            // variable referenced in modifiers, use pattern instead of function
            QString varName = eventPropertyName(p);
            projections.append(varName);
            subselectProjections.append(varName);
            d->parts[EventsQueryPrivate::Patterns].patterns.append(patternForProperty(p));
        } else if (d->parts[EventsQueryPrivate::Patterns].variables.contains(p)) {
            // TODO: varable referenced in used defined pattern and should be defined there
            QString varName = eventPropertyName(p);
            projections.append(varName);
            subselectProjections.append(varName);
            //d->parts[EventsQueryPrivate::Patterns].patterns.append(patternForProperty(p));
        } else {
            QString func = functionForProperty(p);

            if (!func.isEmpty())
                projections.append(func);
            else {
                //fallback to pattern
                QString patterns = patternForProperty(p);
                if (!patterns.isEmpty()) {
                    projections.append(eventPropertyName(p));
                    subselectProjections.append(eventPropertyName(p));
                    d->parts[EventsQueryPrivate::Patterns].patterns.append(patternForProperty(p));
                } else {
                    qDebug() << "Ignored prop" << p;
                    d->variables.removeOne(p);
                }
            }
        }
    }

    projections << d->parts[EventsQueryPrivate::Projections].patterns;

    query << QLatin1String("SELECT");
    if (d->distinct)
        query << QLatin1String("DISTINCT");
    query << projections.join(" ");
    query << QLatin1String("WHERE {");

    /* handle a few properties separately for query purposes -
     */
    query << QLatin1String("SELECT ?message ?from ?to ")
          << QLatin1String("IF (nmo:isSent(?message) = true, ?to, ?from) AS ?target ")
          << subselectProjections.join(" ")
          << QLatin1String("WHERE {"
                           "?message nmo:from ?from ; nmo:to ?to . ");

    query << d->parts[EventsQueryPrivate::Patterns].patterns;
    query << QLatin1String("} }");
    query << d->parts[EventsQueryPrivate::Modifiers].patterns;

    return query.join(" ");
}

} // namespace
