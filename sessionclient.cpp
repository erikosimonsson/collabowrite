#include "sessionclient.h"
#include "protocol.h"

#include <QTcpSocket>
#include <QtEndian>
#include <QStringDecoder>

SessionClient::SessionClient(QObject *parent) : QObject(parent), m_socket(new QTcpSocket(this)) {
    connect(m_socket, &QTcpSocket::connected, this, &SessionClient::connected);
    connect(m_socket, &QTcpSocket::disconnected, this, &SessionClient::disconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        if (error == QAbstractSocket::RemoteHostClosedError) {
            return;
        }

        emit connectionError(m_socket->errorString());
    });

    m_socket->setReadBufferSize(Protocol::HeaderSize + Protocol::MaxPayloadSize);
    connect(m_socket, &QTcpSocket::readyRead, this, &SessionClient::recieveData);
    connect(m_socket, &QTcpSocket::stateChanged, this, [this](QAbstractSocket::SocketState state) {
        emit activeChanged(state != QAbstractSocket::UnconnectedState);
    });
}

void SessionClient::connectToHost(const QString &address, quint16 port) {
    if (isActive()) {
        return;
    }

    m_receiveBuffer.clear();
    m_revision = 0;
    m_socket->connectToHost(address, port);
}

bool SessionClient::isActive() const {
    return m_socket->state() != QAbstractSocket::UnconnectedState;
}

void SessionClient::recieveData() {
    m_receiveBuffer.append(m_socket->readAll());

    while (m_receiveBuffer.size() >= Protocol::HeaderSize) {
        const quint32 payloadSize = qFromBigEndian<quint32>(m_receiveBuffer.constData());

        if (payloadSize == 0 || payloadSize > Protocol::MaxPayloadSize) {
            failConnection(tr("Invalid message size."));
            return;
        }

        const qsizetype frameSize = Protocol::HeaderSize + payloadSize;

        if (m_receiveBuffer.size() < frameSize) {
            return;
        }

        const auto type = static_cast<Protocol::MessageType>(static_cast<quint8>(m_receiveBuffer.at(Protocol::HeaderSize)));
        const QByteArray payload = m_receiveBuffer.mid(Protocol::HeaderSize + 1, payloadSize - 1);

        m_receiveBuffer.remove(0, frameSize);

        if (type == Protocol::MessageType::DocumentSnapshot) {
            if (payload.size() < Protocol::SnapshotMetadataSize) {
                failConnection(tr("Invalid document snapshot."));
                return;
            }

            const quint64 revision = qFromBigEndian<quint64>(payload.constData());
            QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
            const QString text = decoder(payload.mid(Protocol::SnapshotMetadataSize));

            if (decoder.hasError()) {
                failConnection(tr("Invalid UTF-8 in document snapshot."));
                return;
            }

            m_revision = revision;
            emit documentReceived(text, revision);
        }
        else if (type == Protocol::MessageType::Error) {
            failConnection(QString::fromUtf8(payload));
            return;
        }
        else {
            failConnection(tr("Unkown message type."));
            return;
        }
    }
}

void SessionClient::failConnection(const QString &message) {
    m_receiveBuffer.clear();
    m_socket->abort();
    emit connectionError(message);
}