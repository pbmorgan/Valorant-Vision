#include <QtWidgets>

#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QMediaMetadata>
#include <QImage>
// Needed to access Qt's decoded video frames.
#include <QVideoSink>
#include <QVideoFrame>
#include <opencv2/opencv.hpp>

#include <string>
#include <iostream>

#include "videoplayer.h"
#include "modelwrapper.h"

// Takes in a pointer to the parent object
VideoPlayer::VideoPlayer(QWidget *parentAddress) : 
	QWidget(parentAddress)
{
	player = new QMediaPlayer(this);
	videoWidget = new QVideoWidget(this);
	audioOutput = new QAudioOutput(this);
	slider = new QSlider(Qt::Horizontal, this);

	player->setVideoOutput(videoWidget);
	player->setAudioOutput(audioOutput);

	// Receive the actual frames that Qt is displaying.
//
// Both the neural network and OCR reuse Qt's already-decoded
// video frames instead of opening the MP4 separately.
	connect(
		videoWidget->videoSink(),
		&QVideoSink::videoFrameChanged,
		this,
		[this](const QVideoFrame& frame)
		{
			// Ignore invalid frames.
			if (!frame.isValid())
			{
				return;
			}

			qint64 currentPosition = player->position();

			// Neural network gets a frame about every 200 ms.
			bool sendToModel =
				currentPosition - lastModelFramePositionMs >= 200;

			// OCR gets a frame about every 500 ms.
			bool sendToOCR =
				currentPosition - lastOcrFramePositionMs >= 500;

			// If neither system needs this frame, do nothing.
			if (!sendToModel && !sendToOCR)
			{
				return;
			}

			// Convert Qt's decoded frame into a QImage only once.
			QImage image = frame.toImage();

			if (image.isNull())
			{
				return;
			}

			// Send the newest frame to the neural network.
			if (sendToModel)
			{
				lastModelFramePositionMs = currentPosition;
				modelWrapper.submitFrame(image);
			}

			// Send a frame to OCR.
			if (sendToOCR)
			{
				lastOcrFramePositionMs = currentPosition;
				emit ocrFrameReady(image);
			}
		}
	);

	connect(player, &QMediaPlayer::durationChanged, slider, &QSlider::setMaximum);
	connect(player, &QMediaPlayer::positionChanged, slider, &QSlider::setValue);
	connect(slider, &QSlider::sliderMoved, player, &QMediaPlayer::setPosition);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(videoWidget);
	layout->addWidget(slider);

	connect(player, &QMediaPlayer::positionChanged, this, &VideoPlayer::onFrameChange);

	connect(player, &QMediaPlayer::playbackStateChanged, this,
		[this](QMediaPlayer::PlaybackState state) {
			if (state == QMediaPlayer::PlayingState)
				emit videoPlaying(player->position());
			else if (state == QMediaPlayer::PausedState)
				emit videoPaused();
		});
}

qint64 VideoPlayer::currentPositionMs() const
{
	return player->position();
}

//Takes in the url to a video and displays it
void VideoPlayer::displayVideo(std::string source) {

	// Allow OCR to receive a frame immediately from the new video.
	lastOcrFramePositionMs = -500;

	// Allow the neural network to receive a frame immediately
	// from the newly loaded video.
	lastModelFramePositionMs = -200;

	//Store what the video's FPS is
	//Using openCV to do it manually because Qt just gives the wrong FPS (it said a 60 fps video was 17 fps ????)
	cv::VideoCapture video(source);
	currentVideoFPS = video.get(cv::CAP_PROP_FPS);
	// TEMP DEBUG:
// Print the selected video's basic encoding information so we can
// determine whether the source itself is expensive to decode.
	double videoWidth =
		video.get(cv::CAP_PROP_FRAME_WIDTH);

	double videoHeight =
		video.get(cv::CAP_PROP_FRAME_HEIGHT);

	double videoFps =
		video.get(cv::CAP_PROP_FPS);

	int fourcc =
		static_cast<int>(
			video.get(cv::CAP_PROP_FOURCC)
			);

	char codec[] = {
		static_cast<char>(fourcc & 0xFF),
		static_cast<char>((fourcc >> 8) & 0xFF),
		static_cast<char>((fourcc >> 16) & 0xFF),
		static_cast<char>((fourcc >> 24) & 0xFF),
		'\0'
	};

	std::cout
		<< "VIDEO INFO -> "
		<< videoWidth << "x" << videoHeight
		<< " @ " << videoFps << " FPS"
		<< " Codec: " << codec
		<< std::endl;
	video.release();
	


	//End the model's last processing loop if it had one
	modelWrapper.endProcessingLoop();

	// TEMP PERFORMANCE TEST:
	// Disable the neural-network/minimap processing video decoder.
	// We are checking how much GPU Video Decode usage ModelWrapper causes.

	
	// Start the neural-network worker.
	//
	// ModelWrapper no longer opens the video file itself.
	// It now waits for frames supplied by QVideoSink.
	modelWrapper.startProcessingLoop();
	

	//Convert our string into a QString (which Qt can use)
	QString QSource = QString::fromStdString(source);
	player->setSource(QUrl::fromLocalFile(QSource));

	player->play();
}

void VideoPlayer::pauseOrPlayVideo()
{
	// If the video is currently playing, pause it.
	if (player->playbackState() ==
		QMediaPlayer::PlaybackState::PlayingState)
	{
		player->pause();
	}
	else
	{
		// PausedState OR StoppedState should both attempt playback.
		player->play();
	}
}

void VideoPlayer::pause() {
	player->pause();
}

void VideoPlayer::play() {
	player->play();
}

void VideoPlayer::setVolume(float volume) {
    audioOutput->setVolume(volume);
}

void VideoPlayer::onFrameChange(qint64 NthMillisecond)
{
	// Keep sending the current playback position to OCR.
	emit videoPositionChanged(NthMillisecond);

	// Get the newest neural-network result that has finished.
	//
	// We no longer ask for a specific historical frame because
	// the neural network now follows the live playback position.
	Eigen::MatrixXf results =
		modelWrapper.getLatestFrameData();

	// Send the newest prediction to the minimap widget.
	emit frameChanged(results);
}

