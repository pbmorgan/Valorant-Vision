#include <string>
#include <algorithm>
#include <QWidget>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QObject>
#include <QFileInfo>
#include <QPixmap>
#include <QMessageBox>
#include <QLabel>
#include <QDialog>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QFont>


#include <QStatusBar>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include "../headers/logbar.h"
#include "../headers/mainwindow.h"
#include "../headers/videoplayer.h"
#include "../headers/minimap.h"
#include "../headers/Mapdetector.h"
#include "../headers/ocr.h"
#include "../headers/webScraper.h"
#include <thread>
#include <iostream>

MainWindow::MainWindow() :
    QMainWindow() {
    setWindowTitle("ValorVision"); // Set the application window title.
    // adds the logo next to the title
    setWindowIcon(QIcon(":/images/logo.png"));

    centralWidget = new QWidget(this);
    mainVerticalLayout = new QVBoxLayout(centralWidget);

    // Give the main application area a dark background.
    centralWidget->setStyleSheet(
        "background-color: #121212;"
    );

    // Match the main window background to the central widget.
    this->setStyleSheet(
        "QMainWindow {"
        "   background-color: #121212;"
        "}"
    );

    // Add space around the GUI so widgets are not touching the edges.
    mainVerticalLayout->setContentsMargins(25, 25, 25, 20);

    // Add consistent spacing between items in the main layout.
    mainVerticalLayout->setSpacing(12);

    logBar = new LogBar(this);

    std::cerr << "2 - LogBar created" << std::endl;


    addDockWidget(Qt::RightDockWidgetArea, logBar);
    minimap_wid = new Minimap(this);
    std::cerr << "3 - Minimap created" << std::endl;

    addDockWidget(Qt::RightDockWidgetArea, minimap_wid);
    
    // Create the dockable player statistics panel.
    playerStatsWidget = new PlayerStatsWidget(this);

    addDockWidget(
        Qt::RightDockWidgetArea,
        playerStatsWidget
    );


    player = new VideoPlayer(centralWidget);

    std::cerr << "4 - VideoPlayer created" << std::endl;
    // Do not give controlPanel centralWidget as its parent.
    // It will become a child of mainVerticalLayout when addLayout()
    // is called below.
    controlPanel = new QHBoxLayout();
    chooseFileButton = new QPushButton("Select Video", centralWidget);
    // Give the Select Video button a larger, cleaner size.
    chooseFileButton->setMinimumHeight(48);
    chooseFileButton->setMinimumWidth(220);
    chooseFileButton->setMaximumWidth(260);

    // Show a hand cursor when the user hovers over the button.
    chooseFileButton->setCursor(Qt::PointingHandCursor);

    // Style the button to match the redesigned GUI.
    chooseFileButton->setStyleSheet(
        "QPushButton {"
        "   background-color: #00D1C7;"
        "   color: #101010;"
        "   font-size: 16px;"
        "   font-weight: bold;"
        "   border: none;"
        "   border-radius: 10px;"
        "   padding: 10px 24px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #15E6DB;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #00AAA2;"
        "}"
    );


    // Create the reset button using the member variable
    // declared in mainwindow.h.
    resetDatabaseButton =
        new QPushButton("Reset Player Database", centralWidget);

    // Keep the database reset option, but style it as a secondary action.
    resetDatabaseButton->setMinimumHeight(44);
    resetDatabaseButton->setMinimumWidth(220);
    resetDatabaseButton->setMaximumWidth(260);

    // Show a hand cursor when hovering over the button.
    resetDatabaseButton->setCursor(Qt::PointingHandCursor);

    // Use an outlined style so Select Video remains the primary button.
    resetDatabaseButton->setStyleSheet(
        "QPushButton {"
        "   background-color: transparent;"
        "   color: #00D1C7;"
        "   font-size: 14px;"
        "   font-weight: bold;"
        "   border: 2px solid #00D1C7;"
        "   border-radius: 10px;"
        "   padding: 8px 20px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #18312F;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #102321;"
        "}"
    );

    QPixmap pixmap(":/images/logo.png");
    picLabel = new QLabel();

    // Center the logo inside its available space.
    picLabel->setAlignment(Qt::AlignCenter);

    // Resize the logo while keeping its original proportions.
    picLabel->setPixmap(
        pixmap.scaled(
            290,
            290,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        )
    );

    // Create the application title shown underneath the logo.
    QLabel* titleLabel = new QLabel("ValorVision", centralWidget);
    // Give the startup title a name so we can hide it when a video loads.
    titleLabel->setObjectName("startupTitleLabel");

    // Center the title text.
    titleLabel->setAlignment(Qt::AlignCenter);

    // Match the title styling from the redesigned GUI.
    titleLabel->setStyleSheet(
        "QLabel {"
        "   color: #FFFFFF;"
        "   font-size: 32px;"
        "   font-weight: bold;"
        "   background: transparent;"
        "}"
    );
    // Create a short description underneath the application title.
    QLabel* subtitleLabel = new QLabel(
        "Analyze Valorant match footage with OCR, minimap tracking, "
        "and video playback.",
        centralWidget
    );

    // Give the startup subtitle a name so we can hide it when a video loads.
    subtitleLabel->setObjectName("startupSubtitleLabel");

    // Center the subtitle and allow it to wrap if the window becomes narrow.
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setMaximumWidth(800);

    // Use smaller gray text so the subtitle does not compete with the title.
    subtitleLabel->setStyleSheet(
        "QLabel {"
        "   color: #A9B0B7;"
        "   font-size: 15px;"
        "   background: transparent;"
        "   padding: 4px;"
        "}"
    );


    pauseButton = new QPushButton("Pause/Play", centralWidget);

    // Give the Pause/Play control a consistent height in the analysis screen.
    pauseButton->setMinimumHeight(38);

    volumeSlider = new QSlider(Qt::Horizontal, this);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(100);

    logButton = new QPushButton("Reset Log Bar", centralWidget);
    // Give the Reset Log Bar button a consistent height
    // with the other analysis controls.
    logButton->setMinimumHeight(38);


    // removed QObject as it was not needed, as they are calling their own functions.
    connect(chooseFileButton, &QPushButton::clicked, this, &MainWindow::openAndPlayVideoOnClick);
    connect(pauseButton, &QPushButton::clicked, this, &MainWindow::pauseOrPlayVideo);
    connect(logButton, &QPushButton::clicked, this, &MainWindow::resetBar);

    // Call the database reset function when the button is clicked.
    connect(resetDatabaseButton, &QPushButton::clicked,
        this, &MainWindow::resetPlayerDatabase);

    //Connect frame data (minimap icon predictions) to the minimap widget
    // // TEMP TEST: disable minimap updates
    connect(player, &VideoPlayer::frameChanged, minimap_wid, &Minimap::redrawAgents);
    

    QObject::connect(volumeSlider, &QSlider::valueChanged, this, [this](int value) {player->setVolume(value / 100.0f); });
    this->setCentralWidget(centralWidget);

    controlPanel->addWidget(pauseButton);
    controlPanel->addWidget(volumeSlider);

    mainVerticalLayout->addWidget(picLabel);

    mainVerticalLayout->addWidget(titleLabel);

    // Place the description underneath the title.
    mainVerticalLayout->addWidget(subtitleLabel, 0, Qt::AlignCenter);

    mainVerticalLayout->addWidget(player);

    // Starting-screen buttons
    // Center the Select Video button on the startup screen.
    mainVerticalLayout->addWidget(
        chooseFileButton,
        0,
        Qt::AlignCenter
    );
    // Center the database reset button underneath Select Video.
    mainVerticalLayout->addWidget(
        resetDatabaseButton,
        0,
        Qt::AlignCenter
    );

    mainVerticalLayout->addLayout(controlPanel);
    mainVerticalLayout->addWidget(logButton);

    minimap_wid->hide();
    volumeSlider->hide();
    pauseButton->hide();
    logBar->hide();
    logButton->hide();
    player->hide();
    playerStatsWidget->hide();

    std::cerr << "5 - Creating OCR" << std::endl;

    // Set up OCR detector (tessdata folder sits next to the exe)
    ocrDetector = new OCRDetector("tessdata", "eng", this);

    std::cerr << "6 - OCR created" << std::endl;

    std::cerr << "7 - Creating PlayerTracker" << std::endl;

    // Tracks player kills, deaths, and round/match statistics.
    playerTracker = new PlayerTracker(this);

    // Add each completed round to the dockable statistics panel.
    // Playback continues while the user views previous rounds.
    connect(
        playerTracker,
        &PlayerTracker::roundFinished,
        this,
        [this](int roundNumber)
        {
            std::vector<PlayerStats> stats =
                playerTracker->getLastRoundStats();

            playerStatsWidget->addRoundStats(
                roundNumber,
                stats
            );
        }
    );
    // Show cumulative statistics when the video reaches the end.
    // Add cumulative match statistics to the dockable statistics panel
    // when the video reaches the end.
    connect(
        player,
        &VideoPlayer::videoFinished,
        this,
        [this]()
        {
            std::vector<PlayerStats> stats =
                playerTracker->getMatchStats();

            playerStatsWidget->addFinalStats(stats);
        }
    );
    

    std::cerr << "8 - PlayerTracker created" << std::endl;

    std::cerr << "9 - Loading team database" << std::endl;

    // Load every unique team abbreviation from the local player database.
    // getTeamNames() will load players.txt automatically if needed.
    std::vector<std::string> databaseTeamNames =
        WebScraper::getTeamNames();

    std::cerr << "10 - Team database loaded" << std::endl;

    // Give OCR the complete database team list instead of relying only
    // on the limited hardcoded team list in OCRDetector.
    ocrDetector->setTeamNames(databaseTeamNames);

    // Temporary debugging:
    // Show how many different team abbreviations OCR can now match against.
    std::cout << "Loaded "
        << databaseTeamNames.size()
        << " team abbreviations into OCR matcher.\n";

    // TEMP PERFORMANCE TEST:
    // Do not send Qt-decoded frames to OCR.
    
    
    connect(
        player,
        &VideoPlayer::ocrFrameReady,
        ocrDetector,
        &OCRDetector::processVideoFrame
    );

    
    
    connect(ocrDetector, &OCRDetector::ocrLog,
            logBar, &LogBar::addLog);
    connect(ocrDetector, &OCRDetector::teamsProposed,
            this, &MainWindow::onTeamsProposed);
    connect(ocrDetector, &OCRDetector::teamsDetected,
            this, &MainWindow::onTeamsDetected);
    // Keep the existing human-readable kill message in the log.
    connect(ocrDetector, &OCRDetector::killDetected,
        logBar, &LogBar::addLog);

    // Disable minimap agent placement while replay footage is active.
    connect(
        ocrDetector,
        &OCRDetector::replayStateChanged,
        minimap_wid,
        &Minimap::setReplayActive
    );

    // Send structured kill information to the player statistics tracker.
    connect(ocrDetector, &OCRDetector::killEventDetected,
        playerTracker, &PlayerTracker::recordKill);

    // Send detected killer and victim agents to PlayerTracker.
    // PlayerTracker counts observations and locks an agent
    // to a player after two matching detections.
    connect(
        ocrDetector,
        &OCRDetector::agentCandidateDetected,
        playerTracker,
        &PlayerTracker::recordAgentObservation
    );


    // Feed live, automatically identified assist icons into the tracker.
    // Qt queues these signals from OCR's worker thread to PlayerTracker.
    connect(
        ocrDetector,
        &OCRDetector::assistEventDetected,
        playerTracker,
        &PlayerTracker::recordDetectedAssist
    );

    // TEMP TEST: print player statistics whenever PlayerTracker changes.
    connect(playerTracker, &PlayerTracker::statsUpdated, this, [this]() {
        std::cout << "\n=== PLAYER TRACKER UPDATE ===\n";

        for (const auto& stats : playerTracker->getMatchStats())
        {
            std::cout
                << stats.team << " "
                << stats.name
                << " | Agent: "
                << (stats.agent.empty() ? "Unconfirmed" : stats.agent)
                << " | K: " << stats.kills
                << " D: " << stats.deaths
                << " A: " << stats.assists
                << " KD: " << stats.kd()
                << std::endl;
        }

        std::cout << "=============================\n";
        });


    // Refresh player statistics whenever kills, deaths,
    // assists, or confirmed agents change.
    connect(
        playerTracker,
        &PlayerTracker::statsUpdated,
        this,
        [this]()
        {
            // Update full-match statistics.
            playerStatsWidget->updateLiveStats(
                playerTracker->getMatchStats()
            );

            // Refresh previously completed rounds.
            // This handles assists credited retroactively
            // after a player's agent becomes confirmed.
            for (int round = 1;
                round < playerTracker->getRoundNumber();
                ++round)
            {
                playerStatsWidget->updateRoundStats(
                    round,
                    playerTracker->getRoundStats(round)
                );
            }
        }
    );



    // Keep the existing MainWindow score handling.
    connect(ocrDetector, &OCRDetector::scoresChanged,
        this, &MainWindow::onScoresChanged);

    // Show the detected match score on the minimap.
    connect(
        ocrDetector,
        &OCRDetector::scoresChanged,
        minimap_wid,
        &Minimap::updateScore
    );

    // Show the detected round timer on the minimap.
    connect(
        ocrDetector,
        &OCRDetector::roundTimerChanged,
        minimap_wid,
        &Minimap::updateRoundTimer
    );
    connect(
        ocrDetector,
        &OCRDetector::spikeTimerChanged,
        minimap_wid,
        &Minimap::updateSpikeTimer
    );

    // Let PlayerTracker know when the match score changes so it
    // can determine when a round has ended.
    connect(ocrDetector, &OCRDetector::scoresChanged,
        playerTracker, &PlayerTracker::onScoreChanged);

    // Sync OCR to video playback state and position
    connect(player, &VideoPlayer::videoPlaying,
            ocrDetector, &OCRDetector::onVideoPlaying);
    connect(player, &VideoPlayer::videoPaused,
            ocrDetector, &OCRDetector::onVideoPaused);
    connect(player, &VideoPlayer::videoPositionChanged,
            ocrDetector, &OCRDetector::onVideoPositionChanged);

    // Style the status bar to match the dark application theme.
    statusBar()->setStyleSheet(
        "QStatusBar {"
        "   background-color: #171717;"
        "   color: #BFC5CA;"
        "   border-top: 1px solid #292929;"
        "   padding-left: 6px;"
        "}"
    );

    // Show the initial application status.
    statusBar()->showMessage(
        "Ready - Select a video to begin analysis"
    );
    // Team-name fallback: wait 30 seconds of actual playback, not file loading.
    teamDetectionFallbackTimer = new QTimer(this);
    teamDetectionFallbackTimer->setSingleShot(true);

    connect(teamDetectionFallbackTimer, &QTimer::timeout, this, [this]() {
        if (teamsConfirmedForCurrentVideo || teamDialogOpen || teamFallbackShown)
            return;

        // Only show the forced manual-entry dialog once per video.
        teamFallbackShown = true;
        logBar->addLog("Team OCR timed out after 30 seconds. Enter the team names manually.");
        onTeamsProposed(QString(), QString());
    });

    connect(player, &VideoPlayer::videoPlaying, this, [this](qint64) {
        if (!teamsConfirmedForCurrentVideo && !teamDialogOpen &&
            !teamFallbackShown && !teamDetectionFallbackTimer->isActive()) {
            teamDetectionFallbackTimer->start(teamFallbackRemainingMs);
        }
    });

    // Pause the timeout whenever the user pauses the video.
    connect(player, &VideoPlayer::videoPaused, this, [this]() {
        if (teamDetectionFallbackTimer->isActive()) {
            teamFallbackRemainingMs =
                std::max(1, teamDetectionFallbackTimer->remainingTime());
            teamDetectionFallbackTimer->stop();
        }
    });

    // Do not prompt after the video has ended.
    connect(player, &VideoPlayer::videoFinished, this, [this]() {
        teamDetectionFallbackTimer->stop();
    });

    std::cerr << "11 - About to show MainWindow" << std::endl;
    this->show();

}

