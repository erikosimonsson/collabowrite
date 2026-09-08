#include "mainwindow.h"

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

MainWindow::MainWindow(QWidget *parent):QMainWindow(parent), m_editor(new QPlainTextEdit(this)) {
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

    updateWindowTitle();
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