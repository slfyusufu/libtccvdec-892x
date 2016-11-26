//********************************************************************************************
/**
 * @file        tcc_vdec_api.h
 * @brief		Decode H264 frame and display image onto screen through overlay driver. 
 * 				This interface contain : 
 *
 * @author      Yusuf.Sha, Telechips Shenzhen Rep.
 * @date        2016/11/08
 */
//********************************************************************************************

#ifndef	__TCC_VDEC_API_H__
#define	__TCC_VDEC_API_H__

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

typedef unsigned int uint32_t;
#define CONFIG_ARCH_TCC892X
#define CONFIG_TCC_INVITE

#ifdef	__cplusplus
extern "C"{
#endif

//all function return 0 means success, -1 means error

extern int tcc_vdec_open(void);
extern int tcc_vdec_close(void);
extern int tcc_vdec_process_annexb_header( unsigned char* data, int datalen);
extern int tcc_vdec_process( unsigned char* data, int size);
extern void tcc_vdec_SetViewFlag(int isValid);
extern int tcc_vdec_init(unsigned int width, unsigned int height);

#ifdef	__cplusplus
}
#endif

#endif	// __TCC_VDEC_API_H__
