//********************************************************************************************
/**
 * @file        tcc_vdec_api.c
 * @brief		Decode H264 frame and display image onto screen through overlay driver. 
 * 				This interface contain : 
 *
 * @author      Yusuf.Sha, Telechips Shenzhen Rep.
 * @date        2016/11/08
 */
//********************************************************************************************

#include "tcc_vpudec_intf.h"
#include "tcc_vdec_api.h"

#include <mach/tcc_overlay_ioctl.h>
//#include <mach/vioc_global.h>

//#define	DEBUG_MODE
#ifdef	DEBUG_MODE
	#define	DebugPrint( fmt, ... )	printf( "[TCC_VDEC_API](D):"fmt, ##__VA_ARGS__ )
	#define	ErrorPrint( fmt, ... )	printf( "[TCC_VDEC_API](E):"fmt, ##__VA_ARGS__ )
#else
	#define	DebugPrint( fmt, ... )
	#define	ErrorPrint( fmt, ... )	printf( "[TCC_VDEC_API](E):"fmt, ##__VA_ARGS__ )
#endif

// Overlay Driver
#define	OVERLAY_DRIVER	"/dev/overlay"
#define DEC_VIDEO_FORMAT ((unsigned int)'N' | (unsigned int)'V'<<8 | (unsigned int)'1'<<16 | (unsigned int)'2'<<24)
#define TCC_LCDC_SET_WMIXER_OVP         0x0045


// FB Driver
#define FB_DEV "/dev/fb0"

typedef struct _DecodeDate {
	
	int 					OverlayDrv;		//overlay driver handler
	bool					IsViewValid;	//view valid ?
	bool					IsDecoderOpen;	//whether decode is in use ?
	bool					IsConfigured;	//whether be configured ?
	unsigned int			default_ovp;	//set overlay layer
	unsigned int			LCD_width;		//LCD size - width
	unsigned int			LCD_height;		//LCD size - height
	overlay_video_buffer_t	lastinfo;		//backup overlay_info
	overlay_video_buffer_t	cur_info;		//current overlay_info
	
	pthread_mutex_t 		mutex_lock;
} DecodeDate;

static DecodeDate decode_data;
#if 0
//---------------------------------------------------------------
static int g_OverlayDrv = -1;

// Decoder State
//static int g_DecoderState = -1;


// 描画可否フラグ
static int g_IsViewValid = 0;	// 0:不可, 1:可

// SetConfigureを行ったかどうか
static int g_IsSetConfigure = 0;	// 0:未設定, 1:設定済
//static unsigned int ignore = 1;
// 一番最後にDecodeした画像情報
static overlay_video_buffer_t g_lastinfo;

// Mutex
static pthread_mutex_t g_Mutex = PTHREAD_MUTEX_INITIALIZER;
//----------------------------------------------------------------------
static void SetConfigure(void)
{
	overlay_config_t cfg;
	unsigned int format = (unsigned int)'N' | (unsigned int)'V'<<8 | (unsigned int)'1'<<16 | (unsigned int)'2'<<24;
	cfg.sx = 0;
	cfg.sy = 0;
	cfg.width = 800;
	cfg.height = 480;
	cfg.format = format;
	cfg.transform = 0;
	ioctl( g_OverlayDrv, OVERLAY_SET_CONFIGURE, &cfg );
	decode_data.IsConfigured = 1;
	
}
#endif

void tcc_vdec_SetViewFlag(int isValid)
{
	pthread_mutex_lock(&(decode_data.mutex_lock));
	
	decode_data.IsViewValid = isValid;
	if(!isValid)
		decode_data.IsConfigured = 0;
	
	pthread_mutex_unlock(&(decode_data.mutex_lock));
}

int tcc_vdec_init(unsigned int width, unsigned int height)
{
	//Init decode_data value
	decode_data.OverlayDrv = -1;
	decode_data.IsViewValid = 1;
	decode_data.IsDecoderOpen = 0;
	decode_data.IsConfigured = 0;
	decode_data.default_ovp = 24;
	decode_data.LCD_width = 800;//width;
	decode_data.LCD_height = 480;//height;
	
	pthread_mutex_init(&(decode_data.mutex_lock),NULL);
	
	return 0;
}

int tcc_vdec_open(void)
{
	pthread_mutex_lock(&(decode_data.mutex_lock));
	memset( &(decode_data.lastinfo), 0, sizeof(overlay_video_buffer_t));
	memset( &(decode_data.cur_info), 0, sizeof(overlay_video_buffer_t));
	
	//--------init and open vpu------------
	if(decode_data.IsDecoderOpen) {
		ErrorPrint( "Decoder is in use! It will be close!\n" );
		tcc_vpudec_close();
		decode_data.IsDecoderOpen = 0;
		decode_data.IsConfigured = 0;
	}
	if(tcc_vpudec_init(800, 466) < 0)
		ErrorPrint("tcc_vpudec_init error!!");
	else
		decode_data.IsDecoderOpen = 1;
	
	//--------open overlay driver------------
	if( decode_data.OverlayDrv >= 0 ){
		close( decode_data.OverlayDrv );
		decode_data.OverlayDrv = -1;
	}
	decode_data.OverlayDrv = open( OVERLAY_DRIVER, O_RDWR );
	
	if( decode_data.OverlayDrv < 0 ){
		ErrorPrint( "Overlay Driver Open Fail\n" );
	}else{
		/*set overlay to the top layer, otherwise, we overlay maybe covered by UI.
		* This value need set accroding to layer which use in overlay driver
		*/
		//unsigned int overlay_ovp=0; 
		//ioctl(decode_data.OverlayDrv, OVERLAY_GET_WMIXER_OVP, &(decode_data.default_ovp)); //backup default_ovp
		//ioctl(decode_data.OverlayDrv, OVERLAY_SET_WMIXER_OVP, &overlay_ovp);			 //set the new ovp value
		unsigned int ignore_ovp = 1;
		ioctl(decode_data.OverlayDrv, OVERLAY_SET_IGNORE_PRIORITY, &ignore_ovp );
	}
	/////////Set OVP//////////// FIXME
	int fbdev;
	int ovp=8;
	int ret=0;
	fbdev = open(FB_DEV, O_RDWR);
	if ( fbdev < 0 ) {
		printf("Error opening %s.\n", FB_DEV);
		pthread_mutex_unlock(&(decode_data.mutex_lock));
		return -1;
	} else {
		ioctl(fbdev, TCC_LCDC_SET_WMIXER_OVP, &ovp);
		if( ret < 0 ){
			printf("FB Driver IOCTL ERROR\n");
			close(fbdev);
			pthread_mutex_unlock(&(decode_data.mutex_lock));
			return -1;
		}
		close(fbdev);
	}

	pthread_mutex_unlock(&(decode_data.mutex_lock));
	
	return 0;
}

int tcc_vdec_close(void)
{
	pthread_mutex_lock(&(decode_data.mutex_lock));
		
	if( decode_data.OverlayDrv >= 0 ){
		close( decode_data.OverlayDrv );
	}
	decode_data.OverlayDrv = -1;
	decode_data.IsConfigured = 0;
	
	if( decode_data.IsDecoderOpen ){
		tcc_vpudec_close();
	}
	decode_data.IsDecoderOpen = 0;
	decode_data.IsViewValid = 0;
	
	memset( &(decode_data.lastinfo), 0, sizeof(overlay_video_buffer_t) );
	
	/////////Set OVP//////////// FIXME
	int fbdev;
	int ovp=8;
	int ret=0;
	fbdev = open(FB_DEV, O_RDWR);
	if ( fbdev < 0 ) {
		printf("Error opening %s.\n", FB_DEV);
		pthread_mutex_unlock(&(decode_data.mutex_lock));
		pthread_mutex_destroy(&(decode_data.mutex_lock));
		return -1;
	} else {
		ioctl(fbdev, TCC_LCDC_SET_WMIXER_OVP, &ovp);
		if( ret < 0 ){
			printf("FB Driver IOCTL ERROR\n");
			close(fbdev);
			pthread_mutex_unlock(&(decode_data.mutex_lock));
			pthread_mutex_destroy(&(decode_data.mutex_lock));
			return -1;
		}
		close(fbdev);
	}
	
	pthread_mutex_unlock(&(decode_data.mutex_lock));
	pthread_mutex_destroy(&(decode_data.mutex_lock));
	
	return 0;
}

int tcc_vdec_process_annexb_header( unsigned char* data, int datalen)
{
	int iret = 0;
	unsigned int outputdata[11] = {0};
	unsigned int inputdata[4] = {0};
	
	inputdata[0] = (unsigned int)data;
	inputdata[1] = (unsigned int)datalen;
	
	pthread_mutex_lock(&(decode_data.mutex_lock));
	
	if(decode_data.IsDecoderOpen == 0){
		ErrorPrint( "Decoder is not opened!\n" );
		pthread_mutex_unlock(&(decode_data.mutex_lock));
		return -1;
	}
	
	iret = tcc_vpudec_decode(inputdata, outputdata);
	if(iret < 0) {
		ErrorPrint("Annexb Header Decode Error!\n");
		pthread_mutex_unlock(&(decode_data.mutex_lock));
		return -1;
	}
	
	pthread_mutex_unlock(&(decode_data.mutex_lock));
	
	return 0;
}

#define MAX(a,b) (a>b)?a:b
#define TARGET_WIDTH 800.00
#define TARGET_HEIGHT 480.00
float TARGET_RATIO = (TARGET_WIDTH/TARGET_HEIGHT);

int tcc_vdec_process( unsigned char* data, int size)
{
	int iret = 0;
	unsigned int inputdata[4] = {0};
	unsigned int outputdata[15] = {0};
	unsigned int screen_width, screen_height;
	overlay_video_buffer_t info;
	//unsigned int crop_info[4]={0};
	unsigned int scaler_info[2]={0};
	
	pthread_mutex_lock(&(decode_data.mutex_lock));
	
	inputdata[0] = (unsigned int)data;
	inputdata[1] = (unsigned int)size;
	
	screen_width  = decode_data.LCD_width;
	screen_height = decode_data.LCD_height;
	
	if(decode_data.IsDecoderOpen == 0){
		ErrorPrint( "Decoder is not opened!!\n" );
		pthread_mutex_unlock(&(decode_data.mutex_lock));
		return -1;
	}
	if(decode_data.OverlayDrv < 0) {
		ErrorPrint( "Overlay driver is not opened!!\n" );
		pthread_mutex_unlock(&(decode_data.mutex_lock));
		return -1;
	}		
	
	//////////////  Start to Decode video frame
	iret = tcc_vpudec_decode(inputdata, outputdata);
	
	if( iret < 0 ) {
		//ErrorPrint( "Decode Failed!!\n" );
		pthread_mutex_unlock(&(decode_data.mutex_lock));
		return -1;
	}		
	//////////////  Prepare to Push the video /////////////////////
	if(!decode_data.IsViewValid)
	{
		pthread_mutex_unlock(&(decode_data.mutex_lock));
		return 0;
	}
	
	decode_data.cur_info.cfg.width = outputdata[8];
	decode_data.cur_info.cfg.height = outputdata[9];
	decode_data.cur_info.cfg.format = DEC_VIDEO_FORMAT;
	decode_data.cur_info.cfg.transform = 0;		
	decode_data.cur_info.addr = outputdata[1];		// Y Address;

#if !defined(CONFIG_CROP) && !defined(CONFIG_SCALER)
	//Set position X
	if(decode_data.cur_info.cfg.width <= screen_width)
	{
		decode_data.cur_info.cfg.sx =(screen_width-decode_data.cur_info.cfg.width)/2;
	}
	else
	{
		ErrorPrint("Image width is more then screen_width, need scaler!\n");
		decode_data.cur_info.cfg.sx = 0;
		decode_data.cur_info.cfg.width = screen_width;
	}
	//Set position Y
	if(decode_data.cur_info.cfg.height <= screen_height)
	{
		decode_data.cur_info.cfg.sy =(screen_height-decode_data.cur_info.cfg.height)/2;
	}
	else
	{
		ErrorPrint("Image height is more then screen_hieght, need scaler!\n");
		decode_data.cur_info.cfg.sy = 0;
		decode_data.cur_info.cfg.height = screen_height;
	}
#endif

#if defined(CONFIG_CROP)	
	decode_data.cur_info.addr1 = outputdata[2];		// U Address;
	decode_data.cur_info.addr2 = outputdata[3];		// V Address;
	
	//for crop
	decode_data.cur_info.cfg.crop_src.left = 0;
	decode_data.cur_info.cfg.crop_src.top = 0;
	decode_data.cur_info.cfg.crop_src.width = outputdata[8]-outputdata[13];
	decode_data.cur_info.cfg.crop_src.height = outputdata[9]-outputdata[14];
	
	//decode_data.cur_info.cfg.sx = (decode_data.LCD_width-info.cfg.crop_src.width)/2;
	//decode_data.cur_info.cfg.sy = (decode_data.LCD_height-info.cfg.crop_src.height)/2;
	
	//Set position X
	if(decode_data.cur_info.cfg.crop_src.width <= screen_width)
	{
		decode_data.cur_info.cfg.sx =(screen_width - decode_data.cur_info.cfg.crop_src.width)/2;
	}
	else
	{
		ErrorPrint("Image width is more then screen_width, need scaler!\n");
		decode_data.cur_info.cfg.sx = 0;
		decode_data.cur_info.cfg.crop_src.width = screen_width;
	}
	//Set position Y
	if(decode_data.cur_info.cfg.crop_src.height <= screen_height)
	{
		decode_data.cur_info.cfg.sy =(screen_height - decode_data.cur_info.cfg.crop_src.height)/2;
	}
	else
	{
		ErrorPrint("Image height is more then screen_hieght, need scaler!\n");
		decode_data.cur_info.cfg.sy = 0;
		decode_data.cur_info.cfg.crop_src.height = screen_height;
	}
	DebugPrint("Pos [%d,%d], (%d,%d) -> (%d x %d)... \n", 	decode_data.cur_info.cfg.sx, 
															decode_data.cur_info.cfg.sy, 
															decode_data.cur_info.cfg.width, 
															decode_data.cur_info.cfg.height, 
															decode_data.cur_info.cfg.crop_src.width, 
															decode_data.cur_info.cfg.crop_src.height);
	//decode_data.cur_info.cfg.width = decode_data.cur_info.cfg.crop_src.width;
	//decode_data.cur_info.cfg.height = decode_data.cur_info.cfg.crop_src.height;
	
	//ioctl( decode_data.OverlayDrv, OVERLAY_SET_CROP_INFO, &crop_info);
#endif

#if 0//defined(CONFIG_SCALER)			
	///for scaler
	float ratio0 = (float)decode_data.cur_info.cfg.width/(float)decode_data.cur_info.cfg.height;
	float ratio1 = (float)decode_data.cur_info.cfg.height/(float)decode_data.cur_info.cfg.width;
	//printf("[Yusuf] ratio0=%.5f, ratio1=%.5f, target_ratio=%.5f...\n",ratio0, ratio1, TARGET_RATIO);
	if((ratio0 >= TARGET_RATIO) || (ratio1 >= TARGET_RATIO)) //ratio is 16:9
	{
		if(decode_data.cur_info.cfg.width > decode_data.cur_info.cfg.height)
		{
			scaler_info[0] = TARGET_WIDTH;
			//scaler_info[1] = (800*9)/16;
			scaler_info[1] = (TARGET_WIDTH*(float)crop_info[3])/(float)crop_info[2];
		}else{
			//scaler_info[0] = (480*9)/16;
			scaler_info[0] = (TARGET_HEIGHT*(float)crop_info[2])/(float)crop_info[3];
			scaler_info[1] = TARGET_HEIGHT;
		}
	}else{//ratio is 4:3
		if(decode_data.cur_info.cfg.width > decode_data.cur_info.cfg.height)
		{
			scaler_info[0] = (480*4)/3;//(unsigned int)(480.00 * (MAX(ratio0,ratio1))); //640;
			scaler_info[1] = 480;
		}else{
			scaler_info[0] = (480*3)/4;//(unsigned int)(480.00 / (MAX(ratio0,ratio1))); //360;
			scaler_info[1] = 480;
		}
	}
	decode_data.cur_info.cfg.sx = (800-scaler_info[0])/2;
	decode_data.cur_info.cfg.sy = (480-scaler_info[1])/2;
	DebugPrint("Scaler: src (%d x %d) -- dst (%d x %d) \n", decode_data.cur_info.cfg.width, decode_data.cur_info.cfg.height, scaler_info[0], scaler_info[1]);
	DebugPrint("(%d,%d) - (%d x %d)... \n",decode_data.cur_info.cfg.sx, decode_data.cur_info.cfg.sy, scaler_info[0], scaler_info[1]);

	//ioctl( g_OverlayDrv, OVERLAY_SET_SCALER_INFO, &scaler_info);
#endif

//-------------------------------------------------------------------
#if 0
	if( iret >= 0 ){
		
		if( g_OverlayDrv >= 0 ){

			info.cfg.width = outputdata[8];
			info.cfg.height = outputdata[9];
			info.cfg.format = (unsigned int)'N' | (unsigned int)'V'<<8 | (unsigned int)'1'<<16 | (unsigned int)'2'<<24;
			//info.cfg.format = 0;		// 使われてないようなので無視
			info.cfg.transform = 0;		// 使われてないようなので無視
			info.addr = outputdata[1];		// Y Address;
			#if 1	// 2015.3.2 yuichi add, UとVのアドレスも使う
			info.addr1 = outputdata[2];
			info.addr2 = outputdata[3];
			#endif
			//for crop
			crop_info[0] = 0;
			crop_info[1] = 0;
			crop_info[2] = outputdata[8]-outputdata[13];
			crop_info[3] = outputdata[9]-outputdata[14];

			///for scaler
			float ratio0 = (float)info.cfg.width/(float)info.cfg.height;
			float ratio1 = (float)info.cfg.height/(float)info.cfg.width;
			//printf("[Yusuf] ratio0=%.5f, ratio1=%.5f, target_ratio=%.5f...\n",ratio0, ratio1, TARGET_RATIO);
			if((ratio0 >= TARGET_RATIO) || (ratio1 >= TARGET_RATIO)) //ratio is 16:9
			{
				if(info.cfg.width > info.cfg.height)
				{
					scaler_info[0] = TARGET_WIDTH;
					//scaler_info[1] = (800*9)/16;
					scaler_info[1] = (TARGET_WIDTH*(float)crop_info[3])/(float)crop_info[2];
				}else{
					//scaler_info[0] = (480*9)/16;
					scaler_info[0] = (TARGET_HEIGHT*(float)crop_info[2])/(float)crop_info[3];
					scaler_info[1] = TARGET_HEIGHT;
				}
			}else{//ratio is 4:3
				if(info.cfg.width > info.cfg.height)
				{
					scaler_info[0] = (480*4)/3;//(unsigned int)(480.00 * (MAX(ratio0,ratio1))); //640;
					scaler_info[1] = 480;
				}else{
					scaler_info[0] = (480*3)/4;//(unsigned int)(480.00 / (MAX(ratio0,ratio1))); //360;
					scaler_info[1] = 480;
				}
			}
			info.cfg.sx = (800-scaler_info[0])/2;
			info.cfg.sy = (480-scaler_info[1])/2;
			printf("[libH264] crop_width=%d, crop_height=%d\n",crop_info[2],crop_info[3]);
			printf("[libH264] Scaler: src (%d x %d) -- dst (%d x %d) \n", info.cfg.width, info.cfg.height, scaler_info[0], scaler_info[1]);
			printf("[libH264] (%d,%d) - (%d x %d)... \n",info.cfg.sx, info.cfg.sy, scaler_info[0], scaler_info[1]);

			ioctl( g_OverlayDrv, OVERLAY_SET_CROP_INFO, &crop_info);
			ioctl( g_OverlayDrv, OVERLAY_SET_SCALER_INFO, &scaler_info);	
			
			//printf( "%s: [0]=0x%08x, [1]=0x%08x, [2]=0x%08x\n", __func__, 
			//		info.addr, info.addr1, info.addr2 );	// yuichi
			
			if(g_IsViewValid){	// 2015.04.23 : N.Tanaka 描画可否を判断する
				
				// Start時にフラグが立っておらずSetConfigureされていない場合にはここでSetConfiguresする
				if( g_IsSetConfigure == 0 ){
					SetConfigure();
				}
				ioctl( g_OverlayDrv, OVERLAY_SET_CONFIGURE, &info.cfg );	
				ioctl( g_OverlayDrv, OVERLAY_PUSH_VIDEO_BUFFER, &info );
			}else{
				//printf("IsViewValid is false...\n");
			}
			
			// 2015.04.24 N.Tanaka
			// 最後のDecodeデータ情報を保持しておく
			memcpy( &g_lastinfo, &info, sizeof(overlay_video_buffer_t) );
			
		}else{
			ErrorPrint( "Decode but Overlay Driver is not opened\n" );
		}
		
	}else{
		
		ErrorPrint( "Decode fail\n" );
		
	}
#endif
	//----------------------------------------------------------------------
	
	//if( decode_data.IsConfigured == 0 ){
	if(1) {
		ioctl( decode_data.OverlayDrv, OVERLAY_SET_CONFIGURE, &decode_data.cur_info.cfg );
		decode_data.IsConfigured = 1;
	}
	//////////////  Start to Push the video /////////////////////
	ioctl( decode_data.OverlayDrv, OVERLAY_PUSH_VIDEO_BUFFER, &decode_data.cur_info );

	memcpy( &(decode_data.lastinfo), &decode_data.cur_info, sizeof(overlay_video_buffer_t) );

	pthread_mutex_unlock(&(decode_data.mutex_lock));

	return 0;
}

