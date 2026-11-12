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

void OCRDetector::saveKillfeedExample(const cv::Mat& frame, const cv::Rect& roi, const KillEvent& evt)
{
        // ---------------------------------------------------------
        // DEBUG: SAVE AN ACTUAL KILLFEED EXAMPLE
        // ---------------------------------------------------------
        // Save one example where OCR recognized both players.
        // This will help us calibrate our agent template matching.

        static bool savedAgentDebug = false;

        if (!savedAgentDebug &&
            !inReplay &&
            !evt.killer.empty() &&
            !evt.victim.empty())
        {
            // Create a debug folder beside Valorant-Vision.exe.
            std::filesystem::path debugFolder =
                std::filesystem::path(
                    QCoreApplication::applicationDirPath().toStdString()
                ) / "agent_debug";

            std::filesystem::create_directories(debugFolder);

            // Save the entire gameplay frame.
            cv::imwrite(
                (debugFolder / "full_frame.png").string(),
                frame
            );

            // Save the complete killfeed row.
            cv::Mat feedRow = frame(roi);

            cv::imwrite(
                (debugFolder / "full_row.png").string(),
                feedRow
            );

            // Save the exact 140-pixel region currently
            // being searched by our agent matcher.
            cv::Rect searchArea(
                0,
                0,
                std::min(140, feedRow.cols),
                feedRow.rows
            );

            cv::imwrite(
                (debugFolder / "agent_search_area.png").string(),
                feedRow(searchArea)
            );

            std::cout
                << "AGENT DEBUG IMAGES SAVED TO: "
                << debugFolder.string()
                << std::endl;

            savedAgentDebug = true;
        }



}

void OCRDetector::emitKillIfNew(const KillEvent& evt)
{
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

                // Build the timestamp of the video frame where this kill was detected.
// This is the position in the MP4, not the Valorant round timer.
                qint64 videoTotalSeconds =
                    static_cast<qint64>(currentOcrVideoSeconds);

                qint64 videoHours = videoTotalSeconds / 3600;
                qint64 videoMinutes = (videoTotalSeconds % 3600) / 60;
                qint64 videoSeconds = videoTotalSeconds % 60;

                QString videoTime;

                if (videoHours > 0)
                {
                    videoTime = QString("%1:%2:%3")
                        .arg(videoHours)
                        .arg(videoMinutes, 2, 10, QChar('0'))
                        .arg(videoSeconds, 2, 10, QChar('0'));
                }
                else
                {
                    videoTime = QString("%1:%2")
                        .arg(videoMinutes)
                        .arg(videoSeconds, 2, 10, QChar('0'));
                }

                // Start every kill log with the video's timestamp.
                QString prefix =
                    "[" + videoTime + "] ";

                // Keep the Valorant round timer too, when one is available.
                if (!lastRoundTimer.empty())
                {
                    prefix +=
                        QString::fromStdString(lastRoundTimer) + " - ";
                }

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
                emit killEventDetected(
                    killerTeam,
                    QString::fromStdString(evt.killer),
                    victimTeam,
                    QString::fromStdString(evt.victim)
                );
            }
        }

}
