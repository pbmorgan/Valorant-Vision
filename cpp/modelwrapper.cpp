#include "modelwrapper.h"

ModelWrapper::ModelWrapper() {
	// Model is loaded in startProcessingLoop to avoid crashing on startup
	// if the working directory is not set correctly.
}

ModelWrapper::~ModelWrapper() {
	delete model;
	model = nullptr;

	endProcessingLoop();
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

		if (frameID % EVERY_N_FRAMES != 0) {
			frameID++;
			continue;
		}

		//Get dimensions of the frame
		cv::Size s = frame.size();
		int height = s.height, width = s.width;

		//Crop out the minimap
		cv::Rect ROI(LEFT_SIDE * width, TOP_SIDE * height, (RIGHT_SIDE - LEFT_SIDE) * width, (BOTTOM_SIDE - TOP_SIDE) * height);
		cv::Mat croppedFrame = frame(ROI);

		//Run the prediction on the minimap and get the results
		model->predict(croppedFrame);
		Eigen::MatrixXf results = model->getCurrentTracking();

		// Save the frame with the results drawn on it for debugging purposes once per second of video.
		if (frameID % (EVERY_N_FRAMES * 6) == 0) {
			const auto predictions = model->getCurrentPrediction();

			std::cout << "[Detection] frame=" << frameID
				<< " raw" << predictions.rows() 
				<< " tracked=" << results.rows()
				<< std::endl;

			//Overwrite each time; pause processing or copy them to inspect.
			cv::imwrite("debug_minimap_crop.png", croppedFrame);

			model->outputClassificationImageWithBoxesAndLabels(
				croppedFrame, predictions, 0.3f, "debug_agent_predictions.png");
		}
		//model->outputTrackingImageWithBoxesAndLabels(croppedFrame, results, "frames/" + std::to_string(frameID) + ".png");
		//model->outputClassificationImageWithBoxesAndLabels(croppedFrame, results, 0.6, "frames/" + std::to_string(frameID) + ".png");

		//Store the results
		lock.lock();
		data.push_back(results);
		lock.unlock();

		frameID++;
	}

}

void ModelWrapper::endProcessingLoop() {
	if (loopThread.joinable()) {
		keepThreadAlive = false;
		loopThread.join();
	}
}

void ModelWrapper::startProcessingLoop(const std::string fileName, const int RUN_EVERY_N_FRAMES, const int FPS) {

	EVERY_N_FRAMES = RUN_EVERY_N_FRAMES;

	if (model == nullptr) { //TODO: Change this back to being done in the constructor. I don't know why someone changed it to be here. The constructor should be responsible for initializaing the model...
		const int ROWS = 384;
		const int COLS = 384;
		try {
			model = new NeuralNetwork(T_TEXT("faster_rcnn.onnx"), ROWS, COLS);
		}
		catch (const Ort::Exception& e) {
			std::cerr << "[ModelWrapper] Failed to load ONNX model: " << e.what() << std::endl;
			return;
		}
		catch (const std::exception& e) {
			std::cerr << "[ModelWrapper] Failed to load ONNX model: " << e.what() << std::endl;
			return;
		}
	}

	model->resetTracker(FPS / RUN_EVERY_N_FRAMES);
	keepThreadAlive = true;

	data = std::vector<Eigen::MatrixXf>(); //We're processing from the start, so we empty out previous data
	loopThread = std::thread(&ModelWrapper::loop, this, fileName);
}

Eigen::MatrixXf ModelWrapper::getFrameData(const int NthFrame) {
	std::lock_guard<std::mutex> guard(lock); // Ensure thread-safe access to data

	if (EVERY_N_FRAMES <= 0 || NthFrame < 0) {
		return Eigen::MatrixXf(); // Return an empty matrix for invalid input
	}

	const int indexForThisFrame = NthFrame / EVERY_N_FRAMES;
	const int availableFrames = static_cast<int>(data.size());
	const int latestFrame = availableFrames > 0 ? (availableFrames - 1) * EVERY_N_FRAMES : -1;

	if (indexForThisFrame >= availableFrames) {
		qDebug() << "[Playback] requested frame:" << NthFrame
			     << "latest processed frame:" << latestFrame
			     << "PREDICTIONS NOT READY";
		return Eigen::MatrixXf(); // Return an empty matrix if the requested frame is not ready
	}

	qDebug() << "[Playback] requested frame:" << NthFrame
		     << "tracked rows delivered:" << data[indexForThisFrame].rows();

	return data[indexForThisFrame];
}
