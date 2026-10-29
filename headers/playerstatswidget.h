#ifndef PLAYERSTATSWIDGET_H
#define PLAYERSTATSWIDGET_H

#include <QDockWidget>
#include <QTabWidget>
#include <QTableWidget>

#include <vector>

#include "playertracker.h"

class PlayerStatsWidget : public QDockWidget
{
public:
    explicit PlayerStatsWidget(QWidget* parent = nullptr);

    // Add a completed round to the scoreboard.
    void addRoundStats(
        int roundNumber,
        const std::vector<PlayerStats>& stats
    );

    // Add the complete match totals.
    void addFinalStats(
        const std::vector<PlayerStats>& stats
    );

    // Remove all rounds when a new video/match begins.
    void resetStats();

private:
    QTabWidget* roundTabs;

    QTableWidget* createStatsTable(
        const std::vector<PlayerStats>& stats
    );
};

#endif