#ifndef MODELWRAPPER_H
#define MODELWRAPPER_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include <QImage>

#include <opencv2/opencv.hpp>

#include "neuralnetwork.h"

class ModelWrapper {

private:

	NeuralNetwork* model = nullptr;

	// Background thread that runs neural-network inference.
	std::thread loopThread;

	// Controls whether the neural-network thread should keep running.
	std::atomic_bool keepThreadAlive{ false };


	// ============================================================
	// LATEST VIDEO FRAME
	// ============================================================

	// Protects access to latestFrame.
	std::mutex frameLock;

	// Wakes the neural-network thread when a new frame is available.
	std::condition_variable frameCondition;

	// Stores ONLY the newest frame received from the video player.
	// Old frames are replaced instead of being queued.
	cv::Mat latestFrame;

	// Tells the worker thread whether a frame is waiting to be processed.
	bool frameAvailable = false;


	// ============================================================
	// LATEST NEURAL-NETWORK RESULT
	// ============================================================

	// Protects access to latestResult.
	std::mutex resultLock;

	// Stores the most recently completed neural-network prediction.
	Eigen::MatrixXf latestResult;


	// Worker loop used by the neural-network thread.
	void loop();


public:

	ModelWrapper();
	~ModelWrapper();

	// Starts the neural-network processing thread.
	void startProcessingLoop();

	// Stops the neural-network processing thread.
	void endProcessingLoop();

	// Receives the newest decoded frame directly from Qt's video player.
	// If the neural network is busy, this replaces the older waiting frame.
	void submitFrame(const QImage& frame);

	// Returns the most recently completed neural-network result.
	Eigen::MatrixXf getLatestFrameData();
};

#endif