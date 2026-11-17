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
    // Reset the fallback for each new video before async map detection.
    teamDetectionFallbackTimer->stop();
    teamFallbackRemainingMs = 30000;
    teamsConfirmedForCurrentVideo = false;
    teamDialogOpen = false;
    teamFallbackShown = false;

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
    playerStatsWidget->show();
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
    
    // Remove statistics from the previous video/match.
    playerStatsWidget->resetStats();

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

