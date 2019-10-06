#define _CRT_SECURE_NO_WARNINGS
#define CONF_FILE_PATH  "easyrtsplive.ini"
#define CONF_KEY_RTSP_CONNECT "rtsp_connect"
#define CONF_KEY_RTSP_MESSAGE "rtsp_message"
#define CONF_KEY_RTMP_CONNECT "rtmp_connect"
#define CONF_KEY_RTMP_MESSAGE "rtmp_message"

#include "ini.h"

//��INI�ļ���ȡ�ַ�����������  
char *GetIniKeyString(char *title,char *key,char *filename)   
{   
	CSimpleIniA ini;
	ini.LoadFile(filename);
	const char *value = ini.GetValue(title, key, "");
	char *val = new char[strlen(value) + 1];
	strcpy(val, value);
	return val;
}  
  
//��INI�ļ���ȡ����������  
int GetIniKeyInt(char *title,char *key,char *filename)  
{  
	int ret = 0;
	char* strValue = GetIniKeyString(title,key,filename);
	if(strlen(strValue) <= 0)
	{
		return ret;
	}
	else
	{
		ret = atoi(strValue);
	}

    return ret;  
} 

//��INI�ļ�д���ַ����������� 
void WriteIniKeyString(const char* title, const char* key, char* val, const char* filename)
{
	CSimpleIniA ini;
	ini.LoadFile(filename);
	ini.SetValue(title, key, val);
	ini.SaveFile(filename);
}

void WriteChannelRtspConnect(char *channelName, char *connect, char *message)
{
	WriteIniKeyString(channelName, "rtsp_connect", connect, CONF_FILE_PATH);
	WriteIniKeyString(channelName, "rtsp_message", message, CONF_FILE_PATH);
}

void WriteChannelRtmpConnect(char *channelName, char *connect, char *message)
{
	WriteIniKeyString(channelName, "rtmp_connect", connect, CONF_FILE_PATH);
	WriteIniKeyString(channelName, "rtmp_message", message, CONF_FILE_PATH);
}

