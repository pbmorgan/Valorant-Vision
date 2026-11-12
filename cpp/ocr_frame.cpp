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

void OCRDetector::processFrame(const cv::Mat& frame)
{
    if (frame.empty() || !tessOk) return;

    // Hold the tess mutex for the whole frame so the destructor can't End()
    // tesseract out from under SetImage/GetUTF8Text mid-call.
    std::lock_guard<std::mutex> lk(tessMutex);
    if (!tessOk || shuttingDown.load()) return;

    // Give this processed frame a unique ID.
    // Killer and victim observations from this frame
    // will share the same ID.
    const qint64 frameId = ++processedFrameCounter;

    double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    int img_w = frame.cols, img_h = frame.rows;

    // Each stage operates on the same frame and OCR state, in the same order.
    // The mutex remains held across all stages as it was before the refactor.
    processReplayAndSpike(frame, img_w, img_h);
    processTeamDetection(frame, img_w, img_h);
    processScores(frame, img_w, img_h, now);
    processRoundTimer(frame, img_w, img_h);
    processKillfeedRows(frame, img_w, img_h, frameId);
}
