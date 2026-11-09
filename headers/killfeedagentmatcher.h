#ifndef KILLFEED_AGENT_MATCHER_H
#define KILLFEED_AGENT_MATCHER_H

#include <opencv2/core.hpp>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// A single best match within the supplied search crop.
// Scores measure image similarity, not probabilities.
struct AgentIconMatch
{
    std::string agent;
    double score = -1.0;
    double secondBestScore = -1.0;  // best score from a DIFFERENT agent
    cv::Rect box;                   // position within the search crop

    bool hasCandidate() const { return !agent.empty(); }
    double margin() const { return score - secondBestScore; }
};

class KillfeedAgentMatcher
{
public:
    // root must contain templates/{main,assist} and masks/{main,assist}.
    bool load(const std::filesystem::path& root,
              const std::string& mainSize = "48x32",
              const std::string& assistSize = "20x20");

    AgentIconMatch matchMain(const cv::Mat& searchCrop) const;
    AgentIconMatch matchAssist(const cv::Mat& searchCrop) const;

    std::size_t mainCount() const { return mainTemplates_.size(); }
    std::size_t assistCount() const { return assistTemplates_.size(); }

private:
    struct IconTemplate
    {
        std::string agent;
        cv::Mat bgr;            // blurred, color-preserving agent portrait
        cv::Mat flippedBgr;     // same portrait mirrored horizontally
        cv::Mat mask;           // transparency mask for the original
        cv::Mat flippedMask;    // transparency mask for the mirrored image
    };

    std::vector<IconTemplate> mainTemplates_;
    std::vector<IconTemplate> assistTemplates_;

    static std::vector<IconTemplate> loadGroup(
        const std::filesystem::path& root,
        const std::string& group,
        const std::string& size);

    static AgentIconMatch match(const cv::Mat& crop,
                                const std::vector<IconTemplate>& templates);
};

#endif // KILLFEED_AGENT_MATCHER_H
