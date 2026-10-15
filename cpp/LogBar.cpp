#include "../headers/logbar.h"
#include <QMainWindow>

// Takes in a pointer to the parent object
LogBar::LogBar(QWidget *parentAddress) : QDockWidget("Logs", parentAddress) {

	logBar_Out = new QPlainTextEdit();
	logBar_Out->setReadOnly(true);

	setWidget(logBar_Out);
	// Comment the bottom line out, if we want to allow the bar to be hidden too (Close it).
	setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

	setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
}

// Able to add message to the side bar
void LogBar::addLog(const QString& text) {
	logBar_Out->appendPlainText(text);
}

// to reset the bar to its original place
void LogBar::resetBar() {
	setFloating(false);
	show();
}