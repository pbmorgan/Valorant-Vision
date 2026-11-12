#include "../headers/playertracker.h"

#include <algorithm>
#include <cctype>
#include <iostream>


// GET STORED ASSISTS
// ---------------------------------------------------------

std::vector<PendingAssist>
PlayerTracker::getPendingAssists() const
{
    return pendingAssists;
}

// ---------------------------------------------------------
// ACCEPT AN ASSIST EVENT FROM THE LIVE OCR PIPELINE
// ---------------------------------------------------------

void PlayerTracker::recordDetectedAssist(
    QString eventId,
    QString killerTeam,
    QString killerName,
    QString victimName,
    QString assistingAgent)
{
    eventId = eventId.trimmed();
    killerTeam = killerTeam.trimmed();
    killerName = killerName.trimmed();
    victimName = victimName.trimmed();
    assistingAgent = assistingAgent.trimmed().toLower();

    if (eventId.isEmpty() || killerTeam.isEmpty() ||
        killerName.isEmpty() || victimName.isEmpty() ||
        assistingAgent.isEmpty() ||
        getPlayerStats(killerTeam.toStdString(),
                       killerName.toStdString()) == nullptr)
    {
        std::cout << "ASSIST EVENT REJECTED | ID: "
                  << eventId.toStdString()
                  << " | Missing fields or unknown killer"
                  << std::endl;
        return;
    }

    PendingAssist assist;
    assist.eventId = eventId.toStdString();
    assist.roundNumber = getRoundNumber();
    assist.assistingTeam = killerTeam.toStdString();
    assist.killerName = killerName.toStdString();
    assist.victimName = victimName.toStdString();
    assist.detectedAgent = assistingAgent.toStdString();

    std::cout << "ASSIST EVENT RECEIVED | " << assist.eventId
              << " | Round: " << assist.roundNumber
              << " | Team: " << assist.assistingTeam
              << " | Killer: " << assist.killerName
              << " | Victim: " << assist.victimName
              << " | Agent: " << assist.detectedAgent
              << std::endl;

    if (!recordPendingAssist(assist))
    {
        std::cout << "ASSIST EVENT DUPLICATE/INVALID | "
                  << assist.eventId << std::endl;
    }
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




