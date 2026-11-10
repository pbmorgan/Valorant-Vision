#include "../headers/assistcandidatematcher.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace fs = std::filesystem;

// ---------------------------------------------------------
// CALCULATE HSV COLOR HISTOGRAM
// ---------------------------------------------------------

static cv::Mat getAssistHistogram(
    const cv::Mat& image,
    const cv::Mat& mask)
{
    cv::Mat hsv;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);

    int channels[] = { 0, 1 };
    int bins[] = { 18, 8 };

    float hRange[] = { 0, 180 };
    float sRange[] = { 0, 256 };

    const float* ranges[] = { hRange, sRange };

    cv::Mat histogram;

    cv::calcHist(
        &hsv,
        1,
        channels,
        mask,
        histogram,
        2,
        bins,
        ranges,
        true,
        false
    );

    cv::normalize(
        histogram,
        histogram,
        1.0,
        0,
        cv::NORM_L1
    );

    return histogram;
}

// ---------------------------------------------------------
// LOAD ASSIST AGENT TEMPLATES
// ---------------------------------------------------------

bool AssistCandidateMatcher::load(
    const fs::path& assetsRoot)
{
    templates_.clear();

    for (const std::string color : { "red", "teal" })
    {
        fs::path templateDirectory =
            assetsRoot / "team_backgrounds"
            / color / "assist" / "24x24";

        fs::path maskDirectory =
            assetsRoot / "masks" / "assist" / "24x24";

        if (!fs::is_directory(templateDirectory) ||
            !fs::is_directory(maskDirectory))
        {
            std::cerr
                << "Missing assist template directory: "
                << templateDirectory.string()
                << std::endl;

            templates_.clear();
            return false;
        }

        for (const auto& entry :
            fs::directory_iterator(templateDirectory))
        {
            if (!entry.is_regular_file() ||
                entry.path().extension() != ".png")
            {
                continue;
            }

            std::string agent =
                entry.path().stem().string();

            cv::Mat image =
                cv::imread(entry.path().string(),
                    cv::IMREAD_COLOR);

            cv::Mat mask =
                cv::imread(
                    (maskDirectory / entry.path().filename()).string(),
                    cv::IMREAD_GRAYSCALE
                );

            if (image.empty() ||
                mask.empty() ||
                image.size() != mask.size() ||
                image.size() != cv::Size(24, 24))
            {
                std::cerr
                    << "Skipping invalid assist template: "
                    << agent << std::endl;

                continue;
            }

            cv::threshold(
                mask, mask,
                127, 255,
                cv::THRESH_BINARY
            );

            if (cv::countNonZero(mask) == 0)
            {
                continue;
            }

            cv::Mat histogram =
                getAssistHistogram(image, mask);

            templates_.push_back({
                agent,
                color,
                image,
                mask,
                histogram
                });
        }
    }

    std::cout
        << "Loaded "
        << templates_.size()
        << " assist candidate templates."
        << std::endl;

    // Expect templates from both colors.
    bool hasRed = false;
    bool hasTeal = false;

    for (const auto& item : templates_)
    {
        if (item.teamColor == "red")
            hasRed = true;

        if (item.teamColor == "teal")
            hasTeal = true;
    }

    if (!hasRed || !hasTeal)
    {
        templates_.clear();
        return false;
    }

    return true;
}

// ---------------------------------------------------------
// CHECK WHETHER TEMPLATES ARE LOADED
// ---------------------------------------------------------

bool AssistCandidateMatcher::isReady() const
{
    return !templates_.empty();
}


// ---------------------------------------------------------
// COMPARE AGENT FACE STRUCTURE
// ---------------------------------------------------------

static double getAssistFaceCorrelation(
    const cv::Mat& image,
    const cv::Mat& templ,
    const cv::Mat& mask)
{
    // Verify that the images and mask are compatible.
    if (image.empty() ||
        templ.empty() ||
        mask.empty() ||
        image.size() != templ.size() ||
        image.size() != mask.size() ||
        mask.type() != CV_8UC1)
    {
        return 0.0;
    }

    // Convert both images to grayscale.
    cv::Mat grayImage;
    cv::Mat grayTemplate;

    cv::cvtColor(
        image,
        grayImage,
        cv::COLOR_BGR2GRAY
    );

    cv::cvtColor(
        templ,
        grayTemplate,
        cv::COLOR_BGR2GRAY
    );

    // Calculate the mean and standard deviation
    // using only pixels inside the face mask.
    cv::Scalar meanImage;
    cv::Scalar stdImage;

    cv::Scalar meanTemplate;
    cv::Scalar stdTemplate;

    cv::meanStdDev(
        grayImage,
        meanImage,
        stdImage,
        mask
    );

    cv::meanStdDev(
        grayTemplate,
        meanTemplate,
        stdTemplate,
        mask
    );

    // Avoid comparing images with almost no variation.
    if (stdImage[0] < 1.0 ||
        stdTemplate[0] < 1.0)
    {
        return 0.0;
    }

    double numerator = 0.0;
    double denominatorImage = 0.0;
    double denominatorTemplate = 0.0;

    // Compare corresponding pixels inside the face mask.
    for (int y = 0; y < grayImage.rows; ++y)
    {
        for (int x = 0; x < grayImage.cols; ++x)
        {
            if (mask.at<uchar>(y, x) == 0)
            {
                continue;
            }

            double a =
                grayImage.at<uchar>(y, x) -
                meanImage[0];

            double b =
                grayTemplate.at<uchar>(y, x) -
                meanTemplate[0];

            numerator += a * b;

            denominatorImage += a * a;
            denominatorTemplate += b * b;
        }
    }

    double denominator =
        std::sqrt(
            denominatorImage * denominatorTemplate
        );

    if (denominator < 1e-9)
    {
        return 0.0;
    }

    double correlation =
        numerator / denominator;

    // Convert correlation from [-1, 1] to [0, 1].
    return std::clamp(
        (correlation + 1.0) / 2.0,
        0.0,
        1.0
    );
}

// ---------------------------------------------------------
// COMPARE ASSIST PORTRAIT BACKGROUND COLOR
// ---------------------------------------------------------

static double getAssistBackgroundAgreement(
    const cv::Mat& image,
    const cv::Mat& faceMask,
    const std::string& teamColor)
{
    if (image.empty() ||
        faceMask.empty() ||
        image.size() != faceMask.size())
    {
        return 0.0;
    }

    // Convert the image to HSV color space.
    cv::Mat hsv;

    cv::cvtColor(
        image,
        hsv,
        cv::COLOR_BGR2HSV
    );

    int matchingPixels = 0;
    int totalPixels = 0;

    for (int y = 0; y < hsv.rows; ++y)
    {
        for (int x = 0; x < hsv.cols; ++x)
        {
            // Ignore pixels belonging to the agent's face.
            // We only want to examine the background.
            if (faceMask.at<uchar>(y, x) != 0)
            {
                continue;
            }

            cv::Vec3b pixel =
                hsv.at<cv::Vec3b>(y, x);

            int hue = pixel[0];
            int saturation = pixel[1];
            int brightness = pixel[2];

            ++totalPixels;

            // Ignore pixels that are too dark
            // or have very little color.
            if (saturation < 45 ||
                brightness < 45)
            {
                continue;
            }

            // Red appears at both ends of the HSV hue range.
            bool isRed =
                hue <= 12 || hue >= 168;

            // Teal/green occupies this approximate hue range.
            bool isTeal =
                hue >= 35 && hue <= 105;

            if ((teamColor == "red" && isRed) ||
                (teamColor == "teal" && isTeal))
            {
                ++matchingPixels;
            }
        }
    }

    if (totalPixels == 0)
    {
        return 0.0;
    }

    // Return the fraction of background pixels
    // that agree with the expected team color.
    return static_cast<double>(matchingPixels)
        / static_cast<double>(totalPixels);
}

// ---------------------------------------------------------
// RANK TOP FIVE POSSIBLE ASSISTING AGENTS
// ---------------------------------------------------------

std::vector<RankedAssistAgent>
AssistCandidateMatcher::rankCandidates(
    const cv::Mat& killfeedRow,
    const cv::Rect& assistRegion,
    const std::string& templateColor) const
{
    std::vector<RankedAssistAgent> results;

    // Make sure we have a valid image and loaded templates.
    if (killfeedRow.empty() || !isReady())
    {
        return results;
    }

    // Only accept colors we have templates for.
    if (templateColor != "red" &&
        templateColor != "teal")
    {
        return results;
    }

    // Image boundaries.
    cv::Rect imageBounds(
        0, 0,
        killfeedRow.cols,
        killfeedRow.rows
    );

    // Keep the suspected assist region inside the image.
    cv::Rect anchor =
        assistRegion & imageBounds;

    if (anchor.empty())
    {
        return results;
    }

    // Expand the search area around the suspected icon.
    // These offsets come from our experimental detector.
    cv::Rect searchRect(
        anchor.x - 12,
        anchor.y - 10,
        anchor.width + 28,
        anchor.height + 20
    );

    searchRect &= imageBounds;

    // Our templates are 24x24 pixels.
    if (searchRect.width < 24 ||
        searchRect.height < 24)
    {
        return results;
    }

    cv::Mat searchImage =
        killfeedRow(searchRect);

    // Compare the detected region against every agent.
    for (const AssistTemplate& templ : templates_)
    {
        // Only compare templates with the selected
        // killfeed background color.
        if (templ.teamColor != templateColor)
        {
            continue;
        }

        double bestScore = -1.0;

        // Try nearby positions to account for
        // slight inaccuracies in icon localization.
        for (int y = 0;
            y + templ.image.rows <= searchImage.rows;
            y += 2)
        {
            for (int x = 0;
                x + templ.image.cols <= searchImage.cols;
                x += 2)
            {
                cv::Rect local(
                    x,
                    y,
                    templ.image.cols,
                    templ.image.rows
                );

                // Convert the position back into
                // killfeed-row coordinates.
                cv::Rect global(
                    searchRect.x + x,
                    searchRect.y + y,
                    templ.image.cols,
                    templ.image.rows
                );

                // Require the comparison window to overlap
                // the suspected assist region.
                double overlap =
                    static_cast<double>(
                        (global & anchor).area()
                        ) / static_cast<double>(global.area());

                if (overlap < 0.35)
                {
                    continue;
                }

                cv::Mat candidate =
                    searchImage(local);

                // 1. Compare facial structure.
                double faceScore =
                    getAssistFaceCorrelation(
                        candidate,
                        templ.image,
                        templ.mask
                    );

                // 2. Compare HSV color histograms.
                cv::Mat candidateHistogram =
                    getAssistHistogram(
                        candidate,
                        templ.mask
                    );

                double histogramDistance =
                    cv::compareHist(
                        templ.histogram,
                        candidateHistogram,
                        cv::HISTCMP_BHATTACHARYYA
                    );

                double histogramScore =
                    std::clamp(
                        1.0 - histogramDistance,
                        0.0,
                        1.0
                    );

                // 3. Compare team background color.
                double backgroundScore =
                    getAssistBackgroundAgreement(
                        candidate,
                        templ.mask,
                        templateColor
                    );

                // Final weighted similarity score.
                double combinedScore =
                    0.60 * faceScore +
                    0.15 * histogramScore +
                    0.25 * backgroundScore;

                if (!std::isfinite(combinedScore))
                {
                    continue;
                }

                // Keep the highest-scoring position
                // for this particular agent.
                if (combinedScore > bestScore)
                {
                    bestScore = combinedScore;
                }
            }
        }

        // Add this agent if at least one valid
        // comparison was performed.
        if (bestScore >= 0.0)
        {
            results.push_back({
                templ.agent,
                bestScore,
                0
                });
        }
    }

    // Sort from highest score to lowest score.
    std::sort(
        results.begin(),
        results.end(),
        [](const RankedAssistAgent& a,
            const RankedAssistAgent& b)
        {
            if (a.score != b.score)
            {
                return a.score > b.score;
            }

            return a.agent < b.agent;
        }
    );

    // Keep no more than five possible agents.
    if (results.size() > 5)
    {
        results.resize(5);
    }

    // Assign ranks beginning at 1.
    for (std::size_t i = 0; i < results.size(); ++i)
    {
        results[i].rank =
            static_cast<int>(i) + 1;
    }

    return results;
}






