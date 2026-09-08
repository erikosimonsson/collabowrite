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
    
    signals:
        void connected();
        void disconnected();
        void connectionError(const QString &message);
        void documentReceived(const QString &text);
        void activeChanged(bool active);

    private:
        void recieveData();
        void failConnection(const QString &message);
        QTcpSocket *m_socket;
        QByteArray m_receiveBuffer;
};