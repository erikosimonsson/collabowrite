#include "sessionserver.h"
#include "protocol.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDataStream>
#include <QTimer>
#include <limits>

SessionServer::SessionServer(QObject *parent) : QObject(parent), m_server(new QTcpServer(this)), m_broadcastTimer(new QTimer(this)) {
    connect(m_server, &QTcpServer::newConnection, this, &SessionServer::acceptPendingConnections);

    m_broadcastTimer->setSingleShot(true);
    m_broadcastTimer->setInterval(100);

    connect(m_broadcastTimer, &QTimer::timeout, this, &SessionServer::broadcastSnapshot);
}

bool SessionServer::start(quint16 port) {
    if (m_server->isListening()) {
        return true;
    }
    return m_server->listen(QHostAddress::AnyIPv4, port);
}

void SessionServer::stop() {
    m_broadcastTimer->stop();
    m_server->close();

    const QList<QTcpSocket *> clients = m_clients;
    m_clients.clear();

    for (QTcpSocket *client : clients) {
        QObject::disconnect(client, nullptr, this, nullptr);
        client->disconnectFromHost();
        client->deleteLater();
    }
}

bool SessionServer::isListening() const {
    return m_server->isListening();
}

QString SessionServer::errorString() const {
    return m_server->errorString();
}

void SessionServer::acceptPendingConnections() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket *client = m_server->nextPendingConnection();
        m_clients.append(client);

        const QString address = QStringLiteral("%1:%2").arg(client->peerAddress().toString()).arg(client->peerPort());

        connect(client, &QTcpSocket::disconnected, this, [this, client, address]() {
            m_clients.removeOne(client);
            emit clientDisconnected(address);
            client->deleteLater();
        });

        emit clientConnected(address);
        sendSnapshot(client);
    }
}

void SessionServer::setDocumentText(const QString &text) {
    Q_ASSERT(!isListening());

    if (isListening()) {
        return;
    }

    m_documentText = text;
    m_revision = 0;
}

void SessionServer::sendSnapshot(QTcpSocket *client) {
    QByteArray payload;
    auto type = Protocol::MessageType::DocumentSnapshot;
    const QByteArray text = m_documentText.toUtf8();

    if (!m_documentText.isValidUtf16() ||text.size() > Protocol::MaxDocumentBytes) {
        type = Protocol::MessageType::Error;
        payload = "Document cannot be shared: invalid text or size limit exceeded.";
    }
    else {
        QDataStream snapshot(&payload, QIODevice::WriteOnly);
        snapshot.setVersion(QDataStream::Qt_6_5);
        snapshot.setByteOrder(QDataStream::BigEndian);
        snapshot << m_revision;
        payload.append(text);
    }

    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream.setVersion(QDataStream::Qt_6_5);

    stream << quint32(payload.size() + 1) << quint8(type);
    frame.append(payload);

    const qint64 maxQueuedBytes = 4 * (Protocol::HeaderSize + Protocol::MaxPayloadSize);

    if (client->bytesToWrite() + frame.size() > maxQueuedBytes) {
        client->abort();
        return;
    }

    if (client->write(frame) != frame.size()) {
        client->abort();
    }
}

void SessionServer::broadcastSnapshot() {
    const QList<QTcpSocket *> client = m_clients;

    for (QTcpSocket *client : client) {
        if (client->state() == QAbstractSocket::ConnectedState) {
            sendSnapshot(client);
        }
    }
}

quint64 SessionServer::revision() const {
    return m_revision;
}

bool SessionServer::applyEdit(const Protocol::EditRequest &request, QString &error) {
    error.clear();

    if (!isListening()) {
        error = tr("No session is running.");
        return false;
    }

    if (request.baseRevision != m_revision) {
        error = tr("Edit uses revision %1, but the host is at revision %2.").arg(request.baseRevision).arg(m_revision);
        return false;
    }

    const TextEdit &edit = request.edit;
    const qsizetype size = m_documentText.size();

    if (request.operationId.isNull() || edit.position < 0 || edit.position > size || edit.removedLength < 0 || edit.removedLength > size - edit.position || !edit.insertedText.isValidUtf16()) {
        error = tr("Invalid text edit.");
        return false;
    }

    const auto splitsSurrogatePair = [this](qsizetype position) {
        return position > 0 && position < m_documentText.size() && m_documentText.at(position - 1).isHighSurrogate() && m_documentText.at(position).isLowSurrogate();
    };

    if (splitsSurrogatePair(edit.position) || splitsSurrogatePair(edit.position + edit.removedLength)) {
        error = tr("Edit would split a Unicode character.");
        return false;
    }

    if (m_revision == std::numeric_limits<quint64>::max()) {
        error = tr("Revision limit reached. Start a new session.");
        return false;
    }

    QString updated = m_documentText;
    updated.replace(edit.position, edit.removedLength, edit.insertedText);

    if (updated.toUtf8().size() > Protocol::MaxDocumentBytes) {
        error = tr("Document exceeds the sharing size limit.");
        return false;
    }

    m_documentText = updated;
    ++m_revision;

    if (!m_broadcastTimer->isActive()) {
        m_broadcastTimer->start();
    }

    return true;
}