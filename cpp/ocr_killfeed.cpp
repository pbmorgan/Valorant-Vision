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

void OCRDetector::processKillfeedRows(const cv::Mat& frame, int img_w, int img_h, qint64 frameId)
{
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

        //
        // TEMP DEBUG: Save PatMen's complete killfeed row
        // before portrait matching or confidence filtering.
        //

        if (!inReplay &&
            (normalize(evt.killer) == "patmen" ||
                normalize(evt.victim) == "patmen"))
        {
            std::filesystem::path debugDir =
                std::filesystem::path(
                    QCoreApplication::applicationDirPath().toStdString()
                ) / "agent_debug" / "patmen";

            std::filesystem::create_directories(debugDir);

            std::string filename =
                "event_" + std::to_string(frameId) +
                "_row_" + std::to_string(row) + ".png";

            cv::imwrite(
                (debugDir / filename).string(),
                frame(roi)
            );

            std::cout
                << "PATMEN EVENT DEBUG | Killer: "
                << evt.killer
                << " | Victim: " << evt.victim
                << " | Saved: " << filename
                << std::endl;
        }




        processKillerObservation(frame, roi, evt, frameId, row, img_w);
        processVictimObservation(frame, roi, evt, frameId);
        saveKillfeedExample(frame, roi, evt);
        emitKillIfNew(evt);
    }
}
