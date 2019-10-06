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

typedef struct _channel_cfg_struct_t
{
	int channelId;
	int option;
	char channelName[64];
	char srcRtspAddr[512];
	char destRtmpAddr[512];
}_channel_cfg;

typedef struct _rtmp_pusher_struct_t
{
	Easy_Handle aacEncHandle;
	Easy_Handle rtmpHandle;
	unsigned int u32AudioCodec;	
	unsigned int u32AudioSamplerate;
	unsigned int u32AudioChannel;
	unsigned char* pAACCacheBuffer;
}_rtmp_pusher;

typedef struct _channel_info_struct_t
{
	_channel_cfg		fCfgInfo;
	_rtmp_pusher		fPusherInfo;
	Easy_Handle	fNVSHandle;
	FILE*				fLogHandle;
	bool				fHavePrintKeyInfo;
	EASY_MEDIA_INFO_T	fMediainfo;
}_channel_info;

static std::list <_channel_info*> gChannelInfoList;

//rtmp callback
int __EasyRTMP_Callback(int _frameType, char *pBuf, EASY_RTMP_STATE_T _state, void *_userPtr)
{
	_channel_info* pChannel = (_channel_info*)_userPtr;
	switch(_state)
	{
		case EASY_RTMP_STATE_CONNECTING:
			WriteChannelRtmpConnect(pChannel->fCfgInfo.channelName, "0", "connecting");
#ifdef TRACE_LOG
			//TRACE_LOG(pChannel->fLogHandle, "[rtmp][connect][connecting]%s\n", pChannel->fCfgInfo.destRtmpAddr);
#endif
			break;
		case EASY_RTMP_STATE_CONNECTED:
			WriteChannelRtmpConnect(pChannel->fCfgInfo.channelName, "1", "connected");
#ifdef TRACE_LOG
			TRACE_LOG(pChannel->fLogHandle, "[rtmp][connect][connected]%s\n", pChannel->fCfgInfo.destRtmpAddr);
#endif
			break;
		case EASY_RTMP_STATE_CONNECT_FAILED:
			WriteChannelRtmpConnect(pChannel->fCfgInfo.channelName, "2", "connect failed");
#ifdef TRACE_LOG
			TRACE_LOG(pChannel->fLogHandle, "[rtmp][connect][connect_failed]%s\n", pChannel->fCfgInfo.destRtmpAddr);
#endif
			break;
		case EASY_RTMP_STATE_CONNECT_ABORT:
			WriteChannelRtmpConnect(pChannel->fCfgInfo.channelName, "2", "connect abort");
#ifdef TRACE_LOG
			TRACE_LOG(pChannel->fLogHandle, "[rtmp][connect][connect_abort]%s\n", pChannel->fCfgInfo.destRtmpAddr);
#endif
			break;
		case EASY_RTMP_STATE_DISCONNECTED:
			WriteChannelRtmpConnect(pChannel->fCfgInfo.channelName, "2", "disconnect");
#ifdef TRACE_LOG
			TRACE_LOG(pChannel->fLogHandle, "[rtmp][connect][disconnect]%s\n", pChannel->fCfgInfo.destRtmpAddr);
#endif
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
				WriteChannelRtspConnect(pChannel->fCfgInfo.channelName, "1", "I Frame");
				if(pChannel->fPusherInfo.rtmpHandle == 0)
				{
					//rtmp create
					pChannel->fPusherInfo.rtmpHandle = EasyRTMP_Create();
					if (pChannel->fPusherInfo.rtmpHandle == NULL)
					{
						WriteChannelRtmpConnect(pChannel->fCfgInfo.channelName, "2", "Fail to rtmp create failed ...");
						//printf("[rtmp][create][failed]Fail to rtmp create failed ...\n");
#ifdef TRACE_LOG
						TRACE_LOG(pChannel->fLogHandle, "[rtmp][create][failed]Fail to rtmp create failed ...\n");
#endif
						return -1;
					}
					//rtmp callback
					EasyRTMP_SetCallback(pChannel->fPusherInfo.rtmpHandle, __EasyRTMP_Callback, pChannel);
					bRet = EasyRTMP_Connect(pChannel->fPusherInfo.rtmpHandle, pChannel->fCfgInfo.destRtmpAddr);
					if (!bRet)
					{
						WriteChannelRtmpConnect(pChannel->fCfgInfo.channelName, "2", "Fail to rtmp connect failed ");
						//printf("[rtmp][connect][failed]Fail to rtmp connect failed ...\n");
#ifdef TRACE_LOG
						TRACE_LOG(pChannel->fLogHandle, "[rtmp][connect][failed]Fail to rtmp connect failed ...\n");
#endif
					}
					EASY_MEDIA_INFO_T mediaInfo;
					memset(&mediaInfo, 0, sizeof(EASY_MEDIA_INFO_T));
					mediaInfo.u32VideoFps = pChannel->fMediainfo.u32VideoFps;
					mediaInfo.u32AudioSamplerate =pChannel->fMediainfo.u32AudioSamplerate ;				/* 音频采样率 */
					mediaInfo.u32AudioChannel = pChannel->fMediainfo.u32AudioChannel;					/* 音频通道数 */
					mediaInfo.u32AudioBitsPerSample = pChannel->fMediainfo.u32AudioBitsPerSample;		/* 音频采样精度 */
					//rtmp 
					iRet = EasyRTMP_InitMetadata(pChannel->fPusherInfo.rtmpHandle, &mediaInfo, 1024);
					if (iRet < 0)
					{
						WriteChannelRtmpConnect(pChannel->fCfgInfo.channelName, "2", "Fail to Init Metadata ...");
						//printf("[rtmp][init metadata][failed]Fail to Init Metadata ...\n");
#ifdef TRACE_LOG
						TRACE_LOG(pChannel->fLogHandle, "[rtmp][init metadata][failed]Fail to Init Metadata ...\n");
#endif
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
					WriteChannelRtmpConnect(pChannel->fCfgInfo.channelName, "2", "Fail to Send H264 Packet(I-frame) ...");
					//printf("[rtmp][send][failed]Fail to Send H264 Packet(I-frame) ...\n");
#ifdef TRACE_LOG
					TRACE_LOG(pChannel->fLogHandle, "[rtmp][send][failed]Fail to Send H264 Packet(I-frame) ...\n");
#endif
				}
				else
				{
					if(!pChannel->fHavePrintKeyInfo)
					{
#ifdef TRACE_LOG
						TRACE_LOG(pChannel->fLogHandle, "I\n");
#endif
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
						WriteChannelRtmpConnect(pChannel->fCfgInfo.channelName, "2", "Fail to Send H264 Packet(P-frame) ...");
						//printf("[rtmp][send][failed]Fail to Send H264 Packet(P-frame) ...\n");
#ifdef TRACE_LOG
						TRACE_LOG(pChannel->fLogHandle, "[rtmp][send][failed]Fail to Send H264 Packet(P-frame) ...\n");
#endif
					}
					else
					{
						if(!pChannel->fHavePrintKeyInfo)
						{
#ifdef TRACE_LOG
							TRACE_LOG(pChannel->fLogHandle, "P\n");
#endif
						}
					}
				}
			}				
		}	
	}
	//audio frame
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
				int nRet = Easy_AACEncoder_Encode(pChannel->fPusherInfo.aacEncHandle, 
					(unsigned char*)pbuf, frameinfo->length, (unsigned char*)pChannel->fPusherInfo.pAACCacheBuffer, &out_len) ;
				if (nRet>0&&out_len>0)
				{
					pSendBuffer = (unsigned char*)pChannel->fPusherInfo.pAACCacheBuffer ;
					nSendBufferLen = out_len;
					frameinfo->codec = EASY_SDK_AUDIO_CODEC_AAC;
				} 
			}
		}

		if(pChannel->fPusherInfo.rtmpHandle&&pSendBuffer&&nSendBufferLen>0)
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
				WriteChannelRtmpConnect(pChannel->fCfgInfo.channelName, "2", "Fail to Send AAC Packet ...");
				//printf("[aac][send][failed]Fail to Send AAC Packet ...\n");
#ifdef TRACE_LOG
				TRACE_LOG(pChannel->fLogHandle, "[aac][send][failed]Fail to Send AAC Packet ...\n");
#endif
			}
			else
			{
				if(!pChannel->fHavePrintKeyInfo)
				{
#ifdef TRACE_LOG
					TRACE_LOG(pChannel->fLogHandle, "Audio\n");
#endif
				}
			}
		}
	}

	// media info frame
	if (_mediatype == EASY_SDK_MEDIA_INFO_FLAG)
	{
		if(pbuf != NULL)
		{
			EASY_MEDIA_INFO_T mediainfo;
			memset(&(pChannel->fMediainfo), 0x00, sizeof(EASY_MEDIA_INFO_T));
			memcpy(&(pChannel->fMediainfo), pbuf, sizeof(EASY_MEDIA_INFO_T));
#ifdef TRACE_LOG
			TRACE_LOG(pChannel->fLogHandle,"RTSP DESCRIBE Get Media Info: video:%u fps:%u audio:%u channel:%u sampleRate:%u \n", 
				pChannel->fMediainfo.u32VideoCodec, pChannel->fMediainfo.u32VideoFps, pChannel->fMediainfo.u32AudioCodec, pChannel->fMediainfo.u32AudioChannel, pChannel->fMediainfo.u32AudioSamplerate);
#endif
		}
	}

	//event frame
	if (_mediatype == EASY_SDK_EVENT_FRAME_FLAG)
	{
		if (NULL == pbuf && NULL == frameinfo)
		{
			WriteChannelRtspConnect(pChannel->fCfgInfo.channelName, "0", "connecting");
			//printf("[rtsp][connect][connecting]:%s ...\n", pChannel->fCfgInfo.srcRtspAddr);
#ifdef TRACE_LOG
			TRACE_LOG(pChannel->fLogHandle, "[rtsp][connect][connecting]:%s ...\n", pChannel->fCfgInfo.srcRtspAddr);
#endif
		}
		else if (NULL != frameinfo && frameinfo->codec == EASY_SDK_EVENT_CODEC_ERROR)
		{
			int errorCode = EasyRTSP_GetErrCode(pChannel->fNVSHandle);
			sprintf(msg, "error:%s, %d :%s", pChannel->fCfgInfo.srcRtspAddr, errorCode, pbuf ? pbuf : "null");
			WriteChannelRtspConnect(pChannel->fCfgInfo.channelName, "2", msg);
			//printf("[rtsp][connect][error]:%s, %d :%s ...\n", pChannel->fCfgInfo.srcRtspAddr, errorCode, pbuf ? pbuf : "null");
#ifdef TRACE_LOG
			TRACE_LOG(pChannel->fLogHandle, "[rtsp][connect][error]:%s, %d :%s ...\n", pChannel->fCfgInfo.srcRtspAddr, errorCode, pbuf ? pbuf : "null");
#endif
		}
		else if (NULL != frameinfo && frameinfo->codec == EASY_SDK_EVENT_CODEC_EXIT)
		{
			int errorCode = EasyRTSP_GetErrCode(pChannel->fNVSHandle);
			sprintf(msg, "exit:%s,Error:%d", pChannel->fCfgInfo.srcRtspAddr, errorCode);
			WriteChannelRtspConnect(pChannel->fCfgInfo.channelName, "2", msg);
			//printf("[rtsp][connect][exit]:%s,Error:%d ...\n", pChannel->fCfgInfo.srcRtspAddr, errorCode);
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

//release
void ReleaseSpace(void)
{
	std::list<_channel_info*>::iterator it;
	for(it = gChannelInfoList.begin(); it != gChannelInfoList.end(); it++)
	{
		_channel_info* pChannel = *it;

		if (NULL != pChannel->fNVSHandle) 
		{
			//rtsp close
			EasyRTSP_CloseStream(pChannel->fNVSHandle);
			//rtsp deinit
			EasyRTSP_Deinit(&(pChannel->fNVSHandle));
			pChannel->fNVSHandle = NULL;
		}

		if (NULL != pChannel->fPusherInfo.rtmpHandle)
		{
			//rtmp release
			EasyRTMP_Release(pChannel->fPusherInfo.rtmpHandle);
			pChannel->fPusherInfo.rtmpHandle = NULL;
		}

		if(pChannel->fLogHandle)
		{
			//log close
			TRACE_CloseLogFile(pChannel->fLogHandle);
			pChannel->fLogHandle = NULL;
		}
		if (pChannel->fPusherInfo.aacEncHandle )
		{
			//aac release
			Easy_AACEncoder_Release(pChannel->fPusherInfo.aacEncHandle );
			pChannel->fPusherInfo.aacEncHandle  = NULL;
		}
		if (pChannel->fPusherInfo.pAACCacheBuffer )
		{
			delete[] pChannel->fPusherInfo.pAACCacheBuffer;
			pChannel->fPusherInfo.pAACCacheBuffer = NULL;
		}

		//connect
		WriteChannelRtspConnect(pChannel->fCfgInfo.channelName, "0", "");
		WriteChannelRtmpConnect(pChannel->fCfgInfo.channelName, "0", "");

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
		_channel_info* pChannelInfo = new _channel_info();
		if (pChannelInfo)
		{
			memset(pChannelInfo, 0, sizeof(_channel_info));
			pChannelInfo->fCfgInfo.channelId = i;
			pChannelInfo->fHavePrintKeyInfo = false;
			sprintf(pChannelInfo->fCfgInfo.channelName, "channel%d", i);
			//strcpy(pChannelInfo->fCfgInfo.srcRtspAddr, GetIniKeyString(pChannelInfo->fCfgInfo.channelName, "rtsp", CONF_FILE_PATH));
			strcpy(pChannelInfo->fCfgInfo.srcRtspAddr, argv[1]);
			//strcpy(pChannelInfo->fCfgInfo.destRtmpAddr, GetIniKeyString(pChannelInfo->fCfgInfo.channelName, "rtmp", CONF_FILE_PATH));
			strcpy(pChannelInfo->fCfgInfo.destRtmpAddr, argv[2]);
			//pChannelInfo->fCfgInfo.option = GetIniKeyInt(pChannelInfo->fCfgInfo.channelName, "option", CONF_FILE_PATH);
			pChannelInfo->fCfgInfo.option = argc > 3 ? atoi(argv[3]) : 0;
			if (strlen(pChannelInfo->fCfgInfo.srcRtspAddr) > 0 && strlen(pChannelInfo->fCfgInfo.destRtmpAddr) > 0)
			{
				gChannelInfoList.push_back(pChannelInfo);
			}
		}
	}
	else
	{
		int i = 0;
		gChannelInfoList.clear();
		for (i = 0; i < maxChannelIndex; i++)
		{
			_channel_info* pChannelInfo = new _channel_info();
			if (pChannelInfo)
			{
				memset(pChannelInfo, 0, sizeof(_channel_info));
				pChannelInfo->fCfgInfo.channelId = i;
				pChannelInfo->fHavePrintKeyInfo = false;
				sprintf(pChannelInfo->fCfgInfo.channelName, "channel%d", i);
				strcpy(pChannelInfo->fCfgInfo.srcRtspAddr, GetIniKeyString(pChannelInfo->fCfgInfo.channelName, CONF_KEY_RTSP, CONF_FILE_PATH));
				strcpy(pChannelInfo->fCfgInfo.destRtmpAddr, GetIniKeyString(pChannelInfo->fCfgInfo.channelName, CONF_KEY_RTMP, CONF_FILE_PATH));
				pChannelInfo->fCfgInfo.option = GetIniKeyInt(pChannelInfo->fCfgInfo.channelName, "option", CONF_FILE_PATH);
				if (strlen(pChannelInfo->fCfgInfo.srcRtspAddr) > 0 && strlen(pChannelInfo->fCfgInfo.destRtmpAddr) > 0)
				{
					gChannelInfoList.push_back(pChannelInfo);
				}
			}
		}
	}
	return true;
}

//main
int main(int argc, char * argv[])
{
	printf("EasyRTSPLive v2.0.19.0826(Free)\n");

	//splash
#ifdef _WIN32
		Sleep(3000);
#else
		sleep(3);
#endif

	//config
	InitCfgInfo(argc, argv);

	//RTMP Activate
	int iret = 0;
	iret = EasyRTMP_Activate(RTMP_KEY);
	switch (iret)
	{
		case EASY_ACTIVATE_INVALID_KEY:
			WriteChannelRtmpConnect(CONF_SECTION_CHANNEL, "2", "KEY is EASY_ACTIVATE_INVALID_KEY!");
			printf("[rtmp][activate][failed]KEY is EASY_ACTIVATE_INVALID_KEY!\n");
			break;
		case EASY_ACTIVATE_TIME_ERR:
			WriteChannelRtmpConnect(CONF_SECTION_CHANNEL, "2", "KEY is EASY_ACTIVATE_TIME_ERR!");
			printf("[rtmp][activate][failed]KEY is EASY_ACTIVATE_TIME_ERR!\n");
			break;
		case EASY_ACTIVATE_PROCESS_NAME_LEN_ERR:
			WriteChannelRtmpConnect(CONF_SECTION_CHANNEL, "2", "KEY is EASY_ACTIVATE_PROCESS_NAME_LEN_ERR!");
			printf("[rtmp][activate][failed]KEY is EASY_ACTIVATE_PROCESS_NAME_LEN_ERR!\n");
			break;
		case EASY_ACTIVATE_PROCESS_NAME_ERR:
			WriteChannelRtmpConnect(CONF_SECTION_CHANNEL, "2", "KEY is EASY_ACTIVATE_PROCESS_NAME_ERR!");
			printf("[rtmp][activate][failed]KEY is EASY_ACTIVATE_PROCESS_NAME_ERR!\n");
			break;
		case EASY_ACTIVATE_VALIDITY_PERIOD_ERR:
			WriteChannelRtmpConnect(CONF_SECTION_CHANNEL, "2", "KEY is EASY_ACTIVATE_VALIDITY_PERIOD_ERR!");
			printf("[rtmp][activate][failed]KEY is EASY_ACTIVATE_VALIDITY_PERIOD_ERR!\n");
			break;
		case EASY_ACTIVATE_SUCCESS:
			WriteChannelRtmpConnect(CONF_SECTION_CHANNEL, "1", "KEY is EASY_ACTIVATE_SUCCESS!");
			printf("[rtmp][activate][sucess]KEY is EASY_ACTIVATE_SUCCESS!\n");
			break;
		default:
			if (iret <= 0)
			{
				WriteChannelRtmpConnect(CONF_SECTION_CHANNEL, "2", "RTMP Activate error.");
				printf("[rtmp][activate][failed]RTMP Activate error. ret=%d!!!\n", iret);
			}
			else
			{
				WriteChannelRtmpConnect(CONF_SECTION_CHANNEL, "1", "RTMP Activate Success!");
				printf("[rtmp][activate][sucess]RTMP Activate Success!\n");
			}
			break;
	}
	if (iret <= 0)
	{
		getchar();
		return -1;
	}

#ifdef _WIN32
	extern char* optarg;
#endif
	int ch;

	atexit(ReleaseSpace);

	//RTSP Activate
	iret = 0;
	iret = EasyRTSP_Activate(RTSP_KEY);
	switch (iret)
	{
		case EASY_ACTIVATE_INVALID_KEY:
			WriteChannelRtspConnect(CONF_SECTION_CHANNEL, "2", "KEY is EASY_ACTIVATE_INVALID_KEY!");
			printf("[rtsp][activate][failed]KEY is EASY_ACTIVATE_INVALID_KEY!\n");
			break;
		case EASY_ACTIVATE_TIME_ERR:
			WriteChannelRtspConnect(CONF_SECTION_CHANNEL, "2", "KEY is EASY_ACTIVATE_TIME_ERR!");
			printf("[rtsp][activate][failed]KEY is EASY_ACTIVATE_TIME_ERR!\n");
			break;
		case EASY_ACTIVATE_PROCESS_NAME_LEN_ERR:
			WriteChannelRtspConnect(CONF_SECTION_CHANNEL, "2", "KEY is EASY_ACTIVATE_PROCESS_NAME_LEN_ERR!");
			printf("[rtsp][activate][failed]KEY is EASY_ACTIVATE_PROCESS_NAME_LEN_ERR!\n");
			break;
		case EASY_ACTIVATE_PROCESS_NAME_ERR:
			WriteChannelRtspConnect(CONF_SECTION_CHANNEL, "2", "KEY is EASY_ACTIVATE_PROCESS_NAME_ERR!");
			printf("[rtsp][activate][failed]KEY is EASY_ACTIVATE_PROCESS_NAME_ERR!\n");
			break;
		case EASY_ACTIVATE_VALIDITY_PERIOD_ERR:
			WriteChannelRtspConnect(CONF_SECTION_CHANNEL, "2", "KEY is EASY_ACTIVATE_VALIDITY_PERIOD_ERR!");
			printf("[rtsp][activate][failed]KEY is EASY_ACTIVATE_VALIDITY_PERIOD_ERR!\n");
			break;
		case EASY_ACTIVATE_PLATFORM_ERR:
			WriteChannelRtspConnect(CONF_SECTION_CHANNEL, "2", "EASY_ACTIVATE_PLATFORM_ERR!");
			printf("[rtsp][activate][failed]EASY_ACTIVATE_PLATFORM_ERR!\n");
			break;
		case EASY_ACTIVATE_COMPANY_ID_LEN_ERR:
			WriteChannelRtspConnect(CONF_SECTION_CHANNEL, "2", "EASY_ACTIVATE_COMPANY_ID_LEN_ERR!");
			printf("[rtsp][activate][failed]EASY_ACTIVATE_COMPANY_ID_LEN_ERR!\n");
			break;
		case EASY_ACTIVATE_SUCCESS:
			WriteChannelRtspConnect(CONF_SECTION_CHANNEL, "1", "KEY is EASY_ACTIVATE_SUCCESS!");
			printf("[rtsp][activate][success]KEY is EASY_ACTIVATE_SUCCESS!\n");
			break;
		default:
			if (iret <= 0)
			{
				WriteChannelRtspConnect(CONF_SECTION_CHANNEL, "2", "RTSP Activate error.");
				printf("[rtsp][activate][failed]RTSP Activate error. ret=%d!!!\n", iret);
			}
			else
			{
				WriteChannelRtspConnect(CONF_SECTION_CHANNEL, "1", "RTSP Activate Success!");
				printf("[rtsp][activate][success]RTSP Activate Success!\n");
			}
			break;
	}
	if(iret <= 0)
	{
		getchar();
		return -2;
	}

	std::list<_channel_info*>::iterator it;

	//rtsp channel list connect
	for(it = gChannelInfoList.begin(); it != gChannelInfoList.end(); it++)
	{
		_channel_info* pChannel = *it;

#ifdef TRACE_LOG
		//log handle
		pChannel->fLogHandle = TRACE_OpenLogFile(pChannel->fCfgInfo.channelName);
#endif

#ifdef TRACE_LOG
		TRACE_LOG(pChannel->fLogHandle, "channel[%d] rtsp addr : %s\n", pChannel->fCfgInfo.channelId, pChannel->fCfgInfo.srcRtspAddr);
		TRACE_LOG(pChannel->fLogHandle, "channel[%d] rtmp addr : %s\n", pChannel->fCfgInfo.channelId, pChannel->fCfgInfo.destRtmpAddr);
#endif
		//rtsp init
		EasyRTSP_Init(&(pChannel->fNVSHandle));

		if (NULL == pChannel->fNVSHandle)
		{
			WriteChannelRtspConnect(pChannel->fCfgInfo.channelName, "2", "rtsp init error.");
#ifdef TRACE_LOG
			TRACE_LOG(pChannel->fLogHandle, "%s rtsp init error. ret=%d!!!\n", pChannel->fCfgInfo.channelName , iret);
#endif
			continue;
		}

		unsigned int mediaType = EASY_SDK_VIDEO_FRAME_FLAG | EASY_SDK_AUDIO_FRAME_FLAG;
		
		//rtsp callback
		EasyRTSP_SetCallback(pChannel->fNVSHandle, __RTSPSourceCallBack);

		//rtsp log level
		int verbosity = GetIniKeyInt(CONF_SECTION_CHANNEL, "verbosity", CONF_FILE_PATH);
		printf("%s:%d\n", CONF_KEY_VERBOSITY, verbosity);

		//rtsp open
		EasyRTSP_OpenStream(pChannel->fNVSHandle, pChannel->fCfgInfo.channelId, pChannel->fCfgInfo.srcRtspAddr, EASY_RTP_OVER_TCP, mediaType, 0, 0, pChannel, 1000, 0, pChannel->fCfgInfo.option, verbosity);
	
		//connect
		WriteChannelRtspConnect(pChannel->fCfgInfo.channelName, "0", "");
		WriteChannelRtmpConnect(pChannel->fCfgInfo.channelName, "0", "");
	}

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