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
    // adds the logo next to the title
    setWindowIcon(QIcon(":/images/logo.png"));

    centralWidget = new QWidget(this);
    mainVerticalLayout = new QVBoxLayout(centralWidget);

    logBar = new LogBar(this);

    addDockWidget(Qt::RightDockWidgetArea, logBar);
    minimap_wid = new Minimap(this);

    addDockWidget(Qt::RightDockWidgetArea, minimap_wid);
    minimap_wid->loadImage("map_layouts/Ascent_layout.png");

    player = new VideoPlayer(centralWidget);
    // Do not give controlPanel centralWidget as its parent.
    // It will become a child of mainVerticalLayout when addLayout()
    // is called below.
    controlPanel = new QHBoxLayout();
    chooseFileButton = new QPushButton("Select Video", centralWidget);


    // Create the reset button using the member variable
    // declared in mainwindow.h.
    resetDatabaseButton =
        new QPushButton("Reset Player Database", centralWidget);

    QPixmap pixmap(":/images/logo.png");
    picLabel = new QLabel();
    picLabel->setPixmap(pixmap);

    pauseButton = new QPushButton("Pause/Play", centralWidget);

    volumeSlider = new QSlider(Qt::Horizontal, this);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(100);

    logButton = new QPushButton("Reset Log Bar", centralWidget);

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
    mainVerticalLayout->addWidget(player);

    // Starting-screen buttons
    mainVerticalLayout->addWidget(chooseFileButton);
    mainVerticalLayout->addWidget(resetDatabaseButton);

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

    // Send already-decoded Qt video frames directly to OCR.
    connect(player, &VideoPlayer::ocrFrameReady,
        ocrDetector, &OCRDetector::processVideoFrame);
    
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
    QString QSource = QFileDialog::getOpenFileName(this, tr("Open Video"), "C:", tr("*.mp4 *.mp3"));
    QFileInfo fileInfo(QSource);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        logBar->addLog("Invalid file selected.");
        return;
    }
    std::string source = QSource.toStdString();
    /*
    // Detect the map and load the correct layout image
    QString layoutPath = detectMapFromVideo(source);
    if (!layoutPath.isEmpty()) {
        minimap_wid->loadImage(layoutPath);
        logBar->addLog("Map detected: " + layoutPath);
    }
    else {
        logBar->addLog("Map not detected.");
    }*/

    // Hide the starting-screen buttons after a video is selected.
    chooseFileButton->hide();
    resetDatabaseButton->hide();
    picLabel->hide();
    volumeSlider->show();
    minimap_wid->show();
    pauseButton->show();
    logBar->show();
    logButton->show();
    player->show();

    // Temporarily disable Qt video playback/rendering.
    // This lets us test whether OCR alone is light enough for the laptop.
    //player->displayVideo(source);
    // TEMP TEST:  Keep OCR processing enabled.
    // Display the selected video in the Qt video player.
    player->displayVideo(source);

    // Start OCR using its own OpenCV video reader.
// Qt handles visible video playback, while OpenCV supplies frames to OCR.
    ocrDetector->startVideo(source);

    logBar->addLog("Video Loaded.");
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