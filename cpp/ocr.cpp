#include "../headers/ocr.h"
#include <QDateTime>
#include <algorithm>
#include <cctype>

// Set to true when detailed OCR debugging is needed.
static constexpr bool OCR_DEBUG = false;

// Relative coordinate helpers (calibrated for 1920x1080 broadcast layout)
struct RelRect { double x1, y1, x2, y2; };

static cv::Rect rel_to_rect(const RelRect& r, int w, int h)
{
    return cv::Rect(
        int(r.x1 * w), int(r.y1 * h),
        int((r.x2 - r.x1) * w), int((r.y2 - r.y1) * h)
    );
}

static const RelRect KILLFEED_ROWS[] = {
    { 1291.0/1920,  90.0/1080, 1898.0/1920, 130.0/1080 },
    { 1291.0/1920, 130.0/1080, 1898.0/1920, 170.0/1080 },
    { 1291.0/1920, 170.0/1080, 1898.0/1920, 210.0/1080 },
    { 1291.0/1920, 210.0/1080, 1898.0/1920, 250.0/1080 },
};
static const int KILLFEED_ROW_COUNT = 4;

static const RelRect TEAM_LEFT_ROI   = { 720.0/1920,  10.0/1080,  790.0/1920,  38.0/1080 };
static const RelRect TEAM_RIGHT_ROI  = { 1130.0/1920, 10.0/1080, 1200.0/1920,  38.0/1080 };
static const RelRect SCORE_LEFT_ROI  = { 800.0/1920,   0.0/1080,  860.0/1920,  60.0/1080 };
static const RelRect SCORE_RIGHT_ROI = { 1060.0/1920,  10.0/1080, 1120.0/1920,  60.0/1080 };
static const RelRect ROUND_TIMER_ROI = { 880.0/1920,   0.0/1080, 1010.0/1920,  67.0/1080 };

// Region containing the broadcast's "REPLAY" indicator.
// When this text is detected, kills, score changes, and timer anchors
// from the replay should not be treated as live game events.
static const RelRect REPLAY_ROI = {
    1560.0 / 1920,
    930.0 / 1080,
    1870.0 / 1920,
    1040.0 / 1080
};

static const int FRAME_INTERVAL_MS = 500;

// Constructor / destructor
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
    // A newly loaded video always starts with replay detection reset.
inReplay = false;

// Reset the replay miss counter for the newly loaded video.
replayMissCount = 0;


    // Reset internal round-timer tracking for every newly loaded video.
// The timer will remain inactive until OCR finds a valid in-game clock.
    lastRoundTimer.clear();
    timerAnchorValid = false;
    timerAnchorSeconds = 0;
    timerAnchorVideoSeconds = 0.0;
    currentOcrVideoSeconds = 0.0;

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
    lastLeftScore  = -1;
    lastRightScore = -1;
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

// Core OCR logic
std::string OCRDetector::normalize(const std::string& s)
{
    std::string out;
    for (char c : s)
        if (!std::isspace((unsigned char)c))
            out.push_back(std::tolower(c));
    return out;
}

int OCRDetector::levenshtein_distance(const std::string& a, const std::string& b)
{
    size_t m = a.size(), n = b.size();
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1));
    for (size_t i = 0; i <= m; i++) dp[i][0] = (int)i;
    for (size_t j = 0; j <= n; j++) dp[0][j] = (int)j;
    for (size_t i = 1; i <= m; i++)
        for (size_t j = 1; j <= n; j++) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            dp[i][j] = std::min({ dp[i-1][j]+1, dp[i][j-1]+1, dp[i-1][j-1]+cost });
        }
    return dp[m][n];
}
// OCR specifically for one Valorant killfeed row.
//
// The normal ocr_region() uses grayscale + Otsu thresholding.
// That works well for the scoreboard, but the killfeed places white
// player names on bright red and green colored backgrounds.
//
// Here we compare the B, G, and R channels and keep pixels that are
// bright in all three channels, which strongly favors white killfeed text.
std::string OCRDetector::ocr_killfeed_region(const cv::Mat& roi)
{
    if (roi.empty())
    {
        return "";
    }

    // Split the image into its blue, green, and red channels.
 // White text is bright in ALL three channels, while the red and
 // green killfeed backgrounds are strong in only some channels.
    std::vector<cv::Mat> channels;
    cv::split(roi, channels);

    // Find the darkest value among B, G, and R for every pixel.
    // A pixel will only remain bright here if all three color
    // channels were bright, which strongly favors white text.
    cv::Mat minBG;
    cv::Mat minBGR;

    cv::min(channels[0], channels[1], minBG);
    cv::min(minBG, channels[2], minBGR);

    // Keep only pixels where all three BGR channels are sufficiently bright.
    // This removes most of the colored killfeed banners while preserving
    // white player names such as "BuZz" and "Asuna".
    cv::Mat whiteMask;
    cv::threshold(
        minBGR,
        whiteMask,
        150,
        255,
        cv::THRESH_BINARY
    );

    // Make the small killfeed text larger before sending it to Tesseract.
    cv::Mat big;
    cv::resize(
        whiteMask,
        big,
        cv::Size(),
        3.0,
        3.0,
        cv::INTER_CUBIC
    );

    // Tesseract generally performs better with black text on a white background.
    // whiteMask currently contains white text on black, so invert it.
    cv::bitwise_not(big, big);

    // Each ROI represents one horizontal killfeed entry.
    tess.SetPageSegMode(tesseract::PSM_SINGLE_LINE);

    // Player names and team abbreviations only need letters and numbers.
    tess.SetVariable(
        "tessedit_char_whitelist",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    );

    // Send the processed killfeed row directly to Tesseract.
    tess.SetImage(
        big.data,
        big.cols,
        big.rows,
        1,
        static_cast<int>(big.step)
    );

    char* out = tess.GetUTF8Text();

    std::string result = out ? out : "";

    if (out)
    {
        delete[] out;
    }

    // Remove whitespace from the beginning and end of Tesseract's result.
    auto start = result.find_first_not_of(" \n\r\t");
    auto end = result.find_last_not_of(" \n\r\t");

    if (start == std::string::npos)
    {
        return "";
    }

    return result.substr(start, end - start + 1);
}

std::string OCRDetector::ocr_region(const cv::Mat& roi)
{
    if (roi.empty()) return "";

    cv::Mat gray;
    if (roi.channels() == 3)
        cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);
    else
        gray = roi.clone();

    cv::Mat big;
    cv::resize(gray, big, cv::Size(), 3.0, 3.0, cv::INTER_CUBIC);
    cv::threshold(big, big, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);

    // Temporary debugging:
    // Save the exact processed image that Tesseract receives.
    // This lets us check whether thresholding is damaging the characters.
    static int debugImageNumber = 0;

    if (debugImageNumber < 10)
    {
        cv::imwrite(
            "ocr_processed_" + std::to_string(debugImageNumber) + ".png",
            big
        );

        ++debugImageNumber;
    }

    // Give the OpenCV image directly to Tesseract.
       // The image is an 8-bit grayscale image, so it has 1 byte per pixel.
    // This avoids manually copying every pixel into a Leptonica Pix object.
    tess.SetImage(
        big.data,
        big.cols,
        big.rows,
        1,
        static_cast<int>(big.step)
    );

    char* out = tess.GetUTF8Text();

    std::string result = out ? out : "";

    if (out)
    {
        delete[] out;
    }

    auto s = result.find_first_not_of(" \n\r\t");
    auto e = result.find_last_not_of(" \n\r\t");
    if (s == std::string::npos) return "";
    return result.substr(s, e - s + 1);
}

std::string OCRDetector::ocr_team_name(const cv::Mat& roi)
{
    if (roi.empty())
    {
        return "";
    }

    // Add space around the team abbreviation so characters near the
    // edge of the HUD crop are not cut off.
    cv::Mat padded;
    cv::copyMakeBorder(
        roi,
        padded,
        10, 10,
        10, 10,
        cv::BORDER_CONSTANT,
        cv::Scalar(0, 0, 0)
    );

    // Convert the team-name region to grayscale.
    cv::Mat gray;
    if (padded.channels() == 3)
    {
        cv::cvtColor(padded, gray, cv::COLOR_BGR2GRAY);
    }
    else
    {
        gray = padded.clone();
    }

    // Make the small HUD text larger before separating characters.
    cv::Mat big;
    cv::resize(
        gray,
        big,
        cv::Size(),
        3.0,
        3.0,
        cv::INTER_CUBIC
    );

    // Make the bright team-name characters white and the background black.
    // This makes it easier for OpenCV to locate each character separately.
    cv::Mat binary;
    // Use a stricter fixed brightness threshold.
// This keeps the bright white team-name pixels while removing
// dimmer anti-aliased pixels that may connect neighboring characters.
    cv::threshold(
        big,
        binary,
        200,
        255,
        cv::THRESH_BINARY
    );

    
    // Instead of using connected contours, scan the team-name image
    // from left to right and look for groups of white pixels.
    //
    // This is useful because neighboring Valorant characters can touch
    // slightly and become one contour even though they are separate letters.
    std::vector<cv::Rect> characterBoxes;

    // Only inspect the middle part of the image vertically.
    // This avoids small HUD shapes near the top or bottom of the crop.
    int scanTop = binary.rows / 4;
    int scanBottom = (binary.rows * 3) / 4;

    bool insideCharacter = false;
    int characterStart = 0;

    for (int x = 0; x < binary.cols; ++x)
    {
        int whitePixels = 0;

        // Count how many white pixels appear in this vertical column.
        for (int y = scanTop; y < scanBottom; ++y)
        {
            if (binary.at<uchar>(y, x) > 0)
            {
                ++whitePixels;
            }
        }

        // A column with enough white pixels is considered part of a character.
        bool hasCharacterPixels = whitePixels >= 15;

        if (hasCharacterPixels && !insideCharacter)
        {
            // Start of a new character.
            insideCharacter = true;
            characterStart = x;
        }
        else if (!hasCharacterPixels && insideCharacter)
        {
            // End of the current character.
            insideCharacter = false;

            int width = x - characterStart;

            if (width >= 2)
            {
                // Use the middle region only to find the left/right character boundary,
// but keep the full vertical height so features such as the top of "T"
// are not cut off before OCR.
                characterBoxes.emplace_back(
                    characterStart,
                    0,
                    width,
                    binary.rows
                );
            }
        }
    }

    // Handle a character that reaches the right edge of the image.
    if (insideCharacter)
    {
        int width = binary.cols - characterStart;

        if (width >= 2)
        {
            // Use the middle region only to find the left/right character boundary,
// but keep the full vertical height so features such as the top of "T"
// are not cut off before OCR.
            characterBoxes.emplace_back(
                characterStart,
                0,
                width,
                binary.rows
            );
        }
    }

    // Expand each character box slightly left and right.
//
// The vertical scan finds the main vertical stroke of characters,
// but wide features such as the top bar of "T" can extend beyond
// those detected boundaries. Adding horizontal padding keeps those
// features inside the image that Tesseract receives.
    for (cv::Rect& box : characterBoxes)
    {
        const int horizontalPadding = 10;

        int newLeft = std::max(
            0,
            box.x - horizontalPadding
        );

        int newRight = std::min(
            binary.cols,
            box.x + box.width + horizontalPadding
        );

        box.x = newLeft;
        box.width = newRight - newLeft;
    }

    // OCR must read the characters from left to right.
    std::sort(
        characterBoxes.begin(),
        characterBoxes.end(),
        [](const cv::Rect& a, const cv::Rect& b)
        {
            return a.x < b.x;
        }
    );

    // Temporary debugging:
// Draw a rectangle around every shape that OpenCV currently thinks
// is a team-name character. This will show us which false shapes
// need to be filtered out.
    static int teamBoxDebugCount = 0;

    if (teamBoxDebugCount < 2)
    {
        cv::Mat debugBoxes;

        // Convert to color so the rectangles are easy to see.
        cv::cvtColor(binary, debugBoxes, cv::COLOR_GRAY2BGR);

        for (const cv::Rect& box : characterBoxes)
        {
            cv::rectangle(
                debugBoxes,
                box,
                cv::Scalar(0, 0, 255),
                2
            );
        }

        cv::imwrite(
            "team_character_boxes_" +
            std::to_string(teamBoxDebugCount) +
            ".png",
            debugBoxes
        );

        ++teamBoxDebugCount;
    }


    // Each cropped image now contains only one character.
    tess.SetPageSegMode(tesseract::PSM_SINGLE_CHAR);

    tess.SetVariable(
        "tessedit_char_whitelist",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    );

    std::string result;

    // Temporary debugging:
    // Track which individual character box Tesseract is reading.
    int characterIndex = 0;

    for (const cv::Rect& box : characterBoxes)
    {
        cv::Mat character = binary(box);

        // Tesseract generally works better with black text on white.
        cv::Mat inverted;
        cv::bitwise_not(character, inverted);

        // Give each individual character some empty white space.
        cv::Mat characterPadded;
        cv::copyMakeBorder(
            inverted,
            characterPadded,
            10, 10,
            10, 10,
            cv::BORDER_CONSTANT,
            cv::Scalar(255)
        );
        // Temporary debugging:
// Save the exact individual character image that Tesseract receives.
// The first four files should be 1, 0, 0, T from the left team.
// The next two should be T, 1 from the right team.
        static int individualCharacterDebugCount = 0;

        if (individualCharacterDebugCount < 6)
        {
            cv::imwrite(
                "team_individual_character_" +
                std::to_string(individualCharacterDebugCount) +
                ".png",
                characterPadded
            );

            ++individualCharacterDebugCount;
        }
        // Send this single character directly to Tesseract.
        tess.SetImage(
            characterPadded.data,
            characterPadded.cols,
            characterPadded.rows,
            1,
            static_cast<int>(characterPadded.step)
        );

        char* out = tess.GetUTF8Text();
        
        // Temporarily allow only digits so we can test whether
// Tesseract recognizes 1 and 0 better without letters competing.
        tess.SetVariable(
            "tessedit_char_whitelist",
            "0123456789"
        );

        // Give Tesseract the same character image again.
        // SetImage clears the previous recognition result so the
        // digits-only whitelist is actually used for a new OCR pass.
        tess.SetImage(
            characterPadded.data,
            characterPadded.cols,
            characterPadded.rows,
            1,
            static_cast<int>(characterPadded.step)
        );

        char* digitOut = tess.GetUTF8Text();

        std::string rawDigitCharacter =
            digitOut ? digitOut : "";

        if (OCR_DEBUG)
        {
            std::cout << "CHAR BOX "
                << characterIndex
                << " DIGIT RAW -> ["
                << rawDigitCharacter
                << "]\n";
        }

        

        if (digitOut)
        {
            delete[] digitOut;
        }

        // Restore the normal team-character whitelist before
        // processing the next character.
        tess.SetVariable(
            "tessedit_char_whitelist",
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        );

        // Temporary debugging:
        // Print Tesseract's raw result for every individual character box.
        // This will tell us which character in "100T" is being missed.
        std::string rawCharacter = out ? out : "";


        if (OCR_DEBUG)
        {
            std::cout << "CHAR BOX "
                << characterIndex
                << " RAW -> ["
                << rawCharacter
                << "]\n";
        }
        

        ++characterIndex;

        if (out)
        {
            // Track whether Tesseract actually recognized a valid character.
            bool foundCharacter = false;

            for (char c : std::string(out))
            {
                if (std::isalnum(static_cast<unsigned char>(c)))
                {
                    result.push_back(c);
                    foundCharacter = true;
                    break;
                }
            }

            // We know a character box physically exists here even if Tesseract
            // could not identify it. Keep a placeholder so we do not lose the
            // character's position in the team abbreviation.
            if (!foundCharacter)
            {
                result.push_back('?');
            }

            delete[] out;
        }
    }


    if (OCR_DEBUG)
    {
        // Temporary debugging:
        // Show what character-by-character OCR produced.
        std::cout << "CHAR TEAM OCR -> ["
            << result
            << "]\n";
    }

    

    return result;
}
std::string OCRDetector::resolve_team_name(
    const std::string& raw,
    const std::vector<std::string>& teams)
{
    std::string token = normalize(raw);

    if (token.empty())
    {
        return "";
    }

    // OCR may use '?' when a character was detected physically,
    // but Tesseract could not determine what the character was.
    bool validCharacters = std::all_of(
        token.begin(),
        token.end(),
        [](unsigned char c)
        {
            return std::isalnum(c) || c == '?';
        }
    );

    if (!validCharacters)
    {
        return "";
    }

    double bestScore = 9999.0;
    double secondBestScore = 9999.0;

    std::string bestTeam;
    std::string secondBestTeam;

    for (const auto& team : teams)
    {
        std::string normalizedTeam = normalize(team);

        double score = 0.0;

        // Team tags with the same number of characters can be compared
        // position-by-position, which is more useful than plain
        // Levenshtein distance for OCR mistakes.
        if (token.size() == normalizedTeam.size())
        {
            for (size_t i = 0; i < token.size(); ++i)
            {
                char detected = token[i];
                char expected = normalizedTeam[i];

                // Exact match: no penalty.
                if (detected == expected)
                {
                    continue;
                }

                // A '?' means OpenCV found a character in this position,
                // but Tesseract could not identify it.
                //
                // Give this a small penalty rather than treating it as
                // either completely correct or completely wrong.
                if (detected == '?')
                {
                    score += 0.35;
                    continue;
                }

                // Very common OCR confusion:
                // uppercase I can look almost identical to the digit 1.
                if ((detected == 'i' && expected == '1') ||
                    (detected == '1' && expected == 'i'))
                {
                    score += 0.25;
                    continue;
                }

                // Another common OCR confusion:
                // uppercase O and the digit 0 can look very similar.
                if ((detected == 'o' && expected == '0') ||
                    (detected == '0' && expected == 'o'))
                {
                    score += 0.25;
                    continue;
                }

                // A normal incorrect character receives the full penalty.
                score += 1.0;
            }
        }
        else
        {
            // Different-length tags require an insertion or deletion.
            // Use Levenshtein distance and make these mistakes slightly
            // more expensive than a normal character substitution.
            score =
                static_cast<double>(
                    levenshtein_distance(token, normalizedTeam)
                    ) * 1.25;
        }

        // Keep track of both the best and second-best candidates.
        // The difference between them will tell us how confident
        // we should be in the best result.
        if (score < bestScore)
        {
            secondBestScore = bestScore;
            secondBestTeam = bestTeam;

            bestScore = score;
            bestTeam = team;
        }
        else if (score < secondBestScore)
        {
            secondBestScore = score;
            secondBestTeam = team;
        }
    }


    if (OCR_DEBUG)
    {
        // Temporary debugging:
        // Show the two strongest candidates so we can inspect how much
        // better the winning candidate was.
        std::cout << "TEAM SCORE -> Raw: ["
            << raw
            << "] Best: ["
            << bestTeam
            << "] Score: "
            << bestScore
            << " Second: ["
            << secondBestTeam
            << "] Score: "
            << secondBestScore
            << "\n";
    }
    

    // An exact OCR result should always be trusted.
    //
    // Example:
    // OCR = "TI"
    // Database contains an actual team named "TI".
    // That exact team should beat T1 even though I/1 are visually similar.
    if (bestScore == 0.0)
    {
        return bestTeam;
    }

    // Be stricter with very short team tags because tags such as
    // T1, TL, TS, TE, and TI are naturally very similar.
    double maximumAllowedScore;

    if (token.size() <= 2)
    {
        maximumAllowedScore = 0.50;
    }
    else if (token.size() == 3)
    {
        maximumAllowedScore = 0.75;
    }
    else
    {
        maximumAllowedScore = 1.00;
    }

    // The best candidate must also be clearly better than the runner-up.
    //
    // If two teams receive nearly identical scores, we should not guess.
    // Later, multiple-frame voting will try to resolve these cases.
    const double minimumConfidenceMargin = 0.50;

    double confidenceMargin =
        secondBestScore - bestScore;

    if (bestScore <= maximumAllowedScore &&
        confidenceMargin >= minimumConfidenceMargin)
    {
        return bestTeam;
    }

    // The OCR result is too uncertain to choose a team safely.
    return "";
}

std::vector<std::string> OCRDetector::extract_tokens(const std::string& raw)
{
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : raw) {
        if (std::isalnum((unsigned char)c)) {
            cur.push_back(c);
        } else if (!cur.empty()) {
            tokens.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

KillEvent OCRDetector::resolve_killfeed_whole(const std::string& raw,
                                              const std::vector<std::string>& players,
                                              bool player_list_valid)
{
    KillEvent evt;
    std::string norm = normalize(raw);
    if (norm.empty()) return evt;

    // Do not trust kill-feed OCR until we know the actual players
    // in the current match. Once the teams are confirmed,
    // setPlayerRecords() will provide the valid player roster.
    if (!player_list_valid)
    {
        return evt;
    }

    struct Found { std::string player; int pos; int len; int dist; };
    std::vector<Found> found;

    for (const auto& p : players) {
        std::string np = normalize(p);
        if (np.empty()) continue;

        int threshold;
        if (np.size() <= 2)      threshold = 0;
        else if (np.size() <= 5) threshold = 1;
        else                     threshold = 2;

        int minLen = std::max(1, (int)np.size() - 1);
        int maxLen = (int)np.size() + 1;
        int N = (int)norm.size();
        if (minLen > N) continue;

        int bestDist = 9999, bestPos = -1, bestLen = (int)np.size();
        for (int start = 0; start + minLen <= N; ++start) {
            for (int wl = minLen; wl <= maxLen && start + wl <= N; ++wl) {
                int d = levenshtein_distance(norm.substr(start, wl), np);
                if (d < bestDist) { bestDist = d; bestPos = start; bestLen = wl; }
                if (bestDist == 0) break;
            }
            if (bestDist == 0) break;
        }

        if (bestDist <= threshold && bestPos >= 0)
            found.push_back({ p, bestPos, bestLen, bestDist });
    }

    if (OCR_DEBUG)
    {
        // TEMP DEBUG:
        // Show which roster players were successfully found inside
        // the raw killfeed OCR text before overlap filtering happens.
        std::cout << "KILL MATCH CANDIDATES -> ";

        for (const auto& f : found)
        {
            std::cout
                << f.player
                << "(dist=" << f.dist
                << ", pos=" << f.pos
                << ") ";
        }

        std::cout << "\n";
    }


    // Greedy dedup: prefer lower distance, drop overlapping hits.
    std::sort(found.begin(), found.end(),
              [](const Found& a, const Found& b) { return a.dist < b.dist; });

    std::vector<Found> kept;
    for (const auto& f : found) {
        bool overlap = false;
        for (const auto& k : kept) {
            int a1 = f.pos, a2 = f.pos + f.len;
            int b1 = k.pos, b2 = k.pos + k.len;
            if (a1 < b2 && b1 < a2) { overlap = true; break; }
        }
        if (!overlap) kept.push_back(f);
        if (kept.size() == 2) break;
    }

    if (kept.size() != 2) return evt;

    // Killfeed layout: killer on left, victim on right.
    std::sort(kept.begin(), kept.end(),
              [](const Found& a, const Found& b) { return a.pos < b.pos; });

    evt.killer = kept.front().player;
    evt.victim = kept.back().player;
    if (evt.killer == evt.victim) { evt.killer.clear(); evt.victim.clear(); }
    return evt;
}

void OCRDetector::processFrame(const cv::Mat& frame)
{
    if (frame.empty() || !tessOk) return;

    // Hold the tess mutex for the whole frame so the destructor can't End()
    // tesseract out from under SetImage/GetUTF8Text mid-call.
    std::lock_guard<std::mutex> lk(tessMutex);
    if (!tessOk || shuttingDown.load()) return;

    double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    int img_w = frame.cols, img_h = frame.rows;

    // --- REPLAY DETECTION ---
// Check the bottom-right broadcast area for the word "REPLAY".
// For now, this only detects and prints the result.
// We will not block kills, scores, or timer updates until we verify it works.
    {
        tess.SetPageSegMode(tesseract::PSM_SINGLE_WORD);
        tess.SetVariable(
            "tessedit_char_whitelist",
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        );

        cv::Rect replayRect =
            rel_to_rect(REPLAY_ROI, img_w, img_h)
            & cv::Rect(0, 0, img_w, img_h);

        std::string replayRaw =
            ocr_region(frame(replayRect));

        std::string replayNormalized =
            normalize(replayRaw);

        // Check whether this individual frame contains the REPLAY label.
        bool replayDetectedThisFrame =
            replayNormalized.find("replay") != std::string::npos;

        if (replayDetectedThisFrame)
        {
            // A positive detection immediately puts us into replay mode.
            // Reset the miss counter because OCR successfully saw REPLAY.
            inReplay = true;
            replayMissCount = 0;
        }
        else if (inReplay)
        {
            // REPLAY was not detected on this frame, but do not immediately
            // leave replay mode. OCR can occasionally miss the label for one frame.
            ++replayMissCount;

            // Three missed frames at our 500 ms sampling rate is about 1.5 seconds.
            // After that, assume the replay has actually ended.
            if (replayMissCount >= 3)
            {
                inReplay = false;
                replayMissCount = 0;
            }
        }
        else
        {
            // We were already outside replay mode, so there is nothing to retain.
            replayMissCount = 0;
        }

        // Temporary debugging so we can verify the replay detector
        // before it is allowed to suppress real match events.
        if (OCR_DEBUG)
        {
            std::cout
                << "REPLAY OCR -> ["
                << replayRaw
                << "] Detected: "
                << (inReplay ? "YES" : "NO")
                << "\n";
        }
    }

    // --- TEAM DETECTION (propose to user, confirmed by dialog) ---
    if (!teamDetected && !awaitingTeamConfirmation) {
        cv::Rect lr = rel_to_rect(TEAM_LEFT_ROI,  img_w, img_h) & cv::Rect(0,0,img_w,img_h);
        cv::Rect rr = rel_to_rect(TEAM_RIGHT_ROI, img_w, img_h) & cv::Rect(0,0,img_w,img_h);

        // Temporary OCR debugging:
        // Save the exact left and right team-name crops so we can verify
        // whether the ROI coordinates actually contain the team abbreviations.
        cv::imwrite("team_left_debug.png", frame(lr));
        cv::imwrite("team_right_debug.png", frame(rr));

        // Read the raw OCR text from each team-name region first.
        // This helps us determine whether the problem comes from Tesseract/cropping
        // or from the fuzzy matching against the known team names.
        std::string leftRaw = ocr_team_name(frame(lr));
        std::string rightRaw = ocr_team_name(frame(rr));


        if (OCR_DEBUG)
        {
            // Temporary debug output so we can inspect exactly what Tesseract sees.
            std::cout << "RAW TEAM OCR -> Left: ["
                << leftRaw
                << "] Right: ["
                << rightRaw
                << "]\n";
        }

        

        // After printing the raw OCR, continue using the normal fuzzy matching.
        std::string left_team =
            resolve_team_name(leftRaw, teamNames);

        std::string right_team =
            resolve_team_name(rightRaw, teamNames);

        if (!left_team.empty() && !right_team.empty()) {
            awaitingTeamConfirmation = true;
            emit ocrLog(QString("Teams proposed: %1 vs %2 (awaiting confirmation)")
                        .arg(QString::fromStdString(left_team))
                        .arg(QString::fromStdString(right_team)));
            emit teamsProposed(QString::fromStdString(left_team),
                               QString::fromStdString(right_team));
        }
    }

    // --- SCORE OCR ---
    // --- SCORE OCR ---
// The match score changes only between rounds, so there is no need
// to run score OCR every 500 ms. Check it once every 2 seconds instead.
    static double lastScoreOcrTime = 0.0;
    const double SCORE_OCR_INTERVAL_SECONDS = 2.0;

    // Do not read or commit scoreboard values while replay footage is active.
    // A replay may show an older scoreboard that must not change the live match score.
    if (teamDetected &&
        !inReplay &&
        now - lastScoreOcrTime >= SCORE_OCR_INTERVAL_SECONDS)
    {
        // Remember when the score was last checked.
        lastScoreOcrTime = now;
        // Scores can contain one or two digits, such as 0, 9, 10, or 13.
// Treat the score region as one short line instead of one character.
        tess.SetPageSegMode(tesseract::PSM_SINGLE_WORD);
        tess.SetVariable("tessedit_char_whitelist", "0123456789");

        cv::Rect lsr = rel_to_rect(SCORE_LEFT_ROI,  img_w, img_h) & cv::Rect(0,0,img_w,img_h);
        cv::Rect rsr = rel_to_rect(SCORE_RIGHT_ROI, img_w, img_h) & cv::Rect(0,0,img_w,img_h);

        // TEMP DEBUG:
// Save the exact left and right scoreboard regions so we can verify
// that each ROI actually contains the score digit.
        static bool savedScoreDebugImages = false;

        if (!savedScoreDebugImages)
        {
            cv::imwrite("score_left_debug.png", frame(lsr));
            cv::imwrite("score_right_debug.png", frame(rsr));

            savedScoreDebugImages = true;
        }

        // First try reading each score as a word.
        // This supports one-digit and two-digit scores.
        std::string ls = ocr_region(frame(lsr));
        std::string rs = ocr_region(frame(rsr));

        // If one side was not recognized, retry that side as a single character.
        if (ls.empty() || rs.empty())
        {
            tess.SetPageSegMode(tesseract::PSM_SINGLE_CHAR);

            if (ls.empty())
            {
                ls = ocr_region(frame(lsr));
            }

            if (rs.empty())
            {
                rs = ocr_region(frame(rsr));
            }

            // Restore the normal score mode for future readings.
            tess.SetPageSegMode(tesseract::PSM_SINGLE_WORD);
        }

        // TEMP DEBUG:
// Show exactly what Tesseract reads from the left and right score regions.

        if (OCR_DEBUG)
        {
            std::cout << "SCORE OCR -> Left: ["
                << ls
                << "] Right: ["
                << rs
                << "]\n";
        }
     

        try {
            if (!ls.empty() && !rs.empty()) {
                int leftScore  = std::stoi(ls);
                int rightScore = std::stoi(rs);

                // Check whether OCR is proposing a different score.
                bool scoreChanged =
                    leftScore != lastLeftScore ||
                    rightScore != lastRightScore;

                // The very first valid score reading is allowed to initialize the match score.
                bool firstScore =
                    lastLeftScore == -1 &&
                    lastRightScore == -1;

                // After initialization, a valid Valorant score change can only increase
                // one team's score by exactly 1 while the other team's score stays the same.
                bool validNextScore =
                    (leftScore == lastLeftScore + 1 &&
                        rightScore == lastRightScore)
                    ||
                    (rightScore == lastRightScore + 1 &&
                        leftScore == lastLeftScore);

                if (scoreChanged)
                {
                    if (firstScore || validNextScore)
                    {
                        lastLeftScore = leftScore;
                        lastRightScore = rightScore;

                        emit scoresChanged(
                            QString::fromStdString(leftTeamName), leftScore,
                            QString::fromStdString(rightTeamName), rightScore
                        );

                        // Do NOT clear old kill signatures immediately when the score changes.
                    // The previous round's killfeed can remain visible for a few seconds
                    // after the scoreboard updates, which would cause duplicate kills.
                    //
                    // Kill signatures will instead be cleared when the round timer
                    // actually jumps back up at the start of the next round.
                    }
                    else
                    {
                        // Save rejected score crops so we can inspect why Tesseract
                        // is confusing a real scoreboard digit such as 2 with 5.
                        cv::imwrite("rejected_score_left.png", frame(lsr));
                        cv::imwrite("rejected_score_right.png", frame(rsr));
                        // Ignore impossible OCR jumps such as 1-0 suddenly becoming 5-0.
                        std::cout
                            << "SCORE REJECTED -> Left: "
                            << leftScore
                            << " Right: "
                            << rightScore
                            << " Previous: "
                            << lastLeftScore
                            << "-"
                            << lastRightScore
                            << "\n";
                    }
                }
            }
        } catch (...) {}
    }

    // --- ROUND TIMER OCR ---
    // Do not read or update the round timer while replay footage is active.
    // Replay footage may display an old timer that must not replace the live timer.
    if (!inReplay)
    {
        tess.SetPageSegMode(tesseract::PSM_SINGLE_LINE);
        tess.SetVariable("tessedit_char_whitelist", "0123456789:");
        cv::Rect tr = rel_to_rect(ROUND_TIMER_ROI, img_w, img_h) & cv::Rect(0,0,img_w,img_h);
        std::string raw = ocr_region(frame(tr));

        // Strip everything except digits and colons
        std::string cleaned;
        for (char c : raw)
            if (std::isdigit((unsigned char)c) || c == ':') cleaned.push_back(c);

        // Validate format: one or two digits, colon, exactly two digits (00-59)
        // Accepts "1:15", "0:45", "12:00" etc.
        bool valid = false;
        auto col = cleaned.find(':');
        if (col != std::string::npos && col >= 1 && col <= 2) {
            std::string minPart = cleaned.substr(0, col);
            std::string secPart = cleaned.substr(col + 1);
            if (secPart.size() == 2) {
                int secs = std::stoi(secPart);
                valid = (secs >= 0 && secs <= 59);
            }
        }

        if (valid)
        {
            // Convert the detected timer into total seconds.
            // Example: "1:25" becomes 85 seconds.
            int detectedMinutes =
                std::stoi(cleaned.substr(0, col));

            int detectedSeconds =
                std::stoi(cleaned.substr(col + 1));

            int detectedTotalSeconds =
                (detectedMinutes * 60) + detectedSeconds;

                    // If the round timer suddenly jumps upward by a large amount,
        // a new Valorant round has probably started.
        //
        // Example:
        // Previous timer: 0:39
        // New timer:      1:36
        //
        // Clear the old round's kill signatures so the same player matchup
        // can be counted again in the new round.
            if (timerAnchorValid &&
                detectedTotalSeconds > timerAnchorSeconds + 20)
            {
                std::cout
                    << "NEW ROUND DETECTED BY TIMER -> "
                    << timerAnchorSeconds
                    << " seconds to "
                    << detectedTotalSeconds
                    << " seconds\n";

                seenKills.clear();
            }

            // Use this successful OCR reading as the new timer anchor.
            // Future failed OCR frames can count down from this point
            // using the video's timestamp.
            timerAnchorSeconds = detectedTotalSeconds;
            timerAnchorVideoSeconds = currentOcrVideoSeconds;
            timerAnchorValid = true;

            lastRoundTimer = cleaned;
        }
        else if (timerAnchorValid)
        {
            // OCR failed on this frame, so estimate the current Valorant timer
            // from the most recent successful OCR reading and the video's timestamp.
            double elapsedVideoSeconds =
                currentOcrVideoSeconds - timerAnchorVideoSeconds;

            // Convert the elapsed video time into whole seconds.
            int elapsedWholeSeconds =
                static_cast<int>(elapsedVideoSeconds);

            // Count down from the last trustworthy OCR timer value.
            int estimatedSeconds =
                timerAnchorSeconds - elapsedWholeSeconds;

            // Never allow the estimated timer to become negative.
            estimatedSeconds = std::max(0, estimatedSeconds);

            int estimatedMinutes = estimatedSeconds / 60;
            int estimatedRemainingSeconds = estimatedSeconds % 60;

            // Format the estimated timer as M:SS.
            lastRoundTimer =
                std::to_string(estimatedMinutes) + ":" +
                (estimatedRemainingSeconds < 10 ? "0" : "") +
                std::to_string(estimatedRemainingSeconds);
        }
        else if (lastRoundTimer.empty())
        {
            // No valid in-game timer has ever been detected yet.
            // This is expected for videos that begin during agent select,
            // introductions, loading screens, or other pre-game footage.
            lastRoundTimer = "[no time detected]";
        }
    }

    // --- KILLFEED OCR ---
    tess.SetPageSegMode(tesseract::PSM_SPARSE_TEXT);
    tess.SetVariable("tessedit_char_whitelist",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");

    for (int row = 0; row < KILLFEED_ROW_COUNT; ++row) {
        cv::Rect roi = rel_to_rect(KILLFEED_ROWS[row], img_w, img_h)
                       & cv::Rect(0, 0, img_w, img_h);
        if (roi.width < 4 || roi.height < 4) continue;


        // Make a small grayscale copy of this killfeed row.
//
// This comparison is much cheaper than running Tesseract.
// We only want to use OCR when the row has visibly changed.
        cv::Mat currentRowGray;

        cv::cvtColor(
            frame(roi),
            currentRowGray,
            cv::COLOR_BGR2GRAY
        );

        // Make the comparison image smaller to reduce CPU work.
        cv::resize(
            currentRowGray,
            currentRowGray,
            cv::Size(),
            0.25,
            0.25,
            cv::INTER_AREA
        );

        bool rowChanged = true;

        // If we already have a previous image for this row,
        // compare the new image against it.
        if (!previousKillfeedRows[row].empty())
        {
            cv::Mat difference;
            cv::Mat changedPixels;

            cv::absdiff(
                currentRowGray,
                previousKillfeedRows[row],
                difference
            );

            // Ignore tiny pixel differences caused by video compression.
            cv::threshold(
                difference,
                changedPixels,
                20,
                255,
                cv::THRESH_BINARY
            );

            // Calculate what percentage of the row actually changed.
            double changedRatio =
                static_cast<double>(cv::countNonZero(changedPixels)) /
                static_cast<double>(changedPixels.total());

            // Only run Tesseract if at least 2% of the row changed.
            rowChanged = changedRatio >= 0.02;
        }

        // Save this image so the next OCR frame can compare against it.
        previousKillfeedRows[row] = currentRowGray.clone();

        // Nothing meaningful changed, so skip expensive Tesseract OCR
        // for this killfeed row.
        if (!rowChanged)
        {
            continue;
        }

        // Use the killfeed-specific preprocessing so the white player names
        // are separated from the red and green Valorant killfeed backgrounds.
        std::string raw = ocr_killfeed_region(frame(roi));

        
        KillEvent evt = resolve_killfeed_whole(raw, playerNames, playerListValid);

        // Do not record kills while replay footage is active.
        // Replay killfeed entries are repeats of kills that already happened live.
        if (!inReplay &&
            !evt.killer.empty() &&
            !evt.victim.empty())
        {
            std::string sig = evt.killer + "|" + evt.victim;
            if (!seenKills.count(sig)) {
                seenKills.insert(sig);

                // Look up team for each player
                auto killerTeamIt = nameToTeam.find(evt.killer);
                auto victimTeamIt = nameToTeam.find(evt.victim);
                QString killerTeam = (killerTeamIt != nameToTeam.end())
                                     ? QString::fromStdString(killerTeamIt->second) : "";
                QString victimTeam = (victimTeamIt != nameToTeam.end())
                                     ? QString::fromStdString(victimTeamIt->second) : "";

                QString prefix = lastRoundTimer.empty()
                                 ? ""
                                 : QString::fromStdString(lastRoundTimer) + " - ";

                QString logLine;
                if (!killerTeam.isEmpty() && !victimTeam.isEmpty())
                    logLine = prefix
                              + killerTeam + " " + QString::fromStdString(evt.killer)
                              + " killed "
                              + victimTeam + " " + QString::fromStdString(evt.victim);
                else
                    logLine = prefix
                              + QString::fromStdString(evt.killer)
                              + " killed "
                              + QString::fromStdString(evt.victim);

                emit killDetected(logLine);
            }
        }
    }
}
