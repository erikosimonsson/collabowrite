#include "sessionserver.h"
#include "protocol.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDataStream>
#include <QTimer>

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
    if (m_documentText == text) {
        return;
    }

    m_documentText = text;

    if (isListening() && !m_broadcastTimer->isActive()) {
        m_broadcastTimer->start();
    }
}

void SessionServer::sendSnapshot(QTcpSocket *client) {
    QByteArray payload = m_documentText.toUtf8();
    auto type = Protocol::MessageType::DocumentSnapshot;

    if (payload.size() > Protocol::MaxPayloadSize - 1) {
        type = Protocol::MessageType::Error;
        payload = "Document is too large. Maximum payload size is 1 MiB.";
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