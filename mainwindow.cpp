#include "mainwindow.h"
#include "sessionserver.h"
#include "sessionclient.h"
#include "textedit.h"
#include "protocol.h"

#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QStatusBar>
#include <QScrollBar>
#include <QScopedValueRollback>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_editor(new QPlainTextEdit(this)), m_sessionServer(new SessionServer(this)), m_sessionClient(new SessionClient(this)) {
    setCentralWidget(m_editor);
    resize(900, 650);

    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *newAction = fileMenu->addAction(tr("New Page"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newPage);

    QAction *openAction = fileMenu->addAction(tr("Open File"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openFile);

    QAction *saveAction = fileMenu->addAction(tr("&Save"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveFile);

    QMenu *collaborationMenu = menuBar()->addMenu(tr("&Collaboration"));
    QAction *hostAction = collaborationMenu->addAction(tr("&Host Session"));
    connect(hostAction, &QAction::triggered, this, &MainWindow::hostSession);

    statusBar()->showMessage(tr("Ready"));

    connect(m_sessionServer, &SessionServer::clientConnected, this, [this](const QString &address) {
        statusBar()->showMessage(tr("Client connected: %1").arg(address));
    });

    connect(m_sessionServer, &SessionServer::clientDisconnected, this, [this](const QString &address) {
        statusBar()->showMessage(tr("Client disconnected: %1").arg(address));
    });

    QAction *joinAction = collaborationMenu->addAction(tr("&Join Session"));
    connect(joinAction, &QAction::triggered, this, &MainWindow::joinSession);
    connect(m_sessionClient, &SessionClient::connected, this, [this]() {
        statusBar()->showMessage(tr("Connected to host"));
    });
    connect(m_sessionClient, &SessionClient::disconnected, this, [this]() {
        statusBar()->showMessage(tr("Disconnected from host"));
    });
    connect(m_sessionClient,&SessionClient::connectionError, this, [this](const QString &message) {
        statusBar()->showMessage(tr("Connection error: %1").arg(message));
    });

    m_previousText = m_editor->toPlainText();
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this]() {
        const QString currentText = m_editor->toPlainText();
        
        if (m_applyingRemoteText) {
            m_previousText = currentText;
            return;
        }

        if (currentText == m_previousText) {
            return;
        }

        const TextEdit edit = makeTextEdit(m_previousText, currentText);
        m_previousText = currentText;

        if (m_sessionClient->isActive()) {
            return;
        }

        if (!m_sessionServer->isListening()) {
            m_sessionServer->setDocumentText(currentText);
            return;
        }

        const Protocol::EditRequest request {
            QUuid::createUuid(),
            m_sessionServer->revision(),
            edit
        };

        QString error;
        if (!m_sessionServer->applyEdit(request, error)) {
            m_sessionServer->stop();
            m_sessionServer->setDocumentText(currentText);

            statusBar()->showMessage(tr("Sharing stopped; your text is kept here. %1").arg(error));
            return;
        }

        statusBar()->showMessage(tr("Hosting - revision %1").arg(m_sessionServer->revision()));
    });

    m_sessionServer->setDocumentText(m_editor->toPlainText());

    connect(m_sessionClient, &SessionClient::activeChanged, this, [this, fileMenu](bool active) {
        m_editor->setReadOnly(active);

        for (QAction *action : fileMenu->actions()) {
            action->setEnabled(!active);
        }
    });

    connect(m_sessionClient, &SessionClient::documentReceived, this, [this](const QString &text, quint64 revision) {
        QScopedValueRollback<bool> remoteGuard(m_applyingRemoteText, true);
        m_currentFilePath.clear();
        
        if (m_editor->toPlainText() != text) {
            const int vertical = m_editor->verticalScrollBar()->value();
            const int horizontal = m_editor->horizontalScrollBar()->value();

            m_editor->setPlainText(text);
            m_editor->verticalScrollBar()->setValue(vertical);
            m_editor->horizontalScrollBar()->setValue(horizontal);
        }

        setWindowTitle(tr("Shared document - CollaboWrite"));
        statusBar()->showMessage(tr("Connected - revision %1").arg(revision));
    });
}

void MainWindow::newPage() {
    m_editor->clear();
    m_currentFilePath.clear();
    updateWindowTitle();
}

void MainWindow::openFile() {
    const QString filePath = QFileDialog::getOpenFileName(this, tr("Open text file"), QString(), tr("Text files (*.txt)"));

    if (filePath.isEmpty()) {
        return;
    }

    if (!filePath.endsWith(".txt", Qt::CaseInsensitive)) {
        QMessageBox::warning(this, tr("Unsupported file"), tr("Only .txt files can be opened."));
        return;
    }

    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Open failed"), tr("Could not open %1:\n%2").arg(filePath, file.errorString()));
        return;
    }

    m_editor->setPlainText(QString::fromUtf8(file.readAll()));
    m_currentFilePath = filePath;
    updateWindowTitle();
}

void MainWindow::saveFile() {
    QString filePath = m_currentFilePath;

    if (filePath.isEmpty()) {
        filePath = QFileDialog::getSaveFileName(this, tr("Save text file"), QString(), tr("Text files (*.txt)"));

        if (filePath.isEmpty()) {
            return;
        }

        if (!filePath.endsWith(".txt", Qt::CaseInsensitive)) {
            filePath += ".txt";
        }
    }

    if (writeFile(filePath)) {
        m_currentFilePath = filePath;
        updateWindowTitle();
    }
}

bool MainWindow::writeFile(const QString &filePath) {
    QSaveFile file(filePath);

    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Save failed"), tr("Could not save%1:\n%2").arg(filePath, file.errorString()));
        return false;
    }

    const QByteArray text = m_editor->toPlainText().toUtf8();

    if (file.write(text) != text.size() || !file.commit()) {
        QMessageBox::critical(this, tr("Save failed"), tr("Could not save %1:\n%2").arg(filePath, file.errorString()));
        return false;
    }

    return true;
}

void MainWindow::updateWindowTitle() {
    const QString name = m_currentFilePath.isEmpty() ? tr("New Page") : QFileInfo(m_currentFilePath).fileName();
    setWindowTitle(tr("%1 - CollaboWrite").arg(name));
}

void MainWindow::hostSession() {
    if(m_sessionServer->isListening() || m_sessionClient->isActive()) {
        return;
    }

    const QString text = m_editor->toPlainText();

    if (!text.isValidUtf16() || text.toUtf8().size() > Protocol::MaxDocumentBytes) {
        QMessageBox::warning(this, tr("Cannot host document"), tr("Sharing requires valid Unicode text and at most %1 UTF-8 bytes.").arg(Protocol::MaxDocumentBytes));
        return;
    }

    m_sessionServer->setDocumentText(text);
    m_previousText = text;

    if (!m_sessionServer->start(45454)) {
        QMessageBox::critical(this, tr("Could not host session"), m_sessionServer->errorString());
        return;
    }

    statusBar()->showMessage(tr("Hosting on port 45454 - revision 0"));
}

void MainWindow::joinSession() {
    if (m_sessionServer->isListening() || m_sessionClient->isActive()) {
        return;
    }

    statusBar()->showMessage(tr("Connecting to host"));
    m_sessionClient->connectToHost(QStringLiteral("127.0.0.1"), 45454);
}