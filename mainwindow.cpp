#include "mainwindow.h"

#include <QPlainTextEdit>

MainWindow::MainWindow(QWidget *parent):QMainWindow(parent), m_editor(new QPlainTextEdit(this)) {
    setCentralWidget(m_editor);
    setWindowTitle("CollaboWrite");
    resize(900, 650);
}
