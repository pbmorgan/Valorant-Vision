#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QPixmap>
#include <QLabel>
#include <QPushButton>
#include "../headers/logbar.h"
#include "../headers/videoplayer.h"
#include "../headers/minimap.h"
#include "../headers/ocr.h"
#include "playertracker.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

private:
    QWidget* centralWidget;
    QVBoxLayout* mainVerticalLayout;
    QHBoxLayout* controlPanel;
    VideoPlayer* player;
    QPushButton* chooseFileButton;
    QPushButton* resetDatabaseButton;   
    QPushButton* pauseButton;
    QPushButton* logButton;
    LogBar* logBar;
    Minimap* minimap_wid;
    QSlider* volumeSlider;
    QPixmap* pixmap;
    QLabel* picLabel;
    OCRDetector* ocrDetector;
    PlayerTracker* playerTracker = nullptr;

    void showPlayerStats(const QString& title,const std::vector<PlayerStats>& stats);

    void openAndPlayVideoOnClick();
    void pauseOrPlayVideo();
    void resetBar();

private slots:
    void onTeamsDetected(QString left, QString right);
    void onTeamsProposed(QString left, QString right);
    void onScoresChanged(QString leftTeam, int leftScore, QString rightTeam, int rightScore);
    void resetPlayerDatabase();

public:
    MainWindow();
};
#endif
