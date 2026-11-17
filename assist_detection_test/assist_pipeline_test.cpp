#include "playertracker.h"
#include <QCoreApplication>
#include <QMetaObject>
#include <QString>
#include <iostream>

static int failures = 0;

static void expect(bool ok, const char* what)
{
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << what << '\n';
    if (!ok) ++failures;
}

static void confirm(PlayerTracker& tracker, QString team,
                    QString name, QString agent, qint64 frame)
{
    tracker.recordAgentObservation(team, name, agent, frame);
    tracker.recordAgentObservation(team, name, agent, frame + 1);
}

static int matchAssists(const PlayerTracker& tracker,
                        const std::string& team,
                        const std::string& name)
{
    const PlayerStats* p = tracker.getPlayerStats(team, name);
    return p ? p->assists : -1;
}

static int roundAssists(const PlayerTracker& tracker, int round,
                        const std::string& name)
{
    for (const auto& p : tracker.getRoundStats(round))
        if (p.name == name) return p.assists;
    return -1;
}

// Use the same queued Qt slot invocation used by the OCR connection.
static void sendAssist(PlayerTracker& tracker, const QString& id,
                       const QString& team, const QString& killer,
                       const QString& victim, const QString& agent)
{
    bool queued = QMetaObject::invokeMethod(
        &tracker, "recordDetectedAssist", Qt::QueuedConnection,
        Q_ARG(QString, id), Q_ARG(QString, team),
        Q_ARG(QString, killer), Q_ARG(QString, victim),
        Q_ARG(QString, agent)
    );
    expect(queued, "Qt can queue assist signal to tracker");
    QCoreApplication::processEvents();
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    std::cout << "=== AUTOMATIC ASSIST PIPELINE TEST ===\n";

    PlayerTracker tracker;
    tracker.setRoster({
        {"UdoTan", "GE"}, {"PatMen", "GE"},
        {"Leaf", "GE"}, {"Smoggy", "EDG"}
    });
    confirm(tracker, "GE", "PatMen", "sova", 10);
    confirm(tracker, "GE", "Leaf", "fade", 20);

    // One detected assist icon -> one awarded assist.
    sendAssist(tracker, "r1-k1-a0", "GE", "UdoTan", "Smoggy", "sova");
    expect(matchAssists(tracker,"GE","PatMen") == 1,
           "OCR signal credits the correct teammate");
    expect(roundAssists(tracker,1,"PatMen") == 1,
           "Assist credited to Round 1");

    // Same event sent again -> no double count.
    sendAssist(tracker, "r1-k1-a0", "GE", "UdoTan", "Smoggy", "sova");
    expect(matchAssists(tracker,"GE","PatMen") == 1,
           "Repeated OCR event does not double count");

    // Second assisting icon on the same kill.
    sendAssist(tracker, "r1-k1-a1", "GE", "UdoTan", "Smoggy", "fade");
    expect(matchAssists(tracker,"GE","Leaf") == 1,
           "Second icon awards a second player's assist");

    // Next round: same killer/victim is legitimate again.
    tracker.onScoreChanged("GE",0,"EDG",0);
    tracker.onScoreChanged("GE",1,"EDG",0);
    sendAssist(tracker, "r2-k1-a0", "GE", "UdoTan", "Smoggy", "sova");
    expect(matchAssists(tracker,"GE","PatMen") == 2,
           "New-round kill receives a separate assist");
    expect(roundAssists(tracker,2,"PatMen") == 1,
           "New-round assist credited only to Round 2");

    // Agent unknown at detection time; confirm two rounds later.
    tracker.reset();
    tracker.setRoster({{"UdoTan", "GE"}, {"PatMen", "GE"},
                       {"Smoggy", "EDG"}});
    sendAssist(tracker, "r1-k2-a0", "GE", "UdoTan", "Smoggy", "sova");
    expect(matchAssists(tracker,"GE","PatMen") == 0,
           "Unconfirmed player stays pending");
    tracker.onScoreChanged("GE",0,"EDG",0);
    tracker.onScoreChanged("GE",1,"EDG",0);
    tracker.onScoreChanged("GE",1,"EDG",1);
    confirm(tracker,"GE","PatMen","sova",50);
    expect(roundAssists(tracker,1,"PatMen") == 1,
           "Late agent confirmation fixes original round");
    expect(roundAssists(tracker,3,"PatMen") == 0,
           "Late award does not count in current round");

    // Unknown killer team is not a valid match assist.
    sendAssist(tracker, "invalid-team", "UNKNOWN",
               "UdoTan", "Smoggy", "sova");
    expect(matchAssists(tracker,"GE","PatMen") == 1,
           "Unknown-team event rejected");

    std::cout << (failures == 0 ? "ALL PIPELINE TESTS PASSED\n"
                               : "PIPELINE TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
