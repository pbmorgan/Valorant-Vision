#include "modelwrapper.h"

#include <iostream>

ModelWrapper::ModelWrapper()
{
	// The neural network is loaded when startProcessingLoop() is called.
}

ModelWrapper::~ModelWrapper()
{
	// Stop the worker thread before deleting the neural network.
	endProcessingLoop();

	delete model;
	model = nullptr;
}


// ============================================================
// NEURAL-NETWORK WORKER THREAD
// ============================================================

void ModelWrapper::loop()
{
	// Percentages of the full video frame that contain
	// the Valorant minimap.
	const float LEFT_SIDE = 0.024f;
	const float RIGHT_SIDE = 0.24f;
	const float TOP_SIDE = 0.04f;
	const float BOTTOM_SIDE = 0.43f;

	std::cout << "[ModelWrapper] Neural-network thread started."
		<< std::endl;

	while (keepThreadAlive)
	{
		cv::Mat frame;

		{
			// Wait until either:
			// 1. A new video frame is available, or
			// 2. The processing thread is being stopped.
			std::unique_lock<std::mutex> guard(frameLock);

			frameCondition.wait(
				guard,
				[this]()
				{
					return frameAvailable || !keepThreadAlive;
				}
			);

			// If the program asked the thread to stop,
			// exit the processing loop.
			if (!keepThreadAlive)
			{
				break;
			}

			// Grab the newest frame.
			//
			// cv::Mat uses reference-counted image memory, so this
			// does not require another expensive full image copy.
			frame = latestFrame;

			// We have consumed the waiting frame.
			frameAvailable = false;
		}

		if (frame.empty())
		{
			continue;
		}

		if (model == nullptr)
		{
			continue;
		}


		// ========================================================
		// CROP THE VALORANT MINIMAP
		// ========================================================

		int width = frame.cols;
		int height = frame.rows;

		int x = static_cast<int>(LEFT_SIDE * width);
		int y = static_cast<int>(TOP_SIDE * height);

		int cropWidth =
			static_cast<int>((RIGHT_SIDE - LEFT_SIDE) * width);

		int cropHeight =
			static_cast<int>((BOTTOM_SIDE - TOP_SIDE) * height);

		cv::Rect ROI(
			x,
			y,
			cropWidth,
			cropHeight
		);

		// Make sure the ROI remains inside the video frame.
		ROI &= cv::Rect(
			0,
			0,
			width,
			height
		);

		if (ROI.width <= 0 || ROI.height <= 0)
		{
			continue;
		}

		cv::Mat croppedFrame = frame(ROI);


		// ========================================================
		// RUN THE NEURAL NETWORK
		// ========================================================

		try
		{
			model->predict(croppedFrame);

			Eigen::MatrixXf results =
				model->getCurrentTracking();

			// Store ONLY the newest completed prediction.
			{
				std::lock_guard<std::mutex> guard(resultLock);
				latestResult = results;
			}
		}
		catch (const Ort::Exception& e)
		{
			std::cerr
				<< "[ModelWrapper] ONNX inference failed: "
				<< e.what()
				<< std::endl;
		}
		catch (const std::exception& e)
		{
			std::cerr
				<< "[ModelWrapper] Neural-network processing failed: "
				<< e.what()
				<< std::endl;
		}
	}

	std::cout << "[ModelWrapper] Neural-network thread stopped."
		<< std::endl;
}


// ============================================================
// START PROCESSING
// ============================================================

void ModelWrapper::startProcessingLoop()
{
	// Stop an old worker thread first if one is still running.
	endProcessingLoop();


	// ========================================================
	// LOAD THE MODEL
	// ========================================================

	if (model == nullptr)
	{
		const int ROWS = 384;
		const int COLS = 384;

		try
		{
			model = new NeuralNetwork(
				T_TEXT("faster_rcnn.onnx"),
				ROWS,
				COLS
			);
		}
		catch (const Ort::Exception& e)
		{
			std::cerr
				<< "[ModelWrapper] Failed to load ONNX model: "
				<< e.what()
				<< std::endl;

			return;
		}
		catch (const std::exception& e)
		{
			std::cerr
				<< "[ModelWrapper] Failed to load ONNX model: "
				<< e.what()
				<< std::endl;

			return;
		}
	}


	// Remove any frame left over from the previous video.
	{
		std::lock_guard<std::mutex> guard(frameLock);

		latestFrame.release();
		frameAvailable = false;
	}


	// Remove the previous video's neural-network result.
	{
		std::lock_guard<std::mutex> guard(resultLock);

		latestResult = Eigen::MatrixXf();
	}


	// Start the new worker thread.
	keepThreadAlive = true;

	loopThread = std::thread(
		&ModelWrapper::loop,
		this
	);
}


// ============================================================
// STOP PROCESSING
// ============================================================

void ModelWrapper::endProcessingLoop()
{
	// Tell the worker thread to stop.
	keepThreadAlive = false;

	// The worker may currently be asleep inside
	// frameCondition.wait(), so wake it up.
	frameCondition.notify_all();

	if (loopThread.joinable())
	{
		loopThread.join();
	}

	// Throw away any frame that was waiting.
	{
		std::lock_guard<std::mutex> guard(frameLock);

		latestFrame.release();
		frameAvailable = false;
	}
}


// ============================================================
// RECEIVE CURRENT VIDEO FRAME
// ============================================================

void ModelWrapper::submitFrame(const QImage& image)
{
	if (!keepThreadAlive || image.isNull())
	{
		return;
	}

	// Convert Qt's image into a known pixel format first.
	QImage rgbaImage =
		image.convertToFormat(QImage::Format_RGBA8888);

	// Create an OpenCV view of the QImage data.
	cv::Mat rgbaFrame(
		rgbaImage.height(),
		rgbaImage.width(),
		CV_8UC4,
		rgbaImage.bits(),
		rgbaImage.bytesPerLine()
	);

	// The neural network currently expects an OpenCV BGR image.
	cv::Mat bgrFrame;

	cv::cvtColor(
		rgbaFrame,
		bgrFrame,
		cv::COLOR_RGBA2BGR
	);


	// Replace whatever frame was waiting with this newer frame.
	//
	// This is the important part:
	// we DO NOT create a queue of old frames.
	{
		std::lock_guard<std::mutex> guard(frameLock);

		latestFrame = bgrFrame;
		frameAvailable = true;
	}

	// Wake the neural-network worker if it is waiting.
	frameCondition.notify_one();
}


// ============================================================
// GET NEWEST NEURAL-NETWORK RESULT
// ============================================================

Eigen::MatrixXf ModelWrapper::getLatestFrameData()
{
	std::lock_guard<std::mutex> guard(resultLock);

	return latestResult;
}