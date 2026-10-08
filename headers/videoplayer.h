#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <string>
#include <QWidget>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QSlider>
#include <QtGlobal>
#include <QMetaObject>
#include <QImage>

#include "modelwrapper.h"

class VideoPlayer : public QWidget {

	Q_OBJECT

private:

	QMediaPlayer *player;
	QVideoWidget *videoWidget;
	QAudioOutput *audioOutput;
	QSlider *slider;

	ModelWrapper modelWrapper;

	double currentVideoFPS = 0;
	// Keeps track of when we last sent a decoded frame to OCR.
	// This prevents OCR from receiving every single video frame.
	qint64 lastOcrFramePositionMs = -500;

public:
	// Takes in a pointer to the parent object
	VideoPlayer(QWidget *parent);

	//Takes in the url to a video and displays its
	void displayVideo(std::string url);

	//Pauses or plays video (depends on state)
	void pauseOrPlayVideo();

	//Pauses playback unconditionally (used while dialogs are showing)
	void pause();

	//Starts playback unconditionally
	void play();

	//Sets volume for the player
	void setVolume(float volume);


	void onFrameChange(qint64 NthMillisecond);

	qint64 currentPositionMs() const;

signals:
	void frameChanged(Eigen::MatrixXf frameData);
	void videoPlaying(qint64 positionMs);
	void videoPaused();
	void videoPositionChanged(qint64 positionMs);
	void ocrFrameReady(const QImage& frame);
};

#endif