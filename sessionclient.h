#pragma once

#include "protocol.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUuid>

class QTcpSocket;
class QTimer;

class SessionClient : public QObject {
    Q_OBJECT

    public:
        explicit SessionClient(QObject *parent = nullptr);
        void connectToHost(const QString &address, quint16 port = 45454);
        bool isActive() const;
        bool isReady() const { return m_ready; }
        quint64 revision() const { return m_revision; }
        void submitDraft(const QString &text);
    
    signals:
        void connected();
        void disconnected();
        void connectionError(const QString &message);
        void documentReceived(const QString &text, quint64 revision);
        void activeChanged(bool active);
        void readyChanged(bool ready);
        void editAcknowledged(quint64 revision);

    private:
        void recieveData();
        void failConnection(const QString &message);
        void sendNextEdit();
        void handleSnapshot(const QString &text, quint64 revision);
        void handleEditReply(bool accepted, const Protocol::EditReply &reply);
        bool hasPendingChanges() const { return !m_pendingId.isNull() ||m_draftText != m_documentText; }

    quint64 m_revision = 0;
    bool m_ready = false;
    QString m_documentText;
    QString m_draftText;
    QString m_sentText;
    QUuid m_pendingId;
    QTcpSocket *m_socket;
    QTimer *m_replyTimer;
    QByteArray m_receiveBuffer;
};