#include "killfeedagentmatcher.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace
{
    // Both the frame and the template must have the same three color channels.
    // OpenCV convention is BGR (blue, green, red).
    cv::Mat toBgr(const cv::Mat& source)
    {
        cv::Mat bgr;
        if (source.empty() || source.depth() != CV_8U)
            return bgr;

        switch (source.channels())
        {
        case 1:
            cv::cvtColor(source, bgr, cv::COLOR_GRAY2BGR);
            break;
        case 3:
            bgr = source;
            break;
        case 4:
            cv::cvtColor(source, bgr, cv::COLOR_BGRA2BGR);
            break;
        default:
            break;
        }
        return bgr;
    }
}

bool KillfeedAgentMatcher::load(const fs::path& root,
                                const std::string& mainSize,
                                const std::string& assistSize)
{
    auto main = loadGroup(root, "main", mainSize);
    auto assist = loadGroup(root, "assist", assistSize);

    if (main.empty() || assist.empty())
    {
        std::cerr << "Agent template loading failed: " << root.string() << '\n';
        return false;
    }

    mainTemplates_ = std::move(main);
    assistTemplates_ = std::move(assist);
    std::cout << "Loaded " << mainTemplates_.size() << " main and "
              << assistTemplates_.size() << " assist templates (color + mirrored).\n";
    return true;
}

std::vector<KillfeedAgentMatcher::IconTemplate>
KillfeedAgentMatcher::loadGroup(const fs::path& root,
                                const std::string& group,
                                const std::string& size)
{
    std::vector<IconTemplate> templates;
    const fs::path imageDir = root / "templates" / group / size;
    const fs::path maskDir = root / "masks" / group / size;

    std::error_code ec;
    if (!fs::is_directory(imageDir, ec) || !fs::is_directory(maskDir, ec))
    {
        std::cerr << "Missing image/mask directory: "
                  << imageDir.string() << " or " << maskDir.string() << '\n';
        return templates;
    }

    for (const fs::directory_entry& item : fs::directory_iterator(imageDir))
    {
        if (!item.is_regular_file() || item.path().extension() != ".png")
            continue;

        

        const std::string agent = item.path().stem().string();

        const cv::Mat source =
            cv::imread(item.path().string(), cv::IMREAD_UNCHANGED);

        cv::Mat mask =
            cv::imread((maskDir / item.path().filename()).string(),
                cv::IMREAD_GRAYSCALE);

        cv::Mat bgr = toBgr(source);

        // First, verify that the image and mask are valid.
        if (bgr.empty() || mask.empty() || bgr.size() != mask.size())
        {
            std::cerr << "Skipping invalid agent template: "
                << item.path().string() << '\n';
            continue;
        }

        // Experimental: ignore nearly transparent pixels
        // in main agent portrait masks.
        if (group == "main")
        {
            cv::threshold(mask, mask, 127, 255, cv::THRESH_BINARY);
        }

        // Ensure that enough usable pixels remain.
        if (cv::countNonZero(mask) < 8)
        {
            std::cerr << "Skipping agent template with insufficient mask pixels: "
                << item.path().string() << '\n';
            continue;
        }

        // Smooth compression artifacts. We apply the same blur to gameplay crops.
        // This happens once when templates load, not on each frame.
        cv::GaussianBlur(bgr, bgr, cv::Size(3, 3), 0);

        // Broadcast graphics may mirror the original portrait artwork.
        cv::Mat flippedBgr;
        cv::Mat flippedMask;
        cv::flip(bgr, flippedBgr, 1);
        cv::flip(mask, flippedMask, 1);

        templates.push_back({agent, bgr, flippedBgr, mask, flippedMask});
    }

    std::sort(templates.begin(), templates.end(),
              [](const IconTemplate& a, const IconTemplate& b)
              {
                  return a.agent < b.agent;
              });
    return templates;
}


AgentIconMatch KillfeedAgentMatcher::matchMain(const cv::Mat& crop) const
{
    AgentIconMatch result = match(crop, mainTemplates_);

    if (!result.hasCandidate())
    {
        return result;
    }

    // Minimum difference between the best and
    // second-best agent template scores.
    constexpr double MIN_CONFIDENCE_MARGIN = 0.0035;

    // Reject predictions where two agents look
    // almost equally likely to the matcher.
    if (result.margin() < MIN_CONFIDENCE_MARGIN)
    {
        std::cout
            << "AGENT AMBIGUOUS | Best: "
            << result.agent
            << " | Score: " << result.score
            << " | Margin: " << result.margin()
            << std::endl;

        return AgentIconMatch{};
    }

    return result;
}


AgentIconMatch KillfeedAgentMatcher::matchAssist(const cv::Mat& crop) const
{
    return match(crop, assistTemplates_);
}

AgentIconMatch KillfeedAgentMatcher::match(
    const cv::Mat& crop,
    const std::vector<IconTemplate>& templates)
{
    AgentIconMatch best;
    const cv::Mat bgr = toBgr(crop);
    if (bgr.empty())
        return best;

    // Blur gameplay and templates equally before comparing their BGR pixels.
    cv::Mat smoothed;
    cv::GaussianBlur(bgr, smoothed, cv::Size(3, 3), 0);

    for (const IconTemplate& t : templates)
    {
        if (t.bgr.cols > smoothed.cols || t.bgr.rows > smoothed.rows)
            continue;

        // Retain the best orientation for THIS agent. We don't want
        // the same agent's mirrored version to count as second place.
        double agentBestScore = -1.0;
        cv::Rect agentBestBox;

        for (int orientation = 0; orientation < 2; ++orientation)
        {
            const cv::Mat& templ =
                (orientation == 0) ? t.bgr : t.flippedBgr;
            const cv::Mat& mask =
                (orientation == 0) ? t.mask : t.flippedMask;

            cv::Mat similarity;
            cv::matchTemplate(smoothed, templ, similarity,
                              cv::TM_CCORR_NORMED, mask);

            double score = -1.0;
            cv::Point location;
            cv::minMaxLoc(similarity, nullptr, &score, nullptr, &location);
            if (!std::isfinite(score))
                continue;

            if (score > agentBestScore)
            {
                agentBestScore = score;
                agentBestBox = cv::Rect(location, templ.size());
            }
        }

        if (agentBestScore < 0.0)
            continue;

        // TEMPORARY DEBUG: Compare Astra and Sova
        // on main-sized agent templates.
        if (t.bgr.size() == cv::Size(48, 32) &&
            (t.agent == "astra" || t.agent == "sova"))
        {
            std::cout
                << "AGENT SCORE DETAIL | "
                << t.agent
                << " | Score: " << agentBestScore
                << " | Match X: " << agentBestBox.x
                << " | Match Y: " << agentBestBox.y
                << std::endl;
        }

        if (agentBestScore > best.score)
        {
            best.secondBestScore = best.score;
            best.score = agentBestScore;
            best.agent = t.agent;
            best.box = agentBestBox;
        }
        else if (agentBestScore > best.secondBestScore)
        {
            best.secondBestScore = agentBestScore;
        }
    }

    return best;
}
