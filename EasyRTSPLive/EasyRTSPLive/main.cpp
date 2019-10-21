#define _CRTDBG_MAP_ALLOC
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#ifdef _WIN32
#include "windows.h"
#else
#include <string.h>
#include <unistd.h>
#endif
#include "getopt.h"
#include <stdio.h> 
#include <iostream> 
#include <time.h> 
#include <stdlib.h>
//#include <vector>
#include <list>

#include "EasyAACEncoderAPI.h"
#include "EasyRTSPClientAPI.h"
#include "EasyRTMPAPI.h"
#include "ini.h"
#include "trace.h"

#ifdef _WIN32
#pragma comment(lib,"libEasyRTSPClient.lib")
#pragma comment(lib,"libeasyrtmp.lib")
#pragma comment(lib,"libEasyAACEncoder.lib")

#endif

#ifdef _WIN32
#define RTMP_KEY "79736C36655969576B5A7341436D74646F7054317065394659584E35556C525455457870646D55755A58686C4B56634D5671442F706634675A57467A65513D3D"
#define RTSP_KEY "6D75724D7A4969576B5A7341436D74646F7054317065394659584E35556C525455457870646D55755A58686C4931634D5671442F706634675A57467A65513D3D"
#else // linux
#define RTMP_KEY "79736C36655A4F576B596F41436D74646F70543170664E6C59584E35636E527A63477870646D5745567778576F502B6C2F69426C59584E35"
#define RTSP_KEY "6D75724D7A4A4F576B596F41436D74646F70543170664E6C59584E35636E527A63477870646D5868567778576F502B6C2F69426C59584E35"
#endif

#define BUFFER_SIZE  1024*1024

//用户可自定义的RTSP转RTMP拉流转推流路数,官方工具版默认1路拉转推，用户可通过代码定制多路RTSP转RTMP
#define MAX_CHANNEL_INDEX 1024 * 2

#define CONF_FILE_PATH  "easyrtsplive.ini"
#define CONF_SECTION_CHANNEL "channel"
#define CONF_KEY_MAX_CHANNEL_INDEX "max_channel_index"
#define CONF_KEY_VERBOSITY "verbosity"
#define CONF_KEY_RTSP "rtsp"
#define CONF_KEY_RTMP "rtmp"
//#define TRACE_LOG

static bool rtspActivate;

static bool rtmpActivate;

static std::list <_channel_info*> gChannelInfoList;

//Stop
void StopChannel(_channel_info* pChannel)
{
	StopChannelRtsp(pChannel);

	StopChannelPusher(pChannel);
}

//rtmp回调
int __EasyRTMP_Callback(int _frameType, char *pBuf, EASY_RTMP_STATE_T _state, void *_userPtr)
{
	_channel_info* pChannel = (_channel_info*)_userPtr;
	switch(_state)
	{
		case EASY_RTMP_STATE_CONNECTING:
			if (NULL != pChannel->liveCallback)
			{
				pChannel->liveCallback(2, 0, "connecting");
			}
			break;
		case EASY_RTMP_STATE_CONNECTED:
			if (NULL != pChannel->liveCallback)
			{
				pChannel->liveCallback(2, 1, "connected");
			}
			break;
		case EASY_RTMP_STATE_CONNECT_FAILED:
			if (NULL != pChannel->liveCallback)
			{
				pChannel->liveCallback(2, 2, "connect failed");
			}
			break;
		case EASY_RTMP_STATE_CONNECT_ABORT:
			if (NULL != pChannel->liveCallback)
			{
				pChannel->liveCallback(2, 2, "connect abort");
			}
			StopChannel(pChannel);
			break;
		case EASY_RTMP_STATE_DISCONNECTED:
			if (NULL != pChannel->liveCallback)
			{
				pChannel->liveCallback(2, 2, "disconnect");
			}
			StopChannel(pChannel);
			break;
		default:
			break;
	}
	return 0;
}

//rtsp callback
int Easy_APICALL __RTSPSourceCallBack( int _chid, void *_chPtr, int _mediatype, char *pbuf, EASY_FRAME_INFO *frameinfo)
{
	char msg[512];

	if (NULL != frameinfo)
	{
		if (frameinfo->height == 1088) 
		{ 
			frameinfo->height = 1080; 
		}
		else if (frameinfo->height == 544) 
		{ 
			frameinfo->height = 540; 
		}
	}

	Easy_Bool bRet = 0;

	int iRet = 0;
	
	_channel_info* pChannel = (_channel_info*)_chPtr;

	//video frame
	if (_mediatype == EASY_SDK_VIDEO_FRAME_FLAG)
	{
		if(frameinfo && frameinfo->length)
		{
			if(frameinfo->type == EASY_SDK_VIDEO_FRAME_I)
			{
				if (NULL != pChannel->liveCallback)
				{
					pChannel->liveCallback(1, 1, "Recevied I Frame");
				}
				if(pChannel->fPusherInfo.rtmpHandle == 0)
				{
					//rtmp create
					pChannel->fPusherInfo.rtmpHandle = EasyRTMP_Create();
					if (pChannel->fPusherInfo.rtmpHandle == NULL)
					{
						if (NULL != pChannel->liveCallback)
						{
							pChannel->liveCallback(2, 2, "Fail to rtmp create failed ...");
						}
						return -1;
					}
					//rtmp callback
					EasyRTMP_SetCallback(pChannel->fPusherInfo.rtmpHandle, __EasyRTMP_Callback, pChannel);
					bRet = EasyRTMP_Connect(pChannel->fPusherInfo.rtmpHandle, pChannel->fCfgInfo.destRtmpAddr);
					if (!bRet)
					{
						if (NULL != pChannel->liveCallback)
						{
							pChannel->liveCallback(2, 2, "Fail to rtmp connect failed ");
						}
					}
					EASY_MEDIA_INFO_T mediaInfo;
					memset(&mediaInfo, 0, sizeof(EASY_MEDIA_INFO_T));
					//mediaInfo.u32VideoCodec = pChannel->fMediainfo.u32VideoCodec;
					mediaInfo.u32VideoFps = pChannel->fMediainfo.u32VideoFps == 0 ? 25 : pChannel->fMediainfo.u32VideoFps;
					//mediaInfo.u32AudioCodec = pChannel->fMediainfo.u32AudioCodec;
					mediaInfo.u32AudioSamplerate = pChannel->fMediainfo.u32AudioSamplerate ;				/* 音频采样率 */
					mediaInfo.u32AudioChannel = pChannel->fMediainfo.u32AudioChannel;					/* 音频通道数 */
					mediaInfo.u32AudioBitsPerSample = pChannel->fMediainfo.u32AudioBitsPerSample;		/* 音频采样精度 */
					//mediaInfo.u32VpsLength = pChannel->fMediainfo.u32VpsLength;
					//mediaInfo.u32SpsLength = pChannel->fMediainfo.u32SpsLength;
					//mediaInfo.u32PpsLength = pChannel->fMediainfo.u32PpsLength;
					//mediaInfo.u32SeiLength = pChannel->fMediainfo.u32SeiLength;
																										//rtmp 
					iRet = EasyRTMP_InitMetadata(pChannel->fPusherInfo.rtmpHandle, &mediaInfo, 1024);
					if (iRet < 0)
					{
						if (NULL != pChannel->liveCallback)
						{
							pChannel->liveCallback(2, 2, "Fail to Init Metadata ...");
						}
					}
				}

				EASY_AV_Frame avFrame;
				memset(&avFrame, 0, sizeof(EASY_AV_Frame));
				avFrame.u32AVFrameFlag = EASY_SDK_VIDEO_FRAME_FLAG;
				avFrame.u32AVFrameLen = frameinfo->length;
				avFrame.pBuffer = (unsigned char*)pbuf;
				avFrame.u32VFrameType = EASY_SDK_VIDEO_FRAME_I;
				//avFrame.u32TimestampSec = frameinfo->timestamp_sec;
				//avFrame.u32TimestampUsec = frameinfo->timestamp_usec;

				iRet = EasyRTMP_SendPacket(pChannel->fPusherInfo.rtmpHandle, &avFrame);
				if (iRet < 0)
				{
					if (NULL != pChannel->liveCallback)
					{
						pChannel->liveCallback(2, 2, "Fail to Send H264 Packet(I-frame) ...");
					}
				}
				else
				{
					if(!pChannel->fHavePrintKeyInfo)
					{
						pChannel->fHavePrintKeyInfo = true;
					}
				}
			}
			else
			{
				if(pChannel->fPusherInfo.rtmpHandle)
				{
					EASY_AV_Frame avFrame;
					memset(&avFrame, 0, sizeof(EASY_AV_Frame));
					avFrame.u32AVFrameFlag = EASY_SDK_VIDEO_FRAME_FLAG;
					avFrame.u32AVFrameLen = frameinfo->length-4;
					avFrame.pBuffer = (unsigned char*)pbuf+4;
					avFrame.u32VFrameType = EASY_SDK_VIDEO_FRAME_P;
					//avFrame.u32TimestampSec = frameinfo->timestamp_sec;
					//avFrame.u32TimestampUsec = frameinfo->timestamp_usec;
					iRet = EasyRTMP_SendPacket(pChannel->fPusherInfo.rtmpHandle, &avFrame);
					if (iRet < 0)
					{
						if (NULL != pChannel->liveCallback)
						{
							pChannel->liveCallback(2, 2, "Fail to Send H264 Packet(P-frame) ...");
						}
					}
					else
					{
						if(!pChannel->fHavePrintKeyInfo)
						{
						}
					}
				}
			}				
		}	
	}
	//音频帧
	if (_mediatype == EASY_SDK_AUDIO_FRAME_FLAG)
	{
		/* 音频编码 */
		// #define EASY_SDK_AUDIO_CODEC_AAC	0x15002			/* AAC */
		// #define EASY_SDK_AUDIO_CODEC_G711U	0x10006		/* G711 ulaw*/
		// #define EASY_SDK_AUDIO_CODEC_G711A	0x10007		/* G711 alaw*/
		// #define EASY_SDK_AUDIO_CODEC_G726	0x1100B		/* G726 */
		
		unsigned char* pSendBuffer = NULL;
		int nSendBufferLen = 0;
		if (frameinfo->codec == EASY_SDK_AUDIO_CODEC_AAC)
		{
			pSendBuffer =  (unsigned char*)pbuf;
			nSendBufferLen = frameinfo->length;
		} 
		else
		{
			// 音频转码 [8/17/2019 SwordTwelve]
			int bits_per_sample = frameinfo->bits_per_sample;
			int channels = frameinfo->channels;
			int sampleRate = frameinfo->sample_rate;

			if (EASY_SDK_AUDIO_CODEC_G711U   ==  frameinfo->codec
				|| EASY_SDK_AUDIO_CODEC_G726  ==  frameinfo->codec 
				|| EASY_SDK_AUDIO_CODEC_G711A == frameinfo->codec ) 
			{
				if (pChannel->fPusherInfo.pAACCacheBuffer == NULL)
				{
					int buf_size = BUFFER_SIZE;
					pChannel->fPusherInfo.pAACCacheBuffer  = new unsigned char[buf_size];
					memset(pChannel->fPusherInfo.pAACCacheBuffer , 0x00, buf_size);
				}

				if (pChannel->fPusherInfo.aacEncHandle == NULL)
				{
					InitParam initParam;
					initParam.u32AudioSamplerate=frameinfo->sample_rate;
					initParam.ucAudioChannel=frameinfo->channels;
					initParam.u32PCMBitSize=frameinfo->bits_per_sample;
					if (frameinfo->codec == EASY_SDK_AUDIO_CODEC_G711U)
					{
						initParam.ucAudioCodec = Law_ULaw;
					} 
					else if (frameinfo->codec == EASY_SDK_AUDIO_CODEC_G726)
					{
						initParam.ucAudioCodec = Law_G726;
					}
					else if (frameinfo->codec == EASY_SDK_AUDIO_CODEC_G711A)
					{
						initParam.ucAudioCodec = Law_ALaw;
					}
					pChannel->fPusherInfo.aacEncHandle = Easy_AACEncoder_Init( initParam);
				}
				unsigned int out_len = 0;
				//aac encode
				int nRet = Easy_AACEncoder_Encode(pChannel->fPusherInfo.aacEncHandle, (unsigned char*)pbuf, frameinfo->length, (unsigned char*)pChannel->fPusherInfo.pAACCacheBuffer, &out_len) ;
				if (nRet>0&&out_len>0)
				{
					pSendBuffer = (unsigned char*)pChannel->fPusherInfo.pAACCacheBuffer ;
					nSendBufferLen = out_len;
					frameinfo->codec = EASY_SDK_AUDIO_CODEC_AAC;
				} 
			}
		}

		if(pChannel->fPusherInfo.rtmpHandle && pSendBuffer && nSendBufferLen > 0)
		{
			EASY_AV_Frame avFrame;
			memset(&avFrame, 0, sizeof(EASY_AV_Frame));
			avFrame.u32AVFrameFlag = EASY_SDK_AUDIO_FRAME_FLAG;
			avFrame.u32AVFrameLen = nSendBufferLen;
			avFrame.pBuffer = (unsigned char*)pSendBuffer;
			//avFrame.u32TimestampSec = frameinfo->timestamp_sec;
			//avFrame.u32TimestampUsec = frameinfo->timestamp_usec;
			iRet = EasyRTMP_SendPacket(pChannel->fPusherInfo.rtmpHandle, &avFrame);
			if (iRet < 0)
			{
				if (NULL != pChannel->liveCallback)
				{
					pChannel->liveCallback(2, 2, "Fail to Send AAC Packet ...");
				}
			}
			else
			{
				if(!pChannel->fHavePrintKeyInfo)
				{
				}
			}
		}
	}
	//媒体帧
	if (_mediatype == EASY_SDK_MEDIA_INFO_FLAG)
	{
		if(pbuf != NULL)
		{
			EASY_MEDIA_INFO_T mediainfo;
			memset(&(pChannel->fMediainfo), 0x00, sizeof(EASY_MEDIA_INFO_T));
			memcpy(&(pChannel->fMediainfo), pbuf, sizeof(EASY_MEDIA_INFO_T));

			if (NULL != pChannel->liveCallback)
			{
				char desc[255];

				sprintf(desc, "RTSP DESCRIBE Get Media Info: video:%u fps:%u audio:%u channel:%u sampleRate:%u", pChannel->fMediainfo.u32VideoCodec, pChannel->fMediainfo.u32VideoFps, pChannel->fMediainfo.u32AudioCodec, pChannel->fMediainfo.u32AudioChannel, pChannel->fMediainfo.u32AudioSamplerate);
				
				pChannel->liveCallback(1, 1, desc);
			}
		}
	}
	//事件帧
	if (_mediatype == EASY_SDK_EVENT_FRAME_FLAG)
	{
		if (NULL == pbuf && NULL == frameinfo)
		{
			if (NULL != pChannel->liveCallback)
			{
				pChannel->liveCallback(1, 0, "connecting");
			}
		}
		else if (NULL != frameinfo && frameinfo->codec == EASY_SDK_EVENT_CODEC_ERROR)
		{
			int errorCode = EasyRTSP_GetErrCode(pChannel->fNVSHandle);
			sprintf(msg, "error:%s, %d :%s", pChannel->fCfgInfo.srcRtspAddr, errorCode, pbuf ? pbuf : "null");
			if (NULL != pChannel->liveCallback)
			{
				pChannel->liveCallback(1, 2, msg);
			}
		}
		else if (NULL != frameinfo && frameinfo->codec == EASY_SDK_EVENT_CODEC_EXIT)
		{
			int errorCode = EasyRTSP_GetErrCode(pChannel->fNVSHandle);
			sprintf(msg, "exit:%s,Error:%d", pChannel->fCfgInfo.srcRtspAddr, errorCode);
			if (NULL != pChannel->liveCallback)
			{
				pChannel->liveCallback(1, 2, msg);
			}
			//restart
			StopChannelRtsp(pChannel);
			StartChannelRtsp(pChannel, __RTSPSourceCallBack);
		}
		/*
		else if (NULL != frameinfo && frameinfo->codec == 0x7265636F)
		{
			printf("Connect Failed:%s ...\n", pChannel->fCfgInfo.srcRtspAddr);
		}
		*/
	}

	return 0;
}

//启动通道
#ifdef _WIN32
extern "C" __declspec(dllexport)
#endif
#ifndef _WIN32
extern "C"
{
#endif
bool StartChannel(int channelId, char *rtsp, char *rtmp, int option, int verbosity, LiveCallBack liveCallback)
{
	//RTMP Activate
	if (!rtmpActivate)
	{
		int ret = EasyRTMP_Activate(RTMP_KEY);
		switch (ret)
		{
		case EASY_ACTIVATE_INVALID_KEY:
			if (NULL != liveCallback)
			{
				liveCallback(2, 2, "KEY is EASY_ACTIVATE_INVALID_KEY!");
			}
			break;
		case EASY_ACTIVATE_TIME_ERR:
			if (NULL != liveCallback)
			{
				liveCallback(2, 2, "KEY is EASY_ACTIVATE_TIME_ERR!");
			}
			break;
		case EASY_ACTIVATE_PROCESS_NAME_LEN_ERR:
			if (NULL != liveCallback)
			{
				liveCallback(2, 2, "KEY is EASY_ACTIVATE_PROCESS_NAME_LEN_ERR!");
			}
			break;
		case EASY_ACTIVATE_PROCESS_NAME_ERR:
			if (NULL != liveCallback)
			{
				liveCallback(2, 2, "KEY is EASY_ACTIVATE_PROCESS_NAME_ERR!");
			}
			break;
		case EASY_ACTIVATE_VALIDITY_PERIOD_ERR:
			if (NULL != liveCallback)
			{
				liveCallback(2, 2, "KEY is EASY_ACTIVATE_VALIDITY_PERIOD_ERR!");
			}
			break;
		case EASY_ACTIVATE_SUCCESS:
			if (NULL != liveCallback)
			{
				liveCallback(2, 0, "KEY is EASY_ACTIVATE_SUCCESS!");
			}
			break;
		default:
			if (ret <= 0)
			{
				if (NULL != liveCallback)
				{
					liveCallback(2, 2, "RTMP Activate error.");
				}
			}
			else
			{
				if (NULL != liveCallback)
				{
					liveCallback(2, 0, "RTMP Activate Success!");
				}
			}
			break;
		}
		if (ret <= 0)
		{
			getchar();
			return false;
		}
		rtmpActivate = true;
	}

	//RTSP Activate
	if (!rtspActivate)
	{
		int ret = EasyRTSP_Activate(RTSP_KEY);
		switch (ret)
		{
		case EASY_ACTIVATE_INVALID_KEY:
			if (NULL != liveCallback)
			{
				liveCallback(1, 2, "KEY is EASY_ACTIVATE_INVALID_KEY!");
			}
			break;
		case EASY_ACTIVATE_TIME_ERR:
			if (NULL != liveCallback)
			{
				liveCallback(1, 2, "KEY is EASY_ACTIVATE_TIME_ERR!");
			}
			break;
		case EASY_ACTIVATE_PROCESS_NAME_LEN_ERR:
			if (NULL != liveCallback)
			{
				liveCallback(1, 2, "KEY is EASY_ACTIVATE_PROCESS_NAME_LEN_ERR!");
			}
			break;
		case EASY_ACTIVATE_PROCESS_NAME_ERR:
			if (NULL != liveCallback)
			{
				liveCallback(1, 2, "KEY is EASY_ACTIVATE_PROCESS_NAME_ERR!");
			}
			break;
		case EASY_ACTIVATE_VALIDITY_PERIOD_ERR:
			if (NULL != liveCallback)
			{
				liveCallback(1, 2, "KEY is EASY_ACTIVATE_VALIDITY_PERIOD_ERR!");
			}
			break;
		case EASY_ACTIVATE_PLATFORM_ERR:
			if (NULL != liveCallback)
			{
				liveCallback(1, 2, "EASY_ACTIVATE_PLATFORM_ERR!");
			}
			break;
		case EASY_ACTIVATE_COMPANY_ID_LEN_ERR:
			if (NULL != liveCallback)
			{
				liveCallback(1, 2, "EASY_ACTIVATE_COMPANY_ID_LEN_ERR!");
			}
			break;
		case EASY_ACTIVATE_SUCCESS:
			if (NULL != liveCallback)
			{
				liveCallback(1, 0, "KEY is EASY_ACTIVATE_SUCCESS!");
			}
			break;
		default:
			if (ret <= 0)
			{
				if (NULL != liveCallback)
				{
					liveCallback(1, 2, "RTSP Activate error.");
				}
			}
			else
			{
				if (NULL != liveCallback)
				{
					liveCallback(1, 0, "RTSP Activate Success!");
				}
			}
			break;
		}
		if (ret <= 0)
		{
			getchar();
			return false;
		}
		rtspActivate = true;
	}

	_channel_info* pChannelInfo = CreateChannel(channelId, rtsp, rtmp, option, verbosity);
	if (NULL == pChannelInfo)
	{
		return false;
	}
	//rtsp init
	EasyRTSP_Init(&(pChannelInfo->fNVSHandle));
	if (NULL == pChannelInfo->fNVSHandle)
	{
		return false;
	}

	pChannelInfo->liveCallback = liveCallback;

	if (!StartChannelRtsp(pChannelInfo, __RTSPSourceCallBack))
	{
		return false;
	}

	//add to channel list
	gChannelInfoList.push_back(pChannelInfo);

	return true;
}
#ifndef _WIN32
}
#endif

//release
void ReleaseSpace(void)
{
	std::list<_channel_info*>::iterator it;
	for(it = gChannelInfoList.begin(); it != gChannelInfoList.end(); it++)
	{
		_channel_info* pChannel = *it;

		StopChannel(pChannel);

		delete pChannel;
	}

	gChannelInfoList.clear();
}

//config
bool InitCfgInfo(int argc, char * argv[])
{
	//MAX_CHANNEL_INDEX
	int maxChannelIndex = GetIniKeyInt(CONF_SECTION_CHANNEL, CONF_KEY_MAX_CHANNEL_INDEX, CONF_FILE_PATH);
	if (maxChannelIndex < MAX_CHANNEL_INDEX)
	{
		maxChannelIndex = MAX_CHANNEL_INDEX;
	}

	printf("%s:%d\n", CONF_KEY_MAX_CHANNEL_INDEX, maxChannelIndex);
	
	if (argc >= 3)
	{
		int i = 0;
		gChannelInfoList.clear();

		StartChannel(i, argv[1], argv[2], (argc > 3 ? atoi(argv[3]) : 0), 1, NULL);
	}
	else
	{
		int i = 0;
		gChannelInfoList.clear();
		for (i = 0; i < maxChannelIndex; i++)
		{
			char channelName[255];

			sprintf(channelName, "channel%d", i);

			StartChannel(i, 
				GetIniKeyString(channelName, CONF_KEY_RTSP, CONF_FILE_PATH),
				GetIniKeyString(channelName, CONF_KEY_RTMP, CONF_FILE_PATH),
				GetIniKeyInt(channelName, "option", CONF_FILE_PATH),
				1,
				NULL);
		}
	}
	return true;
}

//main
int main(int argc, char * argv[])
{
	printf("EasyRTSPLive v2.0.19.0826(Free)\n");

	__time64_t tt = _time64(0i64);

	//splash
#ifdef _WIN32
		Sleep(3000);
#else
		sleep(3);
#endif

	

#ifdef _WIN32
	extern char* optarg;
#endif
	int ch;

	atexit(ReleaseSpace);

	

	//config
	InitCfgInfo(argc, argv);

	char str[4] = { 0 };

	while (strcmp(str, "exit") != 0)
	{
		scanf("%s", str);
	}

	//release
	ReleaseSpace();

	//exit
	exit(0);

	return 0;
}