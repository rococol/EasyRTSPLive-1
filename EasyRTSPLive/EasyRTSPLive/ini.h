#ifndef __INI_H__
#define __INI_H__

#ifdef __cplusplus
extern "C" {
#endif
//��INI�ļ���ȡ�ַ�����������  
extern char *GetIniKeyString(char *title,char *key,char *filename);

//��INI�ļ���ȡ����������  
extern int GetIniKeyInt(char *title,char *key,char *filename);

#ifdef __cplusplus
}
#endif

#endif//__INI_H__