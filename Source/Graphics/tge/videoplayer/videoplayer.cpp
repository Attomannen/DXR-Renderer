#include "stdafx.h"
#include <tge/videoplayer/videoplayer.h>
#include <fstream>
#include <mmeapi.h>
#include <iostream>
#include <tge/log/Log.h>
#include <tge/application.h>
#pragma comment(lib, "winmm.lib")

#define AUDIO_BUFFER_COUNT 32
#define AUDIO_BUFFER_SIZE 4096 

#ifdef USE_VIDEO
using namespace Tga;
Tga::VideoPlayer::VideoPlayer()
{
	myVideoCodec = nullptr;
	myVideoCodecContext = nullptr;
	myAudioCodecContext = nullptr;
	mySwsVideoContext = nullptr;
	mySwsAudioContext = nullptr;
	myVideoFormatContext = nullptr;
	myAVVideoFrame = nullptr;
	myAVVideoFrameBGR = nullptr;
	myAVAudioFrame = nullptr;
	myUIBuffer = nullptr;
	myAudioOutput = nullptr;
	myAudioStream = nullptr;
	myGotFrame = 0;
	myDecodedBytes = 0;
	myVideoStreamIndex = -1;
	myNumberOfBytes = 0;
	myReturnResult = 0;
	myIsFormatContextEOF = false;
	memset(&myAVPacket, 0, sizeof(myAVPacket));
}

Tga::VideoPlayer::~VideoPlayer()
{
	if (myAVVideoFrameBGR)	av_free(myAVVideoFrameBGR);
	if (myAVVideoFrame)		av_free(myAVVideoFrame);
	if (myAVAudioFrame)		av_free(myAVAudioFrame);

	if (myVideoCodecContext)	avcodec_close(myVideoCodecContext);
	if (myAudioCodecContext)	avcodec_close(myAudioCodecContext);
	if (myVideoFormatContext)	avformat_close_input(&myVideoFormatContext);

	if (myUIBuffer)			av_free(myUIBuffer);
	if (mySwsVideoContext)	sws_freeContext(mySwsVideoContext);
	if (mySwsAudioContext)	swr_free(&mySwsAudioContext);

	if (myAudioOutput)
	{
		delete myAudioOutput;
		myAudioOutput = nullptr;
	}

	delete[] myWavData.data;
	myWavData.data = nullptr;
	myAudioInitialized = false;
}

bool Tga::VideoPlayer::DoFirstFrame()
{
	return (GrabNextFrame() == 0);
}

void Tga::VideoPlayer::Stop()
{
	if (myVideoFormatContext)
	{
		av_seek_frame(myVideoFormatContext, myVideoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
	}

	if (myVideoCodecContext)
	{
		avcodec_flush_buffers(myVideoCodecContext);
	}
	if (myAudioCodecContext)
	{
		avcodec_flush_buffers(myAudioCodecContext);
	}

	myGotFrame = 0;
	myIsFormatContextEOF = false;
}

double r2d(AVRational r)
{
	return r.num == 0 || r.den == 0 ? 0. : (double)r.num / (double)r.den;
}

#define CALC_FFMPEG_VERSION(a,b,c) ( a<<16 | b<<8 | c )
double get_fps(AVFormatContext* aContext, int aStream)
{
	double eps_zero = 0.000025;
	double fps = r2d(aContext->streams[aStream]->r_frame_rate);

#if LIBAVFORMAT_BUILD >= CALC_FFMPEG_VERSION(52, 111, 0)
	if (fps < eps_zero)
	{
		fps = r2d(aContext->streams[aStream]->avg_frame_rate);
	}
#endif

	if (fps < eps_zero)
	{
		fps = 1.0 / r2d(aContext->streams[aStream]->codec->time_base);
	}

	return fps;
}

void VideoPlayer::RestartStream()
{
	if (myVideoFormatContext)
	{
		av_seek_frame(myVideoFormatContext, myVideoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
	}

	if (myVideoCodecContext)
	{
		avcodec_flush_buffers(myVideoCodecContext);
	}
	if (myAudioCodecContext)
	{
		avcodec_flush_buffers(myAudioCodecContext);
	}

	myGotFrame = 0;
	myIsFormatContextEOF = false;
}

double Tga::VideoPlayer::GetFps()
{
	if (!myVideoFormatContext || myVideoStreamIndex < 0)
	{
		return 0;
	}
	return get_fps(myVideoFormatContext, myVideoStreamIndex);
}

bool File_exist(const char* fileName)
{
	std::ifstream infile(fileName);
	bool isGood = infile.good();
	infile.close();
	return isGood;
}

VideoError VideoPlayer::Init(const char* aPath, bool aPlayAudio)
{
	if (!File_exist(aPath)) return VideoError_FileNotFound;
	myFileName = std::string(aPath);

	av_register_all();

	myReturnResult = avformat_open_input(&myVideoFormatContext, myFileName.c_str(), NULL, NULL);

	if (myReturnResult >= 0)
	{
		myReturnResult = avformat_find_stream_info(myVideoFormatContext, NULL);

		if (myReturnResult >= 0)
		{
			for (unsigned int i = 0; i < myVideoFormatContext->nb_streams; i++)
			{
				if (myVideoFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
				{
					myVideoStreamIndex = i;
					myVideoCodecContext = myVideoFormatContext->streams[myVideoStreamIndex]->codec;

					if (myVideoCodecContext)
					{
						myVideoCodec = avcodec_find_decoder(myVideoCodecContext->codec_id);
					}
				}
				else if (aPlayAudio && myVideoFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && !myAudioStream)
				{
					myAudioStream = myVideoFormatContext->streams[i];
				}
			}
		}
	}
	else
	{
		return VideoError_WrongFormat;
	}

	if (myAudioStream)
	{
		myAudioCodecContext = myAudioStream->codec;
		myAudioCodecContext->codec = avcodec_find_decoder(myAudioCodecContext->codec_id);
		if (myAudioCodecContext->codec == NULL || avcodec_open2(myAudioCodecContext, myAudioCodecContext->codec, NULL) != 0)
		{
			return VideoError_WrongFormat;
		}

		int64_t outChannelLayout = AV_CH_LAYOUT_STEREO;
		AVSampleFormat outSampleFormat = AV_SAMPLE_FMT_S16;
		int outSampleRate = 44100;
		myWavData.sampleRate = outSampleRate;
		myWavData.sampleSize = av_get_bytes_per_sample(outSampleFormat);
		myWavData.channels = (short)av_get_channel_layout_nb_channels(outChannelLayout);

		mySwsAudioContext = swr_alloc_set_opts(
			NULL,
			outChannelLayout, 
			outSampleFormat, 
			outSampleRate,
			av_get_default_channel_layout(myAudioCodecContext->channels),
			myAudioCodecContext->sample_fmt, 
			myAudioCodecContext->sample_rate,
			0,
			NULL
		);

		if (mySwsAudioContext == NULL || swr_init(mySwsAudioContext) != 0)
		{
			return VideoError_WrongFormat;
		}
		myAVAudioFrame = avcodec_alloc_frame();
	}

	if (myVideoCodec && myVideoCodecContext)
	{
		myReturnResult = avcodec_open2(myVideoCodecContext, myVideoCodec, NULL);

		if (myReturnResult >= 0)
		{
			myAVVideoFrame = avcodec_alloc_frame();
			myAVVideoFrameBGR = avcodec_alloc_frame();
			AVPixelFormat format = AV_PIX_FMT_RGBA;

			myNumberOfBytes = avpicture_get_size(format, myVideoCodecContext->width, myVideoCodecContext->height);
			myUIBuffer = (uint8_t*)av_malloc(myNumberOfBytes * sizeof(uint8_t));

			avpicture_fill((AVPicture*)myAVVideoFrameBGR, myUIBuffer, format, myVideoCodecContext->width, myVideoCodecContext->height);

			mySwsVideoContext = sws_getContext(
				myVideoCodecContext->width,
				myVideoCodecContext->height,
				myVideoCodecContext->pix_fmt,
				myVideoCodecContext->width,
				myVideoCodecContext->height,
				format,
				SWS_BILINEAR,
				NULL,
				NULL,
				NULL
			);
		}
	}
	else
	{
		return VideoError_WrongFormat;
	}

	return VideoError_NoError;
}

void VideoPlayer::PlayAudioStream(const WavData& wav)
{
	if (!myAudioInitialized)
	{
		int byteRate = wav.channels * wav.sampleRate * wav.sampleSize;
		short blockAlign = wav.channels * (short)wav.sampleSize;
		short bitsPerSample = (short)wav.sampleSize * 8;

		WAVEFORMATEX pFormat;
		pFormat.wFormatTag = WAVE_FORMAT_PCM;
		pFormat.nChannels = wav.channels;
		pFormat.nSamplesPerSec = 44100;
		pFormat.nAvgBytesPerSec = byteRate;
		pFormat.nBlockAlign = blockAlign;
		pFormat.wBitsPerSample = bitsPerSample;
		pFormat.cbSize = 0;

		myAudioOutput = new WaveOut(&pFormat, AUDIO_BUFFER_COUNT, AUDIO_BUFFER_SIZE);
		myAudioInitialized = true;
	}

	if (myAudioOutput && wav.data && wav.size > 0)
	{
		myAudioOutput->Write((PBYTE)&wav.data[0], (int)wav.size);
	}
}

void processFrame(const AVFrame* frame, SwrContext* swrContext, WavData& wav)
{
	std::vector<unsigned char> buffer(wav.channels * wav.sampleRate * wav.sampleSize);
	unsigned char* pointers[SWR_CH_MAX] = { NULL };
	pointers[0] = &buffer[0];

	int numSamplesOut = swr_convert(swrContext, pointers, wav.sampleRate, (const unsigned char**)frame->extended_data, frame->nb_samples);
	if (numSamplesOut <= 0) return;

	int totalBytes = numSamplesOut * wav.sampleSize * wav.channels;
	wav.size = totalBytes;

	delete[] wav.data;
	wav.data = new unsigned char[totalBytes];
	memcpy(wav.data, &buffer[0], totalBytes);
}

int VideoPlayer::GrabNextFrame()
{
	myGotFrame = 0;

	// Loop continuously to drain or read packets
	while (true)
	{
		// Enter codec draining/flush mode if format context ran dry
		if (myIsFormatContextEOF)
		{
			AVPacket emptyPacket;
			av_init_packet(&emptyPacket);
			emptyPacket.data = NULL;
			emptyPacket.size = 0;

			myDecodedBytes = avcodec_decode_video2(myVideoCodecContext, myAVVideoFrame, &myGotFrame, &emptyPacket);
			if (myGotFrame)
			{
				return 0; // successfully flushed delayed frame
			}

			return -1; // decoder fully empty
		}

		int readFrame = av_read_frame(myVideoFormatContext, &myAVPacket);
		if (readFrame < 0)
		{
			myIsFormatContextEOF = true;
			continue; // evaluate empty packet drainage loop on next cycle
		}

		// VIDEO STREAM
		if (myAVPacket.stream_index == myVideoStreamIndex)
		{
			myDecodedBytes = avcodec_decode_video2(myVideoCodecContext, myAVVideoFrame, &myGotFrame, &myAVPacket);
			av_free_packet(&myAVPacket);

			if (myDecodedBytes < 0)
			{
				continue;
			}

			if (myGotFrame)
			{
				return 0;
			}
		}
		// AUDIO STREAM
		else if (myAudioStream && myAVPacket.stream_index == myAudioStream->index)
		{
			AVPacket decodingPacket = myAVPacket;

			while (decodingPacket.size > 0)
			{
				int frameFinished = 0;
				int result = avcodec_decode_audio4(myAudioCodecContext, myAVAudioFrame, &frameFinished, &decodingPacket);

				if (result < 0)
				{
					break;
				}

				if (frameFinished)
				{
					processFrame(myAVAudioFrame, mySwsAudioContext, myWavData);
					PlayAudioStream(myWavData);
				}
				decodingPacket.size -= result;
				decodingPacket.data += result;
			}
			av_free_packet(&myAVPacket);
		}
		else
		{
			av_free_packet(&myAVPacket);
		}
	}
	return -1;
}

bool VideoPlayer::Update(unsigned int*& aBuffer, unsigned int aSizeX, unsigned int aSizeY)
{
	if (!myGotFrame || !mySwsVideoContext || !myAVVideoFrame || !myAVVideoFrameBGR)
	{
		return false;
	}

	myReturnResult = sws_scale(
		mySwsVideoContext,
		myAVVideoFrame->data,
		myAVVideoFrame->linesize,
		0,
		myVideoCodecContext->height,
		myAVVideoFrameBGR->data,
		myAVVideoFrameBGR->linesize
	);

	if (myReturnResult <= 0)
	{
		return false;
	}

	uint8_t* data = myAVVideoFrameBGR->data[0];
	int rgbIndex = 0;

	for (int y = 0; y < myVideoCodecContext->height; y++)
	{
		for (int x = 0; x < myVideoCodecContext->width; x++)
		{
			aBuffer[y * aSizeX + x] =
				(data[rgbIndex + 3] << 24)
				|
				(data[rgbIndex + 2] << 16)
				|
				(data[rgbIndex + 1] << 8)
				|
				(data[rgbIndex + 0])
			;

			rgbIndex += 4;
		}
	}

	for (unsigned int y = myVideoCodecContext->height; y < aSizeY; y++)
	{
		for (unsigned int x = 0; x < aSizeX; x++)
		{
			aBuffer[y * aSizeX + x] = 0x00000000;
		}
	}

	myFrameCount++;
	return true;
}

#endif