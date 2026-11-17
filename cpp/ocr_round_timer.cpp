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

void OCRDetector::processRoundTimer(const cv::Mat& frame, int img_w, int img_h)
{
    // --- ROUND TIMER OCR ---
    // Do not read or update the round timer while replay footage is active.
    // Replay footage may display an old timer that must not replace the live timer.
    if (!inReplay)
    {
        // Use the actual Qt video playback position for the internal timer.
            // This is more stable than using the OpenCV/OCR reader position.
        double currentPlaybackSeconds =
            currentVideoPos.load() / 1000.0;

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

        int detectedMinutes = 0;
        int detectedSeconds = 0;

        auto col = cleaned.find(':');

        if (col != std::string::npos && col >= 1 && col <= 2)
        {
            std::string minPart = cleaned.substr(0, col);
            std::string secPart = cleaned.substr(col + 1);

            if (secPart.size() == 2)
            {
                detectedMinutes = std::stoi(minPart);
                detectedSeconds = std::stoi(secPart);

                valid =
                    detectedSeconds >= 0 &&
                    detectedSeconds <= 59;
            }
        }

        bool acceptedTimerReading = false;

        if (valid)
        {
            // OCR successfully saw a real round timer on this frame.
            // Remember when it was visible so Spike detection can
            // distinguish gameplay from caster/intermission scenes.
            lastRoundTimerVisibleVideoSeconds =
                currentPlaybackSeconds;

            int detectedTotalSeconds =
                detectedMinutes * 60 +
                detectedSeconds;

            if (!timerAnchorValid)
            {
                // Only establish a new round-timer anchor if the detected
                // clock is high enough to plausibly belong to an active round.
                //
                // This prevents the pre-round countdown from being mistaken
                // for the actual Valorant round timer.
                if (detectedTotalSeconds >= MIN_NEW_ROUND_TIMER_SECONDS)
                {
                    acceptedTimerReading = true;
                }
                else
                {
                    acceptedTimerReading = false;

                    if (OCR_DEBUG)
                    {
                        std::cout
                            << "ROUND TIMER START REJECTED -> "
                            << detectedTotalSeconds
                            << " seconds is below minimum "
                            << MIN_NEW_ROUND_TIMER_SECONDS
                            << "\n";
                    }
                }
            }
            else
            {
                // Estimate where the timer should currently be based on
                // the last trustworthy OCR reading and video playback time.
                int elapsedVideoSeconds =
                    static_cast<int>(
                        currentPlaybackSeconds -
                        timerAnchorVideoSeconds
                        );

                if (elapsedVideoSeconds < 0)
                {
                    elapsedVideoSeconds = 0;
                }

                int expectedSeconds =
                    timerAnchorSeconds -
                    elapsedVideoSeconds;

                if (expectedSeconds < 0)
                {
                    expectedSeconds = 0;
                }

                int difference =
                    detectedTotalSeconds -
                    expectedSeconds;

                int absoluteDifference =
                    std::abs(difference);


                
                // should NOT continuously adjust it.
                //
                // The internal timer is calculated from video playback time,
                // which gives us a smooth and stable countdown.
                if (absoluteDifference <= 5)
                {
                    acceptedTimerReading = false;
                }

                
                else
                {
                    qDebug(
                        "Round timer OCR rejected: detected=%d expected=%d difference=%d",
                        detectedTotalSeconds,
                        expectedSeconds,
                        difference
                    );
                }
            }


            if (acceptedTimerReading)
            {
                timerAnchorSeconds =
                    detectedTotalSeconds;

                timerAnchorVideoSeconds =
                    currentPlaybackSeconds;

                timerAnchorValid = true;

                lastRoundTimer = cleaned;

                if (awaitingNewRoundKillfeed &&
                    detectedTotalSeconds >= 80)
                {
                    seenKills.clear();
                    previousKillfeedRows.assign(
                        KILLFEED_ROW_COUNT, cv::Mat()
                    );
                    awaitingNewRoundKillfeed = false;
                    std::cout << "ASSIST ROUND RESET | New round timer: "
                              << cleaned << std::endl;
                }
            }
        }


        // If OCR failed OR produced an unrealistic timer jump,
        // continue counting down from our last trustworthy anchor.
        if (!acceptedTimerReading && timerAnchorValid)
        {
            int elapsedVideoSeconds =
                static_cast<int>(
                    currentPlaybackSeconds -
                    timerAnchorVideoSeconds
                    );

            if (elapsedVideoSeconds < 0)
            {
                elapsedVideoSeconds = 0;
            }

            int estimatedTotalSeconds =
                timerAnchorSeconds -
                elapsedVideoSeconds;

            if (estimatedTotalSeconds < 0)
            {
                estimatedTotalSeconds = 0;
            }

            int estimatedMinutes =
                estimatedTotalSeconds / 60;

            int estimatedRemainingSeconds =
                estimatedTotalSeconds % 60;

            lastRoundTimer =
                std::to_string(estimatedMinutes) +
                ":" +
                (estimatedRemainingSeconds < 10 ? "0" : "") +
                std::to_string(estimatedRemainingSeconds);
        }
        else if (!acceptedTimerReading &&
            !timerAnchorValid &&
            lastRoundTimer.empty())
        {
            lastRoundTimer =
                "[no time detected]";
        }
        // Send the current trustworthy round timer to the GUI.
        // This allows other widgets, such as the minimap, to display
        // the timer without performing their own OCR.
        if (timerAnchorValid && !lastRoundTimer.empty())
        {
            emit roundTimerChanged(
                QString::fromStdString(lastRoundTimer)
            );
        }
    }


}
