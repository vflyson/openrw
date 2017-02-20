#include "MovieState.hpp"
#include "game.hpp"

#include "RWGame.hpp"
#include <rw/defines.hpp>
#include <render/OpenGLRenderer.hpp>
#include <glm/gtc/type_ptr.hpp>

MovieState::MovieState(RWGame* game, const std::string name)
    : State(game)
    , name(name) {
    av_init();

    glEnable(GL_TEXTURE_2D);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glViewport(0, 0, 640, 480);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, pVideoCodecContext->width,
                 pVideoCodecContext->height, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, pVideoFrameRGB->data[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 640, 480, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);

    scaleContext = sws_getCachedContext(
        scaleContext,
        pVideoCodecContext->width,
        pVideoCodecContext->height,
        pVideoCodecContext->pix_fmt,
        640, 480,
        fmt,
        SWS_BILINEAR, //SWS_BICUBIC,
        NULL, NULL, NULL
    );
}

void MovieState::enter() {
    while (1) {
        if (!busy) break;

        next_frame();

        glClear(GL_COLOR_BUFFER_BIT);
        glBindTexture(GL_TEXTURE_2D, texture);
    }
}

void MovieState::draw(GameRenderer* r) {
    State::draw(r);
}

void MovieState::exit() {
    delete [] videoBuffer;

    av_free(picture);
    av_free(pVideoFrameRGB);
    avcodec_close(pVideoCodecContext);
    avformat_close_input(&pFormatContext);

    glDeleteTextures(1, &texture);
}

void MovieState::tick(float dt) {
    RW_UNUSED(dt);

    done();
}

void MovieState::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_KEYDOWN) {
        busy = false;
    }
    State::handleEvent(event);
}

void MovieState::av_init() {
    av_register_all();

    if (avformat_open_input(&pFormatContext, name.c_str(), NULL, NULL) != 0) {
        fprintf(stderr, "ffmpeg: Unable to open input file\n");
        return;
    }

    if (avformat_find_stream_info(pFormatContext, NULL) < 0) {
        fprintf(stderr, "ffmpeg: Unable to find stream info\n");
        return;
    }

    //av_dump_format(pFormatContext, 0, name.c_str(), 0);

    for (uint32_t i = 0; i < pFormatContext->nb_streams; i++) {
        AVMediaType codecType = pFormatContext->streams[i]->codec->codec_type;
        if (videoStreamIndex < 0 && codecType == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
        }
        if (audioStreamIndex < 0 && codecType == AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex = i;
        }
        if (audioStreamIndex > 0 && videoStreamIndex > 0) {
            break;
        }
    }

    if (videoStreamIndex == -1) {
        fprintf(stderr, "ffmpeg: Couldn't find a video stream\n");
        return;
    }
    if (audioStreamIndex == -1) {
        fprintf(stderr, "ffmpeg: Couldn't find an audio stream\n");
        return;
    }

    pVideoCodecContext = pFormatContext->streams[videoStreamIndex]->codec;
    pAudioCodecContext = pFormatContext->streams[audioStreamIndex]->codec;

    pVideoCodec = avcodec_find_decoder(pVideoCodecContext->codec_id);
    pAudioCodec = avcodec_find_decoder(pAudioCodecContext->codec_id);

    if (pVideoCodec == NULL) {
        fprintf(stderr, "ffmpeg: Unsupported codec!\n");
        return;
    }

    // Open video codec
    if (avcodec_open2(pVideoCodecContext, pVideoCodec, NULL) < 0) {
        fprintf(stderr, "ffmpeg: Could not open video codec\n");
        return;
    }
    // Open audio codec
    // if (avcodec_open2(pAudioCodecContext, pAudioCodec, NULL) < 0) {
    //     fprintf(stderr, "ffmpeg: Could not open audio codec\n");
    //     return;
    // }

    // avcodec_alloc_context3(pVideoCodec);

    picture = av_frame_alloc();
    pVideoFrameRGB = av_frame_alloc();

    //Allocate memory for the raw data we get when converting.
    numBytes = avpicture_get_size(fmt, pVideoCodecContext->width,
                                       pVideoCodecContext->height);
    videoBuffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));

    // Associate frame with our buffer
    avpicture_fill((AVPicture*)pVideoFrameRGB, videoBuffer, fmt,
                    pVideoCodecContext->width, pVideoCodecContext->height);
}

void MovieState::next_frame() {
    while (1) {
        if (av_read_frame(pFormatContext, &packet) >= 0) {
            if (packet.stream_index == videoStreamIndex) {
                avcodec_decode_video2(pVideoCodecContext, picture, &videoFrameFinished, &packet);

                if (videoFrameFinished) {
                    sws_scale(scaleContext, (uint8_t const * const *)picture->data,
                                picture->linesize, 0,
                                pVideoCodecContext->height, pVideoFrameRGB->data,
                                pVideoFrameRGB->linesize);
                    glBindTexture(GL_TEXTURE_2D, texture);
                    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pVideoCodecContext->width,
                    pVideoCodecContext->height, GL_RGB, GL_UNSIGNED_BYTE, pVideoFrameRGB->data[0]);

                    printf("> %d\n", videoFrameNumber++);

                    break;
                }
            }

            av_free_packet(&packet);
        } else {
            busy = false;
            break;
        }
    }
}
