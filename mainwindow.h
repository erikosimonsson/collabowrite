#pragma once

#include <QMainWindow>
#include <QString>

class QPlainTextEdit;
class SessionServer;
class SessionClient;

class MainWindow : public QMainWindow {
    public:
        explicit MainWindow(QWidget *parent = nullptr);
    
    private:
        void newPage();
        void openFile();
        void saveFile();
        bool writeFile(const QString &filePath);
        void updateWindowTitle();
        void hostSession();
        void joinSession();

        QPlainTextEdit *m_editor;
        QString m_currentFilePath;
        SessionServer *m_sessionServer;
        SessionClient *m_sessionClient;
        QString m_previousText;
        bool m_applyingRemoteText = false;
};
