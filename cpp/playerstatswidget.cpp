#include "../headers/playerstatswidget.h"

#include <QHeaderView>
#include <QAbstractItemView>
#include <QString>

PlayerStatsWidget::PlayerStatsWidget(QWidget* parent)
    : QDockWidget("Player Statistics", parent)
{
    roundTabs = new QTabWidget(this);

    setWidget(roundTabs);

    // Allow the scoreboard to behave like the other dock widgets.
    setFeatures(
        QDockWidget::DockWidgetMovable |
        QDockWidget::DockWidgetFloatable
    );

    setAllowedAreas(
        Qt::LeftDockWidgetArea |
        Qt::RightDockWidgetArea
    );

    setMinimumWidth(500);
}

QTableWidget* PlayerStatsWidget::createStatsTable(
    const std::vector<PlayerStats>& stats)
{
    QTableWidget* table =
        new QTableWidget(
            static_cast<int>(stats.size()),
            7
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

    table->horizontalHeader()
        ->setSectionResizeMode(
            QHeaderView::Stretch
        );

    table->setEditTriggers(
        QAbstractItemView::NoEditTriggers
    );

    table->setSelectionBehavior(
        QAbstractItemView::SelectRows
    );

    return table;
}

void PlayerStatsWidget::addRoundStats(
    int roundNumber,
    const std::vector<PlayerStats>& stats)
{
    QTableWidget* table =
        createStatsTable(stats);

    roundTabs->addTab(
        table,
        QString("Round %1").arg(roundNumber)
    );

    // Automatically show the newest completed round.
    roundTabs->setCurrentWidget(table);
}

void PlayerStatsWidget::addFinalStats(
    const std::vector<PlayerStats>& stats)
{
    QTableWidget* table =
        createStatsTable(stats);

    roundTabs->addTab(
        table,
        "Final Match"
    );

    roundTabs->setCurrentWidget(table);
}

void PlayerStatsWidget::resetStats()
{
    roundTabs->clear();
}