// Standalone starter example for C++17 + OpenCV. It matches an already localized
// kill-feed portrait ROI. It does NOT perform OCR, track assists, or map names.
#include <opencv2/opencv.hpp>
#include <filesystem>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <iomanip>

namespace fs = std::filesystem;

struct Candidate {
    std::string agent;
    double score;
    cv::Point location;  // local within selected ROI
};

int main(int argc, char** argv) {
    if (argc != 8) {
        std::cerr << "Usage: " << argv[0]
                  << " screenshot.png templates/main/48x32 x y width height\n";
        return 1;
    }
    cv::Mat img = cv::imread(argv[1], cv::IMREAD_COLOR);
    if (img.empty()) { std::cerr << "Could not open frame\n"; return 1; }
    fs::path folder(argv[2]);
    const cv::Rect region(std::stoi(argv[3]), std::stoi(argv[4]),
                          std::stoi(argv[5]), std::stoi(argv[6]));
    if (region.width <= 0 || region.height <= 0 ||
        (region & cv::Rect(0,0,img.cols,img.rows)) != region) {
        std::cerr << "Invalid region\n"; return 1;
    }
    cv::Mat roi = img(region);
    cv::Mat roiGray;
    cv::cvtColor(roi,roiGray,cv::COLOR_BGR2GRAY);
    std::vector<Candidate> candidates;
    for (const auto& entry : fs::directory_iterator(folder)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".png") continue;
        cv::Mat src = cv::imread(entry.path().string(),cv::IMREAD_UNCHANGED);
        if (src.empty() || src.channels() != 4 || src.cols > roi.cols || src.rows > roi.rows) continue;
        std::vector<cv::Mat> channels;
        cv::split(src,channels); // BGRA = [B,G,R,A]
        cv::Mat templGray;
        cv::cvtColor(src,templGray,cv::COLOR_BGRA2GRAY);
        cv::Mat mask = channels[3];
        // OpenCV supports mask with TM_CCORR_NORMED.
        cv::Mat corr;
        cv::matchTemplate(roiGray,templGray,corr,cv::TM_CCORR_NORMED,mask);
        cv::patchNaNs(corr,0.0);
        double maxScore;
        cv::Point maxPoint;
        cv::minMaxLoc(corr,nullptr,&maxScore,nullptr,&maxPoint);
        if (std::isfinite(maxScore)) {
            candidates.push_back({entry.path().stem().string(), maxScore, maxPoint});
        }
    }
    std::sort(candidates.begin(),candidates.end(),
              [](const Candidate& a,const Candidate& b){ return a.score>b.score; });
    for (size_t i=0; i<std::min(size_t(5),candidates.size()); i++) {
        const auto& c = candidates[i];
        std::cout << std::setw(2) << i+1 << ". " << c.agent
                  << " score=" << std::fixed << std::setprecision(4) << c.score
                  << " match_at=(" << c.location.x+region.x
                  << "," << c.location.y+region.y << ")\n";
    }
    if (candidates.empty()) std::cout << "No candidates: check folder / ROI / size.\n";
    return candidates.empty() ? 2 : 0;
}
