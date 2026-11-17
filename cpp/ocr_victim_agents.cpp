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

void OCRDetector::processVictimObservation(const cv::Mat& frame, const cv::Rect& roi,
                                             const KillEvent& evt, qint64 frameId)
{
        // ---------------------------------------------------------
        // PHASE 1: IDENTIFY THE VICTIM'S AGENT
        // ---------------------------------------------------------
        // The victim agent icon is located at the right edge
        // of the killfeed row in our 1920x1080 broadcast.

        if (agentTemplatesReady &&
            !inReplay &&
            !evt.victim.empty())
        {
            // Extract the complete killfeed row.
            cv::Mat feedRow = frame(roi);

            // Approximate width of the victim portrait.
            const int victimIconWidth = 56;

            if (feedRow.cols >= victimIconWidth &&
                feedRow.rows >= 32)
            {
                // Start 56 pixels before the right edge.
                cv::Rect victimRect(
                    feedRow.cols - victimIconWidth,
                    0,
                    victimIconWidth,
                    feedRow.rows
                );

                // Extract only the victim agent portrait.
                cv::Mat victimIcon = feedRow(victimRect);


                if (OCR_DEBUG && normalize(evt.victim) == "reduxx")
                {
                    static int reduxxDebugCount = 0;

                    if (reduxxDebugCount < 4)
                    {
                        std::filesystem::path debugDir =
                            std::filesystem::path(
                                QCoreApplication::applicationDirPath().toStdString()
                            ) / "agent_debug" / "reduxx";

                        std::filesystem::create_directories(debugDir);

                        const std::string filename =
                            "reduxx_" + std::to_string(frameId);

                        // Save the exact portrait sent to the matcher.
                        cv::imwrite(
                            (debugDir / (filename + "_crop.png")).string(),
                            victimIcon
                        );

                        // Save the full killfeed row with the crop highlighted.
                        cv::Mat markedRow = feedRow.clone();

                        cv::rectangle(
                            markedRow,
                            victimRect,
                            cv::Scalar(0, 0, 255),
                            2
                        );

                        cv::imwrite(
                            (debugDir / (filename + "_row.png")).string(),
                            markedRow
                        );

                        ++reduxxDebugCount;
                    }
                }

                // TEMP DEBUG: Inspect Viper players incorrectly
                // identified as Clove.
                if (OCR_DEBUG)
                {
                    const std::string victimName =
                        normalize(evt.victim);

                    static int leafDebugCount = 0;
                    static int corteziaDebugCount = 0;

                    int* debugCount = nullptr;

                    if (victimName == "leaf")
                        debugCount = &leafDebugCount;
                    else if (victimName == "cortezia")
                        debugCount = &corteziaDebugCount;

                    if (debugCount && *debugCount < 3)
                    {
                        std::filesystem::path debugDir =
                            std::filesystem::path(
                                QCoreApplication::applicationDirPath().toStdString()
                            ) / "agent_debug" / "viper_check";

                        std::filesystem::create_directories(debugDir);

                        const std::string filename =
                            victimName + "_" + std::to_string(frameId);

                        cv::imwrite(
                            (debugDir / (filename + "_victim_crop.png")).string(),
                            victimIcon
                        );

                        cv::Mat markedRow = feedRow.clone();

                        cv::rectangle(
                            markedRow,
                            victimRect,
                            cv::Scalar(0, 0, 255),
                            2
                        );

                        cv::imwrite(
                            (debugDir / (filename + "_victim_row.png")).string(),
                            markedRow
                        );

                        ++(*debugCount);
                    }
                }



                // Save one image so we can inspect the crop.
                static bool savedVictimIcon = false;

                if (!savedVictimIcon)
                {
                    std::filesystem::path debugFolder =
                        std::filesystem::path(
                            QCoreApplication::applicationDirPath().toStdString()
                        ) / "agent_debug";

                    std::filesystem::create_directories(debugFolder);

                    cv::imwrite(
                        (debugFolder / "victim_icon.png").string(),
                        victimIcon
                    );

                    savedVictimIcon = true;
                }

                // Compare the portrait against our 29 templates.
                AgentIconMatch result =
                    agentMatcher.matchMain(victimIcon);

                // Print the victim name and candidate agent.

                if (result.hasCandidate())
                {
                    std::cout
                        << "VICTIM AGENT DEBUG | Player: "
                        << evt.victim
                        << " | Agent: " << result.agent
                        << " | Score: " << result.score
                        << " | Margin: " << result.margin()
                        << std::endl;

                    // TEMP DEBUG: Save every PatMen victim-agent prediction.
                    if (normalize(evt.victim) == "patmen")
                    {
                        std::filesystem::path debugDir =
                            std::filesystem::path(
                                QCoreApplication::applicationDirPath().toStdString()
                            ) / "agent_debug" / "patmen";

                        std::filesystem::create_directories(debugDir);

                        std::string filename =
                            "victim_" + std::to_string(frameId) +
                            "_" + result.agent;

                        cv::imwrite(
                            (debugDir / (filename + "_crop.png")).string(),
                            victimIcon
                        );

                        cv::Mat markedRow = feedRow.clone();

                        cv::rectangle(
                            markedRow,
                            victimRect,
                            cv::Scalar(0, 0, 255),
                            2
                        );

                        cv::imwrite(
                            (debugDir / (filename + "_row.png")).string(),
                            markedRow
                        );
                    }

                    // Find the victim's team in our player database.
                    auto victimTeamIt = nameToTeam.find(evt.victim);

                    // Send this observation to PlayerTracker.
                    if (victimTeamIt != nameToTeam.end())
                    {
                        emit agentCandidateDetected(
                            QString::fromStdString(victimTeamIt->second),
                            QString::fromStdString(evt.victim),
                            QString::fromStdString(result.agent),
                            frameId
                        );
                    }
                }

            }
        }




}
