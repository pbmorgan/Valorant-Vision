#ifndef LOGBAR_H
#define LOGBAR_H

#include <QDockWidget>
#include <QPlainTextEdit>

// This class is used by the side bar to show logs and reset bar
class LogBar : public QDockWidget {

private:
	QPlainTextEdit *logBar_Out;

public:
	// sets the parent to nullptr if no main parent
	explicit LogBar(QWidget* parent = nullptr);

	// allows to add text in the log bar
	void addLog(const QString& text);

	// allows to reset the log bar
    void resetBar();
};

#endif