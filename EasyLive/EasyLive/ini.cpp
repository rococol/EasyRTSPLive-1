#define _CRT_SECURE_NO_WARNINGS
#define CONF_FILE_PATH  "easyrtsplive.ini"
#define CONF_KEY_RTSP_CONNECT "rtsp_connect"
#define CONF_KEY_RTSP_MESSAGE "rtsp_message"
#define CONF_KEY_RTMP_CONNECT "rtmp_connect"
#define CONF_KEY_RTMP_MESSAGE "rtmp_message"

#include "ini.h"

//从INI文件读取字符串类型数据  
char *GetIniKeyString(char *title,char *key,char *filename)   
{   
	CSimpleIniA ini;
	ini.SetUnicode();
	ini.LoadFile(filename);
	const char *value = ini.GetValue(title, key, "");
	char *val = new char[strlen(value) + 1];
	strcpy(val, value);
	return val;
}  
  
//从INI文件读取整类型数据  
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

//从INI文件写入字符串类型数据 
void WriteIniKeyString(const char* title, const char* key, char* val, const char* filename)
{
	CSimpleIniA ini;
	ini.SetUnicode();
	ini.LoadFile(filename);
	if (ini.SetValue(title, key, val) < 0)
	{
		return;
	}
	if (ini.SaveFile(filename) < 0)
	{
		return;
	}
}

void WriteChannelRtspConnect(char *channelName, char *connect, char *message)
{
	char filename[512] = {};
	strcat(filename, channelName);
	strcat(filename, ".ini");
	WriteIniKeyString(channelName, "rtsp_connect", connect, filename);
	WriteIniKeyString(channelName, "rtsp_message", message, filename);
}

void WriteChannelRtmpConnect(char *channelName, char *connect, char *message)
{
	char filename[512] = {};
	strcat(filename, channelName);
	strcat(filename, ".ini");
	WriteIniKeyString(channelName, "rtmp_connect", connect, filename);
	WriteIniKeyString(channelName, "rtmp_message", message, filename);
}

