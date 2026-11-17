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

std::vector<std::string> OCRDetector::extract_tokens(const std::string& raw)
{
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : raw) {
        if (std::isalnum((unsigned char)c)) {
            cur.push_back(c);
        } else if (!cur.empty()) {
            tokens.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

KillEvent OCRDetector::resolve_killfeed_whole(const std::string& raw,
                                              const std::vector<std::string>& players,
                                              bool player_list_valid)
{
    KillEvent evt;
    std::string norm = normalize(raw);
    if (norm.empty()) return evt;

    // Do not trust kill-feed OCR until we know the actual players
    // in the current match. Once the teams are confirmed,
    // setPlayerRecords() will provide the valid player roster.
    if (!player_list_valid)
    {
        return evt;
    }

    struct Found { std::string player; int pos; int len; int dist; };
    std::vector<Found> found;

    for (const auto& p : players) {
        std::string np = normalize(p);
        if (np.empty()) continue;

        int threshold;
        if (np.size() <= 2)      threshold = 0;
        else if (np.size() <= 5) threshold = 1;
        else                     threshold = 2;

        int minLen = std::max(1, (int)np.size() - 1);
        int maxLen = (int)np.size() + 1;
        int N = (int)norm.size();
        if (minLen > N) continue;

        int bestDist = 9999, bestPos = -1, bestLen = (int)np.size();
        for (int start = 0; start + minLen <= N; ++start) {
            for (int wl = minLen; wl <= maxLen && start + wl <= N; ++wl) {
                int d = levenshtein_distance(norm.substr(start, wl), np);
                if (d < bestDist) { bestDist = d; bestPos = start; bestLen = wl; }
                if (bestDist == 0) break;
            }
            if (bestDist == 0) break;
        }

        if (bestDist <= threshold && bestPos >= 0)
            found.push_back({ p, bestPos, bestLen, bestDist });
    }

    if (OCR_DEBUG)
    {
        // TEMP DEBUG:
        // Show which roster players were successfully found inside
        // the raw killfeed OCR text before overlap filtering happens.
        std::cout << "KILL MATCH CANDIDATES -> ";

        for (const auto& f : found)
        {
            std::cout
                << f.player
                << "(dist=" << f.dist
                << ", pos=" << f.pos
                << ") ";
        }

        std::cout << "\n";
    }


    // Greedy dedup: prefer lower distance, drop overlapping hits.
    std::sort(found.begin(), found.end(),
              [](const Found& a, const Found& b) { return a.dist < b.dist; });

    std::vector<Found> kept;
    for (const auto& f : found) {
        bool overlap = false;
        for (const auto& k : kept) {
            int a1 = f.pos, a2 = f.pos + f.len;
            int b1 = k.pos, b2 = k.pos + k.len;
            if (a1 < b2 && b1 < a2) { overlap = true; break; }
        }
        if (!overlap) kept.push_back(f);
        if (kept.size() == 2) break;
    }

    if (kept.size() != 2) return evt;

    // Killfeed layout: killer on left, victim on right.
    std::sort(kept.begin(), kept.end(),
              [](const Found& a, const Found& b) { return a.pos < b.pos; });

    evt.killer = kept.front().player;
    evt.victim = kept.back().player;
    if (evt.killer == evt.victim) { evt.killer.clear(); evt.victim.clear(); }
    return evt;
}
