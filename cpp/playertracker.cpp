#include "../headers/playertracker.h"

#include <algorithm>
#include <cctype>
#include <iostream>


PlayerTracker::PlayerTracker(
    QObject* parent)
    : QObject(parent)
{
}


// ---------------------------------------------------------
// CREATE LOOKUP KEY
// ---------------------------------------------------------

std::string PlayerTracker::makeKey(
    const std::string& team,
    const std::string& player)
{
    std::string key =
        team + "|" + player;

    // Lowercase the key so:
    //
    // T1|iZu
    //
    // and
    //
    // t1|IZU
    //
    // refer to the same player.
    std::transform(
        key.begin(),
        key.end(),
        key.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(
                std::tolower(c)
            );
        }
    );

    return key;
}


// ---------------------------------------------------------
// FIND OR CREATE PLAYER
// ---------------------------------------------------------

PlayerStats& PlayerTracker::ensurePlayer(
    std::unordered_map<
        std::string,
        PlayerStats
    >& statsMap,
    const std::string& team,
    const std::string& player)
{
    std::string key =
        makeKey(team, player);

    auto it =
        statsMap.find(key);

    if (it == statsMap.end())
    {
        PlayerStats stats;

        stats.team = team;
        stats.name = player;

        statsMap[key] = stats;
    }

    return statsMap[key];
}


// ---------------------------------------------------------
// RESET EVERYTHING
// ---------------------------------------------------------

void PlayerTracker::reset()
{
    matchStats.clear();
    roundStats.clear();

    agentVotes.clear();
    lastAgentVoteFrame.clear();

    lastRoundStats.clear();

    roundNumber = 1;

    lastLeftScore = -1;
    lastRightScore = -1;

    emit statsUpdated();
}


// ---------------------------------------------------------
// LOAD MATCH ROSTER
// ---------------------------------------------------------

void PlayerTracker::setRoster(
    const std::vector<
        std::pair<std::string, std::string>
    >& players)
{
    matchStats.clear();
    roundStats.clear();
    lastRoundStats.clear();
    agentVotes.clear();
    lastAgentVoteFrame.clear();


    roundNumber = 1;




    for (const auto& entry : players)
    {
        // Existing project format:
        //
        // first  = player name
        // second = team
        //
        const std::string& playerName =
            entry.first;

        const std::string& teamName =
            entry.second;


        // Add zeroed player to match stats.
        ensurePlayer(
            matchStats,
            teamName,
            playerName
        );


        // Add zeroed player to round stats.
        ensurePlayer(
            roundStats,
            teamName,
            playerName
        );
    }


    emit statsUpdated();
}


// ---------------------------------------------------------
// RECORD AGENT OBSERVATION
// ---------------------------------------------------------


void PlayerTracker::recordAgentObservation(
    QString team,
    QString player,
    QString agent,
    qint64 frameId)
{
    std::string teamName = team.trimmed().toStdString();
    std::string playerName = player.trimmed().toStdString();
    std::string agentName =
        agent.trimmed().toLower().toStdString();

    // Ignore invalid observations.
    if (teamName.empty() ||
        playerName.empty() ||
        agentName.empty())
    {
        return;
    }

    std::string key = makeKey(teamName, playerName);

    auto playerIt = matchStats.find(key);

    // Only accept players from the current match roster.
    if (playerIt == matchStats.end())
    {
        return;
    }

    // Do not count more than one observation for
    // the same player in the same processed frame.
    auto previousFrame = lastAgentVoteFrame.find(key);

    if (previousFrame != lastAgentVoteFrame.end() &&
        previousFrame->second == frameId)
    {
        return;
    }

    lastAgentVoteFrame[key] = frameId;

    // Keep accumulating votes throughout the match,
    // even after an agent has been confirmed.
    auto& playerVotes = agentVotes[key];

    int votes = ++playerVotes[agentName];

    std::cout
        << "AGENT VOTE | "
        << teamName << " " << playerName
        << " | " << agentName
        << " | Total votes: " << votes
        << std::endl;

    // Find the agent with the highest vote count.
    std::string leadingAgent;
    int leadingVotes = 0;
    bool tiedForLead = false;

    for (const auto& entry : playerVotes)
    {
        const std::string& candidate = entry.first;
        int candidateVotes = entry.second;

        if (candidateVotes > leadingVotes)
        {
            leadingAgent = candidate;
            leadingVotes = candidateVotes;
            tiedForLead = false;
        }
        else if (candidateVotes == leadingVotes)
        {
            tiedForLead = true;
        }
    }

    // At least two votes are needed before
    // confirming an agent.
    if (leadingVotes < 2)
    {
        return;
    }

    // If agents are tied, keep the previous
    // assignment instead of constantly switching.
    if (tiedForLead)
    {
        return;
    }

    // Remember the agent previously displayed.
    std::string previousAgent = playerIt->second.agent;

    // The leading agent has not changed.
    // No scoreboard update is needed.
    if (previousAgent == leadingAgent)
    {
        return;
    }

    // Assign the current leading agent.
    // This can be an initial confirmation
    // or a correction of an earlier prediction.
    playerIt->second.agent = leadingAgent;

    // Update the current round's statistics.
    auto roundIt = roundStats.find(key);

    if (roundIt != roundStats.end())
    {
        roundIt->second.agent = leadingAgent;
    }

    // Update the most recently completed round.
    for (auto& stats : lastRoundStats)
    {
        if (makeKey(stats.team, stats.name) == key)
        {
            stats.agent = leadingAgent;
        }
    }

    if (previousAgent.empty())
    {
        std::cout
            << "AGENT CONFIRMED | "
            << teamName << " " << playerName
            << " -> " << leadingAgent
            << " | Votes: " << leadingVotes
            << std::endl;
    }
    else
    {
        std::cout
            << "AGENT CORRECTED | "
            << teamName << " " << playerName
            << " | Old: " << previousAgent
            << " | New: " << leadingAgent
            << " | Votes: " << leadingVotes
            << std::endl;
    }

    // Notify other components that this player's
    // confirmed agent has changed.
    emit agentConfirmed(
        team,
        player,
        QString::fromStdString(leadingAgent)
    );

    // Refresh the Live Match scoreboard.
    emit statsUpdated();
}



// ---------------------------------------------------------
// RECORD KILL
// ---------------------------------------------------------

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
        // Save the completed round before resetting it.
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