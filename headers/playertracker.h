#ifndef PLAYERTRACKER_H
#define PLAYERTRACKER_H

#include <QObject>
#include <QString>

#include <string>
#include <utility>
#include <vector>
#include <unordered_map>


// Statistics for one player.
struct PlayerStats
{
    std::string team;
    std::string name;

    int kills = 0;
    int deaths = 0;

    int damageDone = 0;
    int damageTaken = 0;

    double kd() const
    {
        if (deaths == 0)
        {
            return static_cast<double>(kills);
        }

        return static_cast<double>(kills) /
               static_cast<double>(deaths);
    }
};


class PlayerTracker : public QObject
{
    Q_OBJECT

public:

    explicit PlayerTracker(
        QObject* parent = nullptr
    );

    // Completely reset statistics for a new video/match.
    void reset();

    /*
        Load the players participating in the match.

        IMPORTANT:
        The vector uses:

            first  = player name
            second = team name

        Example:

            { "iZu", "T1" }
            { "Asuna", "100T" }
    */
    void setRoster(
        const std::vector<
            std::pair<std::string, std::string>
        >& players
    );


    // Full-match statistics.
    std::vector<PlayerStats>
        getMatchStats() const;


    // Statistics for the round currently being played.
    std::vector<PlayerStats>
        getCurrentRoundStats() const;


    // Statistics from the round that most recently ended.
    std::vector<PlayerStats>
        getLastRoundStats() const;


    // Get one player's full-match statistics.
    const PlayerStats* getPlayerStats(
        const std::string& team,
        const std::string& player
    ) const;


    // Round currently being played.
    int getRoundNumber() const;


public slots:

    // Called whenever OCR detects a kill.
    void recordKill(
        QString killerTeam,
        QString killerName,
        QString victimTeam,
        QString victimName
    );


    // Called whenever damage detection is added later.
    void recordDamage(
        QString attackerTeam,
        QString attackerName,
        QString victimTeam,
        QString victimName,
        int damage
    );


    /*
        Called whenever OCR detects that the match
        score has changed.

        Example:

            100T 3 - 2 T1
                   ↓
            100T 4 - 2 T1

        The total score increased by one, so one
        round has ended.
    */
    void onScoreChanged(
        QString leftTeam,
        int leftScore,
        QString rightTeam,
        int rightScore
    );


signals:

    // Fired after one round has finished.
    void roundFinished(
        int roundNumber
    );

    // Fired whenever player statistics change.
    void statsUpdated();


private:

    // Entire match.
    std::unordered_map<
        std::string,
        PlayerStats
    > matchStats;


    // Current round only.
    std::unordered_map<
        std::string,
        PlayerStats
    > roundStats;


    // Saved copy of the round that just ended.
    std::vector<PlayerStats>
        lastRoundStats;


    // Current round number.
    int roundNumber = 1;


    // Last scoreboard values detected.
    int lastLeftScore = -1;
    int lastRightScore = -1;


    // Build a case-insensitive lookup key.
    static std::string makeKey(
        const std::string& team,
        const std::string& player
    );


    // Find/create a player inside a statistics map.
    PlayerStats& ensurePlayer(
        std::unordered_map<
            std::string,
            PlayerStats
        >& statsMap,
        const std::string& team,
        const std::string& player
    );


    // Reset round statistics while preserving the roster.
    void resetRoundStats();


    // Convert a map to a sorted vector for displaying.
    static std::vector<PlayerStats>
        mapToVector(
            const std::unordered_map<
                std::string,
                PlayerStats
            >& statsMap
        );
};

#endif // PLAYERTRACKER_H