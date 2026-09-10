#include "sessionclient.h"
#include "protocol.h"

#include <QTcpSocket>
#include <QtEndian>
#include <QStringDecoder>
#include <QTimer>
#include <limits>

SessionClient::SessionClient(QObject *parent) : QObject(parent), m_socket(new QTcpSocket(this)), m_replyTimer(new QTimer(this)) {
    m_replyTimer->setSingleShot(true);
    m_replyTimer->setInterval(10000);

    connect(m_replyTimer, &QTimer::timeout, this, [this]() {
        failConnection(tr("Timed out waiting for the host's edit reply."));
    });

    connect(m_socket, &QTcpSocket::connected, this, &SessionClient::connected);

    connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
        emit disconnected();

        if(hasPendingChanges()) {
            emit connectionError(tr("Disconnected with unconfirmed changes. Your draft is still in the editor; save it before rejoining. The last submitted edit might already have reached the host."));
        }
    });

    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        if (error != QAbstractSocket::RemoteHostClosedError) {
            emit connectionError(m_socket->errorString());
        }
    });

    m_socket->setReadBufferSize(Protocol::HeaderSize + Protocol::MaxPayloadSize);

    connect(m_socket, &QTcpSocket::readyRead, this, &SessionClient::recieveData);

    connect(m_socket, &QTcpSocket::stateChanged, this, [this](QAbstractSocket::SocketState state) {
        if (state == QAbstractSocket::UnconnectedState) {
            m_replyTimer->stop();
            m_receiveBuffer.clear();
            m_ready = false;
            emit readyChanged(false);
        }

        emit activeChanged(state != QAbstractSocket::UnconnectedState);
    });
}

void SessionClient::connectToHost(const QString &address, quint16 port) {
    if (isActive()) {
        return;
    }

    m_replyTimer->stop();
    m_receiveBuffer.clear();
    m_revision = 0;
    m_ready = false;
    m_documentText.clear();
    m_draftText.clear();
    m_sentText.clear();
    m_pendingId = QUuid{};
    emit readyChanged(false);

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

            handleSnapshot(text, revision);
        }
        else if (type == Protocol::MessageType::EditAccepted || type == Protocol::MessageType::EditRejected) {
            Protocol::EditReply reply;

            if (!Protocol::decodeEditReply(payload, reply)) {
                failConnection(tr("Invalid edit reply."));
                return;
            }

            handleEditReply(type == Protocol::MessageType::EditAccepted, reply);
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
    QString explanation = message;

    if (hasPendingChanges()) {
        explanation += tr("Your draft is still in the editor; save it before rejoining. The last submitted edit might have already reached the host.");
    }

    m_replyTimer->stop();
    m_ready = false;
    m_receiveBuffer.clear();
    m_socket->abort();

    emit connectionError(explanation);
}

void SessionClient::submitDraft(const QString &text) {
    m_draftText = text;

    if (!m_ready || m_socket->state() != QAbstractSocket::ConnectedState) {
        failConnection(tr("The session is not ready for editing."));
        return;
    }

    if (!text.isValidUtf16() || text.toUtf8().size() > Protocol::MaxDocumentBytes) {
        failConnection(tr("The draft is invalid or too large to share."));
        return;
    }

    sendNextEdit();
}

void SessionClient::sendNextEdit() {
    if (!m_ready || !m_pendingId.isNull() || m_draftText == m_documentText) {
        return;
    }

    if (m_revision == std::numeric_limits<quint64>::max()) {
        failConnection(tr("The session revision limit was reached."));
        return;
    }

    const Protocol::EditRequest request{
        QUuid::createUuid(),
        m_revision,
        makeTextEdit(m_documentText, m_draftText)
    };

    const QByteArray body = Protocol::encodeEditRequest(request);

    if (body.isEmpty()) {
        failConnection(tr("The edit could not be encoded; it may be too large."));
        return;
    }

    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_5);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(body.size() + 1) << quint8(Protocol::MessageType::EditRequest);
    frame.append(body);

    m_pendingId = request.operationId;
    m_sentText = m_draftText;
    m_replyTimer->start();

    const qint64 maxQueuedBytes = 4 * (Protocol::HeaderSize + Protocol::MaxPayloadSize);

    if (m_socket->bytesToWrite() + frame.size() > maxQueuedBytes || m_socket->write(frame) != frame.size()) {
        failConnection(tr("Could not queue the edit for sending."));
    }
}

void SessionClient::handleSnapshot(const QString &text, quint64 revision) {
    if (m_ready) {
        if (revision < m_revision) {
            return;
        }

        if (revision == m_revision) {
            if (text != m_documentText) {
                failConnection(tr("The host sent inconsisten document data."));
            }
            
            return;
        }

        if (hasPendingChanges()) {
            failConnection(tr("The host document changed while you were editing."));
            return;
        }
    }

    const bool firstSnapshot = !m_ready;

    m_documentText = text;
    m_draftText = text;
    m_revision = revision;
    m_ready = true;

    emit documentReceived(text, revision);

    if (firstSnapshot && m_ready) {
        emit readyChanged(true);
    }
}

void SessionClient::handleEditReply(bool accepted, const Protocol::EditReply &reply) {
    if (!m_ready || m_pendingId.isNull() || reply.operationId != m_pendingId) {
        failConnection(tr("Unexpected edit reply."));
        return;
    }

    if (!accepted) {
        failConnection(tr("The host rejected the edit at revision %1.").arg(reply.revision));
        return;
    }
    if (m_revision == std::numeric_limits<quint64>::max() || reply.revision != m_revision + 1) {
        failConnection(tr("Invalid revision in edit acknowledgement."));
        return;
    }

    m_replyTimer->stop();
    m_documentText = m_sentText;
    m_revision = reply.revision;
    m_pendingId = QUuid{};
    m_sentText.clear();

    emit editAcknowledged(m_revision);
    sendNextEdit();
}