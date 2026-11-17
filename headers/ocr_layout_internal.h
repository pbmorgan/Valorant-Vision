#pragma once
// Internal OCR broadcast coordinates and debugging switches.
// This is intentionally NOT a QObject header.
#include <opencv2/opencv.hpp>

// Set to true when detailed OCR debugging is needed.
static constexpr bool OCR_DEBUG = true;

// Relative coordinate helpers (calibrated for 1920x1080 broadcast layout)
struct RelRect { double x1, y1, x2, y2; };

static cv::Rect rel_to_rect(const RelRect& r, int w, int h)
{
    return cv::Rect(
        int(r.x1 * w), int(r.y1 * h),
        int((r.x2 - r.x1) * w), int((r.y2 - r.y1) * h)
    );
}

static const RelRect KILLFEED_ROWS[] = {
    { 1291.0/1920,  90.0/1080, 1898.0/1920, 130.0/1080 },
    { 1291.0/1920, 130.0/1080, 1898.0/1920, 170.0/1080 },
    { 1291.0/1920, 170.0/1080, 1898.0/1920, 210.0/1080 },
    { 1291.0/1920, 210.0/1080, 1898.0/1920, 250.0/1080 },
};
static const int KILLFEED_ROW_COUNT = 4;

static const RelRect TEAM_LEFT_ROI   = { 720.0/1920,  10.0/1080,  790.0/1920,  38.0/1080 };
static const RelRect TEAM_RIGHT_ROI  = { 1130.0/1920, 10.0/1080, 1200.0/1920,  38.0/1080 };
static const RelRect SCORE_LEFT_ROI  = { 800.0/1920,   0.0/1080,  860.0/1920,  60.0/1080 };
static const RelRect SCORE_RIGHT_ROI = { 1060.0/1920,  10.0/1080, 1120.0/1920,  60.0/1080 };
static const RelRect ROUND_TIMER_ROI = { 880.0/1920,   0.0/1080, 1010.0/1920,  67.0/1080 };

// Region containing the Spike icon directly below the round timer.
//
// Before the Spike is planted, this icon is mostly white.
// After the Spike is planted, the same icon becomes bright red/pink.
//
// Keeping this ROI small prevents the red team-side scoreboard
// graphics from being mistaken for the planted Spike.
static const RelRect SPIKE_ICON_ROI = {
    920.0 / 1920,
    20.0 / 1080,
    1005.0 / 1920,
    80.0 / 1080
};

// Region containing the broadcast's "REPLAY" indicator.
// When this text is detected, kills, score changes, and timer anchors
// from the replay should not be treated as live game events.
static const RelRect REPLAY_ROI = {
    1560.0 / 1920,
    930.0 / 1080,
    1870.0 / 1920,
    1040.0 / 1080
};

static const int FRAME_INTERVAL_MS = 500;

// A new round timer must be at least this high before
// it can establish a new internal round-timer anchor.
//
// This prevents the pre-round/buy-phase countdown from
// being mistaken for the actual round clock.
static constexpr int MIN_NEW_ROUND_TIMER_SECONDS = 50;

