#pragma once
#include <QApplication>
#include <QPalette>
#include <QIcon>

inline void applyWorkshopTheme(QApplication& app){
    app.setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window,QColor("#152237"));
    palette.setColor(QPalette::WindowText,QColor("#ecf2fc"));
    palette.setColor(QPalette::Base,QColor("#0d1729"));
    palette.setColor(QPalette::AlternateBase,QColor("#182842"));
    palette.setColor(QPalette::Text,QColor("#ecf2fc"));
    palette.setColor(QPalette::Button,QColor("#263958"));
    palette.setColor(QPalette::ButtonText,QColor("#ecf2fc"));
    palette.setColor(QPalette::Highlight,QColor("#315a95"));
    palette.setColor(QPalette::HighlightedText,Qt::white);
    palette.setColor(QPalette::ToolTipBase,QColor("#1a2b49"));
    palette.setColor(QPalette::ToolTipText,QColor("#ecf2fc"));
    for(auto role:{QPalette::WindowText,QPalette::Text,QPalette::ButtonText})
        palette.setColor(QPalette::Disabled,role,QColor("#8391a7"));
    app.setPalette(palette);
    app.setStyleSheet(R"(
        QToolBar { background: #111c2f; padding: 5px; spacing: 5px; border-bottom: 1px solid #304661; }
        QDockWidget::title { background: #1a2a44; font-weight: bold; padding: 7px; }
        QToolButton, QPushButton { background: #263958; border: 1px solid #405879; border-radius: 5px; padding: 5px 8px; }
        QToolButton:hover, QPushButton:hover { border-color: #8cb0e2; }
        QWidget[accent="ai"] { background: #643abe; color: white; font-weight: bold; }
        QWidget[accent="calculate"] { background: #087955; color: white; font-weight: bold; }
        QWidget[accent="export"] { background: #f0a837; color: #182034; font-weight: bold; }
        QWidget[accent="cancel"] { background: #a73343; color: white; }
        QToolButton:disabled, QPushButton:disabled { background: #1b2a41; border-color: #30405a; color: #8292aa; }
        QProgressBar { background: #0c1729; border: 1px solid #405879; border-radius: 5px; text-align: center; min-height: 19px; }
        QProgressBar::chunk { background: #147759; border-radius: 4px; }
        QLabel#camReport { background: #172745; color: #b1eddd; border: 1px solid #475b83; padding: 8px; border-radius: 5px; }
    )");
    app.setWindowIcon(QIcon(":/artisanaxis/artisanaxis.ico"));
}
