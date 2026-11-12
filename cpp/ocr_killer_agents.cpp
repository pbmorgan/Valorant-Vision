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

void OCRDetector::processKillerObservation(const cv::Mat& frame, const cv::Rect& roi,
                                             const KillEvent& evt, qint64 frameId, int row, int img_w)
{
        // ---------------------------------------------------------
// DEBUG: FIND KILLER AND VICTIM NAME BOXES
// ---------------------------------------------------------
        if (!inReplay &&
            !evt.killer.empty() &&
            !evt.victim.empty())
        {
            cv::Mat feedRow = frame(roi);

            // Track what the killer-agent detector sees for this kill.
            const bool observeKill =
                !seenKills.count(evt.killer + "|" + evt.victim);

            std::string observedStatus = "KILLER_NAME_NOT_FOUND";
            std::string observedAgent = "NONE";
            double observedScore = -1.0;
            double observedMargin = -1.0;

            cv::Rect observedPortrait;
            cv::Rect observedNameBox;

            // Exact killer portrait position returned by template matching.
// Coordinates are relative to the complete killfeed row.
            cv::Rect observedMatchedPortrait;


            if (OCR_DEBUG &&
                evt.killer == "ZmjjKK" &&
                evt.victim == "UdoTan")
            {
                static int sampleCount = 0;

                if (sampleCount < 3)
                {
                    std::filesystem::path debugDir =
                        std::filesystem::path(
                            QCoreApplication::applicationDirPath().toStdString()
                        ) / "agent_debug";

                    std::filesystem::create_directories(debugDir);

                    cv::imwrite(
                        (debugDir / (
                            "zmjjkk_killfeed_" +
                            std::to_string(sampleCount) +
                            ".png"
                            )).string(),
                        feedRow
                    );

                    ++sampleCount;
                }
            }



            std::vector<OCRWordBox> wordBoxes =
                ocr_killfeed_word_boxes(feedRow);

            // DEBUG: Show every word Tesseract recognized
            // and its position inside the killfeed row.
            std::cout
                << "OCR WORD BOXES | Count: "
                << wordBoxes.size()
                << std::endl;


            for (const auto& wb : wordBoxes)
            {
                std::cout
                    << "OCR WORD DEBUG | Text: [" << wb.text << "]"
                    << " | x=" << wb.box.x
                    << " y=" << wb.box.y
                    << " w=" << wb.box.width
                    << " h=" << wb.box.height
                    << std::endl;
            }



            auto normKiller = normalize(evt.killer);
            auto normVictim = normalize(evt.victim);



            // Get the killer's team abbreviation from our player database.
            std::string normKillerTeam;

            auto killerTeamIt = nameToTeam.find(evt.killer);

            if (killerTeamIt != nameToTeam.end())
            {
                normKillerTeam = normalize(killerTeamIt->second);
            }



            bool killerFound = false;
            bool victimFound = false;

            for (const auto& wb : wordBoxes)
            {
                std::string normWord = normalize(wb.text);






               


                    // First, try the normal team abbreviation + player name.
                    // Example: "GEKr1stal" or "1GEPatMenSPPat".
                    bool startsWithTeam = false;
                    bool matchesKillerName = false;
                    bool matchedWithoutTeam = false;

                    if (!normKillerTeam.empty() &&
                        !normKiller.empty())
                    {
                        std::size_t teamPos =
                            normWord.find(normKillerTeam);

                        if (teamPos != std::string::npos &&
                            teamPos <= 2)
                        {
                            startsWithTeam = true;

                            std::size_t nameStart =
                                teamPos + normKillerTeam.size();

                            if (normWord.size() >=
                                nameStart + normKiller.size())
                            {
                                std::string detectedName =
                                    normWord.substr(
                                        nameStart,
                                        normKiller.size()
                                    );

                                int allowedErrors =
                                    normKiller.size() <= 5 ? 1 : 2;

                                matchesKillerName =
                                    levenshtein_distance(
                                        detectedName,
                                        normKiller
                                    ) <= allowedErrors;
                            }
                        }
                    }

                    // FALLBACK: Tesseract sometimes misses the team tag.
                    // Example: "KeistalJRP606noboay" for GE Kr1stal.
                    //
                    // Only check the first three possible starting positions
                    // and only on the left side of the killfeed.
                    // This reduces the chance of matching the victim's name.
                    if (!startsWithTeam &&
                        normKiller.size() >= 6 &&
                        normWord.size() >= normKiller.size() &&
                        wb.box.x < (feedRow.cols * 3) / 5)
                    {
                        const std::size_t lastStart =
                            std::min<std::size_t>(
                                2,
                                normWord.size() - normKiller.size()
                            );

                        for (std::size_t start = 0;
                            start <= lastStart;
                            ++start)
                        {
                            std::string detectedName =
                                normWord.substr(
                                    start,
                                    normKiller.size()
                                );

                            int distance =
                                levenshtein_distance(
                                    detectedName,
                                    normKiller
                                );

                            if (distance <= 2)
                            {
                                matchedWithoutTeam = true;
                                matchesKillerName = true;

                                std::cout
                                    << "KILLER NAME FALLBACK | Player: "
                                    << evt.killer
                                    << " | OCR: " << detectedName
                                    << " | Distance: " << distance
                                    << std::endl;

                                break;
                            }
                        }
                    }



                  if (!killerFound &&
                    matchesKillerName &&
                    (startsWithTeam || matchedWithoutTeam))
                {

                      observedNameBox = wb.box;
                      observedStatus = "PORTRAIT_NOT_LOCATED";
                    std::cout
                        << "KILLER BOX DEBUG | Player: " << evt.killer
                        << " | x=" << wb.box.x
                        << " y=" << wb.box.y
                        << " w=" << wb.box.width
                        << " h=" << wb.box.height
                        << std::endl;

                    // Save a test image of the killer's agent portrait.
                    static bool savedKillerCrop = false;

                    // Only use a tightly recognized name box.
                    // Large boxes may incorrectly include other killfeed elements.
                    if (OCR_DEBUG)
                    {

                        // TEMPORARY DEBUG CROP
                        // Layout: killer agent -> team tag -> killer name.
                        //
                        // The OCR word may include both "GE" and "UdoTan".
                        // For this 1920x1080 test, use the portrait position
                        // calibrated from the saved killfeed screenshot.

                        // The killer's portrait is immediately to the left
                        // of the combined team-tag and player-name region.
                        //
                        // In our verified example:
                        // Team tag starts at x=239.
                        // Portrait starts approximately at x=182.
                        // Difference = 57 pixels.


                        



                        const double scaleX =
                            static_cast<double>(img_w) / 1920.0;


                        // Use a 58-pixel crop for clean OCR.
                        // Use a wider portrait crop for noisy OCR.
                        const int killerIconWidth =
                            std::max(
                                1,
                                static_cast<int>(std::round(65 * scaleX))
                            );

                        const int iconOffset =
                            static_cast<int>(std::round(57 * scaleX));

                        int iconLeft = std::max(0, wb.box.x - iconOffset);

                        bool portraitLocated = false;

                        // For noisy OCR, locate the colored vertical border
                        // at the beginning of the killer's portrait.
                        if (feedRow.type() == CV_8UC3 &&
                            feedRow.cols >= killerIconWidth)
                        {
                            const int searchStart =
                                std::max(
                                    0,
                                    wb.box.x -
                                    static_cast<int>(std::round(80 * scaleX))
                                );

                            const int searchEnd =
                                std::min(
                                    feedRow.cols - killerIconWidth,
                                    wb.box.x +
                                    static_cast<int>(std::round(25 * scaleX))
                                );

                            if (OCR_DEBUG && normalize(evt.killer) == "kr1stal")
                            {
                                static int kr1stalDebugCount = 0;

                                if (kr1stalDebugCount < 6)
                                {
                                    std::filesystem::path debugDir =
                                        std::filesystem::path(
                                            QCoreApplication::applicationDirPath().toStdString()
                                        ) / "agent_debug" / "kr1stal";

                                    std::filesystem::create_directories(debugDir);

                                    std::string filename =
                                        "kr1stal_" + std::to_string(frameId);

                                    // Original killfeed row.
                                    cv::imwrite(
                                        (debugDir / (filename + "_raw.png")).string(),
                                        feedRow
                                    );

                                    cv::Mat markedRow = feedRow.clone();

                                    // Green: OCR word box.
                                    cv::rectangle(
                                        markedRow, wb.box,
                                        cv::Scalar(0, 255, 0), 2
                                    );

                                    // Red: original estimated portrait crop.
                                    cv::rectangle(
                                        markedRow,
                                        cv::Rect(iconLeft, 0, killerIconWidth, feedRow.rows),
                                        cv::Scalar(0, 0, 255), 2
                                    );

                                    // Yellow: beginning of border-search range.
                                    cv::line(
                                        markedRow,
                                        cv::Point(searchStart, 0),
                                        cv::Point(searchStart, feedRow.rows - 1),
                                        cv::Scalar(0, 255, 255), 1
                                    );

                                    // Cyan: end of border-search range.
                                    cv::line(
                                        markedRow,
                                        cv::Point(searchEnd, 0),
                                        cv::Point(searchEnd, feedRow.rows - 1),
                                        cv::Scalar(255, 255, 0), 1
                                    );

                                    cv::imwrite(
                                        (debugDir / (filename + "_marked.png")).string(),
                                        markedRow
                                    );

                                    std::cout
                                        << "KR1STAL BORDER SEARCH | Start: "
                                        << searchStart
                                        << " | End: " << searchEnd
                                        << " | Estimated portrait X: " << iconLeft
                                        << std::endl;

                                    ++kr1stalDebugCount;
                                }
                            }


                            const int yStart = feedRow.rows / 5;
                            const int yEnd = (feedRow.rows * 4) / 5;

                            const int minimumPixels =
                                std::max(
                                    1,
                                    static_cast<int>(
                                        std::ceil(0.65 * (yEnd - yStart))
                                        )
                                );

                            // Require a longer continuous colored border.
                            // This helps reject small unrelated HUD graphics
                            // before the actual killer portrait.
                            const int bandWidth =
                                std::max(
                                    3,
                                    static_cast<int>(std::round(10 * scaleX))
                                );

                            auto isColoredBorder = [&](int x)
                                {
                                    int redPixels = 0;
                                    int tealPixels = 0;

                                    for (int y = yStart; y < yEnd; ++y)
                                    {
                                        const cv::Vec3b pixel =
                                            feedRow.at<cv::Vec3b>(y, x);

                                        const int b = pixel[0];
                                        const int g = pixel[1];
                                        const int r = pixel[2];

                                        if (r >= 170 &&
                                            r - g >= 65 &&
                                            r - b >= 65)
                                        {
                                            ++redPixels;
                                        }

                                        if (g >= 145 &&
                                            g - r >= 25 &&
                                            g - b >= 7)
                                        {
                                            ++tealPixels;
                                        }
                                    }

                                    return redPixels >= minimumPixels ||
                                        tealPixels >= minimumPixels;
                                };

                            for (int x = searchStart;
                                x + bandWidth - 1 <= searchEnd;
                                ++x)
                            {
                                bool validBand = true;

                                for (int dx = 0; dx < bandWidth; ++dx)
                                {
                                    if (!isColoredBorder(x + dx))
                                    {
                                        validBand = false;
                                        break;
                                    }
                                }

                                if (validBand)
                                {
                                    const int leftPadding =
                                        static_cast<int>(std::round(5 * scaleX));

                                    iconLeft = std::clamp(
                                        x - leftPadding,
                                        0,
                                        feedRow.cols - killerIconWidth
                                    );

                                    portraitLocated = true;

                                    std::cout
                                        << "KILLER PORTRAIT BORDER | Player: "
                                        << evt.killer
                                        << " | Border X: " << x
                                        << " | Portrait X: " << iconLeft
                                        << std::endl;

                                    break;
                                }
                            }


                            // FALLBACK: Search farther left and accept
                            // a narrower colored border.
                            // Only runs when the original search fails.
                            if (!portraitLocated)
                            {
                                const int fallbackStart = std::max(
                                    0,
                                    wb.box.x -
                                    static_cast<int>(std::round(110 * scaleX))
                                );

                                const int fallbackBandWidth = std::max(
                                    3,
                                    static_cast<int>(std::round(4 * scaleX))
                                );

                                for (int x = fallbackStart;
                                    x + fallbackBandWidth - 1 <= searchEnd;
                                    ++x)
                                {
                                    bool validBand = true;

                                    for (int dx = 0;
                                        dx < fallbackBandWidth;
                                        ++dx)
                                    {
                                        if (!isColoredBorder(x + dx))
                                        {
                                            validBand = false;
                                            break;
                                        }
                                    }

                                    if (validBand)
                                    {
                                        const int leftPadding =
                                            static_cast<int>(
                                                std::round(5 * scaleX)
                                                );

                                        iconLeft = std::clamp(
                                            x - leftPadding,
                                            0,
                                            feedRow.cols - killerIconWidth
                                        );

                                        portraitLocated = true;

                                        std::cout
                                            << "KILLER PORTRAIT FALLBACK | Player: "
                                            << evt.killer
                                            << " | Border X: " << x
                                            << " | Portrait X: " << iconLeft
                                            << std::endl;

                                        break;
                                    }
                                }
                            }

                        }

                        // Crop only the killer's portrait.
                        cv::Rect iconRect(
                            iconLeft,
                            0,
                            killerIconWidth,
                            feedRow.rows
                        );





                        // Keep the crop inside the killfeed row.
                        iconRect &= cv::Rect(
                            0, 0, feedRow.cols, feedRow.rows
                        );
                        observedPortrait = iconRect;

                        if (portraitLocated &&
                            iconRect.width > 0 &&
                            iconRect.height > 0)
                        {
                            observedStatus = "TEMPLATES_NOT_LOADED";
                            std::filesystem::path debugDir =
                                std::filesystem::path(
                                    QCoreApplication::applicationDirPath().toStdString()
                                ) / "agent_debug";

                            std::filesystem::create_directories(debugDir);

                            // Save the candidate agent icon.
                            cv::imwrite(
                                (debugDir / "killer_icon_candidate.png").string(),
                                feedRow(iconRect)
                            );


                            if (agentTemplatesReady)
                            {
                                // Compare the killer's portrait with our agent templates.
                                AgentIconMatch killerResult =
                                    agentMatcher.matchMain(feedRow(iconRect));

                                if (killerResult.hasCandidate())
                                {
                                    observedStatus = "MATCHED";
                                    observedAgent = killerResult.agent;
                                    observedScore = killerResult.score;
                                    observedMargin = killerResult.margin();

                                    // killerResult.box is relative to iconRect.
                                    // Convert it to coordinates within the full killfeed row.
                                    observedMatchedPortrait = killerResult.box;

                                    observedMatchedPortrait.x += iconRect.x;
                                    observedMatchedPortrait.y += iconRect.y;

                                    // Ensure the detected portrait stays inside the row.
                                    observedMatchedPortrait &=
                                        cv::Rect(0, 0, feedRow.cols, feedRow.rows);
                                }
                                else
                                {
                                    observedStatus = "REJECTED_OR_AMBIGUOUS";
                                }

                                if (killerResult.hasCandidate())
                                {
                                    std::cout
                                        << "KILLER AGENT DEBUG | Player: "
                                        << evt.killer
                                        << " | Agent: " << killerResult.agent
                                        << " | Score: " << killerResult.score
                                        << " | Margin: " << killerResult.margin()
                                        << std::endl;

                                    // TEMP DEBUG: Save every PatMen killer-agent prediction.
                                    if (normalize(evt.killer) == "patmen")
                                    {
                                        std::filesystem::path debugDir =
                                            std::filesystem::path(
                                                QCoreApplication::applicationDirPath().toStdString()
                                            ) / "agent_debug" / "patmen";

                                        std::filesystem::create_directories(debugDir);

                                        std::string filename =
                                            "killer_" + std::to_string(frameId) +
                                            "_" + killerResult.agent;

                                        cv::imwrite(
                                            (debugDir / (filename + "_crop.png")).string(),
                                            feedRow(iconRect)
                                        );

                                        cv::Mat markedRow = feedRow.clone();

                                        cv::rectangle(
                                            markedRow,
                                            iconRect,
                                            cv::Scalar(0, 0, 255),
                                            2
                                        );

                                        // Show the exact portrait region selected by the matcher.
                                        // killerResult.box coordinates are relative to iconRect.
                                        cv::Rect matchedRect = killerResult.box;

                                        matchedRect.x += iconRect.x;
                                        matchedRect.y += iconRect.y;

                                        // Green rectangle = best template match.
                                        cv::rectangle(
                                            markedRow,
                                            matchedRect,
                                            cv::Scalar(0, 255, 0),
                                            2
                                        );

                                        cv::imwrite(
                                            (debugDir / (filename + "_row.png")).string(),
                                            markedRow
                                        );
                                    }

                                    // Send the detected killer agent to PlayerTracker.
                                    // The tracker will count votes and confirm
                                    // the agent after two matching observations.
                                    if (killerTeamIt != nameToTeam.end())
                                    {
                                        emit agentCandidateDetected(
                                            QString::fromStdString(killerTeamIt->second),
                                            QString::fromStdString(evt.killer),
                                            QString::fromStdString(killerResult.agent),
                                            frameId
                                        );
                                    }
                                }

                            }




                            // Save an annotated row to verify positioning.
                            cv::Mat debugRow = feedRow.clone();

                            // Red rectangle = proposed agent icon.
                            cv::rectangle(
                                debugRow, iconRect,
                                cv::Scalar(0, 0, 255), 2
                            );

                            // Green rectangle = recognized killer name.
                            cv::rectangle(
                                debugRow, wb.box,
                                cv::Scalar(0, 255, 0), 2
                            );

                            cv::imwrite(
                                (debugDir / "killer_row_annotated.png").string(),
                                debugRow
                            );

                            std::cout
                                << "KILLER ICON DEBUG | Saved candidate crop"
                                << " | x=" << iconRect.x
                                << " w=" << iconRect.width
                                << std::endl;

                            savedKillerCrop = true;
                        }
                    }

                    killerFound = true;
                }


                if (!victimFound && normWord == normVictim)
                {
                    std::cout
                        << "VICTIM BOX DEBUG | Player: " << evt.victim
                        << " | x=" << wb.box.x
                        << " y=" << wb.box.y
                        << " w=" << wb.box.width
                        << " h=" << wb.box.height
                        << std::endl;
                    victimFound = true;

                }
            }
            if (observeKill)
            {
                namespace fs = std::filesystem;

                fs::path debugDir =
                    fs::path(QCoreApplication::applicationDirPath().toStdString())
                    / "agent_debug"
                    / "killer_observations";

                fs::create_directories(debugDir);

                const std::string id =
                    "kill_" + std::to_string(frameId) +
                    "_row_" + std::to_string(row);

                // Save the entire killfeed row.
                cv::imwrite(
                    (debugDir / (id + "_full.png")).string(),
                    feedRow
                );

                // Mark the detected regions.
                cv::Mat annotated = feedRow.clone();

                if (observedPortrait.area() > 0)
                {
                    cv::rectangle(
                        annotated, observedPortrait,
                        cv::Scalar(0, 0, 255), 2
                    );

                    cv::imwrite(
                        (debugDir / (id + "_portrait.png")).string(),
                        feedRow(observedPortrait)
                    );
                }

                if (observedNameBox.area() > 0)
                {
                    cv::rectangle(
                        annotated, observedNameBox,
                        cv::Scalar(0, 255, 0), 2
                    );
                }

                


                detectAssistAgents(feedRow, evt, frameId, row, debugDir, id,
                                   annotated, observedMatchedPortrait, observedPortrait);
                cv::imwrite(
                    (debugDir / (id + "_annotated.png")).string(),
                    annotated
                );

                std::cout
                    << "KILL OBSERVATION"
                    << " | Frame: " << frameId
                    << " | Killer: " << evt.killer
                    << " | Victim: " << evt.victim
                    << " | Agent: " << observedAgent
                    << " | Status: " << observedStatus
                    << " | Score: " << observedScore
                    << " | Margin: " << observedMargin
                    << " | Row: " << row
                    << std::endl;
            }

        }

}
