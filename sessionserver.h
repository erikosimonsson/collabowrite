#pragma once

#include "protocol.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QtGlobal>
#include <QByteArray>
#include <QHash>

class QTcpServer;
class QTcpSocket;
class QTimer;

class SessionServer : public QObject {
    Q_OBJECT

    public:
        explicit SessionServer(QObject *parent = nullptr);
        bool start(quint16 port = 45454);
        void stop();
        bool isListening() const;
        QString errorString() const;
        void setDocumentText(const QString &text);
        quint64 revision() const;
        bool applyEdit(const Protocol::EditRequest &request, QString &error);
    
    signals:
        void clientConnected(const QString &address);
        void clientDisconnected(const QString &address);
        void remoteDocumentChanged(const QString &text, quint64 revision);

    private:
        void acceptPendingConnections();
        void sendSnapshot(QTcpSocket *client);
        void broadcastSnapshot();
        void receiveFromClient(QTcpSocket *client);
        bool sendMessage(QTcpSocket *client, Protocol::MessageType type, const QByteArray &payload);
    
    QHash<QTcpSocket *, QByteArray> m_receiveBuffers;
    QString m_documentText;
    quint64 m_revision = 0;
    QTcpServer *m_server;
    QList<QTcpSocket *> m_clients;
    QTimer *m_broadcastTimer;
};