#pragma once

#include <QObject>
#include <QString>
#include <QtGlobal>
#include <QByteArray>

class QTcpSocket;

class SessionClient : public QObject {
    Q_OBJECT

    public:
        explicit SessionClient(QObject *parent = nullptr);
        void connectToHost(const QString &address, quint16 port = 45454);
        bool isActive() const;
        quint64 revision() const { return m_revision; }
    
    signals:
        void connected();
        void disconnected();
        void connectionError(const QString &message);
        void documentReceived(const QString &text, quint64 revision);
        void activeChanged(bool active);

    private:
        void recieveData();
        void failConnection(const QString &message);
        quint64 m_revision = 0;
        QTcpSocket *m_socket;
        QByteArray m_receiveBuffer;
};