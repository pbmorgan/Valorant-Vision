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
    roundHistory.clear();
    pendingAssists.clear();


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
    roundHistory.clear();




    agentVotes.clear();
    lastAgentVoteFrame.clear();
    pendingAssists.clear();  // NEW

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

    // Update agent assignments in all completed rounds.
    for (auto& roundEntry : roundHistory)
    {
        auto& players = roundEntry.second;

        auto historicalPlayer = players.find(key);

        if (historicalPlayer != players.end())
        {
            historicalPlayer->second.agent = leadingAgent;
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

    // New agent information may help identify older assists.
    reevaluatePendingAssists();


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

