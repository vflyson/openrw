#pragma once

#include <render/GameRenderer.hpp>
#include <render/OpenGLRenderer.hpp>
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

    virtual void handleEvent(const SDL_Event& event);
private:
    AVFrame* picture;
    AVFrame* pVideoFrameRGB;
    AVPacket packet;
    struct SwsContext* scaleContext = nullptr;
    bool busy = true;
    int videoStream;
    int numBytes;
    const AVPixelFormat fmt = AV_PIX_FMT_RGB24;
    void av_init();
    void next_frame();
    int videoFrameFinished = 0;
    int videoFrameNumber = 0;
    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    AVFormatContext* pFormatContext = nullptr;
    AVCodecParameters* pVideoCodecParameters = nullptr;
    AVCodecParameters* pAudioCodecParameters = nullptr;
    AVCodecContext* pVideoCodecContext = nullptr;
    AVCodecContext* pAudioCodecContext = nullptr;
    AVCodec* pVideoCodec = nullptr;
    AVCodec* pAudioCodec = nullptr;
    GLuint texture;
    uint8_t* videoBuffer = nullptr;
};
