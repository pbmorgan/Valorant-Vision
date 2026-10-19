#include <string>
#include <thread>
#include <vector>
#include <utility>

#include <QWidget>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QPixmap>
#include <QLabel>
#include <QDialog>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QStatusBar>
#include <QMessageBox>
#include <QSlider>
#include <QIcon>
#include <QMetaObject>

#include "../headers/logbar.h"
#include "../headers/mainwindow.h"
#include "../headers/videoplayer.h"
#include "../headers/minimap.h"
#include "../headers/mapdetector.h"
#include "../headers/ocr.h"
#include "../headers/webScraper.h"


MainWindow::MainWindow() :
    QMainWindow()
{
    // ---------------------------------------------------------
    // MAIN WINDOW SETUP
    // ---------------------------------------------------------

    setWindowTitle("ValorVision");

    // Adds the ValorVision logo next to the window title
    setWindowIcon(QIcon(":/images/logo.png"));

    // Default window size
    resize(1100, 750);
    setMinimumSize(900, 650);

    centralWidget = new QWidget(this);
    mainVerticalLayout = new QVBoxLayout(centralWidget);

    // Main application background
    centralWidget->setStyleSheet(
        "background-color: #121212;"
        );

    this->setStyleSheet(
        "QMainWindow {"
        "   background-color: #121212;"
        "}"
        );

    mainVerticalLayout->setContentsMargins(25, 25, 25, 20);
    mainVerticalLayout->setSpacing(12);


    // ---------------------------------------------------------
    // LOG BAR
    // ---------------------------------------------------------

    logBar = new LogBar(this);

    addDockWidget(
        Qt::RightDockWidgetArea,
        logBar
        );


    // ---------------------------------------------------------
    // MINIMAP
    // ---------------------------------------------------------

    minimap_wid = new Minimap(this);

    addDockWidget(
        Qt::RightDockWidgetArea,
        minimap_wid
        );

    // Default minimap before map detection occurs
    minimap_wid->loadImage(
        "map_layouts/Ascent_layout.png"
        );


    // ---------------------------------------------------------
    // VIDEO PLAYER
    // ---------------------------------------------------------

    player = new VideoPlayer(centralWidget);


    // ---------------------------------------------------------
    // WELCOME / STARTUP SCREEN
    // ---------------------------------------------------------

    QWidget *welcomeWidget = new QWidget(centralWidget);
    welcomeWidget->setObjectName("welcomeWidget");

    QVBoxLayout *welcomeLayout =
        new QVBoxLayout(welcomeWidget);

    welcomeLayout->setContentsMargins(
        40,
        35,
        40,
        35
        );

    welcomeLayout->setSpacing(12);


    // Flexible empty space above the content
    welcomeLayout->addStretch();


    // ---------------------------------------------------------
    // LOGO
    // ---------------------------------------------------------

    QPixmap pixmap(":/images/logo.png");

    picLabel = new QLabel(welcomeWidget);

    picLabel->setAlignment(
        Qt::AlignCenter
        );

    // Resize the logo while keeping its original proportions
    picLabel->setPixmap(
        pixmap.scaled(
            290,
            290,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
            )
        );

    welcomeLayout->addWidget(
        picLabel,
        0,
        Qt::AlignCenter
        );


    // ---------------------------------------------------------
    // APPLICATION TITLE
    // ---------------------------------------------------------

    QLabel *titleLabel =
        new QLabel(
            "ValorVision",
            welcomeWidget
            );

    titleLabel->setAlignment(
        Qt::AlignCenter
        );

    titleLabel->setStyleSheet(
        "QLabel {"
        "   color: #FFFFFF;"
        "   font-size: 32px;"
        "   font-weight: bold;"
        "   background: transparent;"
        "}"
        );

    welcomeLayout->addWidget(titleLabel);


    // ---------------------------------------------------------
    // APPLICATION SUBTITLE
    // ---------------------------------------------------------

    QLabel *subtitleLabel =
        new QLabel(
            "Analyze Valorant match footage with OCR, minimap tracking, "
        "and video playback.",
            welcomeWidget
            );

    subtitleLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    subtitleLabel->setWordWrap(true);

    subtitleLabel->setMaximumWidth(800);
    subtitleLabel->setMinimumHeight(70);

    subtitleLabel->setSizePolicy(
        QSizePolicy::Preferred,
        QSizePolicy::MinimumExpanding
        );

    subtitleLabel->setStyleSheet(
        "QLabel {"
        "   color: #A9B0B7;"
        "   font-size: 15px;"
        "   background: transparent;"
        "   padding: 4px;"
        "}"
        );

    welcomeLayout->addWidget(
        subtitleLabel,
        0,
        Qt::AlignCenter
        );


    // Add a small amount of space before the button
    welcomeLayout->addSpacing(15);


    // ---------------------------------------------------------
    // SELECT VIDEO BUTTON
    // ---------------------------------------------------------

    chooseFileButton =
        new QPushButton(
            "Select Video",
            welcomeWidget
            );

    chooseFileButton->setMinimumHeight(48);
    chooseFileButton->setMinimumWidth(220);
    chooseFileButton->setMaximumWidth(260);

    chooseFileButton->setCursor(
        Qt::PointingHandCursor
        );

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

    welcomeLayout->addWidget(
        chooseFileButton,
        0,
        Qt::AlignCenter
        );


    // Flexible empty space underneath the startup content
    welcomeLayout->addStretch();


    // ---------------------------------------------------------
    // VIDEO CONTROL PANEL
    // ---------------------------------------------------------

    controlPanel = new QHBoxLayout();


    pauseButton =
        new QPushButton(
            "Pause / Play",
            centralWidget
            );

    pauseButton->setMinimumHeight(38);


    volumeSlider =
        new QSlider(
            Qt::Horizontal,
            centralWidget
            );

    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(100);


    logButton =
        new QPushButton(
            "Reset Log Bar",
            centralWidget
            );

    logButton->setMinimumHeight(38);


    controlPanel->addWidget(pauseButton);
    controlPanel->addWidget(volumeSlider);


    // ---------------------------------------------------------
    // ADD EVERYTHING TO MAIN WINDOW LAYOUT
    // ---------------------------------------------------------

    mainVerticalLayout->addWidget(
        welcomeWidget,
        1
        );

    mainVerticalLayout->addWidget(
        player,
        1
        );

    mainVerticalLayout->addLayout(
        controlPanel
        );

    mainVerticalLayout->addWidget(
        logButton
        );


    this->setCentralWidget(
        centralWidget
        );


    // ---------------------------------------------------------
    // BUTTON CONNECTIONS
    // ---------------------------------------------------------

    connect(
        chooseFileButton,
        &QPushButton::clicked,
        this,
        &MainWindow::openAndPlayVideoOnClick
        );

    connect(
        pauseButton,
        &QPushButton::clicked,
        this,
        &MainWindow::pauseOrPlayVideo
        );

    connect(
        logButton,
        &QPushButton::clicked,
        this,
        &MainWindow::resetBar
        );


    // ---------------------------------------------------------
    // MINIMAP CONNECTION
    // ---------------------------------------------------------

    // Connect frame data (minimap icon predictions)
    // to the minimap widget
    connect(
        player,
        &VideoPlayer::frameChanged,
        minimap_wid,
        &Minimap::redrawAgents
        );


    // ---------------------------------------------------------
    // VOLUME CONTROL
    // ---------------------------------------------------------

    connect(
        volumeSlider,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            player->setVolume(
                value / 100.0f
                );
        }
        );


    // ---------------------------------------------------------
    // HIDE ANALYSIS CONTROLS AT STARTUP
    // ---------------------------------------------------------

    minimap_wid->hide();
    volumeSlider->hide();
    pauseButton->hide();
    logBar->hide();
    logButton->hide();
    player->hide();


    // ---------------------------------------------------------
    // OCR SETUP
    // ---------------------------------------------------------

    // tessdata folder sits next to the executable
    ocrDetector =
        new OCRDetector(
            "tessdata",
            "eng",
            this
            );


    connect(
        ocrDetector,
        &OCRDetector::ocrLog,
        logBar,
        &LogBar::addLog
        );

    connect(
        ocrDetector,
        &OCRDetector::teamsProposed,
        this,
        &MainWindow::onTeamsProposed
        );

    connect(
        ocrDetector,
        &OCRDetector::teamsDetected,
        this,
        &MainWindow::onTeamsDetected
        );

    connect(
        ocrDetector,
        &OCRDetector::killDetected,
        logBar,
        &LogBar::addLog
        );

    connect(
        ocrDetector,
        &OCRDetector::scoresChanged,
        this,
        &MainWindow::onScoresChanged
        );


    // ---------------------------------------------------------
    // SYNC OCR WITH VIDEO PLAYER
    // ---------------------------------------------------------

    connect(
        player,
        &VideoPlayer::videoPlaying,
        ocrDetector,
        &OCRDetector::onVideoPlaying
        );

    connect(
        player,
        &VideoPlayer::videoPaused,
        ocrDetector,
        &OCRDetector::onVideoPaused
        );

    connect(
        player,
        &VideoPlayer::videoPositionChanged,
        ocrDetector,
        &OCRDetector::onVideoPositionChanged
        );


    // ---------------------------------------------------------
    // STATUS BAR
    // ---------------------------------------------------------

    statusBar()->setStyleSheet(
        "QStatusBar {"
        "   background-color: #171717;"
        "   color: #BFC5CA;"
        "   border-top: 1px solid #292929;"
        "   padding-left: 6px;"
        "}"
        );

    statusBar()->showMessage(
        "Ready - Select a video to begin analysis"
        );


    // Display main window
    this->show();
}


// =============================================================
// TEAM DETECTION / WEB SCRAPER
// =============================================================

void MainWindow::onTeamsDetected(
    QString left,
    QString right
    )
{
    logBar->addLog(
        QString(
            "Scraping VLR.gg for %1 / %2..."
            ).arg(left, right)
        );


    std::thread(
        [
            this,
            l = left.toStdString(),
            r = right.toStdString()
    ]()
        {
            bool ok =
                WebScraper::prepareForMatch(
                    l,
                    r
                    );


            // Fallback to cached file if scraping fails
            if (
                !ok &&
                WebScraper::fileExists()
                )
            {
                WebScraper::loadPlayersFromFile();
            }


            std::vector<
                std::pair<
                    std::string,
                    std::string
                    >
                > records;


            for (
                const auto &p :
                WebScraper::getPlayers()
                )
            {
                records.emplace_back(
                    p.name,
                    p.team
                    );
            }


            QString source =
                ok
                    ? "VLR.gg"
                    : "cached file";


            QMetaObject::invokeMethod(
                this,

                [
                    this,
                    records,
                    source
            ]()
                {
                    ocrDetector->setPlayerRecords(
                        records
                        );


                    logBar->addLog(
                        QString(
                            "Roster loaded from %1: %2 players"
                            )
                            .arg(source)
                            .arg(records.size())
                        );
                },

                Qt::QueuedConnection
                );
        }
        ).detach();
}


// =============================================================
// TEAM CONFIRMATION DIALOG
// =============================================================

void MainWindow::onTeamsProposed(
    QString left,
    QString right
    )
{
    // Pause playback so the user has time
    // to read/edit detected team names
    player->pause();


    QDialog dlg(this);

    dlg.setWindowTitle(
        "Confirm Teams"
        );


    QVBoxLayout *layout =
        new QVBoxLayout(&dlg);


    layout->addWidget(
        new QLabel(
            "Confirm or edit the detected team names:",
            &dlg
            )
        );


    // ---------------------------------------------------------
    // LEFT TEAM
    // ---------------------------------------------------------

    layout->addWidget(
        new QLabel(
            "Left:",
            &dlg
            )
        );


    QLineEdit *leftEdit =
        new QLineEdit(
            left,
            &dlg
            );


    layout->addWidget(
        leftEdit
        );


    // ---------------------------------------------------------
    // RIGHT TEAM
    // ---------------------------------------------------------

    layout->addWidget(
        new QLabel(
            "Right:",
            &dlg
            )
        );


    QLineEdit *rightEdit =
        new QLineEdit(
            right,
            &dlg
            );


    layout->addWidget(
        rightEdit
        );


    // ---------------------------------------------------------
    // OK / CANCEL BUTTONS
    // ---------------------------------------------------------

    QDialogButtonBox *box =
        new QDialogButtonBox(
            QDialogButtonBox::Ok |
                QDialogButtonBox::Cancel,
            &dlg
            );


    connect(
        box,
        &QDialogButtonBox::accepted,
        &dlg,
        &QDialog::accept
        );


    connect(
        box,
        &QDialogButtonBox::rejected,
        &dlg,
        &QDialog::reject
        );


    layout->addWidget(
        box
        );


    // ---------------------------------------------------------
    // HANDLE USER RESPONSE
    // ---------------------------------------------------------

    if (
        dlg.exec() ==
        QDialog::Accepted
        )
    {
        QString l =
            leftEdit->text().trimmed();

        QString r =
            rightEdit->text().trimmed();


        if (
            l.isEmpty() ||
            r.isEmpty()
            )
        {
            ocrDetector->rejectTeams();

            player->play();

            return;
        }


        ocrDetector->confirmTeams(
            l,
            r
            );


        player->play();
    }
    else
    {
        ocrDetector->rejectTeams();

        player->play();
    }
}


// =============================================================
// SCORE UPDATES
// =============================================================

void MainWindow::onScoresChanged(
    QString leftTeam,
    int leftScore,
    QString rightTeam,
    int rightScore
    )
{
    logBar->addLog(
        QString(
            "[Score] %1 %2 - %3 %4"
            )
            .arg(leftTeam)
            .arg(leftScore)
            .arg(rightScore)
            .arg(rightTeam)
        );
}


// =============================================================
// SELECT AND PLAY VIDEO
// =============================================================

void MainWindow::openAndPlayVideoOnClick()
{
    statusBar()->showMessage(
        "Waiting for video selection..."
        );


    QString QSource =
        QFileDialog::getOpenFileName(
            this,
            tr("Open Valorant Video"),
            "C:",
            tr("Video Files (*.mp4)")
            );


    // ---------------------------------------------------------
    // USER PRESSED CANCEL
    // ---------------------------------------------------------

    if (QSource.isEmpty())
    {
        statusBar()->showMessage(
            "Ready - No video selected"
            );

        return;
    }


    QFileInfo fileInfo(
        QSource
        );


    // ---------------------------------------------------------
    // INVALID VIDEO
    // ---------------------------------------------------------

    if (
        !fileInfo.exists() ||
        !fileInfo.isFile()
        )
    {
        QMessageBox::warning(
            this,
            "Invalid Video",
            "The selected video could not be opened. "
            "Please select a valid MP4 file."
            );


        statusBar()->showMessage(
            "Error - Invalid video selected"
            );


        logBar->addLog(
            "Invalid file selected."
            );


        return;
    }


    std::string source =
        QSource.toStdString();


    // ---------------------------------------------------------
    // MAP DETECTION
    // ---------------------------------------------------------

    statusBar()->showMessage(
        "Detecting map..."
        );


    QString layoutPath =
        detectMapFromVideo(
            source
            );


    if (!layoutPath.isEmpty())
    {
        minimap_wid->loadImage(
            layoutPath
            );


        logBar->addLog(
            "Map detected: " +
            layoutPath
            );
    }
    else
    {
        logBar->addLog(
            "Map not detected."
            );
    }


    // ---------------------------------------------------------
    // SWITCH FROM HOME SCREEN TO ANALYSIS SCREEN
    // ---------------------------------------------------------

    QWidget *welcomeWidget =
        centralWidget->findChild<QWidget *>(
            "welcomeWidget"
            );


    if (welcomeWidget)
    {
        welcomeWidget->hide();
    }


    // Show analysis controls
    volumeSlider->show();
    minimap_wid->show();
    pauseButton->show();
    logBar->show();
    logButton->show();
    player->show();


    // Reduce margins slightly once the video is running
    mainVerticalLayout->setContentsMargins(
        12,
        12,
        12,
        12
        );


    // ---------------------------------------------------------
    // START VIDEO + OCR
    // ---------------------------------------------------------

    player->displayVideo(
        source
        );


    ocrDetector->startVideo(
        source
        );


    logBar->addLog(
        "Video Loaded."
        );


    statusBar()->showMessage(
        "Video loaded - Analysis running"
        );
}


// =============================================================
// PAUSE / PLAY
// =============================================================

void MainWindow::pauseOrPlayVideo()
{
    player->pauseOrPlayVideo();
}


// =============================================================
// RESET LOG BAR
// =============================================================

void MainWindow::resetBar()
{
    logBar->resetBar();
}
