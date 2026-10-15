#include <QtWidgets>
#include <QVideoSink>
#include <QVideoFrame>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QMediaMetadata>
#include <QImage>
#include <opencv2/opencv.hpp>

#include <string>
#include <iostream>

#include "videoplayer.h"
#include "modelwrapper.h"



static QString formatTime(qint64 milliseconds)
{
    qint64 totalSeconds = milliseconds / 1000;

    qint64 hours = totalSeconds / 3600;
    qint64 minutes = (totalSeconds % 3600) / 60;
    qint64 seconds = totalSeconds % 60;

    if (hours > 0)
    {
        return QString("%1:%2:%3")
            .arg(hours, 2, 10, QChar('0'))
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }

    return QString("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}


// Takes in a pointer to the parent object
VideoPlayer::VideoPlayer(QWidget *parentAddress) : 
	QWidget(parentAddress)
{
	player = new QMediaPlayer(this);
	videoWidget = new QVideoWidget(this);
	audioOutput = new QAudioOutput(this);
	slider = new QSlider(Qt::Horizontal, this);

	currentTimeLabel = new QLabel("00:00", this);
	durationLabel = new QLabel("00:00", this);

	player->setVideoOutput(videoWidget);
	player->setAudioOutput(audioOutput);

// Send the frames Qt is already displaying to OCR.
connect(
    videoWidget->videoSink(),
    &QVideoSink::videoFrameChanged,
    this,
    [this](const QVideoFrame& frame)
    {
        if (!frame.isValid()) {
            return;
        }

        qint64 currentPosition = player->position();

        // Only send a frame every 500 ms.
        if (currentPosition - lastOcrFramePositionMs < 500) {
            return;
        }

        lastOcrFramePositionMs = currentPosition;

        QImage image = frame.toImage();

        if (image.isNull()) {
            return;
        }

        emit ocrFrameReady(image);
    }
);
	//	});


	connect(player,&QMediaPlayer::durationChanged,this,[this](qint64 duration)
    {
    	slider->setMaximum(duration);
        durationLabel->setText(formatTime(duration));
    });
	connect(player,&QMediaPlayer::positionChanged,this,[this](qint64 position)
    {
        slider->setValue(position);
        currentTimeLabel->setText(formatTime(position));
    });
	connect(slider, &QSlider::sliderMoved, player, &QMediaPlayer::setPosition);

	QVBoxLayout *layout = new QVBoxLayout(this);

	layout->addWidget(videoWidget);

	// Bottom video progress row.
	QHBoxLayout *progressLayout = new QHBoxLayout();

	progressLayout->addWidget(currentTimeLabel);
	progressLayout->addWidget(slider, 1);
	progressLayout->addWidget(durationLabel);

	layout->addLayout(progressLayout);

	connect(player, &QMediaPlayer::positionChanged, this, &VideoPlayer::onFrameChange);

	connect(player, &QMediaPlayer::playbackStateChanged, this,
		[this](QMediaPlayer::PlaybackState state) {
			if (state == QMediaPlayer::PlayingState)
				emit videoPlaying(player->position());
			else if (state == QMediaPlayer::PausedState)
				emit videoPaused();
		});

		connect(
    player,
    &QMediaPlayer::mediaStatusChanged,
    this,
    [this](QMediaPlayer::MediaStatus status)
    {
        if (status ==
            QMediaPlayer::EndOfMedia)
        {
            emit videoFinished();
        }
    }
);

	
}

qint64 VideoPlayer::currentPositionMs() const
{
	return player->position();
}

//Takes in the url to a video and displays it
void VideoPlayer::displayVideo(std::string source) {

	// Allow OCR to receive a frame immediately from the new video.
	lastOcrFramePositionMs = -500;

	//Store what the video's FPS is
	//Using openCV to do it manually because Qt just gives the wrong FPS (it said a 60 fps video was 17 fps ????)
	cv::VideoCapture video(source);
	currentVideoFPS = video.get(cv::CAP_PROP_FPS);
	video.release();
	


	//End the model's last processing loop if it had one
	modelWrapper.endProcessingLoop();
	//Let the model start another thread for processing the video's frames
	int RUN_EVERY_N_FRAMES = currentVideoFPS / 6; //Run every third of a second
	while (RUN_EVERY_N_FRAMES % 5 != 0) { RUN_EVERY_N_FRAMES++; } //Round up to the nearest integer divisible by 5 (it personally would annoy be if it wasn't)

	modelWrapper.startProcessingLoop(source, RUN_EVERY_N_FRAMES, currentVideoFPS);

	//Convert our string into a QString (which Qt can use)
	QString QSource = QString::fromStdString(source);
	player->setSource(QUrl::fromLocalFile(QSource));

	player->play();
}

void VideoPlayer::pauseOrPlayVideo() {
	if (player->playbackState() == QMediaPlayer::PlaybackState::PausedState) {
		player->play();
	}
	else {
		player->pause();
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

void VideoPlayer::onFrameChange(qint64 NthMillisecond) {
	emit videoPositionChanged(NthMillisecond);

	//Get frame rate of the video to convert milliseconds to frames
	qint64 frameNum = (NthMillisecond / 1000.0) * currentVideoFPS;
	
	Eigen::MatrixXf results = modelWrapper.getFrameData(frameNum);

	emit frameChanged(results);
}

