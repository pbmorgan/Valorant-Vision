
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <string>
#include <iomanip>
#include <cmath>

namespace fs = std::filesystem;

struct Region {
    cv::Rect rect;
    int pixels;
};

struct AgentMatch {
    std::string agent;
    std::string teamColor;
    fs::path templatePath;
    cv::Rect location;
    int size;
    double faceScore;
    double histogramScore;
    double backgroundScore;
    double combinedScore;
};

// ==========================================
// 1. HSV COLOR HISTOGRAM
// ==========================================

cv::Mat getHistogram(
    const cv::Mat& image,
    const cv::Mat& mask
) {
    cv::Mat hsv;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);

    int channels[] = { 0, 1 };
    int bins[] = { 18, 8 };

    float hRange[] = { 0, 180 };
    float sRange[] = { 0, 256 };
    const float* ranges[] = { hRange, sRange };

    cv::Mat hist;

    cv::calcHist(
        &hsv, 1, channels, mask,
        hist, 2, bins, ranges, true, false
    );

    cv::normalize(hist, hist, 1.0, 0, cv::NORM_L1);
    return hist;
}

// ==========================================
// 2. FACE CORRELATION WITH MASK
// ==========================================

double faceCorrelation(
    const cv::Mat& image,
    const cv::Mat& templ,
    const cv::Mat& mask
) {
    cv::Mat a, b;
    cv::cvtColor(image, a, cv::COLOR_BGR2GRAY);
    cv::cvtColor(templ, b, cv::COLOR_BGR2GRAY);

    cv::Scalar meanA, stdA, meanB, stdB;
    cv::meanStdDev(a, meanA, stdA, mask);
    cv::meanStdDev(b, meanB, stdB, mask);

    if (stdA[0] < 1.0 || stdB[0] < 1.0)
        return 0.0;

    double numerator = 0;
    double denomA = 0;
    double denomB = 0;

    for (int y = 0; y < a.rows; y++) {
        for (int x = 0; x < a.cols; x++) {

            if (mask.at<uchar>(y, x) == 0)
                continue;

            double va = a.at<uchar>(y, x) - meanA[0];
            double vb = b.at<uchar>(y, x) - meanB[0];

            numerator += va * vb;
            denomA += va * va;
            denomB += vb * vb;
        }
    }

    double denom = std::sqrt(denomA * denomB);

    if (denom < 1e-9)
        return 0.0;

    double correlation = numerator / denom;

    // Convert correlation from [-1, 1] into [0, 1]
    return std::clamp(
        (correlation + 1.0) / 2.0,
        0.0, 1.0
    );
}

// ==========================================
// 3. TEAM BACKGROUND COLOR
// ==========================================

double backgroundAgreement(
    const cv::Mat& image,
    const cv::Mat& faceMask,
    const std::string& teamColor
) {
    cv::Mat hsv;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);

    int matching = 0;
    int total = 0;

    for (int y = 0; y < hsv.rows; y++) {
        for (int x = 0; x < hsv.cols; x++) {

            // Only analyze pixels outside the face mask.
            if (faceMask.at<uchar>(y, x) != 0)
                continue;

            cv::Vec3b pixel = hsv.at<cv::Vec3b>(y, x);

            int h = pixel[0];
            int s = pixel[1];
            int v = pixel[2];

            total++;

            if (s < 45 || v < 45)
                continue;

            bool red =
                h <= 12 || h >= 168;

            bool teal =
                h >= 35 && h <= 105;

            if ((teamColor == "red" && red) ||
                (teamColor == "teal" && teal)) {
                matching++;
            }
        }
    }

    if (total == 0)
        return 0.0;

    return static_cast<double>(matching) / total;
}

// ==========================================
// MAIN
// ==========================================

int main(int argc, char* argv[]) {

    fs::path root(PROJECT_ROOT);
    fs::path folder = root / "assist_detection_test";

    fs::path templateRoot =
        root / "assets/agent_templates/team_backgrounds";

    fs::path maskRoot =
        root / "assets/agent_templates/masks/assist";

    fs::path input = folder / "test.png";

    if (argc >= 2) {
        input = fs::path(argv[1]);

        if (!input.is_absolute())
            input = folder / input;
    }

    cv::Mat original = cv::imread(input.string());

    if (original.empty()) {
        std::cerr << "Could not load image: "
            << input << "\n";
        return 1;
    }

    cv::Mat debug = original.clone();

    // Remove stale best-match images.
    for (const std::string& filename : {
        "color_best_crop.png",
        "color_best_reference.png",
        "color_search_region.png"
        }) {
        fs::path path = folder / filename;

        if (fs::exists(path))
            fs::remove(path);
    }







    // ==========================================
    // 4. COLOR-FIRST ASSIST DETECTION
    //    WITH KILLER-PORTRAIT FALLBACK
    // ==========================================

    cv::Mat hsv;
    cv::cvtColor(original, hsv, cv::COLOR_BGR2HSV);

    cv::Mat red1, red2, orange, green;

    cv::inRange(
        hsv, cv::Scalar(0, 65, 55),
        cv::Scalar(10, 255, 255), red1
    );

    cv::inRange(
        hsv, cv::Scalar(170, 65, 55),
        cv::Scalar(179, 255, 255), red2
    );

    cv::inRange(
        hsv, cv::Scalar(10, 65, 55),
        cv::Scalar(25, 255, 255), orange
    );

    cv::inRange(
        hsv, cv::Scalar(35, 55, 45),
        cv::Scalar(90, 255, 255), green
    );

    cv::Mat combinedMask =
        red1 | red2 | orange | green;

    cv::Mat kernel = cv::getStructuringElement(
        cv::MORPH_RECT, cv::Size(3, 3)
    );

    // Reusable contour detector
    auto detectRegions = [&](const cv::Mat& sourceMask) {

        cv::Mat processed;
        cv::morphologyEx(
            sourceMask, processed,
            cv::MORPH_CLOSE, kernel
        );

        std::vector<std::vector<cv::Point>> contours;

        cv::findContours(
            processed.clone(),
            contours,
            cv::RETR_EXTERNAL,
            cv::CHAIN_APPROX_SIMPLE
        );

        std::vector<Region> found;

        for (const auto& contour : contours) {

            cv::Rect rect = cv::boundingRect(contour);
            int pixels = cv::countNonZero(processed(rect));

            if (pixels < 12 ||
                rect.width < 4 ||
                rect.height < 4)
                continue;

            found.push_back({ rect, pixels });
        }

        std::sort(
            found.begin(), found.end(),
            [](const Region& a, const Region& b) {
                return a.rect.x < b.rect.x;
            }
        );

        return found;
        };

    cv::Rect anchor;
    bool possibleAssist = false;
    std::string detectionMethod = "NONE";

    // ==========================================
    // 4A. PRIMARY: COMBINED COLOR DETECTION
    // ==========================================

    std::cout
        << "\n====== PRIMARY COLOR DETECTION ======\n";

    std::vector<Region> primaryRegions =
        detectRegions(combinedMask);

    std::cout << "Regions: "
        << primaryRegions.size() << "\n";

    if (!primaryRegions.empty()) {

        const Region& first = primaryRegions.front();
        cv::Rect rect = first.rect;

        double ratio =
            static_cast<double>(rect.width) /
            rect.height;

        bool valid =
            rect.width >= 14 &&
            rect.width <= 42 &&
            rect.height >= 9 &&
            rect.height <= 26 &&
            ratio >= 0.7 &&
            ratio <= 2.8 &&
            first.pixels >= 25;

        std::cout
            << "Leftmost: x=" << rect.x
            << " y=" << rect.y
            << " width=" << rect.width
            << " height=" << rect.height
            << " pixels=" << first.pixels
            << "\n";

        if (valid) {

            anchor = rect;
            possibleAssist = true;
            detectionMethod = "COLOR";

            std::cout
                << "Primary color detection succeeded.\n";
        }
        else {
            std::cout
                << "Primary color detection failed.\n";
        }
    }

    // ==========================================
    // 4B. FALLBACK: TEAL KILLER PORTRAIT
    // ==========================================

    if (!possibleAssist) {

        std::cout
            << "\n====== FALLBACK DETECTION ======\n";

        std::vector<Region> tealRegions =
            detectRegions(green);

        std::cout
            << "Green regions: "
            << tealRegions.size() << "\n";

        for (size_t i = 0; i < tealRegions.size(); i++) {

            const Region& r = tealRegions[i];

            std::cout
                << i + 1
                << ". x=" << r.rect.x
                << " y=" << r.rect.y
                << " width=" << r.rect.width
                << " height=" << r.rect.height
                << " pixels=" << r.pixels
                << "\n";
        }

        // First find a wide teal bar.
        // This is a layout heuristic, not a
        // general-purpose killer detector.

        cv::Rect tealBar;
        bool barFound = false;

        for (const auto& r : tealRegions) {

            if (r.rect.width >= 100 &&
                r.rect.height >= 25) {

                if (!barFound ||
                    r.rect.width > tealBar.width) {

                    tealBar = r.rect;
                    barFound = true;
                }
            }
        }

        if (barFound) {

            std::cout
                << "\nTeal bar found at x="
                << tealBar.x << "\n";

            // Find the nearest compact green
            // region immediately to the left
            // of the large teal bar.

            cv::Rect killerCandidate;
            bool killerFound = false;

            int bestGap = 999999;

            for (const auto& r : tealRegions) {

                cv::Rect rect = r.rect;

                if (rect.width < 12 ||
                    rect.width > 45 ||
                    rect.height < 9 ||
                    rect.height > 35)
                    continue;

                if (rect.x >= tealBar.x)
                    continue;

                int gap =
                    tealBar.x -
                    (rect.x + rect.width);

                if (gap < 0 || gap > 90)
                    continue;

                if (gap < bestGap) {

                    bestGap = gap;
                    killerCandidate = rect;
                    killerFound = true;
                }
            }

            if (killerFound) {

                std::cout
                    << "Killer portrait candidate:"
                    << " x=" << killerCandidate.x
                    << " y=" << killerCandidate.y
                    << " width=" << killerCandidate.width
                    << " height=" << killerCandidate.height
                    << "\n";

                // Experimental layout offsets.
                // Search left of the estimated
                // killer portrait for the assist.

                cv::Rect estimatedAssist(
                    killerCandidate.x - 28,
                    killerCandidate.y - 9,
                    28,
                    24
                );

                estimatedAssist &=
                    cv::Rect(
                        0, 0,
                        original.cols,
                        original.rows
                    );

                if (estimatedAssist.width >= 14 &&
                    estimatedAssist.height >= 14) {

                    anchor = estimatedAssist;
                    possibleAssist = true;

                    detectionMethod =
                        "KILLER_PORTRAIT_FALLBACK";

                    std::cout
                        << "Estimated assist region:"
                        << " x=" << anchor.x
                        << " y=" << anchor.y
                        << " width=" << anchor.width
                        << " height=" << anchor.height
                        << "\n";

                    cv::rectangle(
                        debug, killerCandidate,
                        cv::Scalar(255, 255, 0), 2
                    );
                }
            }
            else {
                std::cout
                    << "No suitable killer portrait "
                    << "candidate found.\n";
            }
        }
        else {
            std::cout
                << "No wide teal bar detected.\n";
        }
    }

    // ==========================================
    // 4C. FINAL LOCALIZATION RESULT
    // ==========================================

    std::cout
        << "\n====== DETECTION RESULT ======\n";

    if (!possibleAssist) {

        std::cout
            << "NO ASSIST CANDIDATE\n";

        cv::imwrite(
            (folder / "color_match_result.png").string(),
            debug
        );

        return 0;
    }

    std::cout
        << "POSSIBLE ASSIST DETECTED\n"
        << "Method: " << detectionMethod << "\n"
        << "Anchor: (" << anchor.x
        << ", " << anchor.y << ") "
        << anchor.width << "x"
        << anchor.height << "\n";

    if (detectionMethod ==
        "KILLER_PORTRAIT_FALLBACK") {

        std::cout
            << "WARNING: Fallback is experimental; "
            << "assist presence is unconfirmed.\n";
    }

    cv::rectangle(
        debug, anchor,
        cv::Scalar(0, 255, 0), 2
    );



    // ==========================================
    // 5. SEARCH REGION
    // ==========================================

    cv::Rect searchRect(
        anchor.x - 12,
        anchor.y - 10,
        anchor.width + 28,
        anchor.height + 20
    );

    searchRect &= cv::Rect(
        0, 0, original.cols, original.rows
    );

    cv::Mat searchImage =
        original(searchRect).clone();

    cv::imwrite(
        (folder / "color_search_region.png").string(),
        searchImage
    );

    cv::rectangle(
        debug, searchRect,
        cv::Scalar(0, 255, 255), 1
    );

    // ==========================================
    // 6. LOAD RED AND TEAL TEMPLATES
    // ==========================================
    // ==========================================

    std::vector<std::string> colors = {
    "teal"
    };

    std::vector<int> sizes = {
     24
    };

    std::vector<AgentMatch> matches;

    int loaded = 0;
    int comparisons = 0;

    for (const auto& color : colors) {
        for (int size : sizes) {

            std::string sizeName =
                std::to_string(size) + "x" +
                std::to_string(size);

            fs::path directory =
                templateRoot / color / "assist" / sizeName;

            fs::path maskDirectory =
                maskRoot / sizeName;

            if (!fs::exists(directory))
                continue;

            for (const auto& entry :
                fs::directory_iterator(directory)) {

                if (!entry.is_regular_file() ||
                    entry.path().extension() != ".png")
                    continue;

                std::string agent =
                    entry.path().stem().string();

                cv::Mat templ = cv::imread(
                    entry.path().string(),
                    cv::IMREAD_COLOR
                );

                if (templ.empty())
                    continue;

                loaded++;

                if (templ.cols > searchImage.cols ||
                    templ.rows > searchImage.rows)
                    continue;

                // Load the agent face mask.
                fs::path maskPath =
                    maskDirectory / entry.path().filename();

                cv::Mat faceMask = cv::imread(
                    maskPath.string(),
                    cv::IMREAD_GRAYSCALE
                );

                if (!faceMask.empty() &&
                    faceMask.size() == templ.size()) {

                    cv::threshold(
                        faceMask, faceMask,
                        127, 255, cv::THRESH_BINARY
                    );
                }

                // If the mask is missing or unusable,
                // use a central ellipse as a fallback.
                int active = faceMask.empty()
                    ? 0 : cv::countNonZero(faceMask);

                int total = templ.rows * templ.cols;

                if (active < total * 0.15 ||
                    active > total * 0.90) {

                    faceMask = cv::Mat::zeros(
                        templ.size(), CV_8UC1
                    );

                    cv::ellipse(
                        faceMask,
                        cv::Point(
                            templ.cols / 2,
                            templ.rows / 2
                        ),
                        cv::Size(
                            std::max(1, templ.cols / 3),
                            std::max(1, templ.rows / 3)
                        ),
                        0, 0, 360,
                        cv::Scalar(255), -1
                    );
                }

                cv::Mat templateHist =
                    getHistogram(templ, faceMask);

                AgentMatch best;
                best.agent = agent;
                best.teamColor = color;
                best.templatePath = entry.path();
                best.size = size;
                best.combinedScore = -1.0;

                // ==================================
                // 7. TEST NEARBY POSITIONS
                // ==================================

                for (int y = 0;
                    y + templ.rows <= searchImage.rows;
                    y += 2) {

                    for (int x = 0;
                        x + templ.cols <= searchImage.cols;
                        x += 2) {

                        cv::Rect local(
                            x, y, templ.cols, templ.rows
                        );

                        cv::Rect global(
                            searchRect.x + x,
                            searchRect.y + y,
                            templ.cols, templ.rows
                        );

                        double overlap =
                            static_cast<double>(
                                (global & anchor).area()
                                ) / global.area();

                        if (overlap < 0.35)
                            continue;

                        cv::Mat candidate =
                            searchImage(local);

                        // A. Face structure
                        double faceScore =
                            faceCorrelation(
                                candidate, templ, faceMask
                            );

                        // B. Face color distribution
                        cv::Mat candidateHist =
                            getHistogram(
                                candidate, faceMask
                            );

                        double distance =
                            cv::compareHist(
                                templateHist,
                                candidateHist,
                                cv::HISTCMP_BHATTACHARYYA
                            );

                        double histScore =
                            std::clamp(
                                1.0 - distance, 0.0, 1.0
                            );

                        // C. Team background color
                        double bgScore =
                            backgroundAgreement(
                                candidate,
                                faceMask,
                                color
                            );

                        // Experimental weights.
                        // The agent's face has most influence.
                        double combined =
                            0.60 * faceScore +
                            0.15 * histScore +
                            0.25 * bgScore;

                        comparisons++;

                        if (combined > best.combinedScore) {

                            best.faceScore = faceScore;
                            best.histogramScore = histScore;
                            best.backgroundScore = bgScore;
                            best.combinedScore = combined;
                            best.location = global;
                        }
                    }
                }

                if (best.combinedScore < 0)
                    continue;

                // Keep the best size/team combination
                // for each agent.
                auto existing = std::find_if(
                    matches.begin(), matches.end(),
                    [&](const AgentMatch& m) {
                        return m.agent == agent;
                    }
                );

                if (existing == matches.end()) {
                    matches.push_back(best);
                }
                else if (
                    best.combinedScore >
                    existing->combinedScore
                    ) {
                    *existing = best;
                }
            }
        }
    }

    // ==========================================
    // 8. RANK RESULTS
    // ==========================================

    std::sort(
        matches.begin(), matches.end(),
        [](const AgentMatch& a, const AgentMatch& b) {
            return a.combinedScore > b.combinedScore;
        }
    );

    std::cout
        << "\nTemplates loaded: " << loaded
        << "\nComparisons: " << comparisons
        << "\n";

    std::cout
        << "\n====== TOP AGENT MATCHES ======\n";

    int count = std::min(
        5, static_cast<int>(matches.size())
    );

    for (int i = 0; i < count; i++) {

        const auto& m = matches[i];

        std::cout
            << i + 1 << ". "
            << std::setw(12) << std::left
            << m.agent
            << " Team: " << m.teamColor
            << " Size: " << m.size
            << " Face: " << std::fixed
            << std::setprecision(4)
            << m.faceScore
            << " Hist: " << m.histogramScore
            << " BG: " << m.backgroundScore
            << " Combined: " << m.combinedScore
            << "\n";
    }

    std::cout << "\n====== FADE DIAGNOSTICS ======\n";

    for (size_t i = 0; i < matches.size(); i++) {
        if (matches[i].agent == "fade") {
            const auto& m = matches[i];

            std::cout
                << "Fade rank: " << i + 1
                << " / " << matches.size()
                << "\nTeam: " << m.teamColor
                << "\nMatch location: x=" << m.location.x
                << " y=" << m.location.y
                << "\nFace: " << m.faceScore
                << "\nHistogram: " << m.histogramScore
                << "\nBackground: " << m.backgroundScore
                << "\nCombined: " << m.combinedScore
                << "\n";

            break;
        }
    }
    std::cout << "\n====== SAGE DIAGNOSTICS ======\n";

    for (size_t i = 0; i < matches.size(); i++) {
        if (matches[i].agent == "sage") {

            const auto& m = matches[i];

            std::cout
                << "Rank: " << i + 1
                << "\nTeam: " << m.teamColor
                << "\nMatch location: x=" << m.location.x
                << " y=" << m.location.y
                << "\nWidth: " << m.location.width
                << " Height: " << m.location.height
                << "\nFace: " << m.faceScore
                << "\nHistogram: " << m.histogramScore
                << "\nBackground: " << m.backgroundScore
                << "\nCombined: " << m.combinedScore
                << "\n";

            cv::imwrite(
                (folder / "sage_best_crop.png").string(),
                original(m.location)
            );

            cv::Mat reference =
                cv::imread(m.templatePath.string());

            if (!reference.empty()) {
                cv::imwrite(
                    (folder / "sage_reference.png").string(),
                    reference
                );
            }

            break;
        }
    }

    // ==========================================
    // 9. ASTRA DIAGNOSTICS
    // ==========================================

    std::cout
        << "\n====== ASTRA DIAGNOSTICS ======\n";

    bool astraFound = false;

    for (size_t i = 0; i < matches.size(); i++) {

        if (matches[i].agent == "astra") {

            const auto& m = matches[i];

            astraFound = true;

            std::cout
                << "Astra rank: " << i + 1
                << " / " << matches.size()
                << "\nTeam: " << m.teamColor
                << "\nTemplate size: " << m.size
                << "\nFace score: " << m.faceScore
                << "\nHistogram score: "
                << m.histogramScore
                << "\nBackground score: "
                << m.backgroundScore
                << "\nCombined score: "
                << m.combinedScore
                << "\n";
            break;
        }
    }

    if (!astraFound)
        std::cout << "Astra not matched.\n";

    // ==========================================
    // 10. SAVE WINNING MATCH
    // ==========================================

    if (!matches.empty()) {

        const auto& best = matches.front();

        cv::rectangle(
            debug,
            best.location,
            cv::Scalar(255, 0, 255),
            2
        );

        cv::imwrite(
            (folder / "color_best_crop.png").string(),
            original(best.location)
        );

        cv::Mat reference = cv::imread(
            best.templatePath.string()
        );

        if (!reference.empty()) {
            cv::imwrite(
                (folder / "color_best_reference.png").string(),
                reference
            );
        }

        std::cout
            << "\nBEST MATCH: " << best.agent
            << "\nTemplate team: " << best.teamColor
            << "\nCombined score: "
            << best.combinedScore
            << "\n";

        if (matches.size() >= 2) {
            std::cout
                << "Margin: "
                << matches[0].combinedScore -
                matches[1].combinedScore
                << "\n";
        }
    }

    cv::imwrite(
        (folder / "color_match_result.png").string(),
        debug
    );

    std::cout
        << "\nTest complete.\n"
        << "WARNING: Matches are experimental.\n"
        << "No assists assigned.\n";

    return 0;
}
