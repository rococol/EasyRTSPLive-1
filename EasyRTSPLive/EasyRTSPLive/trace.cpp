#define _CRT_SECURE_NO_WARNINGS
#include "trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

FILE* TRACE_OpenLogFile(const char * filenameprefix)
{
    FILE* fLog = NULL;
    char szTime[64] = {0,};
    time_t tt = time(NULL);
    struct tm *_timetmp = NULL;
    _timetmp = localtime(&tt);
    if (NULL != _timetmp)   strftime(szTime, 32, "%Y%m%d_%H%M%S",_timetmp);

    if (NULL == fLog)
    {
        char szFile[MAX_PATH] = {0,};
        sprintf(szFile, "%s.%s.log", filenameprefix,szTime);
        fLog = fopen(szFile, "wb");
    }

	return fLog;
}

void TRACE_CloseLogFile(FILE* fLog)
{
    if (NULL != fLog)
    {
        fclose(fLog);
        fLog = NULL;
    }
}

void TRACE(char* szFormat, ...)
{
	char buff[1024] = {0,};
	va_list args;
	va_start(args,szFormat);
	#ifdef _WIN32
	_vsnprintf(buff, 1023, szFormat,args);
	#else
	vsnprintf(buff, 1023, szFormat,args);
	#endif
	va_end(args);

	printf(buff);
}

void TRACE_LOG(FILE* fLog, const char *szFormat, ...)
{
	char buff[1024] = {0,};

	va_list args;

	va_start(args,szFormat);
	#ifdef _WIN32
	_vsnprintf(buff, 1023, szFormat,args);
	#else
	vsnprintf(buff, 1023, szFormat,args);
	#endif
	va_end(args);

	if (NULL != fLog)
	{
		char szTime[64] = {0,};
		time_t tt = time(NULL);
		struct tm *_timetmp = NULL;
		_timetmp = localtime(&tt);
		if (NULL != _timetmp)	strftime(szTime, 32, "%Y-%m-%d %H:%M:%S ", _timetmp);

		fwrite(szTime, 1, (int)strlen(szTime), fLog);

		fwrite(buff, 1, (int)strlen(buff), fLog);
		fflush(fLog);
	}
}

//创建通道结构
_channel_info* CreateChannel(int channelId, char *rtsp, char *rtmp, int option, int verbosity)
{
	_channel_info* pChannelInfo = new _channel_info();
	if (pChannelInfo)
	{
		memset(pChannelInfo, 0, sizeof(_channel_info));
		pChannelInfo->fCfgInfo.channelId = channelId;
		pChannelInfo->fHavePrintKeyInfo = false;
		sprintf(pChannelInfo->fCfgInfo.channelName, "channel%d", channelId);
		strcpy(pChannelInfo->fCfgInfo.srcRtspAddr, rtsp);
		strcpy(pChannelInfo->fCfgInfo.destRtmpAddr, rtmp);
		pChannelInfo->fCfgInfo.option = option;
		pChannelInfo->verbosity = verbosity;
		if (strlen(pChannelInfo->fCfgInfo.srcRtspAddr) > 0 && strlen(pChannelInfo->fCfgInfo.destRtmpAddr) > 0)
		{
			return pChannelInfo;
		}
		else
		{
			return NULL;
		}
	}
	else
	{
		return NULL;
	}
}

//关闭通道rtsp
void StopChannelRtsp(_channel_info* pChannel)
{
	if (NULL != pChannel->fNVSHandle)
	{
		//rtsp close
		EasyRTSP_CloseStream(pChannel->fNVSHandle);
		//rtsp deinit
		EasyRTSP_Deinit(&(pChannel->fNVSHandle));
		pChannel->fNVSHandle = NULL;
	}
}

bool StartChannelRtsp(_channel_info* pChannelInfo, RTSPSourceCallBack _callback)
{
	if (NULL == pChannelInfo)
	{
		return false;
	}
	//stop rtsp
	StopChannelRtsp(pChannelInfo);
	//init rtsp
	EasyRTSP_Init(&(pChannelInfo->fNVSHandle));
	if (NULL == pChannelInfo->fNVSHandle)
	{
		return false;
	}
	unsigned int mediaType = EASY_SDK_VIDEO_FRAME_FLAG | EASY_SDK_AUDIO_FRAME_FLAG;

	//rtsp callback
	EasyRTSP_SetCallback(pChannelInfo->fNVSHandle, _callback);

	//rtsp open
	EasyRTSP_OpenStream(pChannelInfo->fNVSHandle, pChannelInfo->fCfgInfo.channelId, pChannelInfo->fCfgInfo.srcRtspAddr, EASY_RTP_OVER_TCP, mediaType, 0, 0, pChannelInfo, 1000, 0, pChannelInfo->fCfgInfo.option, pChannelInfo->verbosity);

	return true;
}

//关闭通道推流
void StopChannelPusher(_channel_info* pChannel)
{
	if (NULL != pChannel->fPusherInfo.rtmpHandle)
	{
		//rtmp release
		EasyRTMP_Release(pChannel->fPusherInfo.rtmpHandle);
		pChannel->fPusherInfo.rtmpHandle = NULL;
	}

	if (pChannel->fPusherInfo.aacEncHandle)
	{
		//aac release
		Easy_AACEncoder_Release(pChannel->fPusherInfo.aacEncHandle);
		pChannel->fPusherInfo.aacEncHandle = NULL;
	}

	if (pChannel->fPusherInfo.pAACCacheBuffer)
	{
		delete[] pChannel->fPusherInfo.pAACCacheBuffer;
		pChannel->fPusherInfo.pAACCacheBuffer = NULL;
	}
}
