#include "MovieState.hpp"
#include "game.hpp"

#include "RWGame.hpp"
#include <rw/defines.hpp>
#include <render/OpenGLRenderer.hpp>
#include <glm/gtc/type_ptr.hpp>

MovieState::MovieState(RWGame* game, const std::string name)
	: State(game)
	, name(name)
{
	av_register_all();

	glGenTextures(1, &textureId);
	glBindTexture(GL_TEXTURE_2D, textureId);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 640,
		480, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
}

void MovieState::enter()
{
	const char filePath[] = "../../Games/GTA 3/movies/Logo.mpg"; // TODO use findPathRealCase() instead
	if( avformat_open_input(&pFormatContext, filePath, NULL, NULL) != 0 ){
		fprintf(stderr, "avformat_open_input: Unable to open input\n");
		return;
	}

	if( avformat_find_stream_info(pFormatContext, NULL) < 0 ){
		fprintf(stderr, "avformat_find_stream_info: Unable to find stream info\n");
		return;
	}

	av_dump_format(pFormatContext, 0, (const char*)filePath, 0);

	videoStreamIndex = -1;
	audioStreamIndex = -1;

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
		fprintf(stderr, "ffmpeg: Couldn't find a video stream.");
	}
	if (audioStreamIndex == -1) {
		fprintf(stderr, "ffmpeg: Couldn't find an audio stream.");
	}

	pVideoCodecContext = pFormatContext->streams[videoStreamIndex]->codec;
	pAudioCodecContext = pFormatContext->streams[audioStreamIndex]->codec;

	pVideoCodec = avcodec_find_decoder(pVideoCodecContext->codec_id);
	pAudioCodec = avcodec_find_decoder(pAudioCodecContext->codec_id);

	if( pVideoCodec == NULL ) {
		fprintf(stderr, "ffmpeg: Unsupported codec!\n");
		return;
	}

	// Open vedeo codec
	if( avcodec_open2(pVideoCodecContext, pVideoCodec, NULL) < 0 ) {
		fprintf(stderr, "ffmpeg: Could not open video codec\n");
		return;
	}
	// Open audio codec
	// if( avcodec_open2(pAudioCodecContext, pAudioCodec, NULL) < 0 ) {
	// 	fprintf(stderr, "ffmpeg: Could not open audio codec\n");
	// 	return;
	// }

	// avcodec_alloc_context3(pVideoCodec);

	// Allocate video pVideoFrame
	pVideoFrame = av_frame_alloc();
	pVideoFrameRGB = av_frame_alloc();

	//Allocate memory for the raw data we get when converting.
	int numBytes = avpicture_get_size(PIX_FMT_RGB24, pVideoCodecContext->width,
										pVideoCodecContext->height);
	videoBuffer = new uint8_t[numBytes];

	// Associate frame with our buffer
	avpicture_fill((AVPicture*)pVideoFrameRGB, videoBuffer, PIX_FMT_RGB24,
		pVideoCodecContext->width, pVideoCodecContext->height);
}

void MovieState::draw(GameRenderer* r)
{
	int videoFrameFinished;
	AVPacket packet;
	int videoFrameNumber = 0;

	while( av_read_frame(pFormatContext, &packet) >= 0 )
	{
		static struct SwsContext* scaleContext = NULL;
		// glActiveTexture(textureId);
		// glBindTexture(GL_TEXTURE_2D, textureId);

		/*if( packet.stream_index == audioStreamIndex ) {
			int32_t consumed = 0;
			while( consumed < packet.size ) {
				AVFrame* pAudioFrame = audioRingBuffer->getCurrent();
				int result = avcodec_decode_audio4(pAudioCodecContext, pAudioFrame, &frameFinished, &packet);
				if (result < 0) {
					break;
				} else {
					consumed += result;
				}
				if( frameFinished ) {

				}
			}
		} else*/ if( packet.stream_index == videoStreamIndex ) {
			// Decode the video frame
			avcodec_decode_video2(pVideoCodecContext, pVideoFrame,
									&videoFrameFinished, &packet);

			// If we got a frame then convert it and put it into RGB buffer
			if( videoFrameFinished ) {
				printf("frame finished: %i\n", videoFrameNumber);

				if( scaleContext == NULL ) {
					scaleContext = sws_getContext(
						pVideoCodecContext->width,
						pVideoCodecContext->height,
						pVideoCodecContext->pix_fmt,
						640, 480,
						PIX_FMT_RGB24,
						SWS_BILINEAR,
						NULL, NULL, NULL
					);
				}

				// Convert the image frame to RGB
				sws_scale(scaleContext,
					pVideoFrame->data,
					pVideoFrame->linesize,
					0, pVideoCodecContext->height,
					pVideoFrameRGB->data,
					pVideoFrameRGB->linesize
				);

				glClear(GL_COLOR_BUFFER_BIT);

				glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
				glBindTexture(GL_TEXTURE_2D, textureId);
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
					pVideoFrameRGB->width, pVideoFrameRGB->height,
					GL_RGB, GL_UNSIGNED_BYTE, pVideoFrameRGB->data[0]);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

				videoFrameNumber++;

				glBindTexture(GL_TEXTURE_2D, textureId);

				av_free_packet(&packet);

				usleep(1000);
			}
		}
	}

	// av_free(videoBuffer);

	State::draw(r);
}

void MovieState::exit()
{
	delete [] videoBuffer;

	av_free(pVideoFrameRGB);
	av_free(pVideoFrame);
	avcodec_close(pVideoCodecContext);
	avformat_close_input(&pFormatContext);

	glDeleteTextures(1, &textureId);
}

void MovieState::tick(float dt)
{
	RW_UNUSED(dt);

	// If background work is completed, switch to the next state
	if( getWorld()->_work->isEmpty() ) {
		StateManager::get().exec(next);
	}
}

void MovieState::setNextState(State* nextState)
{
	next = nextState;
}

void MovieState::handleEvent(const SDL_Event& e)
{
	if (e.type == SDL_KEYUP) {
		StateManager::get().exit();
	}
	State::handleEvent(e);
}
