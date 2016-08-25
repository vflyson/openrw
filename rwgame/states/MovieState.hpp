#pragma once

// #include <platform/FileIndex.hpp>
#include <render/GameRenderer.hpp>
#include <render/OpenGLRenderer.hpp>
#include <data/Model.hpp>
#include "State.hpp"

extern "C" {
	#include <libavformat/avformat.h>
	#include <libavcodec/avcodec.h>
	#include <libswscale/swscale.h>
}

class AVFormatContext;
class AVCodecContext;
class AVCodec;
class AVFrame;

class MovieState : public State
{
	State* next;
	const std::string name;
public:
	MovieState(RWGame* game, const std::string name);

	virtual void enter();
	virtual void exit();

	virtual void tick(float dt);

	virtual void draw(GameRenderer* r);

	void setNextState(State* nextState);

	virtual void handleEvent(const SDL_Event& event);
protected:
	int videoStreamIndex;
	int audioStreamIndex;

	AVFormatContext* pFormatContext;
	AVCodecContext* pVideoCodecContext;
	AVCodecContext* pAudioCodecContext;
	AVCodec* pVideoCodec;
	AVCodec* pAudioCodec;
	AVFrame* pVideoFrame;
	AVFrame* pVideoFrameRGB;

	GLuint textureId;
	uint8_t *videoBuffer;
};
