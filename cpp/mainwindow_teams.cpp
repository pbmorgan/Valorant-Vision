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

void MainWindow::onTeamsDetected(QString left, QString right)
{
    teamsConfirmedForCurrentVideo = true;
    if (teamDetectionFallbackTimer)
        teamDetectionFallbackTimer->stop();
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
    // An OCR proposal and the fallback timer can arrive nearly together.
    // Never open two dialogs for the same match.
    if (teamsConfirmedForCurrentVideo || teamDialogOpen)
        return;

    teamDialogOpen = true;

    // Suspend the remaining timeout while the user responds.
    if (teamDetectionFallbackTimer && teamDetectionFallbackTimer->isActive()) {
        teamFallbackRemainingMs =
            std::max(1, teamDetectionFallbackTimer->remainingTime());
        teamDetectionFallbackTimer->stop();
    }

    player->pause();

    const bool manualEntry = left.trimmed().isEmpty() && right.trimmed().isEmpty();

    QDialog dlg(this);
    dlg.setWindowTitle(manualEntry ? "Enter Team Names" : "Confirm Teams");

    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(
        manualEntry
            ? "OCR could not identify both teams within 30 seconds of playback.\n"
              "Enter the team abbreviations (for example, GE and EDG):"
            : "Confirm or edit the detected team names:", &dlg));

    layout->addWidget(new QLabel("Left team:", &dlg));
    QLineEdit* leftEdit = new QLineEdit(left, &dlg);
    layout->addWidget(leftEdit);

    layout->addWidget(new QLabel("Right team:", &dlg));
    QLineEdit* rightEdit = new QLineEdit(right, &dlg);
    layout->addWidget(rightEdit);

    QDialogButtonBox* box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(box);

    bool confirmed = false;
    QString selectedLeft, selectedRight;
    while (dlg.exec() == QDialog::Accepted) {
        selectedLeft = leftEdit->text().trimmed();
        selectedRight = rightEdit->text().trimmed();

        if (selectedLeft.isEmpty() || selectedRight.isEmpty()) {
            QMessageBox::warning(&dlg, "Team Names Required",
                                 "Please enter both team names.");
            continue;
        }
        if (selectedLeft.compare(selectedRight, Qt::CaseInsensitive) == 0) {
            QMessageBox::warning(&dlg, "Different Teams Required",
                                 "The left and right teams must be different.");
            continue;
        }

        confirmed = true;
        break;
    }

    if (confirmed) {
        teamsConfirmedForCurrentVideo = true;
        if (teamDetectionFallbackTimer)
            teamDetectionFallbackTimer->stop();
        // Existing path: updates OCR state and emits teamsDetected to load rosters.
        ocrDetector->confirmTeams(selectedLeft, selectedRight);
    } else {
        // Existing path: let OCR try again after cancellation.
        ocrDetector->rejectTeams();
    }

    teamDialogOpen = false;
    player->play();
}

void MainWindow::onScoresChanged(QString leftTeam, int leftScore, QString rightTeam, int rightScore)
{
    logBar->addLog(QString("[Score] %1 %2 - %3 %4")
                   .arg(leftTeam).arg(leftScore)
                   .arg(rightScore).arg(rightTeam));
}

