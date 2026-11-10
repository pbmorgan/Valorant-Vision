#ifndef PLAYERTRACKER_H
#define PLAYERTRACKER_H

#include <QObject>
#include <QString>
#include <QtGlobal>

#include <string>
#include <utility>
#include <vector>
#include <unordered_map>


// Statistics for one player.
struct PlayerStats
{
    std::string team;
    std::string name;
    std::string agent;  // Empty until confirmed


    int kills = 0;
    int deaths = 0;
    int assists = 0;

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

// One possible assisting agent detected in a kill-feed row.
struct AssistCandidate
{
    std::string agent;
    double score = 0.0;
    int rank = 0;
};

// An assist waiting for enough confirmed player-agent assignments.
struct PendingAssist
{
    std::string eventId;

    int roundNumber = 1;

    std::string assistingTeam;
    std::string killerName;
    std::string victimName;

    // Top five predictions from the assist detector.
    // These are possible matches, not confirmed assists.
    std::vector<AssistCandidate> candidates;

    // The actual assisting agent identified by the detector.
    // Empty until an assist icon has been positively identified.
    std::string detectedAgent;

    // Whether this assist has been assigned to a player.
    bool resolved = false;

    // The player currently credited with this assist.
    // Used to prevent duplicate credits and handle corrections.
    std::string assignedPlayer;
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

    // Statistics for a specific round, including completed rounds.
    std::vector<PlayerStats>
        getRoundStats(int round) const;

    

    // Get one player's full-match statistics.
    const PlayerStats* getPlayerStats(
        const std::string& team,
        const std::string& player
    ) const;


    // Round currently being played.
    int getRoundNumber() const;

    // Return stored assist records.
    // Some records may already be marked as resolved.
    std::vector<PendingAssist> getPendingAssists() const;
    // Save a new assist event for later identification.
    // Returns false if the event is invalid or already stored.
    bool recordPendingAssist(const PendingAssist& assist);


public slots:

    // Called whenever OCR detects a kill.
    void recordKill(
        QString killerTeam,
        QString killerName,
        QString victimTeam,
        QString victimName
    );

    void recordAgentObservation(
        QString team,
        QString player,
        QString agent,
        qint64 frameId
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

    void agentConfirmed(
        QString team,
        QString player,
        QString agent
    );



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

    // Statistics for every completed round.
    // Key: round number
    // Value: player statistics from that round.
    std::unordered_map<
        int,
        std::unordered_map<std::string, PlayerStats>
    > roundHistory;

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

    // Assists waiting for player-agent identification.
    std::vector<PendingAssist> pendingAssists;

    // Count players with confirmed agents on a specific team.
    int countConfirmedAgentsForTeam(
        const std::string& team
    ) const;

    // Reevaluate stored assists using confirmed player agents.
    void reevaluatePendingAssists();


    // Reset round statistics while preserving the roster.
    void resetRoundStats();

    // Count detected agents for each player.
    std::unordered_map<
        std::string,
        std::unordered_map<std::string, int>
    > agentVotes;

    // Prevent multiple votes for one player in one frame.
    std::unordered_map<std::string, qint64> lastAgentVoteFrame;

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