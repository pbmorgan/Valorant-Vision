
#include "playertracker.h"

#include <QCoreApplication>
#include <iostream>
#include <string>
#include <vector>

int failures = 0;

// Display whether a test passed or failed.
void check(const std::string& description, bool condition)
{
    if (condition)
    {
        std::cout << "[PASS] " << description << "\n";
    }
    else
    {
        std::cout << "[FAIL] " << description << "\n";
        failures++;
    }
}

// Find a player's assist count in a round.
int roundAssists(
    const PlayerTracker& tracker,
    int round,
    const std::string& name)
{
    for (const auto& player : tracker.getRoundStats(round))
    {
        if (player.name == name)
        {
            return player.assists;
        }
    }

    return -1;
}

// Find a player's total assists.
int matchAssists(
    const PlayerTracker& tracker,
    const std::string& team,
    const std::string& name)
{
    const PlayerStats* player =
        tracker.getPlayerStats(team, name);

    return player ? player->assists : -1;
}

// Simulate two observations confirming an agent.
void confirmAgent(
    PlayerTracker& tracker,
    const QString& team,
    const QString& player,
    const QString& agent,
    qint64 firstFrame)
{
    tracker.recordAgentObservation(
        team, player, agent, firstFrame
    );

    tracker.recordAgentObservation(
        team, player, agent, firstFrame + 1
    );
}

// Create a synthetic assist detection.
PendingAssist makeAssist(
    const std::string& eventId,
    int round,
    const std::string& agent)
{
    PendingAssist assist;

    assist.eventId = eventId;
    assist.roundNumber = round;
    assist.assistingTeam = "GE";
    assist.killerName = "UdoTan";
    assist.victimName = "Smoggy";
    assist.detectedAgent = agent;

    return assist;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    std::cout << "\n=== PLAYERTRACKER ASSIST TESTS ===\n";

    // ---------------------------------------------
    // TEST 1: Immediate assist + duplicate prevention
    // ---------------------------------------------
    {
        std::cout << "\nTEST 1: Immediate assist\n";

        PlayerTracker tracker;

        tracker.setRoster({
            {"UdoTan", "GE"},
            {"PatMen", "GE"},
            {"Smoggy", "EDG"}
            });

        confirmAgent(
            tracker, "GE", "PatMen", "sova", 10
        );

        PendingAssist assist =
            makeAssist("kill1-assist0", 1, "sova");

        check(
            "Assist event accepted",
            tracker.recordPendingAssist(assist)
        );

        check(
            "PatMen receives exactly one assist",
            matchAssists(tracker, "GE", "PatMen") == 1
        );

        check(
            "Round 1 contains the assist",
            roundAssists(tracker, 1, "PatMen") == 1
        );

        check(
            "Duplicate event rejected",
            !tracker.recordPendingAssist(assist)
        );

        check(
            "Duplicate did not increase assists",
            matchAssists(tracker, "GE", "PatMen") == 1
        );
    }

    // ---------------------------------------------
    // TEST 2: Retroactive assist from Round 1
    // ---------------------------------------------
    {
        std::cout << "\nTEST 2: Retroactive assist\n";

        PlayerTracker tracker;

        tracker.setRoster({
            {"UdoTan", "GE"},
            {"PatMen", "GE"},
            {"Smoggy", "EDG"}
            });

        PendingAssist assist =
            makeAssist("kill2-assist0", 1, "sova");

        tracker.recordPendingAssist(assist);

        check(
            "Assist remains pending without confirmed agent",
            matchAssists(tracker, "GE", "PatMen") == 0 &&
            !tracker.getPendingAssists().front().resolved
        );

        // Finish Round 1.
        tracker.onScoreChanged("GE", 0, "EDG", 0);
        tracker.onScoreChanged("GE", 1, "EDG", 0);

        // Finish Round 2.
        tracker.onScoreChanged("GE", 1, "EDG", 1);

        check(
            "Now in Round 3",
            tracker.getRoundNumber() == 3
        );

        // Confirm Sova during Round 3.
        confirmAgent(
            tracker, "GE", "PatMen", "sova", 20
        );

        check(
            "Retroactive assist awarded to PatMen",
            matchAssists(tracker, "GE", "PatMen") == 1
        );

        check(
            "Assist credited to original Round 1",
            roundAssists(tracker, 1, "PatMen") == 1
        );

        check(
            "Round 2 has no extra assist",
            roundAssists(tracker, 2, "PatMen") == 0
        );

        check(
            "Round 3 has no extra assist",
            roundAssists(tracker, 3, "PatMen") == 0
        );

        check(
            "Pending assist is now resolved",
            tracker.getPendingAssists().front().resolved
        );
    }

    // ---------------------------------------------
    // TEST 3: Correcting a player-agent assignment
    // ---------------------------------------------
    {
        std::cout << "\nTEST 3: Agent correction\n";

        PlayerTracker tracker;

        tracker.setRoster({
            {"UdoTan", "GE"},
            {"PatMen", "GE"},
            {"Leaf", "GE"},
            {"Smoggy", "EDG"}
            });

        confirmAgent(
            tracker, "GE", "PatMen", "sova", 30
        );

        confirmAgent(
            tracker, "GE", "Leaf", "fade", 40
        );

        tracker.recordPendingAssist(
            makeAssist("kill3-assist0", 1, "sova")
        );

        check(
            "PatMen initially credited",
            matchAssists(tracker, "GE", "PatMen") == 1
        );

        // Correct PatMen's agent from Sova to Fade.
        tracker.recordAgentObservation(
            "GE", "PatMen", "fade", 50
        );
        tracker.recordAgentObservation(
            "GE", "PatMen", "fade", 51
        );
        tracker.recordAgentObservation(
            "GE", "PatMen", "fade", 52
        );

        check(
            "Incorrect assist credit reversed",
            matchAssists(tracker, "GE", "PatMen") == 0
        );

        // Correct Leaf's agent from Fade to Sova.
        tracker.recordAgentObservation(
            "GE", "Leaf", "sova", 60
        );
        tracker.recordAgentObservation(
            "GE", "Leaf", "sova", 61
        );
        tracker.recordAgentObservation(
            "GE", "Leaf", "sova", 62
        );

        check(
            "Assist reassigned to Leaf",
            matchAssists(tracker, "GE", "Leaf") == 1
        );

        check(
            "Total assist credits remain one",
            matchAssists(tracker, "GE", "PatMen") +
            matchAssists(tracker, "GE", "Leaf") == 1
        );
    }

    // ---------------------------------------------
    // TEST 4: Killer cannot assist their own kill
    // ---------------------------------------------
    {
        std::cout << "\nTEST 4: Killer exclusion\n";

        PlayerTracker tracker;

        tracker.setRoster({
            {"UdoTan", "GE"},
            {"PatMen", "GE"},
            {"Smoggy", "EDG"}
            });

        confirmAgent(
            tracker, "GE", "UdoTan", "sova", 70
        );

        tracker.recordPendingAssist(
            makeAssist("kill4-assist0", 1, "sova")
        );

        check(
            "Killer receives no self-assist",
            matchAssists(tracker, "GE", "UdoTan") == 0
        );

        check(
            "Self-assist remains unresolved",
            !tracker.getPendingAssists().front().resolved
        );
    }

    // ---------------------------------------------
    // FINAL RESULTS
    // ---------------------------------------------
    std::cout << "\n============================\n";

    if (failures == 0)
    {
        std::cout << "ALL TESTS PASSED!\n";
    }
    else
    {
        std::cout
            << failures << " TEST(S) FAILED.\n";
    }

    std::cout << "============================\n";

    return failures == 0 ? 0 : 1;
}
