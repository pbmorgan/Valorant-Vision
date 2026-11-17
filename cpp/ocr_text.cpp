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

std::string OCRDetector::normalize(const std::string& s)
{
    std::string out;
    for (char c : s)
        if (!std::isspace((unsigned char)c))
            out.push_back(std::tolower(c));
    return out;
}

std::vector<OCRWordBox> OCRDetector::ocr_killfeed_word_boxes(const cv::Mat& roi)
{
    std::vector<OCRWordBox> results;

    if (roi.empty() || !tessOk)
        return results;


    // Use the SAME preprocessing as our working killfeed OCR.
    // The killfeed has white text over colored backgrounds.

    // Separate the blue, green, and red channels.
    std::vector<cv::Mat> channels;
    cv::split(roi, channels);

    // Keep the minimum brightness across all three channels.
    // White text is bright in every channel.
    cv::Mat minBG, minBGR;
    cv::min(channels[0], channels[1], minBG);
    cv::min(minBG, channels[2], minBGR);

    // Isolate white player-name text.
    cv::Mat whiteMask;
    cv::threshold(
        minBGR,
        whiteMask,
        150,
        255,
        cv::THRESH_BINARY
    );

    // Enlarge the text 3x for Tesseract.
    cv::Mat big;
    cv::resize(
        whiteMask,
        big,
        cv::Size(),
        3.0,
        3.0,
        cv::INTER_CUBIC
    );

    // Convert to black text on a white background.
    cv::bitwise_not(big, big);
    


    


    tess.SetPageSegMode(tesseract::PSM_SINGLE_LINE);

    // Player names can contain letters and numbers.
    tess.SetVariable(
        "tessedit_char_whitelist",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    );

    tess.SetImage(
        big.data,
        big.cols,
        big.rows,
        1,
        static_cast<int>(big.step)
    );

    tess.Recognize(0);


    tesseract::ResultIterator* ri = tess.GetIterator();
    if (!ri)
        return results;


    do
    {
        // Read the current word recognized by Tesseract.
        char* word = ri->GetUTF8Text(tesseract::RIL_WORD);

        // Tesseract may fail to recognize some words.
        // Skip processing the word if the pointer is null.
        if (word != nullptr)
        {
            int x1, y1, x2, y2;

            // Get the pixel coordinates of the recognized word.
            if (ri->BoundingBox(
                tesseract::RIL_WORD,
                &x1, &y1, &x2, &y2))
            {
                std::string text(word);

                if (!text.empty())
                {
                    OCRWordBox wb;

                    // Store the recognized word.
                    wb.text = text;

                    // Convert coordinates back from the
                    // 2x enlarged image to the original size.

                    wb.box = cv::Rect(
                        x1 / 3,
                        y1 / 3,
                        std::max(1, (x2 - x1) / 3),
                        std::max(1, (y2 - y1) / 3)
                    );


                    results.push_back(wb);
                }
            }

            // Free the memory allocated by Tesseract.
            delete[] word;
        }


    } while (ri->Next(tesseract::RIL_WORD));

    // Free the Tesseract result iterator after processing all words.
    // GetIterator() allocates an iterator that we must delete.
    delete ri;

    return results;
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

