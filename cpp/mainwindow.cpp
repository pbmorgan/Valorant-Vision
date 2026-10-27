#include <string>
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
#include "../headers/mapdetector.h"
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

    std::cerr << "5 - Creating OCR" << std::endl;

    // Set up OCR detector (tessdata folder sits next to the exe)
    ocrDetector = new OCRDetector("tessdata", "eng", this);

    std::cerr << "6 - OCR created" << std::endl;

    std::cerr << "7 - Creating PlayerTracker" << std::endl;

    // Tracks player kills, deaths, and round/match statistics.
    playerTracker = new PlayerTracker(this);

    // Show statistics whenever PlayerTracker determines a round has ended.
    connect(
        playerTracker,
        &PlayerTracker::roundFinished,
        this,
        [this](int roundNumber)
        {
            player->pause();

            std::vector<PlayerStats> stats =
                playerTracker->getLastRoundStats();

            showPlayerStats(
                QString("Round %1 Stats")
                .arg(roundNumber),
                stats
            );

            player->play();
        }
    );
    // Show cumulative statistics when the video reaches the end.
    connect(
        player,
        &VideoPlayer::videoFinished,
        this,
        [this]()
        {
            std::vector<PlayerStats> stats =
                playerTracker->getMatchStats();

            showPlayerStats(
                "Final Match Stats",
                stats
            );
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

    // Send structured kill information to the player statistics tracker.
    connect(ocrDetector, &OCRDetector::killEventDetected,
        playerTracker, &PlayerTracker::recordKill);

    // TEMP TEST: print player statistics whenever PlayerTracker changes.
    connect(playerTracker, &PlayerTracker::statsUpdated, this, [this]() {
        std::cout << "\n=== PLAYER TRACKER UPDATE ===\n";

        for (const auto& stats : playerTracker->getMatchStats())
        {
            std::cout
                << stats.team << " "
                << stats.name
                << " | K: " << stats.kills
                << " D: " << stats.deaths
                << " KD: " << stats.kd()
                << std::endl;
        }

        std::cout << "=============================\n";
        });

    // Keep the existing MainWindow score handling.
    connect(ocrDetector, &OCRDetector::scoresChanged,
        this, &MainWindow::onScoresChanged);

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
    std::cerr << "11 - About to show MainWindow" << std::endl;
    this->show();

}

void MainWindow::onTeamsDetected(QString left, QString right)
{
    // Tell the user that the detected teams are being looked up
    // in the local players.txt database.
    logBar->addLog(
        QString("Looking up local roster for %1 / %2...")
        .arg(left, right)
    );

    // Run the roster lookup on a background thread so the GUI
    // remains responsive while the match roster is prepared.
    std::thread([this, l = left.toStdString(), r = right.toStdString()]() {

        // Look up both detected teams in the local in-memory database.
        bool ok = WebScraper::prepareForMatch(l, r);

        // Copy the players selected for this match into the format
        // expected by OCRDetector.
        std::vector<std::pair<std::string, std::string>> records;

        for (const auto& p : WebScraper::getPlayers())
        {
            records.emplace_back(p.name, p.team);
        }

        // Safely send the roster back to the Qt GUI thread.
        QMetaObject::invokeMethod(this, [this, records, ok]() {

            // Give OCR only the players from the detected teams.
            ocrDetector->setPlayerRecords(records);

            // Give PlayerTracker the same roster so it can initialize
            // each player's match and round statistics.
            playerTracker->setRoster(records);
            // Report whether both teams were successfully found.
            if (ok)
            {
                logBar->addLog(
                    QString("Local roster loaded: %1 players")
                    .arg(records.size())
                );
            }
            else
            {
                logBar->addLog(
                    QString("Could not find both teams in the local player database. "
                        "%1 players loaded.")
                    .arg(records.size())
                );
            }

            }, Qt::QueuedConnection);

        }).detach();
}

void MainWindow::onTeamsProposed(QString left, QString right)
{
    // Pause playback so the user has time to read/edit the detected names.
    player->pause();

    QDialog dlg(this);
    dlg.setWindowTitle("Confirm Teams");

    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel("Confirm or edit the detected team names:", &dlg));

    layout->addWidget(new QLabel("Left:", &dlg));
    QLineEdit* leftEdit = new QLineEdit(left, &dlg);
    layout->addWidget(leftEdit);

    layout->addWidget(new QLabel("Right:", &dlg));
    QLineEdit* rightEdit = new QLineEdit(right, &dlg);
    layout->addWidget(rightEdit);

    QDialogButtonBox* box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(box);

    if (dlg.exec() == QDialog::Accepted) {
        QString l = leftEdit->text().trimmed();
        QString r = rightEdit->text().trimmed();
        if (l.isEmpty() || r.isEmpty()) {
            ocrDetector->rejectTeams();
            player->play();
            return;
        }
        ocrDetector->confirmTeams(l, r);
        player->play();
    } else {
        ocrDetector->rejectTeams();
        player->play();
    }
}

void MainWindow::onScoresChanged(QString leftTeam, int leftScore, QString rightTeam, int rightScore)
{
    logBar->addLog(QString("[Score] %1 %2 - %3 %4")
                   .arg(leftTeam).arg(leftScore)
                   .arg(rightScore).arg(rightTeam));
}

void MainWindow::openAndPlayVideoOnClick() {
    // Let the user know the application is waiting for a video file.
    statusBar()->showMessage(
        "Waiting for video selection..."
    );
    // Open the file picker and only show supported MP4 video files.
    QString QSource = QFileDialog::getOpenFileName(
        this,
        tr("Open Valorant Video"),
        "C:",
        tr("Video Files (*.mp4)")
    );

    // If the user closes the file picker without choosing a video,
    // return the status bar to a ready state.
    if (QSource.isEmpty())
    {
        statusBar()->showMessage(
            "Ready - No video selected"
        );

        return;
    }

    QFileInfo fileInfo(QSource);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        // Tell the user directly if the selected video cannot be opened.
        QMessageBox::warning(
            this,
            "Invalid Video",
            "The selected video could not be opened. "
            "Please select a valid MP4 file."
        );

        // Update the status bar so the error is also visible there.
        statusBar()->showMessage(
            "Error - Invalid video selected"
        );

        logBar->addLog("Invalid file selected.");
        return;
    }
    std::string source = QSource.toStdString();
    

    // Hide the starting-screen buttons after a video is selected.
    chooseFileButton->hide();
    resetDatabaseButton->hide();
    picLabel->hide();
    // Hide the startup title once a video has been selected.
    if (QLabel* startupTitle =
        centralWidget->findChild<QLabel*>("startupTitleLabel"))
    {
        startupTitle->hide();
    }

    // Hide the startup subtitle once a video has been selected.
    if (QLabel* startupSubtitle =
        centralWidget->findChild<QLabel*>("startupSubtitleLabel"))
    {
        startupSubtitle->hide();
    }

    volumeSlider->show();
    minimap_wid->show();
    pauseButton->show();
    logBar->show();
    logButton->show();
    player->show();

    // Reduce the outer margins once the analysis screen is visible
    // so the video, minimap, and logs have more usable space.
    mainVerticalLayout->setContentsMargins(
        12,
        12,
        12,
        12
    );

    // Temporarily disable Qt video playback/rendering.
    // This lets us test whether OCR alone is light enough for the laptop.
    //player->displayVideo(source);
    // TEMP TEST:  Keep OCR processing enabled.
    // Reset OCR state before playback begins.
    // This ensures the first decoded frame belongs to a clean OCR session.
    // Start fresh player statistics for this video/match.
    playerTracker->reset();

    // Start map detection on a background thread.
    //
    // detectMapFromVideo() currently opens the video with OpenCV and checks
    // several timestamps. Running it here prevents that work from blocking
    // the Qt GUI thread or delaying video playback.
        
    // Detect the map before normal video playback begins.
    // This prevents MapDetector and QMediaPlayer from decoding
    // the same video at the same time.
    logBar->addLog("Detecting map...");

    std::thread([this, source]()
        {
            // Open the selected video with OpenCV
            cv::VideoCapture cap(source);

            QString detectedLayout = "";

            if (!cap.isOpened())
            {
                qWarning("Minimap detection: could not open video");
            }
            else
            {
                // Get total video length in milliseconds
                double fps =
                    cap.get(cv::CAP_PROP_FPS);

                double frameCount =
                    cap.get(cv::CAP_PROP_FRAME_COUNT);

                double videoLengthMs = 0.0;

                if (fps > 0.0)
                {
                    videoLengthMs =
                        (frameCount / fps) * 1000.0;
                }

                qDebug(
                    "Minimap detection: video length %.1f seconds",
                    videoLengthMs / 1000.0
                );


                // ---------------------------------------------
                // Check one frame every 20 seconds
                // ---------------------------------------------

                const double checkIntervalMs = 20000.0;

                for (double timeMs = 0.0;
                    timeMs < videoLengthMs;
                    timeMs += checkIntervalMs)
                {
                    // Jump to this point in the video
                    cap.set(
                        cv::CAP_PROP_POS_MSEC,
                        timeMs
                    );

                    cv::Mat frame;

                    // Try to read the frame
                    if (!cap.read(frame))
                    {
                        qWarning(
                            "Minimap detection: could not read frame at %.1f seconds",
                            timeMs / 1000.0
                        );

                        continue;
                    }

                    qDebug(
                        "\nChecking minimap at %.1f seconds...",
                        timeMs / 1000.0
                    );


                    // Ask checkMap() whether this frame
                    // contains a recognizable minimap
                    QString result =
                        minimap_wid->checkMap(frame);


                    // If checkMap returned a map,
                    // we are finished searching
                    if (!result.isEmpty())
                    {
                        detectedLayout = result;

                        qDebug(
                            "Minimap found at %.1f seconds",
                            timeMs / 1000.0
                        );

                        break;
                    }
                }

                cap.release();
            }


            // ---------------------------------------------
            // Return to the Qt GUI thread
            // ---------------------------------------------

            QMetaObject::invokeMethod(
                this,
                [this, source, detectedLayout]()
                {
                    if (!detectedLayout.isEmpty())
                    {
                        // Now that we know the map,
                        // display it in the minimap widget
                        minimap_wid->loadImage(
                            detectedLayout
                        );

                        logBar->addLog(
                            "Map detected: " +
                            detectedLayout
                        );
                    }
                    else
                    {
                        logBar->addLog(
                            "Map could not be detected."
                        );
                    }


                    // Start normal video playback
                    player->displayVideo(source);

                    // Start the normal OCR system
                    ocrDetector->startVideo(source);

                    logBar->addLog(
                        "Video Loaded."
                    );

                    statusBar()->showMessage(
                        "Video loaded - Analysis running"
                    );
                },
                Qt::QueuedConnection
            );

        }).detach();

    

}

void MainWindow::pauseOrPlayVideo() {
    player->pauseOrPlayVideo();
}

void MainWindow::resetBar() {
    logBar->resetBar();
}
void MainWindow::resetPlayerDatabase()
{
    // Open the region selection dialog first.
    std::vector<std::string> selectedRegions =
        WebScraper::promptRegionSelection();

    // Empty means the user cancelled or selected nothing.
    if (selectedRegions.empty())
    {
        return;
    }

    // Confirm before modifying the local database.
    QMessageBox::StandardButton answer =
        QMessageBox::question(
            this,
            "Refresh Player Database",
            "The selected regions will be refreshed from VLR.gg.\n"
            "All other regions will remain unchanged.\n\n"
            "Do you want to continue?",
            QMessageBox::Yes | QMessageBox::No
        );

    if (answer != QMessageBox::Yes)
    {
        return;
    }

    // Prevent another refresh from being started.
    resetDatabaseButton->setEnabled(false);
    resetDatabaseButton->setText("Refreshing Database...");

    // Network work happens on a background thread.
    std::thread(
        [this, selectedRegions]()
        {
            bool success =
                WebScraper::refreshRegions(
                    selectedRegions);

            // Return to the Qt GUI thread before touching widgets.
            QMetaObject::invokeMethod(
                this,
                [this, success]()
                {
                    resetDatabaseButton->setEnabled(true);
                    resetDatabaseButton->setText(
                        "Reset Player Database");

                    if (success)
                    {
                        // Reload all team abbreviations from the newly refreshed database.
                        std::vector<std::string> updatedTeamNames =
                            WebScraper::getTeamNames();

                        // Give OCR the updated team list immediately so newly added
                        // teams can be recognized without restarting the application.
                        ocrDetector->setTeamNames(updatedTeamNames);

                        QMessageBox::information(
                            this,
                            "Player Database",
                            "The selected regions were refreshed successfully."
                        );
                    }
                    else
                    {
                        QMessageBox::warning(
                            this,
                            "Player Database",
                            "The regional refresh could not be completed.\n\n"
                            "If this is your first refresh after the database update, "
                            "choose Select All once to migrate the old player database."
                        );
                    }
                },
                Qt::QueuedConnection
            );
        }
    ).detach();
}

void MainWindow::showPlayerStats(
    const QString& title,
    const std::vector<PlayerStats>& stats)
{
    QDialog dialog(this);

    dialog.setWindowTitle(title);
    dialog.resize(750, 450);

    QVBoxLayout* layout =
        new QVBoxLayout(&dialog);

    // Title
    QLabel* titleLabel =
        new QLabel(title, &dialog);

    QFont titleFont =
        titleLabel->font();

    titleFont.setBold(true);
    titleFont.setPointSize(16);

    titleLabel->setFont(titleFont);

    layout->addWidget(titleLabel);

    // Player stat table
    QTableWidget* table =
        new QTableWidget(
            static_cast<int>(stats.size()),
            7,
            &dialog
        );

    table->setHorizontalHeaderLabels(
        {
            "Team",
            "Player",
            "Kills",
            "Deaths",
            "K/D",
            "Damage Done",
            "Damage Taken"
        }
    );

    for (int row = 0;
        row < static_cast<int>(stats.size());
        ++row)
    {
        const PlayerStats& playerStats =
            stats[row];

        table->setItem(
            row,
            0,
            new QTableWidgetItem(
                QString::fromStdString(playerStats.team)
            )
        );

        table->setItem(
            row,
            1,
            new QTableWidgetItem(
                QString::fromStdString(playerStats.name)
            )
        );

        table->setItem(
            row,
            2,
            new QTableWidgetItem(
                QString::number(playerStats.kills)
            )
        );

        table->setItem(
            row,
            3,
            new QTableWidgetItem(
                QString::number(playerStats.deaths)
            )
        );

        table->setItem(
            row,
            4,
            new QTableWidgetItem(
                QString::number(
                    playerStats.kd(),
                    'f',
                    2
                )
            )
        );

        table->setItem(
            row,
            5,
            new QTableWidgetItem(
                QString::number(
                    playerStats.damageDone
                )
            )
        );

        table->setItem(
            row,
            6,
            new QTableWidgetItem(
                QString::number(
                    playerStats.damageTaken
                )
            )
        );
    }

    // Stretch columns across the dialog.
    table->horizontalHeader()
        ->setSectionResizeMode(
            QHeaderView::Stretch
        );

    // Stats are view-only.
    table->setEditTriggers(
        QAbstractItemView::NoEditTriggers
    );

    table->setSelectionBehavior(
        QAbstractItemView::SelectRows
    );

    layout->addWidget(table);

    // Close button
    QDialogButtonBox* buttons =
        new QDialogButtonBox(
            QDialogButtonBox::Close,
            &dialog
        );

    connect(
        buttons,
        &QDialogButtonBox::rejected,
        &dialog,
        &QDialog::reject
    );

    layout->addWidget(buttons);

    dialog.exec();
}
