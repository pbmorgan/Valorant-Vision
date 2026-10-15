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

    addDockWidget(Qt::RightDockWidgetArea, logBar);
    minimap_wid = new Minimap(this);

    addDockWidget(Qt::RightDockWidgetArea, minimap_wid);
    

    player = new VideoPlayer(centralWidget);
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

    // Set up OCR detector (tessdata folder sits next to the exe)
    ocrDetector = new OCRDetector("tessdata", "eng", this);

    // Load every unique team abbreviation from the local player database.
// getTeamNames() will load players.txt automatically if needed.
    std::vector<std::string> databaseTeamNames =
        WebScraper::getTeamNames();

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
    connect(ocrDetector, &OCRDetector::killDetected,
            logBar, &LogBar::addLog);
    connect(ocrDetector, &OCRDetector::scoresChanged,
            this, &MainWindow::onScoresChanged);

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
            // MapDetector performs its work on the background thread.
            QString detectedLayout =
                detectMapFromVideo(source);

            // Return to the GUI thread before touching Qt widgets
            // or starting the visible video player.
            QMetaObject::invokeMethod(
                this,
                [this, source, detectedLayout]()
                {
                    if (!detectedLayout.isEmpty())
                    {
                        minimap_wid->loadImage(detectedLayout);

                        logBar->addLog(
                            "Map detected: " + detectedLayout
                        );
                    }
                    else
                    {
                        logBar->addLog(
                            "Map could not be detected."
                        );
                    }

                    // Map detection is now completely finished.
                    // Start the normal working playback pipeline.
                    player->displayVideo(source);

                    // Start the normal OCR video reader after playback begins.
                    ocrDetector->startVideo(source);

                    logBar->addLog("Video Loaded.");

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