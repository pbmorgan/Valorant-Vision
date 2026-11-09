#ifndef OCR_H
#define OCR_H

#include <QObject>
#include <QTimer>
#include <QImage>
#include <QtGlobal>
#include <opencv2/opencv.hpp>
#include "killfeedagentmatcher.h"
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <thread>

struct KillEvent {
    std::string killer;
    std::string victim;
};

struct OCRWordBox {
    std::string text;
    cv::Rect box;
};

class OCRDetector : public QObject {
    Q_OBJECT

public:
    explicit OCRDetector(const std::string& tessdataPath,
                         const std::string& lang,
                         QObject* parent = nullptr);
    ~OCRDetector();

    // Pass full records so kill events can include team names
    void setPlayerRecords(const std::vector<std::pair<std::string,std::string>>& nameTeamPairs);
    void setTeamNames(const std::vector<std::string>& names);

public slots:

    void startVideo(const std::string& videoPath);
    void reset();

    // Receives a frame already decoded by Qt
    void processVideoFrame(const QImage& image);

    // User accepted / rejected the proposed team names from the dialog
    void confirmTeams(QString left, QString right);
    void rejectTeams();
    // Sync slots wired to VideoPlayer signals
    void onVideoPlaying(qint64 positionMs);
    void onVideoPaused();
    void onVideoPositionChanged(qint64 positionMs);

signals:
    void ocrLog(QString text);

    // OCR detected team names - MainWindow shows a confirmation dialog
    void teamsProposed(QString left, QString right);

    // Fired only after the user has confirmed the names (roster scrape trigger)
    void teamsDetected(QString left, QString right);

    // Existing human-readable kill log
    void killDetected(QString logLine);

    // Structured kill information for PlayerTracker
    void killEventDetected(
        QString killerTeam,
        QString killerName,
        QString victimTeam,
        QString victimName
    );

    // Send detected agent information to PlayerTracker.
    // Both killer and victim detections use this signal.
    void agentCandidateDetected(
        QString team,
        QString player,
        QString agent,
        qint64 frameId
    );



    void scoresChanged(
        QString leftTeam,
        int leftScore,
        QString rightTeam,
        int rightScore
    );

    // Sends the current Valorant round timer to the GUI.
    // Example: "1:25", "0:42", "0:05".
    void roundTimerChanged(QString timer);

    // Notifies the GUI when replay footage starts or ends.
    void replayStateChanged(bool inReplay);

    // Sends the remaining Spike countdown to the GUI.
    // -1 means there is currently no active planted Spike.
    void spikeTimerChanged(int secondsRemaining);

  




private slots:
    void processNextFrame();



private:
    tesseract::TessBaseAPI tess;
    bool tessOk = false;

    cv::VideoCapture cap;
    QTimer* frameTimer = nullptr;


    // VALORANT AGENT TEMPLATE MATCHING

    // Handles loading agent portrait templates and comparing
    // them against the icons displayed in the kill feed.
    KillfeedAgentMatcher agentMatcher;

    // Tracks whether our agent templates loaded successfully.
    // If loading fails, we should not attempt template matching.
    bool agentTemplatesReady = false;


    // name -> team lookup
    std::unordered_map<std::string, std::string> nameToTeam;
    bool playerListValid = false;
    std::vector<std::string> playerNames; // for matching

    std::vector<std::string> teamNames;
    bool teamDetected = false;
    bool awaitingTeamConfirmation = false;
    std::string leftTeamName;
    std::string rightTeamName;

    std::unordered_set<std::string> seenKills;

    // Store the previous image for each killfeed row.
//
// Before running Tesseract, we will compare the current killfeed
// row against the previous one. If the image has barely changed,
// OCR can skip that row instead of wasting time reading it again.
    std::vector<cv::Mat> previousKillfeedRows;

    int lastLeftScore  = -1;
    int lastRightScore = -1;

    double startTime = 0.0;
    std::atomic<qint64> currentVideoPos{0};
    double capFps = 0.0;
    std::string lastRoundTimer;

    // Internal round-timer tracking.
    // This stays false until OCR successfully detects a valid in-game timer.
    // That way videos can begin during agent select, loading screens,
    // introductions, or in the middle of an existing round.
    bool timerAnchorValid = false;


    // Video position where the normal round timer was most
    // recently visible on the broadcast HUD.
    //
    // This helps prevent caster/intermission scenes from
    // accidentally starting the Spike countdown.
    double lastRoundTimerVisibleVideoSeconds = -1.0;


    // True while the broadcast is showing replay footage.
    // While this is true, replay kills, scores, and timer readings
    // must not be treated as live match events.
    bool inReplay = false;

    // Counts consecutive frames where the REPLAY label was not detected.
    // This prevents one temporary OCR failure from ending replay mode too early.
    int replayMissCount = 0;

    // True once the broadcast HUD confirms that the Spike has been planted.
    bool spikePlanted = false;

    // Require the red planted icon to appear on multiple OCR samples
    // before accepting it as a real plant event.
    int spikePlantConfirmCount = 0;

    // Qt video playback position when the Spike plant was confirmed.
    // The internal 45-second countdown will use this as its anchor.
    double spikePlantVideoSeconds = 0.0;

    // Timer value, in seconds, at the most recent trustworthy OCR reading.
    // Example: "1:25" is stored as 85 seconds.
    int timerAnchorSeconds = 0;

    // Video position where that trustworthy timer reading was detected.
    // We use VIDEO time rather than real-world computer time.
    double timerAnchorVideoSeconds = 0.0;

    // Video timestamp of the frame currently being processed by OCR.
    double currentOcrVideoSeconds = 0.0;
    std::atomic_bool ocrBusy{false};
    std::atomic_bool shuttingDown{false};

    // Unique identifier for each processed OCR frame.
    // Prevents counting the same frame twice for one player.
    std::atomic<qint64> processedFrameCounter{ 0 };

    // Serializes tess access between the OCR worker thread and destructor cleanup
    std::mutex tessMutex;

    std::string normalize(const std::string& s);
    int levenshtein_distance(const std::string& a, const std::string& b);
    std::string ocr_region(const cv::Mat& roi);

    // OCR specifically for the Valorant killfeed.
    // This will use different image preprocessing because the killfeed
    // has white text on colored red/green backgrounds.
    std::string ocr_killfeed_region(const cv::Mat& roi);

    std::vector<OCRWordBox> ocr_killfeed_word_boxes(const cv::Mat& roi);

    std::string ocr_team_name(const cv::Mat& roi);
    std::string resolve_team_name(const std::string& raw,
                                  const std::vector<std::string>& teams);
    std::vector<std::string> extract_tokens(const std::string& raw);
    KillEvent resolve_killfeed_whole(const std::string& raw,
                                     const std::vector<std::string>& players,
                                     bool player_list_valid);
    void processFrame(const cv::Mat& frame);
};

#endif
