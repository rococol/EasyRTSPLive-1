#ifndef __TRACE_H__
#define __TRACE_H__

#include <stdio.h>
#ifndef _WIN32
#include <signal.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#endif
#define MAX_PATH 260

extern FILE* TRACE_OpenLogFile(const char *filenameprefix);
extern void TRACE_CloseLogFile(FILE* fLog);
extern void TRACE(char* szFormat, ...);
extern void TRACE_LOG(FILE* fLog, const char* szFormat, ...);

#endif

#include "EasyTypes.h"
#include "EasyRTSPClientAPI.h"
#include "EasyRTMPAPI.h"
#include "EasyAACEncoderAPI.h"

#ifdef _WIN32
#include "windows.h"
#else
#include <string.h>
#include <unistd.h>
#endif

#ifdef _WIN32
typedef void(__stdcall *ResultCallBack)(int code, char *msg);
#else
typedef void(*ResultCallBack)(int code, char *msg);
#endif

#ifdef _WIN32
typedef int(__stdcall *LiveCallBack)(int rtspOrRtmp, int code, char *msg);
#else
typedef int(*LiveCallBack)(int rtspOrRtmp, int code, char *msg);
#endif

//ͨ�����ýṹ
typedef struct _channel_cfg_struct_t
{
	//ͨ��Id
	int channelId;
	//ѡ��
	int option;
	//ͨ������
	char channelName[64];
	//rtsp��ַ
	char srcRtspAddr[512];
	//rtmp��ַ
	char destRtmpAddr[512];
}_channel_cfg;

typedef struct _rtmp_pusher_struct_t
{
	Easy_Handle aacEncHandle;
	//rtmp���
	Easy_Handle rtmpHandle;
	unsigned int u32AudioCodec;
	unsigned int u32AudioSamplerate;
	unsigned int u32AudioChannel;
	unsigned char* pAACCacheBuffer;
}_rtmp_pusher;

//ͨ����Ϣ�ṹ
typedef struct _channel_info_struct_t
{
	//ͨ��������Ϣ
	_channel_cfg		fCfgInfo;
	//������Ϣ
	_rtmp_pusher		fPusherInfo;
	//rtsp���
	Easy_Handle	fNVSHandle;
	FILE*				fLogHandle;
	bool				fHavePrintKeyInfo;
	//ý����Ϣ
	EASY_MEDIA_INFO_T	fMediainfo;
	//�ص�
	LiveCallBack liveCallback;
	//��־����
	int verbosity;
}_channel_info;

//����ͨ���ṹ
extern _channel_info* CreateChannel(int channelId, char *rtsp, char *rtmp, int option, int verbosity);

//�ر�ͨ��rtsp
extern void StopChannelRtsp(_channel_info* pChannel);

extern bool StartChannelRtsp(_channel_info* pChannelInfo, RTSPSourceCallBack _callback);

extern void StopChannelPusher(_channel_info* pChannel);
