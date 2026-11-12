#include "../headers/ocr.h"
#include "../headers/ocr_layout_internal.h"
#include <tesseract/resultiterator.h>
#include <QDateTime>
#include <QCoreApplication>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>

OCRDetector::OCRDetector(const std::string& tessdataPath,
                         const std::string& lang,
                         QObject* parent)
    : QObject(parent)
{
    tessOk = (tess.Init(tessdataPath.c_str(), lang.c_str()) == 0);
    if (tessOk) {
        tess.SetVariable("debug_level", "0");
        tess.SetVariable("debug_file", "NUL");
    }

    teamNames = {
    "ENT", "FNC", "BBL", "EF", "FUT", "GX", "M8", "TL", "VIT", "TLA",

    "NRG", "G2", "100T", "ENVY", "SEN", "FLY", "LEV", "C9", "NBG", "SAD",

    "TS", "INTZ", "FUR", "MIBR", "2GE", "MIBRA", "ELV", "TBK", "TLBR", "PEEK",

    "PRX", "SHR", "FS", "RRQ", "FU", "BOOM", "MOT", "555", "GE", "NKT",

    "NSR", "T1", "DRX", "SLT", "GEN", "ONS", "IAM", "DPLUS", "GGA", "STR", "NS",

    "XLG", "EDG", "BLG", "AG", "TYL", "JDG", "RA", "DRG", "TE", "CCG",

    "CGZ", "DFM", "VL", "RO", "QTD", "FNL", "INS", "SCZ", "IGZ", "DLT",

    "KRU", "SHN", "MK", "BER", "KRUS", "LEVA", "MIX", "KLG", "9Z", "FRE",

    "LYON", "SA", "CON", "TEZ", "DCT", "FUEGO", "CHIV", "LAZ", "AB3", "ATL",

    "BNK", "CD", "EKING"
    };

    frameTimer = new QTimer(this);
    frameTimer->setInterval(FRAME_INTERVAL_MS);
    connect(frameTimer, &QTimer::timeout, this, &OCRDetector::processNextFrame);


    // LOAD VALORANT AGENT TEMPLATES
    // Find the directory containing Valorant-Vision.exe.
    // CMake copies our agent_templates folder here after building.
    std::filesystem::path templatesPath =
        std::filesystem::path(QCoreApplication::applicationDirPath().toStdString()) / "agent_templates";

    // Load the agent templates into memory.
    // 48x32 = normal killfeed agent portraits.
    // 20x20 = smaller assist agent portraits.
    // The load function returns true if both template
    // collections were successfully loaded.
    agentTemplatesReady = agentMatcher.load(templatesPath,"48x32","20x20");

    // Load the experimental 24x24 red and teal assist templates.
    // Reuse the same agent_templates directory.
    assistCandidateTemplatesReady =
        assistCandidateMatcher.load(templatesPath);

    if (!assistCandidateTemplatesReady)
    {
        std::cerr
            << "WARNING: Assist candidate templates failed to load."
            << std::endl;
    }


}

OCRDetector::~OCRDetector()
{
    // Tell the in-flight worker (if any) to bail before it touches tess.
    shuttingDown.store(true);
    if (frameTimer) {
        frameTimer->stop();
    };

    // Detached OCR thread may still be running. Spin briefly until it releases
    // the busy flag, otherwise tess.End() races with SetImage/GetUTF8Text and crashes.
    for (int i = 0; i < 200 && ocrBusy.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    cap.release();

    std::lock_guard<std::mutex> lk(tessMutex);
    if (tessOk) {
        tess.End();
        tessOk = false;
    }
}

// Public control
void OCRDetector::startVideo(const std::string& videoPath)
{
    // Old timer-based OCR path is disabled.
    if (frameTimer) {
        frameTimer->stop();
    }
    cap.release();

    teamDetected = false;
    awaitingTeamConfirmation = false;
    seenKills.clear();
    awaitingNewRoundKillfeed = false;
    // Reset the saved killfeed images for the new video.
// Each row starts empty until OCR sees its first frame.
    previousKillfeedRows.assign(
        KILLFEED_ROW_COUNT,
        cv::Mat()
    );
    lastLeftScore  = -1;
    lastRightScore = -1;
    leftTeamName.clear();
    rightTeamName.clear();
    inReplay = false;
    replayMissCount = 0;

    // Reset Spike tracking for the newly loaded video.
    spikePlanted = false;
    spikePlantConfirmCount = 0;
    spikePlantVideoSeconds = 0.0;

    // Tell the GUI there is no active Spike timer yet.
    emit spikeTimerChanged(-1);


    // Make sure a newly loaded video begins with replay mode disabled.
    emit replayStateChanged(false);


    // Reset internal round-timer tracking for every newly loaded video.
// The timer will remain inactive until OCR finds a valid in-game clock.
    lastRoundTimer.clear();
    timerAnchorValid = false;
    timerAnchorSeconds = 0;
    timerAnchorVideoSeconds = 0.0;
    currentOcrVideoSeconds = 0.0;

    // No round timer has been seen yet in this new video.
    lastRoundTimerVisibleVideoSeconds = -1.0;

    if (!tessOk) return;

    // Open the video for the existing OCR sampling path.
    cap.open(videoPath);

    if (cap.isOpened()) {
        capFps = cap.get(cv::CAP_PROP_FPS);

        if (capFps <= 0) {
            capFps = 30.0;
        }

        // Do NOT start OCR yet.
        // Wait until QMediaPlayer actually enters the Playing state.
        if (frameTimer) {
            frameTimer->stop();
        }
    }
}

void OCRDetector::reset()
{
    if (frameTimer) {
        frameTimer->stop();
    }
    cap.release();
    teamDetected = false;
    awaitingTeamConfirmation = false;
    seenKills.clear();
    awaitingNewRoundKillfeed = false;
    lastLeftScore  = -1;
    lastRightScore = -1;
    // Reset Spike tracking.
    spikePlanted = false;
    spikePlantConfirmCount = 0;
    spikePlantVideoSeconds = 0.0;
    // Forget when the previous video's round timer was visible.
    lastRoundTimerVisibleVideoSeconds = -1.0;


    // Remove any active Spike timer from the GUI.
    emit spikeTimerChanged(-1);
}

// Receives a frame that Qt has already decoded.
// This lets OCR process the frame without opening the video again.
void OCRDetector::processVideoFrame(const QImage& image)
{
    // Do not start another OCR job if the previous one is still running.
    if (ocrBusy.load() || image.isNull() || !tessOk) {
        return;
    }

    // Convert the Qt image into a predictable RGB format.
    QImage converted = image.convertToFormat(QImage::Format_RGB888);

    // Create an OpenCV matrix that points to the QImage data.
    cv::Mat rgb(
        converted.height(),
        converted.width(),
        CV_8UC3,
        converted.bits(),
        converted.bytesPerLine()
    );

    // Copy the frame because the QImage owns the original memory.
    cv::Mat frame = rgb.clone();

    // Convert Qt RGB order into OpenCV BGR order.
    cv::cvtColor(frame, frame, cv::COLOR_RGB2BGR);

    // Store the current Qt video position for this OCR frame.
    // The round-timer fallback uses this timestamp to keep counting down
    // when Tesseract temporarily cannot read the timer.
    currentOcrVideoSeconds =
        currentVideoPos.load() / 1000.0;

    // Mark OCR as busy so frames do not pile up.
    ocrBusy.store(true);

    // Run OCR on a background thread so the GUI stays responsive.
    std::thread([this, f = std::move(frame)]() mutable {
        if (!shuttingDown.load()) {
            processFrame(f);
        }

        ocrBusy.store(false);
        }).detach();
}


void OCRDetector::confirmTeams(QString left, QString right)
{
    leftTeamName  = left.toStdString();
    rightTeamName = right.toStdString();
    teamDetected = true;
    awaitingTeamConfirmation = false;
    emit ocrLog(QString("Teams confirmed: %1 vs %2").arg(left).arg(right));
    emit teamsDetected(left, right);
}

void OCRDetector::rejectTeams()
{
    // Allow the next frame's detection to propose fresh names
    awaitingTeamConfirmation = false;
    emit ocrLog("Team confirmation cancelled — detection will retry.");
}

void OCRDetector::setTeamNames(const std::vector<std::string>& names)
{
    if (!names.empty()) teamNames = names;
}

void OCRDetector::setPlayerRecords(const std::vector<std::pair<std::string,std::string>>& nameTeamPairs)
{
    nameToTeam.clear();
    playerNames.clear();
    for (const auto& p : nameTeamPairs)
    {
        // Add the player's database name normally.
        nameToTeam[p.first] = p.second;
        playerNames.push_back(p.first);

        // VLR stores this player as "Cryocells", but the Valorant broadcast
        // displays his in-game name as "Cryo".
        // Add "Cryo" as an alias so killfeed OCR can recognize either name.
        if (p.first == "Cryocells")
        {
            nameToTeam["Cryo"] = p.second;
            playerNames.push_back("Cryo");
        }
    }
    playerListValid = !playerNames.empty();

    if (OCR_DEBUG)
    {
        // TEMP DEBUG:
        // Print every player that the killfeed matcher is allowed to recognize.
        std::cout << "MATCH ROSTER -> ";

        for (const auto& player : playerNames)
        {
            std::cout << player << " ";
        }

        std::cout << "\n";
    }

}

void OCRDetector::onVideoPlaying(qint64 positionMs)
{
    // Store the actual Qt video-player position.
    currentVideoPos = positionMs;

    if (!frameTimer || !cap.isOpened()) {
        return;
    }

    // Synchronize OpenCV's OCR reader with the visible video before
    // allowing OCR processing to begin.
    if (capFps > 0)
    {
        double targetFrame =
            (positionMs / 1000.0) * capFps;

        cap.set(
            cv::CAP_PROP_POS_FRAMES,
            targetFrame
        );
    }

    // Now that the visible video is actually playing,
    // OCR may begin processing frames.
    frameTimer->start();
}

void OCRDetector::onVideoPaused()
{
    // Old timer-based OCR path is disabled.
    if (frameTimer) {
        frameTimer->stop();
    }
}



void OCRDetector::onVideoPositionChanged(qint64 positionMs)
{
    currentVideoPos = positionMs;
}

// Timer slot
void OCRDetector::processNextFrame()
{
    if (!cap.isOpened()) return;

    // If Tesseract is still processing the previous OCR frame,
// do NOT let the OCR video fall behind.
//
// Instead of running more OCR or performing an expensive video seek,
// quietly discard about 500 ms of old video frames.
//
// This keeps the OpenCV video reader moving forward with the visible
// Qt video while Tesseract finishes its current work.
    if (ocrBusy.load())
    {
        int framesToSkip =
            std::max(
                1,
                static_cast<int>(
                    capFps * (FRAME_INTERVAL_MS / 1000.0)
                    )
            );

        // grab() advances the video without copying the frame into a cv::Mat
        // and without running any OCR on it.
        for (int i = 0; i < framesToSkip; ++i)
        {
            if (!cap.grab())
            {
                break;
            }
        }

        return;
    }

    /*
    // Only seek when OCR has drifted more than 1.5 seconds from the video.
    // Sequential cap.read() is fast; H264 seeks are expensive (keyframe decode).
    if (capFps > 0) {
        double ocrPosMs = (cap.get(cv::CAP_PROP_POS_FRAMES) / capFps) * 1000.0;
        double drift    = std::abs(ocrPosMs - static_cast<double>(currentVideoPos.load()));
        if (drift > 1500.0) {
            double targetFrame = (currentVideoPos.load() / 1000.0) * capFps;
            cap.set(cv::CAP_PROP_POS_FRAMES, targetFrame);
        }
    }
    */

    cv::Mat frame;
    if (!cap.read(frame)) {
        if (frameTimer) {
            frameTimer->stop();
        }
        return;
    }


    // TEMP DEBUG:
// Print the precise video position so we can see how quickly OCR
// is advancing through the actual video.
    double ocrTimeSeconds =
        cap.get(cv::CAP_PROP_POS_FRAMES) / capFps;

    // Store the video timestamp for the frame currently being processed.
    // The internal Valorant timer will use this instead of real-world time.
    currentOcrVideoSeconds = ocrTimeSeconds;

    if (OCR_DEBUG)
    {
        std::cout << "OCR VIDEO TIME -> "
            << ocrTimeSeconds
            << " seconds\n";
    }

    // TEMP DEBUG:
// Save the exact video frames around the first kill so we can inspect
// whether the full killfeed entry for BuZz -> Asuna is inside our ROI.
    if (ocrTimeSeconds >= 14.0 && ocrTimeSeconds <= 16.0)
    {
        std::string filename =
            "kill_debug_" +
            std::to_string(ocrTimeSeconds) +
            ".png";

        cv::imwrite(filename, frame);
    }

    // Skip the frames between OCR samples.
//
// Example:
// A 60 FPS video contains 30 frames every 500 ms.
// We already read one frame above, so skip the other 29 frames.
// This makes OCR analyze approximately one frame every 500 ms
// of VIDEO time instead of processing every single video frame.
    int framesPerSample =
        static_cast<int>(
            capFps * (FRAME_INTERVAL_MS / 1000.0)
            );

    int framesToSkip =
        std::max(0, framesPerSample - 1);

    for (int i = 0; i < framesToSkip; ++i)
    {
        // grab() advances the video without performing OCR on the frame.
        if (!cap.grab())
        {
            break;
        }
    }





    // Run all Tesseract work off the main thread so the UI stays responsive.
    // ocrBusy prevents a second step until this one finishes.
    ocrBusy.store(true);
    std::thread([this, f = std::move(frame)]() mutable {
        if (!shuttingDown.load())
            processFrame(f);
        ocrBusy.store(false);
    }).detach();
}

