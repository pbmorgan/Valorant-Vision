#ifndef NEURALNETWORK_H
#define NEURALNETWORK_H

#include <filesystem>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>

#include "Eigen/Dense"




//Make sure we use the right type of char, because ONNX on Windows has to be unique and use wchar_t...
//Example usage: const T_CHAR *str = T_TEXT("Chikorita is the best pokemon.")
#ifdef _WIN32
typedef wchar_t T_CHAR;
#define T_TEXT(quote) L##quote
#else
typedef char T_CHAR;
#define T_TEXT(quote) quote
#endif

class NeuralNetwork {

private:

	Ort::Env *env = nullptr;
	Ort::SessionOptions sessionOptions;
	Ort::Session *model = nullptr; // Has to be dynamically created because we can't create it with no parameters
	int ROWS; //Height of images to use
	int COLS; //Width of images to use

	

	/*Names of the nodes for the model (i.e., the names of the inputs and outputs)
	Storing an std::string version and a c-style version separaretly
	We need c-style to pass into the model but that gets really annoying and yes I know this is ineffecient but like, it's like 4 extra strings
	The model has over 40 million parameters
	I think we'll live
	*/
	std::vector<std::string> inputNodeNames;
	std::vector<const char *> inputNodeNamesCStyle;
	std::vector<std::string> outputNodeNames;
	std::vector<const char *> outputNodeNamesCStyle;

	Eigen::MatrixXf currentPrediction; //Stores current raw prediction info from model in format: x1, y1, x2, y2, confidence, classID
	Eigen::MatrixXf currentTracking; //Stores current tracking info in format: x1, y1, x2, y2, trackID, confidence, classID, DetIndex


	//Takes in a reference to an opencv image file, does needed preprocessing, and returns it as a vector of floats
	std::vector<float> prepareImage(const cv::Mat &);



public:

	NeuralNetwork(const T_CHAR *onnxFilePath, const int ROWS, const int COLS);
	~NeuralNetwork();

	Eigen::MatrixXf getCurrentPrediction() const;
	Eigen::MatrixXf getCurrentTracking() const;

	//Takes in an image in opencv BGR format and updates internal prediction
	void predict(cv::Mat imageBGR);

	//Outputs an image file with predictions drawn on it. Takes in the image, arrays of bounding boxes, labels, and scores, the amount of predictions (N), and a threshold for scores to show up
	void outputClassificationImageWithBoxesAndLabels(const cv::Mat image, const Eigen::MatrixXf prediction, const float threshold, std::string outFileName);

	//Outputs an image file with tracking information drawn on it. Takes in the image, eigen matrix of tracking information, and the name of the file to be created
	void outputTrackingImageWithBoxesAndLabels(const cv::Mat image, Eigen::MatrixXf, std::string outFileName);

	void resetTracker(const int FPS);
};

#endif