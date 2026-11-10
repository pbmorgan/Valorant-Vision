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
// GET STORED ASSISTS
// ---------------------------------------------------------

std::vector<PendingAssist>
PlayerTracker::getPendingAssists() const
{
    return pendingAssists;
}

// ---------------------------------------------------------
// RECORD PENDING ASSIST
// ---------------------------------------------------------

bool PlayerTracker::recordPendingAssist(
    const PendingAssist& assist)
{
    // An assist must belong to an identifiable kill event.
    if (assist.eventId.empty() ||
        assist.assistingTeam.empty() ||
        assist.killerName.empty() ||
        assist.victimName.empty() ||
        assist.roundNumber < 1 ||
        (assist.detectedAgent.empty() &&
            assist.candidates.empty()) ||
        assist.candidates.size() > 5)
    {
        return false;
    }

    // Prevent the same assist event from being stored twice.
    for (const auto& existing : pendingAssists)
    {
        if (existing.eventId == assist.eventId)
        {
            return false;
        }
    }

    // Store the assist as unresolved.
    PendingAssist savedAssist = assist;
    savedAssist.resolved = false;
    savedAssist.assignedPlayer.clear();

    pendingAssists.push_back(savedAssist);

    std::cout
        << "PENDING ASSIST SAVED | "
        << "Round: " << savedAssist.roundNumber
        << " | Team: " << savedAssist.assistingTeam
        << " | Killer: " << savedAssist.killerName
        << " | Candidates: " << savedAssist.candidates.size()
        << std::endl;


    reevaluatePendingAssists();

    return true;
}

// ---------------------------------------------------------
// REEVALUATE PENDING ASSISTS
// ---------------------------------------------------------


void PlayerTracker::reevaluatePendingAssists()
{
    bool statsChanged = false;

    for (PendingAssist& assist : pendingAssists)
    {
        // Only a positively identified assisting agent
        // can be used to award an assist.
        // Ranked candidates alone are not enough.

        std::string matchedPlayerKey;
        int matchingPlayers = 0;

        if (!assist.detectedAgent.empty())
        {
            for (const auto& entry : matchStats)
            {
                const PlayerStats& player = entry.second;

                // The assisting player must belong
                // to the killer's team.
                if (makeKey(player.team, "") !=
                    makeKey(assist.assistingTeam, ""))
                {
                    continue;
                }

                // The killer cannot assist their own kill.
                if (makeKey(player.team, player.name) ==
                    makeKey(assist.assistingTeam,
                        assist.killerName))
                {
                    continue;
                }

                // The player's agent must be confirmed
                // and match the detected assisting agent.
                if (player.agent.empty() ||
                    makeKey("", player.agent) !=
                    makeKey("", assist.detectedAgent))
                {
                    continue;
                }

                matchedPlayerKey = entry.first;
                matchingPlayers++;
            }
        }

        // Never guess between multiple matching players.
        // Keep the assist unresolved until there is
        // exactly one eligible player.
        if (matchingPlayers != 1)
        {
            matchedPlayerKey.clear();
        }

        // Find any player who was previously credited
        // with this assist.
        std::string oldPlayerKey;

        if (!assist.assignedPlayer.empty())
        {
            oldPlayerKey = makeKey(
                assist.assistingTeam,
                assist.assignedPlayer
            );
        }

        // Nothing has changed.
        // This also prevents duplicate assist credits.
        if (matchedPlayerKey == oldPlayerKey)
        {
            assist.resolved = !matchedPlayerKey.empty();
            continue;
        }

        // Find the round in which the assist occurred.
        std::unordered_map<std::string, PlayerStats>*
            eventRound = nullptr;

        if (assist.roundNumber == roundNumber)
        {
            eventRound = &roundStats;
        }
        else
        {
            auto roundIt =
                roundHistory.find(assist.roundNumber);

            if (roundIt != roundHistory.end())
            {
                eventRound = &roundIt->second;
            }
        }

        // Never award an assist to the wrong round.
        if (eventRound == nullptr)
        {
            continue;
        }

        // Ensure both the old and new player records
        // exist before changing any statistics.
        auto playerExists =
            [&](const std::string& key) -> bool
            {
                return key.empty() ||
                    (matchStats.find(key) != matchStats.end() &&
                        eventRound->find(key) != eventRound->end());
            };

        if (!playerExists(oldPlayerKey) ||
            !playerExists(matchedPlayerKey))
        {
            continue;
        }

        // Prevent an inconsistent correction from
        // producing negative assist totals.
        if (!oldPlayerKey.empty() &&
            (matchStats.at(oldPlayerKey).assists <= 0 ||
                eventRound->at(oldPlayerKey).assists <= 0))
        {
            continue;
        }

        // Apply an assist credit or remove an old credit
        // from both the match and the original round.
        auto changeAssists =
            [&](const std::string& key, int amount)
            {
                if (key.empty())
                {
                    return;
                }

                matchStats.at(key).assists += amount;
                eventRound->at(key).assists += amount;

                // Keep the most recently completed
                // round snapshot synchronized.
                if (assist.roundNumber == roundNumber - 1)
                {
                    for (PlayerStats& player : lastRoundStats)
                    {
                        if (makeKey(player.team, player.name)
                            == key)
                        {
                            player.assists += amount;
                        }
                    }
                }
            };

        // Remove any previous, now-invalid credit.
        changeAssists(oldPlayerKey, -1);

        // Award the correct player, if identified.
        changeAssists(matchedPlayerKey, +1);

        // Remember who currently owns this assist.
        if (matchedPlayerKey.empty())
        {
            assist.assignedPlayer.clear();
            assist.resolved = false;

            std::cout
                << "ASSIST UNRESOLVED | "
                << assist.eventId
                << " | Agent: " << assist.detectedAgent
                << std::endl;
        }
        else
        {
            const PlayerStats& player =
                matchStats.at(matchedPlayerKey);

            assist.assignedPlayer = player.name;
            assist.resolved = true;

            std::cout
                << "ASSIST AWARDED | "
                << assist.eventId
                << " | Round: " << assist.roundNumber
                << " | Player: " << player.team
                << " " << player.name
                << " | Agent: " << player.agent
                << " | Match assists: " << player.assists
                << std::endl;
        }

        statsChanged = true;
    }

    // Refresh the scoreboard when assist totals change.
    if (statsChanged)
    {
        emit statsUpdated();
    }
}




