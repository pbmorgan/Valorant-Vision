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

// Receives the killer portrait found by the OCR observation path.
// Keeps experimental candidate matching in its own translation unit.
void OCRDetector::detectAssistAgents(const cv::Mat& feedRow, const KillEvent& evt,
                                    qint64 frameId, int row,
                                    const std::filesystem::path& debugDir,
                                    const std::string& id, cv::Mat& annotated,
                                    const cv::Rect& observedMatchedPortrait,
                                    const cv::Rect& observedPortrait)
{
    // The caller's killer-team iterator is local to its own stage.
    // Resolve it here before running the existing assist matching logic.
    auto killerTeamIt = nameToTeam.find(evt.killer);

                // ---------------------------------------------------------
                // ASSIST DEBUG: SEARCH LEFT OF THE KILLER PORTRAIT
                // ---------------------------------------------------------

                const int assistAnchorX =
                    observedMatchedPortrait.area() > 0
                        ? observedMatchedPortrait.x
                        : (observedPortrait.area() > 0
                               ? observedPortrait.x : -1);

                if (assistAnchorX > 0)
                {
                    // Search up to 110 pixels to the left of
                    // the precisely matched killer portrait.
                    // This may contain one or more assist icons.
                    const int searchRight = assistAnchorX;

                    const int searchLeft =
                        std::max(0, searchRight - 110);

                    cv::Rect assistSearchRect(
                        searchLeft,
                        0,
                        searchRight - searchLeft,
                        feedRow.rows
                    );

                    if (assistSearchRect.area() > 0)
                    {
                        // Save the original pixels for later inspection.
                        cv::imwrite(
                            (debugDir / (id + "_assist_search.png")).string(),
                            feedRow(assistSearchRect)
                        );

                        // Purple rectangle: possible assist search area.
                        cv::rectangle(
                            annotated,
                            assistSearchRect,
                            cv::Scalar(255, 0, 255),
                            2
                        );

                        // Cyan rectangle: matched killer portrait.
                        if (observedMatchedPortrait.area() > 0)
                        {
                            cv::rectangle(
                                annotated,
                                observedMatchedPortrait,
                                cv::Scalar(255, 255, 0),
                                2
                            );
                        }

                        std::cout
                            << "ASSIST SEARCH DEBUG"
                            << " | Killer: " << evt.killer
                            << " | Victim: " << evt.victim
                            << " | Search X: " << assistSearchRect.x
                            << " | Search Width: " << assistSearchRect.width
                            << " | Killer Portrait X: "
                            << assistAnchorX
                            << (observedMatchedPortrait.area() > 0
                                    ? " | Anchor: MATCHED"
                                    : " | Anchor: OCR_FALLBACK")
                            << std::endl;

                        // LIVE ASSIST SCORING: detect colored agent-icon patches
                        // to the left of the killer portrait. For this experimental
                        // integration, we assume each identified patch is a real
                        // assist icon and its best template match is correct.
                        // This is deliberately separate from kill/death scoring.
                        if (assistCandidateTemplatesReady &&
                            killerTeamIt != nameToTeam.end())
                        {
                            cv::Mat hsv;
                            cv::cvtColor(
                                feedRow(assistSearchRect),
                                hsv, cv::COLOR_BGR2HSV
                            );

                            // Red / orange / teal icon colors. Excludes the
                            // usual purple killfeed background, so blank
                            // space does not automatically earn an assist.
                            cv::Mat redLow, redHigh, orange, teal;
                            cv::inRange(hsv, cv::Scalar(0, 65, 55),
                                        cv::Scalar(10, 255, 255), redLow);
                            cv::inRange(hsv, cv::Scalar(170, 65, 55),
                                        cv::Scalar(179, 255, 255), redHigh);
                            cv::inRange(hsv, cv::Scalar(10, 65, 55),
                                        cv::Scalar(30, 255, 255), orange);
                            cv::inRange(hsv, cv::Scalar(35, 55, 45),
                                        cv::Scalar(105, 255, 255), teal);

                            cv::Mat iconMask =
                                redLow | redHigh | orange | teal;
                            cv::morphologyEx(
                                iconMask, iconMask, cv::MORPH_CLOSE,
                                cv::getStructuringElement(
                                    cv::MORPH_RECT, cv::Size(3, 3)
                                )
                            );

                            std::vector<std::vector<cv::Point>> contours;
                            cv::findContours(
                                iconMask.clone(), contours,
                                cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE
                            );

                            std::vector<cv::Rect> iconRects;
                            for (const auto& contour : contours)
                            {
                                cv::Rect local =
                                    cv::boundingRect(contour);
                                int colored =
                                    cv::countNonZero(iconMask(local));

                                // Keep compact portrait-like colored shapes,
                                // not tiny compression artifacts or long bars.
                                if (local.width < 10 || local.width > 48 ||
                                    local.height < 8 || local.height > 32 ||
                                    colored < 18)
                                {
                                    continue;
                                }

                                cv::Rect icon = local;
                                icon.x += assistSearchRect.x;
                                icon.y += assistSearchRect.y;

                                if (icon.br().x >=
                                    assistAnchorX)
                                {
                                    continue;
                                }

                                iconRects.push_back(icon);
                            }

                            std::sort(
                                iconRects.begin(), iconRects.end(),
                                [](const cv::Rect& a, const cv::Rect& b) {
                                    return a.x < b.x;
                                }
                            );

                            // Merge overlapping color patches belonging
                            // to the same icon, without joining distinct
                            // adjacent assist icons.
                            std::vector<cv::Rect> distinctIcons;
                            for (const cv::Rect& icon : iconRects)
                            {
                                if (!distinctIcons.empty() &&
                                    (distinctIcons.back() & icon).area() > 0)
                                {
                                    distinctIcons.back() |= icon;
                                }
                                else
                                {
                                    distinctIcons.push_back(icon);
                                }
                            }

                            int assistIndex = 0;
                            std::unordered_set<std::string> emittedAssistAgents;
                            for (const cv::Rect& icon : distinctIcons)
                            {
                                if (assistIndex >= 4)
                                {
                                    break;
                                }

                                // Try both template backgrounds. The
                                // template matcher handles small shifts.
                                auto redCandidates =
                                    assistCandidateMatcher.rankCandidates(
                                        feedRow, icon, "red");
                                auto tealCandidates =
                                    assistCandidateMatcher.rankCandidates(
                                        feedRow, icon, "teal");

                                const RankedAssistAgent* best = nullptr;
                                if (!redCandidates.empty())
                                {
                                    best = &redCandidates.front();
                                }
                                if (!tealCandidates.empty() &&
                                    (best == nullptr ||
                                     tealCandidates.front().score >
                                     best->score))
                                {
                                    best = &tealCandidates.front();
                                }
                                if (best == nullptr || best->agent.empty())
                                {
                                    std::cout
                                        << "ASSIST SKIPPED | No agent match"
                                        << " | Killer: " << evt.killer
                                        << " | Victim: " << evt.victim
                                        << std::endl;
                                    continue;
                                }

                                // A single agent cannot receive two
                                // assists on the same kill. This also avoids
                                // double-counting one fragmented color blob.
                                if (!emittedAssistAgents.insert(
                                        best->agent).second)
                                {
                                    std::cout
                                        << "ASSIST SKIPPED | Duplicate agent "
                                        << best->agent << " on this kill"
                                        << std::endl;
                                    continue;
                                }

                                // This ID identifies one icon on one
                                // unique kill-feed observation. The existing
                                // seenKills gate prevents repeated frames.
                                const std::string eventId =
                                    "assist-" + std::to_string(frameId) +
                                    "-row-" + std::to_string(row) +
                                    "-icon-" + std::to_string(assistIndex);

                                std::cout
                                    << "ASSIST OCR EVENT | " << eventId
                                    << " | Team: " << killerTeamIt->second
                                    << " | Killer: " << evt.killer
                                    << " | Victim: " << evt.victim
                                    << " | Agent: " << best->agent
                                    << " | Score: " << best->score
                                    << " | IconX: " << icon.x
                                    << std::endl;

                                // The signal is safely queued onto the GUI
                                // thread where PlayerTracker lives.
                                emit assistEventDetected(
                                    QString::fromStdString(eventId),
                                    QString::fromStdString(
                                        killerTeamIt->second),
                                    QString::fromStdString(evt.killer),
                                    QString::fromStdString(evt.victim),
                                    QString::fromStdString(best->agent)
                                );

                                cv::rectangle(
                                    annotated, icon,
                                    cv::Scalar(0, 255, 255), 2
                                );
                                ++assistIndex;
                            }

                            if (distinctIcons.empty())
                            {
                                std::cout
                                    << "ASSIST OCR NONE | Killer: "
                                    << evt.killer << " | Victim: "
                                    << evt.victim << std::endl;
                            }
                        }
                    }
                }

}
