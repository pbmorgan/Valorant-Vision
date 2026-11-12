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

void OCRDetector::processScores(const cv::Mat& frame, int img_w, int img_h, double now)
{
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
                        // A real score increase means the previous round has ended.
                        // Stop using the old timer anchor so the next valid timer OCR
                        // can establish the starting point for the new round.
                        if (!firstScore)
                        {
                            awaitingNewRoundKillfeed = true;
                            timerAnchorValid = false;
                            lastRoundTimer.clear();

                            // The round has ended, so clear the planted-Spike state.
                            // This allows the next round to detect a new Spike plant.
                            spikePlanted = false;
                            spikePlantConfirmCount = 0;
                            spikePlantVideoSeconds = 0.0;

                            // Remove the Spike countdown from the GUI.
                            emit spikeTimerChanged(-1);
                        }

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


}
