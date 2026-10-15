#include "modelwrapper.h"
#include <algorithm>  // std::max

ModelWrapper::ModelWrapper() {
	// Model is loaded in startProcessingLoop to avoid crashing on startup
	// if the working directory is not set correctly.
}

ModelWrapper::~ModelWrapper() {

	// Stop the processing thread BEFORE deleting the model.
	endProcessingLoop();

	delete model;
	model = nullptr;
}

void ModelWrapper::setOCRFrameCallback(
	std::function<void(const cv::Mat&)> callback)
{
	// Store the function that should receive frames for OCR.
	ocrFrameCallback = std::move(callback);
}

void ModelWrapper::loop(std::string fileName) {

	//What percent of the screen is the minimap at
	const float LEFT_SIDE = 0.024;
	const float RIGHT_SIDE = 0.24;
	const float TOP_SIDE = 0.04;
	const float BOTTOM_SIDE = 0.43;

	std::cout << "Starting frame processing" << fileName << std::endl;

	cv::VideoCapture cap(fileName);
	cv::Mat frame;

	int frameID = 0;
	while (keepThreadAlive) {

		if (!cap.read(frame)) break;

		// Send one of the already-decoded frames to OCR about every 500 ms.
		// OCR receives a clone so VideoCapture can safely reuse 'frame'
		// on the next iteration.
		if (ocrFrameCallback &&
			frameID % OCR_EVERY_N_FRAMES == 0) {

			ocrFrameCallback(frame.clone());
		}
		if (frameID % EVERY_N_FRAMES != 0) {
			frameID++;
			continue;
		}

		// Only run neural-network processing if the model
// loaded successfully.
		if (model != nullptr) {

			// Get dimensions of the frame.
			cv::Size s = frame.size();
			int height = s.height;
			int width = s.width;

			// Crop out the minimap.
			cv::Rect ROI(
				LEFT_SIDE * width,
				TOP_SIDE * height,
				(RIGHT_SIDE - LEFT_SIDE) * width,
				(BOTTOM_SIDE - TOP_SIDE) * height
			);

			cv::Mat croppedFrame = frame(ROI);

			// Run the prediction on the minimap.
			model->predict(croppedFrame);

			Eigen::MatrixXf results =
				model->getCurrentTracking();

			// Store the neural-network results.
			{
				std::lock_guard<std::mutex> guard(lock);
				data.push_back(results);
			}
		}

		frameID++;
	}

}

void ModelWrapper::endProcessingLoop() {
	if (loopThread.joinable()) {
		keepThreadAlive = false;
		loopThread.join();
	}
}

void ModelWrapper::startProcessingLoop(
	const std::string fileName,
	const int RUN_EVERY_N_FRAMES,
	const int FPS)
{
	EVERY_N_FRAMES = RUN_EVERY_N_FRAMES;

	// Approximately one OCR frame every 500 ms.
	// 60 FPS -> every 30 frames
	// 30 FPS -> every 15 frames
	OCR_EVERY_N_FRAMES = std::max(1, FPS / 2);

	// Try to load the neural network.
	if (model == nullptr) {

		const int ROWS = 384;
		const int COLS = 384;

		try {
			model = new NeuralNetwork(
				T_TEXT("faster_rcnn.onnx"),
				ROWS,
				COLS
			);
		}
		catch (const Ort::Exception& e) {
			std::cerr
				<< "[ModelWrapper] Failed to load ONNX model: "
				<< e.what()
				<< std::endl;

			// IMPORTANT:
			// Do NOT return here.
			// We still want the video processing thread to run
			// so its decoded frames can be shared with OCR.
			return;
		}
		catch (const std::exception& e) {
			std::cerr
				<< "[ModelWrapper] Failed to load ONNX model: "
				<< e.what()
				<< std::endl;

			return;
		}
	}

	// Only reset the tracker if the model loaded successfully.
	if (model != nullptr) {
		model->resetTracker(FPS / RUN_EVERY_N_FRAMES);
	}

	keepThreadAlive = true;

	// Starting from the beginning, so remove old results.
	data = std::vector<Eigen::MatrixXf>();

	// Start video decoding whether or not the model loaded.
	loopThread = std::thread(
		&ModelWrapper::loop,
		this,
		fileName
	);
}

Eigen::MatrixXf ModelWrapper::getFrameData(const int NthFrame) {

	int indexForThisFrame = NthFrame / EVERY_N_FRAMES;

	// Protect all access to the shared data vector.
	std::lock_guard<std::mutex> guard(lock);

	if (indexForThisFrame < data.size()) {
		return data[indexForThisFrame];
	}

	// No processed data exists for this frame yet.
	return Eigen::MatrixXf();
}
