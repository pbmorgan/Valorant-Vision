#include "../headers/playertracker.h"

#include <algorithm>
#include <cctype>
#include <iostream>


void PlayerTracker::recordKill(
    QString killerTeam,
    QString killerName,
    QString victimTeam,
    QString victimName)
{
    const std::string kt =
        killerTeam.toStdString();

    const std::string kn =
        killerName.toStdString();

    const std::string vt =
        victimTeam.toStdString();

    const std::string vn =
        victimName.toStdString();


    // Ignore incomplete kill events.
    if (kn.empty() ||
        vn.empty())
    {
        return;
    }


    // -----------------------------------------------------
    // MATCH TOTAL
    // -----------------------------------------------------

    PlayerStats& matchKiller =
        ensurePlayer(
            matchStats,
            kt,
            kn
        );

    PlayerStats& matchVictim =
        ensurePlayer(
            matchStats,
            vt,
            vn
        );

    matchKiller.kills++;
    matchVictim.deaths++;


    // -----------------------------------------------------
    // CURRENT ROUND
    // -----------------------------------------------------

    PlayerStats& roundKiller =
        ensurePlayer(
            roundStats,
            kt,
            kn
        );

    PlayerStats& roundVictim =
        ensurePlayer(
            roundStats,
            vt,
            vn
        );

    roundKiller.kills++;
    roundVictim.deaths++;


    emit statsUpdated();
}


// ---------------------------------------------------------
// RECORD DAMAGE
// ---------------------------------------------------------

void PlayerTracker::recordDamage(
    QString attackerTeam,
    QString attackerName,
    QString victimTeam,
    QString victimName,
    int damage)
{
    if (damage <= 0)
    {
        return;
    }


    const std::string at =
        attackerTeam.toStdString();

    const std::string an =
        attackerName.toStdString();

    const std::string vt =
        victimTeam.toStdString();

    const std::string vn =
        victimName.toStdString();


    if (an.empty() ||
        vn.empty())
    {
        return;
    }


    // -----------------------------------------------------
    // MATCH TOTAL
    // -----------------------------------------------------

    PlayerStats& matchAttacker =
        ensurePlayer(
            matchStats,
            at,
            an
        );

    PlayerStats& matchVictim =
        ensurePlayer(
            matchStats,
            vt,
            vn
        );

    matchAttacker.damageDone +=
        damage;

    matchVictim.damageTaken +=
        damage;


    // -----------------------------------------------------
    // CURRENT ROUND
    // -----------------------------------------------------

    PlayerStats& roundAttacker =
        ensurePlayer(
            roundStats,
            at,
            an
        );

    PlayerStats& roundVictim =
        ensurePlayer(
            roundStats,
            vt,
            vn
        );

    roundAttacker.damageDone +=
        damage;

    roundVictim.damageTaken +=
        damage;


    emit statsUpdated();
}


// ---------------------------------------------------------
// SCORE CHANGE / ROUND DETECTION
// ---------------------------------------------------------

void PlayerTracker::onScoreChanged(
    QString leftTeam,
    int leftScore,
    QString rightTeam,
    int rightScore)
{
    // Team names are not needed for determining
    // whether the total score increased.
    (void)leftTeam;
    (void)rightTeam;


    // First valid score reading.
    //
    // Example:
    //
    // The video might begin at:
    //
    //     100T 5 - 4 T1
    //
    // That does NOT mean a round just ended.
    if (lastLeftScore < 0 ||
        lastRightScore < 0)
    {
        lastLeftScore =
            leftScore;

        lastRightScore =
            rightScore;

        return;
    }


    int previousTotal =
        lastLeftScore +
        lastRightScore;

    int currentTotal =
        leftScore +
        rightScore;


    // A valid Valorant round increases the combined
    // match score by exactly one.
    bool roundEnded =
        currentTotal ==
        previousTotal + 1;


    if (roundEnded)
    {
        // Preserve the completed round in match history.
        roundHistory[roundNumber] = roundStats;

        // Keep the existing last-round statistics.
        lastRoundStats =
            mapToVector(
                roundStats
            );


        // Remember which round finished.
        int finishedRound =
            roundNumber;


        // Prepare storage for the next round.
        roundNumber++;

        resetRoundStats();


        // Tell MainWindow that it can display
        // the completed round statistics.
        emit roundFinished(
            finishedRound
        );


        emit statsUpdated();
    }


    // Store newest scoreboard state.
    lastLeftScore =
        leftScore;

    lastRightScore =
        rightScore;
}


// ---------------------------------------------------------
// RESET ONLY CURRENT ROUND
// ---------------------------------------------------------

void PlayerTracker::resetRoundStats()
{
    roundStats.clear();


    // Recreate every player currently known from
    // the match roster/statistics.
    for (const auto& entry :
         matchStats)
    {
        const PlayerStats& matchPlayer =
            entry.second;

        PlayerStats freshRoundPlayer;

        freshRoundPlayer.team =
            matchPlayer.team;

        freshRoundPlayer.agent =
            matchPlayer.agent;


        freshRoundPlayer.name =
            matchPlayer.name;

        // K/D and damage start at zero
        // automatically.

        std::string key =
            makeKey(
                freshRoundPlayer.team,
                freshRoundPlayer.name
            );

        roundStats[key] =
            freshRoundPlayer;
    }
}


// ---------------------------------------------------------
// MAP -> VECTOR
// ---------------------------------------------------------

std::vector<PlayerStats>
PlayerTracker::mapToVector(
    const std::unordered_map<
        std::string,
        PlayerStats
    >& statsMap)
{
    std::vector<PlayerStats> result;

    result.reserve(
        statsMap.size()
    );


    for (const auto& entry :
         statsMap)
    {
        result.push_back(
            entry.second
        );
    }


    // Sort first by team, then by player name.
    std::sort(
        result.begin(),
        result.end(),
        [](
            const PlayerStats& a,
            const PlayerStats& b)
        {
            if (a.team != b.team)
            {
                return a.team < b.team;
            }

            return a.name < b.name;
        }
    );


    return result;
}


// ---------------------------------------------------------
// GET MATCH STATS
// ---------------------------------------------------------

std::vector<PlayerStats>
PlayerTracker::getMatchStats() const
{
    return mapToVector(
        matchStats
    );
}


// ---------------------------------------------------------
// GET CURRENT ROUND
// ---------------------------------------------------------

std::vector<PlayerStats>
PlayerTracker::getCurrentRoundStats() const
{
    return mapToVector(
        roundStats
    );
}


// ---------------------------------------------------------
// GET LAST COMPLETED ROUND
// ---------------------------------------------------------

std::vector<PlayerStats>
PlayerTracker::getLastRoundStats() const
{
    return lastRoundStats;
}
// ---------------------------------------------------------
// GET STATISTICS FOR A SPECIFIC ROUND
// ---------------------------------------------------------

std::vector<PlayerStats>
PlayerTracker::getRoundStats(int round) const
{
    // Return live statistics for the current round.
    if (round == roundNumber)
    {
        return mapToVector(roundStats);
    }

    // Look for a previously completed round.
    auto it = roundHistory.find(round);

    if (it == roundHistory.end())
    {
        return {};
    }

    return mapToVector(it->second);
}

// ---------------------------------------------------------
// GET ONE PLAYER
// ---------------------------------------------------------

const PlayerStats*
PlayerTracker::getPlayerStats(
    const std::string& team,
    const std::string& player) const
{
    std::string key =
        makeKey(
            team,
            player
        );


    auto it =
        matchStats.find(key);


    if (it ==
        matchStats.end())
    {
        return nullptr;
    }


    return &it->second;
}


// ---------------------------------------------------------
// GET ROUND NUMBER
// ---------------------------------------------------------

int PlayerTracker::getRoundNumber() const
{

    return roundNumber;
}

// ---------------------------------------------------------
// COUNT CONFIRMED AGENTS ON A TEAM
// ---------------------------------------------------------

int PlayerTracker::countConfirmedAgentsForTeam(
    const std::string& team) const
{
    int confirmedCount = 0;

    for (const auto& entry : matchStats)
    {
        const PlayerStats& player = entry.second;

        // Only examine players from the assisting team.
        if (makeKey(player.team, "") != makeKey(team, ""))
        {
            continue;
        }

        // An empty agent means the player is unconfirmed.
        if (!player.agent.empty())
        {
            confirmedCount++;
        }
    }

    return confirmedCount;
}


// ---------------------------------------------------------
