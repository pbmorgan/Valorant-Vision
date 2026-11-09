
#include "../headers/playerstatswidget.h"

#include <QHeaderView>
#include <QAbstractItemView>
#include <QTableWidgetItem>
#include <QString>

#include <algorithm>

// ---------------------------------------------------------
// HELPER FUNCTIONS
// ---------------------------------------------------------

namespace
{
    // Display a confirmed agent with a capitalized name.
    // Unconfirmed players display a dash.
    QString displayAgent(const std::string& agent)
    {
        if (agent.empty())
        {
            return QString(QChar(0x2014));
        }

        QString name = QString::fromStdString(agent);
        name[0] = name[0].toUpper();

        return name;
    }

    // Populate an existing scoreboard table.
    // This allows the live table to update without
    // replacing the entire QTableWidget.
    void populateStatsTable(
        QTableWidget* table,
        const std::vector<PlayerStats>& stats)
    {
        table->setRowCount(
            static_cast<int>(stats.size())
        );

        for (int row = 0;
            row < static_cast<int>(stats.size());
            ++row)
        {
            const PlayerStats& player = stats[row];

            // Reuse existing cells when possible.
            auto setCell =
                [table, row](int column, const QString& value)
                {
                    QTableWidgetItem* item =
                        table->item(row, column);

                    if (!item)
                    {
                        item = new QTableWidgetItem();
                        table->setItem(row, column, item);
                    }

                    item->setText(value);
                };

            setCell(
                0,
                QString::fromStdString(player.team)
            );

            setCell(
                1,
                QString::fromStdString(player.name)
            );

            // New Agent column
            setCell(
                2,
                displayAgent(player.agent)
            );

            setCell(
                3,
                QString::number(player.kills)
            );

            setCell(
                4,
                QString::number(player.deaths)
            );

            setCell(
                5,
                QString::number(player.kd(), 'f', 2)
            );

            setCell(
                6,
                QString::number(player.damageDone)
            );

            setCell(
                7,
                QString::number(player.damageTaken)
            );
        }
    }
}

// ---------------------------------------------------------
// CONSTRUCTOR
// ---------------------------------------------------------

PlayerStatsWidget::PlayerStatsWidget(QWidget* parent)
    : QDockWidget("Player Statistics", parent)
{
    roundTabs = new QTabWidget(this);

    setWidget(roundTabs);

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

// ---------------------------------------------------------
// CREATE SCOREBOARD TABLE
// ---------------------------------------------------------

QTableWidget* PlayerStatsWidget::createStatsTable(
    const std::vector<PlayerStats>& stats)
{
    // Eight columns now, including Agent.
    QTableWidget* table =
        new QTableWidget(
            static_cast<int>(stats.size()),
            8
        );

    table->setHorizontalHeaderLabels(
        {
            "Team",
            "Player",
            "Agent",
            "Kills",
            "Deaths",
            "K/D",
            "Damage Done",
            "Damage Taken"
        }
    );

    // Fill the table with player statistics.
    populateStatsTable(table, stats);

    // Keep columns readable when the dock is narrow.
    table->horizontalHeader()
        ->setSectionResizeMode(
            QHeaderView::ResizeToContents
        );

    table->horizontalHeader()
        ->setStretchLastSection(true);

    table->setEditTriggers(
        QAbstractItemView::NoEditTriggers
    );

    table->setSelectionBehavior(
        QAbstractItemView::SelectRows
    );

    return table;
}

// ---------------------------------------------------------
// UPDATE LIVE MATCH SCOREBOARD
// ---------------------------------------------------------

void PlayerStatsWidget::updateLiveStats(
    const std::vector<PlayerStats>& stats)
{
    // Create the Live Match tab on the first update.
    if (liveMatchTable == nullptr)
    {
        liveMatchTable = createStatsTable(stats);

        roundTabs->insertTab(
            0,
            liveMatchTable,
            "Live Match"
        );

        roundTabs->setCurrentWidget(liveMatchTable);
    }
    else
    {
        // Refresh the existing live scoreboard.
        populateStatsTable(liveMatchTable, stats);
    }

    // Update agent names in previously completed
    // round tabs if an agent was confirmed later.
    // Historical kills, deaths and damage remain unchanged.
    for (int tabIndex = 0;
        tabIndex < roundTabs->count();
        ++tabIndex)
    {
        QTableWidget* table =
            qobject_cast<QTableWidget*>(
                roundTabs->widget(tabIndex)
            );

        if (!table || table == liveMatchTable)
        {
            continue;
        }

        for (int row = 0; row < table->rowCount(); ++row)
        {
            QTableWidgetItem* teamItem =
                table->item(row, 0);

            QTableWidgetItem* playerItem =
                table->item(row, 1);

            if (!teamItem || !playerItem)
            {
                continue;
            }

            // Find this player in the current match stats.
            auto match = std::find_if(
                stats.begin(),
                stats.end(),
                [&](const PlayerStats& player)
                {
                    return
                        QString::fromStdString(player.team)
                        .compare(
                            teamItem->text(),
                            Qt::CaseInsensitive
                        ) == 0
                        &&
                        QString::fromStdString(player.name)
                        .compare(
                            playerItem->text(),
                            Qt::CaseInsensitive
                        ) == 0;
                }
            );

            if (match == stats.end() ||
                match->agent.empty())
            {
                continue;
            }

            QTableWidgetItem* agentItem =
                table->item(row, 2);

            if (agentItem)
            {
                agentItem->setText(
                    displayAgent(match->agent)
                );
            }
        }
    }
}

// ---------------------------------------------------------
// ADD COMPLETED ROUND
// ---------------------------------------------------------

void PlayerStatsWidget::addRoundStats(
    int roundNumber,
    const std::vector<PlayerStats>& stats)
{
    QTableWidget* table = createStatsTable(stats);

    roundTabs->addTab(
        table,
        QString("Round %1").arg(roundNumber)
    );

    // Keep the currently selected tab visible.
    // Playback can continue on the Live Match tab.
}

// ---------------------------------------------------------
// ADD FINAL MATCH STATISTICS
// ---------------------------------------------------------

void PlayerStatsWidget::addFinalStats(
    const std::vector<PlayerStats>& stats)
{
    QTableWidget* table = createStatsTable(stats);

    roundTabs->addTab(
        table,
        "Final Match"
    );

    roundTabs->setCurrentWidget(table);
}

// ---------------------------------------------------------
// RESET SCOREBOARD
// ---------------------------------------------------------

void PlayerStatsWidget::resetStats()
{
    // Remove and delete every previous scoreboard tab.
    while (roundTabs->count() > 0)
    {
        QWidget* tab = roundTabs->widget(0);
        roundTabs->removeTab(0);
        delete tab;
    }

    liveMatchTable = nullptr;
}
