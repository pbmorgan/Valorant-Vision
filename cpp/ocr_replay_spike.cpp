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

void OCRDetector::processReplayAndSpike(const cv::Mat& frame, int img_w, int img_h)
{
    // --- REPLAY DETECTION ---
// Check the bottom-right broadcast area for the word "REPLAY".

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
            // Only notify the rest of the program when replay mode
            // actually begins.
            if (!inReplay)
            {
                inReplay = true;
                emit replayStateChanged(true);
            }
            


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

                // Replay has ended, so normal minimap updates can resume.
                emit replayStateChanged(false);
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

    // --------------------------------------------------
// Decide whether the normal round timer is still active.
// --------------------------------------------------
//
// The Spike timer should only be allowed to start during
// an active Valorant round.
//
// This also prevents caster/intermission scenes from
// accidentally triggering the Spike detector after the
// round timer has reached 0:00 or has not been detected.

    int estimatedRoundSeconds = 0;

    if (timerAnchorValid)
    {
        double currentPlaybackSeconds =
            currentVideoPos.load() / 1000.0;

        int elapsedRoundSeconds =
            static_cast<int>(
                currentPlaybackSeconds -
                timerAnchorVideoSeconds
                );

        if (elapsedRoundSeconds < 0)
        {
            elapsedRoundSeconds = 0;
        }

        estimatedRoundSeconds =
            timerAnchorSeconds -
            elapsedRoundSeconds;
    }

    // Check how long it has been since the normal round timer
    // was actually visible on the broadcast.
    double currentPlaybackSeconds =
        currentVideoPos.load() / 1000.0;

    double secondsSinceTimerVisible =
        currentPlaybackSeconds -
        lastRoundTimerVisibleVideoSeconds;

    // Consider the gameplay HUD active only if the round timer
    // was physically visible within the last 2 seconds.
    //
    // The short grace period is important because the normal
    // round timer disappears immediately when the Spike is planted.
    bool roundTimerRecentlyVisible =
        lastRoundTimerVisibleVideoSeconds >= 0.0 &&
        secondsSinceTimerVisible >= 0.0 &&
        secondsSinceTimerVisible <= 2.0;

    // Once OCR has established the round timer, trust the internal
// countdown based on video playback time.
//
// Do not require Tesseract to keep rereading the clock because
// some broadcasts make the timer difficult to recognize even
// though the round is still actively being played.
    bool roundTimerActive =
        timerAnchorValid &&
        estimatedRoundSeconds > 0;

    // Once Spike confirmation has started during active gameplay,
    // allow it to finish even if the normal round timer disappears.
    //
    // This is necessary because Valorant removes the normal round
    // timer as soon as the Spike is planted.
    bool spikeConfirmationInProgress =
        spikePlantConfirmCount > 0 &&
        lastRoundTimerVisibleVideoSeconds >= 0.0 &&
        secondsSinceTimerVisible <= 4.0;

    bool spikeDetectionAllowed =
        roundTimerActive ||
        spikeConfirmationInProgress;


    // --- SPIKE PLANT DETECTION ---
//
// The Spike icon below the round timer is white before planting.
// Once the Spike is planted, that same icon becomes bright red/pink.
//
// We detect the color change instead of using OCR because the icon
// is a much more reliable visual signal.
    if (!inReplay &&
        !spikePlanted &&
        spikeDetectionAllowed)
    {
        cv::Rect spikeRect =
            rel_to_rect(SPIKE_ICON_ROI, img_w, img_h)
            & cv::Rect(0, 0, img_w, img_h);

        if (spikeRect.width > 0 && spikeRect.height > 0)
        {
            cv::Mat spikeRegion =
                frame(spikeRect);
            if (OCR_DEBUG &&
                estimatedRoundSeconds <= 20 &&
                estimatedRoundSeconds >= 10)
            {
                static int spikeRoiDebugCount = 0;

                if (spikeRoiDebugCount < 25)
                {
                    cv::imwrite(
                        "spike_roi_debug_" +
                        std::to_string(spikeRoiDebugCount) +
                        ".png",
                        spikeRegion
                    );

                    cv::Mat fullDebugFrame =
                        frame.clone();

                    cv::rectangle(
                        fullDebugFrame,
                        spikeRect,
                        cv::Scalar(0, 255, 0),
                        3
                    );

                    cv::imwrite(
                        "spike_full_debug_" +
                        std::to_string(spikeRoiDebugCount) +
                        ".png",
                        fullDebugFrame
                    );

                    ++spikeRoiDebugCount;
                }
            }


            // HSV makes it easier to identify red/pink pixels even if
            // brightness changes slightly between broadcast frames.
            cv::Mat hsv;
            cv::cvtColor(
                spikeRegion,
                hsv,
                cv::COLOR_BGR2HSV
            );

            // Red wraps around both ends of OpenCV's hue range,
            // so check both red ranges.
            cv::Mat redMaskLow;
            cv::Mat redMaskHigh;
            cv::Mat redMask;

            cv::inRange(
                hsv,
                cv::Scalar(0, 100, 120),
                cv::Scalar(15, 255, 255),
                redMaskLow
            );

            cv::inRange(
                hsv,
                cv::Scalar(160, 100, 120),
                cv::Scalar(179, 255, 255),
                redMaskHigh
            );

            cv::bitwise_or(
                redMaskLow,
                redMaskHigh,
                redMask
            );

            // Calculate how much of the Spike ROI is red/pink.
            double redPixelRatio =
                static_cast<double>(
                    cv::countNonZero(redMask)
                    )
                /
                static_cast<double>(
                    redMask.total()
                    );

            if (OCR_DEBUG)
            {
                std::cout
                    << "SPIKE RED RATIO -> "
                    << redPixelRatio
                    << "\n";
            }



            // TEMPORARY: disable plant detection while we measure
            // the normal and planted red-pixel ratios.
            bool plantedIconDetected =
                redPixelRatio >= 0.08;

            if (plantedIconDetected)
            {
                ++spikePlantConfirmCount;
            }
            else
            {
                spikePlantConfirmCount = 0;
            }

            // Require four consecutive detections.
            // At the current 500 ms sampling interval, the planted icon
            // must remain visible for about 2 seconds.
            //
            // Short red HUD flashes may briefly enter this region,
            // but the planted Spike icon stays red continuously.
            if (spikePlantConfirmCount >= 4)
            {
                // TEMP DEBUG:
                // Save exactly what the Spike detector saw when it decided
                // that four consecutive red frames meant the Spike was planted.
                cv::imwrite(
                    "spike_trigger_region.png",
                    spikeRegion
                );

                cv::imwrite(
                    "spike_trigger_mask.png",
                    redMask
                );

                // Also save the full frame with the Spike ROI outlined so
                // we can verify that the detector is looking at the correct area.
                cv::Mat spikeDebugFrame =
                    frame.clone();

                cv::rectangle(
                    spikeDebugFrame,
                    spikeRect,
                    cv::Scalar(0, 255, 0),
                    3
                );

                cv::imwrite(
                    "spike_trigger_full_frame.png",
                    spikeDebugFrame
                );

                spikePlanted = true;

                // The icon had to remain red for four samples before we
                // accepted the plant. Move the anchor backward by about
                // 1.5 seconds so the countdown begins near the first
                // confirmed red sample instead of after the fourth one.
                spikePlantVideoSeconds =
                    (currentVideoPos.load() / 1000.0) - 1.5;

                spikePlantConfirmCount = 0;

                // For now, notify the GUI that the countdown begins at 45.
                emit spikeTimerChanged(45);

                qDebug(
                    "SPIKE PLANTED detected at video time %.2f",
                    spikePlantVideoSeconds
                );
            }
        }
    }
    
	
    // Cancel a partial Spike confirmation if it has lasted too long
    // after the normal round timer disappeared, or if a replay begins.
    if ((!spikeDetectionAllowed || inReplay) &&
        !spikePlanted)
    {
        spikePlantConfirmCount = 0;
    }
    if (OCR_DEBUG)
    {
        std::cout
            << "SPIKE GATE -> "
            << "timerAnchorValid=" << timerAnchorValid
            << " estimatedRoundSeconds=" << estimatedRoundSeconds
            << " recentlyVisible=" << roundTimerRecentlyVisible
            << " secondsSinceVisible=" << secondsSinceTimerVisible
            << " confirmCount=" << spikePlantConfirmCount
            << " allowed=" << spikeDetectionAllowed
            << "\n";
    }



    // --- SPIKE COUNTDOWN ---
    //
    // Once the Spike plant has been confirmed, do not rely on image
    // detection anymore. Count down from 45 seconds using the actual
    // Qt video playback position.
    if (spikePlanted)
    {
        double currentPlaybackSeconds =
            currentVideoPos.load() / 1000.0;

        int elapsedSeconds =
            static_cast<int>(
                currentPlaybackSeconds -
                spikePlantVideoSeconds
                );

        if (elapsedSeconds < 0)
        {
            elapsedSeconds = 0;
        }

        int secondsRemaining =
            45 - elapsedSeconds;

        if (secondsRemaining < 0)
        {
            secondsRemaining = 0;
        }

        // Send the current Spike countdown to the GUI.
        emit spikeTimerChanged(secondsRemaining);
    }


}
