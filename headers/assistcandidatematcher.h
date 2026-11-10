
#ifndef ASSIST_CANDIDATE_MATCHER_H
#define ASSIST_CANDIDATE_MATCHER_H

#include <opencv2/core.hpp>

#include <filesystem>
#include <string>
#include <vector>

// One ranked prediction for an assisting agent.
struct RankedAssistAgent
{
    std::string agent;
    double score = 0.0;
    int rank = 0;
};

// Matches a suspected assist icon against agent templates.
// This class does NOT award assists to players.
class AssistCandidateMatcher
{
public:

    // Load the red and teal 24x24 agent templates
    // and their corresponding face masks.
    bool load(const std::filesystem::path& assetsRoot);

    // Returns whether templates were loaded successfully.
    bool isReady() const;

    // Rank up to five agents for an already-localized
    // suspected assist icon.
    //
    // Coordinates are relative to the supplied killfeed row.
    // This function does not establish assist presence.
    std::vector<RankedAssistAgent> rankCandidates(
        const cv::Mat& killfeedRow,
        const cv::Rect& assistRegion,
        const std::string& templateColor
    ) const;

private:

    struct AssistTemplate
    {
        std::string agent;
        std::string teamColor;

        cv::Mat image;
        cv::Mat mask;
        cv::Mat histogram;
    };

    // Cached templates, loaded once.
    std::vector<AssistTemplate> templates_;
};

#endif // ASSIST_CANDIDATE_MATCHER_H