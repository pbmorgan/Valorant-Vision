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


void OCRDetector::processTeamDetection(const cv::Mat& frame, int img_w, int img_h)
{
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


}
