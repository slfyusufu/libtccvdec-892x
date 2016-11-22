//********************************************************************************************
/**
 * @file        tcc_vpudec_intf.c
 * @brief		Decode one video frame using TCC VPU, now, we just set it to H264 decode. 
 * 				This interface contain : Init VPU, Decode frame and Close VPU.
 *
 * @author      Yusuf.Sha, Telechips Shenzhen Rep.
 * @date        2016/11/08
 */
//********************************************************************************************

#include "tcc_vpudec_intf.h"


//#define	DEBUG_MODE
#ifdef	DEBUG_MODE
	#define	DebugPrint( fmt, ... )	printf( "[TCC_VPUDEC_INTF](D):"fmt"\n", ##__VA_ARGS__ )
	#define	ErrorPrint( fmt, ... )	printf( "[TCC_VPUDEC_INTF](E):"fmt"\n", ##__VA_ARGS__ )
#else
	#define	DebugPrint( fmt, ... )
	#define	ErrorPrint( fmt, ... )	printf( "[TCC_VPUDEC_INTF](E):"fmt"\n", ##__VA_ARGS__ )
#endif

///////////   Global Define    //////////////////////
static tDEC_PRIVATE *dec_private;

static void
disp_pic_info (int Opcode, void* pParam1, void *pParam2, void *pParam3, unsigned int fps)
{
	int i;
	dec_disp_info_ctrl_t  *pInfoCtrl = (dec_disp_info_ctrl_t*)pParam1;
	dec_disp_info_t 	  *pInfo = (dec_disp_info_t *)pParam2;
	dec_disp_info_input_t *pInfoInput = (dec_disp_info_input_t*)pParam3;

	switch( Opcode )
	{
	case CVDEC_DISP_INFO_INIT:	//init.
			pInfoCtrl->m_iStdType = pInfoInput->m_iStdType; 					
			pInfoCtrl->m_iFmtType = pInfoInput->m_iFmtType;
			pInfoCtrl->m_iTimeStampType = pInfoInput->m_iTimeStampType;

			if( pInfoCtrl->m_iFmtType == CONTAINER_MPG )
			{
				dec_private->gsMPEG2PtsInfo.m_iLatestPTS = 0;
				dec_private->gsMPEG2PtsInfo.m_iRamainingDuration = 0;
				if(fps!=0)
				dec_private->gsMPEG2PtsInfo.m_iPTSInterval = (((1000 * 1000) << 10) / fps) >> 10;
			}

            #ifdef TS_TIMESTAMP_CORRECTION
			if( pInfoCtrl->m_iFmtType == CONTAINER_TS )
			{
				dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iLatestPTS = 0;
				dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iRamainingDuration = 0;
				if(fps!=0)
					dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iPTSInterval = (((1000 * 1000) << 10) / fps) >> 10;
			}
            #endif
            
	case CVDEC_DISP_INFO_RESET: //reset
			for( i=0 ; i<32 ; i++ )
			{
				pInfoCtrl->m_iRegIdxPTS[i] = -1;	//unused
				pInfoCtrl->m_pRegInfoPTS[i] = (void*)&pInfo[i];
			}
			pInfoCtrl->m_iUsedIdxPTS = 0;

			if( pInfoCtrl->m_iTimeStampType == CDMX_DTS_MODE )	//Decode Timestamp (Decode order)
			{
				pInfoCtrl->m_iDecodeIdxDTS = 0;
				pInfoCtrl->m_iDispIdxDTS = 0;
				for( i=0 ; i<32 ; i++ )
				{
					pInfoCtrl->m_iDTS[i] = 0;
				}
			}

			memset(&dec_private->pVideoDecodInstance.gsEXT_F_frame_time, 0, sizeof(EXT_F_frame_time_t));
			dec_private->pVideoDecodInstance.gsextReference_Flag = 1;
			dec_private->pVideoDecodInstance.gsextP_frame_cnt = 0;

			if( pInfoCtrl->m_iFmtType == CONTAINER_MPG )
			{
				dec_private->gsMPEG2PtsInfo.m_iLatestPTS = 0;
				dec_private->gsMPEG2PtsInfo.m_iRamainingDuration = 0;
			}
			
            #ifdef TS_TIMESTAMP_CORRECTION
			if( pInfoCtrl->m_iFmtType == CONTAINER_TS )
			{
				dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iLatestPTS = 0;
				dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iRamainingDuration = 0;
			}
            #endif
		break;

	case CVDEC_DISP_INFO_UPDATE: //update
		{
			int iDecodedIdx;
			int usedIdx, startIdx, regIdx;
			dec_disp_info_t * pdec_disp_info;

			iDecodedIdx = pInfoInput->m_iFrameIdx;

			//In case that frame rate is changed...
			#if 1
			if( pInfoCtrl->m_iFmtType == CONTAINER_MPG )
			{
				if(pInfoInput->m_iFrameRate)
				{
					fps = ((pInfoInput->m_iFrameRate & 0xffff) * 1000) / (((pInfoInput->m_iFrameRate >> 16) + 1)&0xffff);
					dec_private->gsMPEG2PtsInfo.m_iPTSInterval = (((1000 * 1000) << 10) / fps) >> 10;

					
					//ALOGD("CVDEC_DISP_INFO_UPDATE m_iPTSInterval %d m_iFrameRate %d input FrameRate %x ",gsMPEG2PtsInfo.m_iPTSInterval , fps,pInfoInput->m_iFrameRate);
				}
			}
			#endif
			//Presentation Timestamp (Display order)
			{
				//sort
				usedIdx=0;
				startIdx = -1;
				for( i=0 ; i<32 ; i++ )
				{
					if( pInfoCtrl->m_iRegIdxPTS[i] > -1 )
					{
						if( startIdx == -1 )
						{
							startIdx = i;
						}
						usedIdx++;
					}
				}

				if( usedIdx > 0 )
				{
					regIdx = 0;
					for( i=startIdx ; i<32 ; i++ )
					{
						if( pInfoCtrl->m_iRegIdxPTS[i] > -1 )
						{
							if( i != regIdx )
							{
								void * pswap;
								int iswap;

								iswap = pInfoCtrl->m_iRegIdxPTS[regIdx];
								pswap = pInfoCtrl->m_pRegInfoPTS[regIdx];
								
								pInfoCtrl->m_iRegIdxPTS[regIdx] = pInfoCtrl->m_iRegIdxPTS[i];
								pInfoCtrl->m_pRegInfoPTS[regIdx] = pInfoCtrl->m_pRegInfoPTS[i];

								pInfoCtrl->m_iRegIdxPTS[i] = iswap;
								pInfoCtrl->m_pRegInfoPTS[i] = pswap;
							}
							regIdx++;
							if( regIdx == usedIdx )
								break;
						}
					}
				}

				//save the side info.
				pInfoCtrl->m_iRegIdxPTS[usedIdx] = iDecodedIdx;
				pdec_disp_info = (dec_disp_info_t*)pInfoCtrl->m_pRegInfoPTS[usedIdx];

				pdec_disp_info->m_iTimeStamp = pInfo->m_iTimeStamp;
				pdec_disp_info->m_iFrameType = pInfo->m_iFrameType;
				pdec_disp_info->m_iPicStructure = pInfo->m_iPicStructure;
				pdec_disp_info->m_iextTimeStamp = pInfo->m_iextTimeStamp;
				pdec_disp_info->m_iM2vFieldSequence = pInfo->m_iM2vFieldSequence;
				pdec_disp_info->m_iFrameDuration = pInfo->m_iFrameDuration;				
				pdec_disp_info->m_iFrameSize = pInfo->m_iFrameSize;
				
				if( pInfoCtrl->m_iStdType  == STD_RV )
				{
					int curTimestamp, ext_Timestamp, ext_FrameType;

					curTimestamp = pInfo->m_iTimeStamp;
					ext_Timestamp = pInfo->m_iextTimeStamp;
					ext_FrameType = pInfo->m_iFrameType;
								
					if(dec_private->pVideoDecodInstance.gsextReference_Flag)
					{
						dec_private->pVideoDecodInstance.gsextReference_Flag = 0;
						dec_private->pVideoDecodInstance.gsEXT_F_frame_time.ref_frame.Current_time_stamp = curTimestamp;
						dec_private->pVideoDecodInstance.gsEXT_F_frame_time.ref_frame.Previous_TR = ext_Timestamp;
						dec_private->pVideoDecodInstance.gsEXT_F_frame_time.frame_P2.Current_time_stamp = curTimestamp;
						dec_private->pVideoDecodInstance.gsEXT_F_frame_time.frame_P2.Current_TR = ext_Timestamp;
					}
					else
					{
						dec_private->pVideoDecodInstance.gsextTRDelta = ext_Timestamp - dec_private->pVideoDecodInstance.gsEXT_F_frame_time.ref_frame.Current_TR;
						if(dec_private->pVideoDecodInstance.gsextTRDelta < 0)
						{
							dec_private->pVideoDecodInstance.gsextTRDelta += 8192;
						}

						if(ext_FrameType == 2) //B-frame
						{
							curTimestamp = dec_private->pVideoDecodInstance.gsEXT_F_frame_time.ref_frame.Current_time_stamp + dec_private->pVideoDecodInstance.gsextTRDelta;
						}
						else
						{
							dec_private->pVideoDecodInstance.gsextP_frame_cnt++;
						}
					}

					if( dec_private->pVideoDecodInstance.gsextP_frame_cnt == 1)
					{
						dec_private->pVideoDecodInstance.gsEXT_F_frame_time.frame_P1.Current_TR = ext_Timestamp;
						dec_private->pVideoDecodInstance.gsEXT_F_frame_time.frame_P1.Current_time_stamp = curTimestamp;

						dec_private->pVideoDecodInstance.gsEXT_F_frame_time.ref_frame.Current_time_stamp = dec_private->pVideoDecodInstance.gsEXT_F_frame_time.frame_P2.Current_time_stamp;
						dec_private->pVideoDecodInstance.gsEXT_F_frame_time.ref_frame.Current_TR = dec_private->pVideoDecodInstance.gsEXT_F_frame_time.frame_P2.Current_TR;
					}
					else if( dec_private->pVideoDecodInstance.gsextP_frame_cnt == 2)
					{
						dec_private->pVideoDecodInstance.gsextP_frame_cnt = 0;
						dec_private->pVideoDecodInstance.gsEXT_F_frame_time.frame_P2.Current_TR = ext_Timestamp;
						dec_private->pVideoDecodInstance.gsEXT_F_frame_time.frame_P2.Current_time_stamp = curTimestamp;

						dec_private->pVideoDecodInstance.gsEXT_F_frame_time.ref_frame.Current_time_stamp = dec_private->pVideoDecodInstance.gsEXT_F_frame_time.frame_P1.Current_time_stamp;
						dec_private->pVideoDecodInstance.gsEXT_F_frame_time.ref_frame.Current_TR = dec_private->pVideoDecodInstance.gsEXT_F_frame_time.frame_P1.Current_TR;
					}

					pdec_disp_info->m_iextTimeStamp = curTimestamp;
				}

				pInfoCtrl->m_iUsedIdxPTS = usedIdx + 1;
				if( pInfoCtrl->m_iUsedIdxPTS > 31 )
				{
//					DBUG_MSG( "[CDK_CORE] disp_pic_info index failed\n" );
					for( i=0 ; i<32 ; i++ )
					{
						pInfoCtrl->m_iRegIdxPTS[i] = -1;
					}
				}
			}

			if( pInfoCtrl->m_iTimeStampType == CDMX_DTS_MODE )	//Decode Timestamp (Decode order)
			{
				if( iDecodedIdx >= 0 || ( iDecodedIdx == -2 && pInfoCtrl->m_iStdType  == STD_MPEG4  ) )
				{		
					pInfoCtrl->m_iDTS[pInfoCtrl->m_iDecodeIdxDTS] = pInfo->m_iTimeStamp;
					pInfoCtrl->m_iDecodeIdxDTS = ( pInfoCtrl->m_iDecodeIdxDTS + 1 ) & 31;
				}
			}
		}
		break;
	case CVDEC_DISP_INFO_GET:	//display
		{
			dec_disp_info_t **pInfo = (dec_disp_info_t **)pParam2;
			int dispOutIdx = pInfoInput->m_iFrameIdx;

			//Presentation Timestamp (Display order)
			{
				*pInfo = 0;
			
				for( i=0; i<pInfoCtrl->m_iUsedIdxPTS ; i++ )
				{
					if( dispOutIdx == pInfoCtrl->m_iRegIdxPTS[i] )
					{
						*pInfo = (dec_disp_info_t*)pInfoCtrl->m_pRegInfoPTS[i];

						if( pInfoCtrl->m_iFmtType  == CONTAINER_MPG )
						{
						//ALOGD("CVDEC_DISP_INFO_GET m_iPTSInterval %d m_iLatestPTS %d input m_iTimeStamp %d m_iRamainingDuration %d ",gsMPEG2PtsInfo.m_iPTSInterval , 
						//gsMPEG2PtsInfo.m_iLatestPTS,(*pInfo)->m_iTimeStamp,gsMPEG2PtsInfo.m_iRamainingDuration);
							if( (*pInfo)->m_iTimeStamp <= dec_private->gsMPEG2PtsInfo.m_iLatestPTS )
								(*pInfo)->m_iTimeStamp = dec_private->gsMPEG2PtsInfo.m_iLatestPTS + ((dec_private->gsMPEG2PtsInfo.m_iPTSInterval * dec_private->gsMPEG2PtsInfo.m_iRamainingDuration) >> 1);

							dec_private->gsMPEG2PtsInfo.m_iLatestPTS = (*pInfo)->m_iTimeStamp;
							dec_private->gsMPEG2PtsInfo.m_iRamainingDuration = (*pInfo)->m_iFrameDuration;
						}

                        #ifdef TS_TIMESTAMP_CORRECTION
						if( pInfoCtrl->m_iFmtType == CONTAINER_TS )
						{
							if( (*pInfo)->m_iTimeStamp <= dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iLatestPTS )
								(*pInfo)->m_iTimeStamp = dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iLatestPTS + ((dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iPTSInterval * dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iRamainingDuration) >> 1);

							dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iLatestPTS = (*pInfo)->m_iTimeStamp;
							dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iRamainingDuration = (*pInfo)->m_iFrameDuration;
						}
                        #endif

						pInfoCtrl->m_iRegIdxPTS[i] = -1; //unused
						pInfoCtrl->m_iUsedIdxPTS--;
						break;
					}
				}
			}
			
			if( pInfoCtrl->m_iTimeStampType == CDMX_DTS_MODE )	//Decode Timestamp (Decode order)
			{
				if( *pInfo != 0 )
				{
					(*pInfo)->m_iTimeStamp =
					(*pInfo)->m_iextTimeStamp = pInfoCtrl->m_iDTS[pInfoCtrl->m_iDispIdxDTS];
					pInfoCtrl->m_iDispIdxDTS = ( pInfoCtrl->m_iDispIdxDTS + 1 ) & 31;
				}
			}
		}
		break;
	}

	return;
}

static int
get_frame_type_for_frame_skipping(int iStdType, int iPicType, int iPicStructure)
{
	int frameType = 0; //unknown

	switch ( iStdType )
	{
	case STD_VC1 :
		switch( (iPicType>>3) ) //Frame or // FIELD_INTERLACED(TOP FIELD)
		{
		case PIC_TYPE_I:	frameType = 1; break;//I
		case PIC_TYPE_P:	frameType = 2; break;//P
		case 2:				frameType = 3; break;//B //DSTATUS( "BI  :" );
		case 3:				frameType = 3; break;//B //DSTATUS( "B   :" );
		case 4:				frameType = 3; break;//B //DSTATUS( "SKIP:" );
		}
		if( iPicStructure == 3) 
		{
			switch( (iPicType&0x7) ) // FIELD_INTERLACED(BOTTOM FIELD)
			{
			case PIC_TYPE_I:	frameType = 1; break;//I
			case PIC_TYPE_P:	frameType = 2; break;//P
			case 2:				frameType = 3; break;//B //DSTATUS( "BI  :" );
			case 3:				frameType = 3; break;//B //DSTATUS( "B   :" );
			case 4:				frameType = 3; break;//B //DSTATUS( "SKIP:" );
			}
		}
		break;
	case STD_MPEG4 :
		switch( iPicType )
		{
		case PIC_TYPE_I:	frameType = 1;	break;//I
		case PIC_TYPE_P:	frameType = 2;	break;//P
		case PIC_TYPE_B:	frameType = 3;	break;//B
		case PIC_TYPE_B_PB: frameType = 4;	break;//B of Packed PB-frame
		}
		break;
	case STD_MPEG2 :
	default:
		switch( iPicType )
		{
		case PIC_TYPE_I:	frameType = 1;	break;//I
		case PIC_TYPE_P:	frameType = 2;	break;//P
		case PIC_TYPE_B:	frameType = 3;	break;//B
		}
	}
	return frameType;
}

void print_user_data(unsigned char * pUserData)
{
	unsigned int i, j;
	unsigned char * pTmpPTR;
	unsigned char * pRealData;
	unsigned int nNumUserData;
	unsigned int nTotalSize;
	unsigned int nDataSize;

	pTmpPTR = pUserData;
	nNumUserData = (pTmpPTR[0] << 8) | pTmpPTR[1];
	nTotalSize = (pTmpPTR[2] << 8) | pTmpPTR[3];

	pTmpPTR = pUserData + 8;
	pRealData = pUserData + (8 * 17);

	DebugPrint( "\n***User Data Print***\n");
	for(i = 0;i < nNumUserData;i++)
	{
		nDataSize = (pTmpPTR[2] << 8) | pTmpPTR[3];
		DebugPrint( "[User Data][Idx : %02d][Size : %05d]", i, nDataSize);
		for(j = 0;j < nDataSize;j++)
		{
			DebugPrint( "%02x ", pRealData[j]);
		}
		pTmpPTR += 8;
		pRealData += nDataSize;
	}
}

static void VideoDecErrorProcess(int ret)
{
    if(dec_private->cntDecError > MAX_CONSECUTIVE_VPU_FAIL_TO_RESTORE_COUNT)
    {
		DebugPrint("Consecutive decode-cmd failure is occurred");
    }

#ifdef RESTORE_DECODE_ERR
	if((ret == -RETCODE_CODEC_EXIT || ret == -RETCODE_MULTI_CODEC_EXIT_TIMEOUT) && dec_private->cntDecError <= MAX_CONSECUTIVE_VPU_FAIL_TO_RESTORE_COUNT && dec_private->seqHeader_backup != NULL)
	{
		dec_private->seq_header_init_error_count = SEQ_HEADER_INIT_ERROR_COUNT;
		dec_private->ConsecutiveBufferFullCnt = 0;
		dec_private->cntDecError++;
		if(dec_private->pVideoDecodInstance.isVPUClosed != 1)
		{
			dec_private->pVideoDecodInstance.gspfVDec( VDEC_CLOSE, NULL, NULL, &dec_private->pVideoDecodInstance.gsVDecOutput, dec_private->pVideoDecodInstance.pVdec_Instance);
			dec_private->pVideoDecodInstance.isVPUClosed = 1;
		}

		dec_private->isSequenceHeaderDone = 0;
		dec_private->cntDecError = 1;
		dec_private->in_index = dec_private->out_index = dec_private->frm_clear = 0;
		DebugPrint("try to restore decode error");
	}
#endif
}

#ifdef CHECK_SEQHEADER_WITH_SYNCFRAME
static int extract_h264_seqheader(
		const unsigned char	*pbyStreamData, 
		long				lStreamDataSize,
		unsigned char		**ppbySeqHeaderData,
		long				*plSeqHeaderSize
		)
{
	long i;
	long l_seq_start_pos = 0, l_seq_end_pos = 0, l_seq_length = 0; // Start Position, End Position, Length of the sequence header
	long l_sps_found = 0;
	long l_pps_found = 0;

	unsigned long ul_read_word_buff;	   	    	            //4 byte temporary buffer 
	unsigned long ul_masking_word_seq          = 0x0FFFFFFF;    //Masking Value for finding H.264 sequence header
	unsigned long ul_masking_word_sync         = 0x00FFFFFF;    //Masking Value for finding sync word of H.264
	unsigned long ul_h264_result_word_seq_SPS  = 0x07010000;    //Masking result should be this value in case of SPS. SPS Sequence header of H.264 must start by "00 00 01 x7"
	unsigned long ul_h264_result_word_seq_PPS  = 0x08010000;    //Masking result should be this value in case of PPS. PPS Sequence header of H.264 must start by "00 00 01 x8"
	unsigned long ul_h264_result_word_sync     = 0x00010000;    //Masking result should be this value. Sequence header of H.264 must start by "00 00 01 x7"

	if ( lStreamDataSize < 4 )
		return 0; // there's no Seq. header in this frame. we need the next frame.

	if ( *plSeqHeaderSize > 0 )
	{
		// we already find the sps, pps in previous frame
		l_sps_found = 1;
		l_pps_found = 1;
		l_seq_start_pos = 0;
	}
	else
	{
		// find the SPS of H.264 
		ul_read_word_buff = 0;
		ul_read_word_buff |= (pbyStreamData[0] << 8);
		ul_read_word_buff |= (pbyStreamData[1] << 16);
		ul_read_word_buff |= (pbyStreamData[2] << 24);

		for ( i = 0; i < lStreamDataSize-4; i++ )      
		{
			ul_read_word_buff = ul_read_word_buff >> 8;
			ul_read_word_buff &= 0x00FFFFFF; 
			ul_read_word_buff |= (pbyStreamData[i+3] << 24);

			if ( (ul_read_word_buff & ul_masking_word_seq) == ul_h264_result_word_seq_SPS ) 
			{
				// SPS Sequence Header has been detected
				l_sps_found = 1;              
				l_seq_start_pos = i;          // save the start position of the sequence header 

				break;                        
			}

			// Continue to find the sps in next loop
		}

		if ( l_sps_found == 1 )
		{
			// Now, let's start to find the PPS of the Seq. header.

			i = i + 4; 
			ul_read_word_buff = 0;
			ul_read_word_buff |= (pbyStreamData[i] << 8);
			ul_read_word_buff |= (pbyStreamData[i+1] << 16);
			ul_read_word_buff |= (pbyStreamData[i+2] << 24);

			for (  ; i < lStreamDataSize - 4; i++ )    
			{
				ul_read_word_buff = ul_read_word_buff >> 8;
				ul_read_word_buff &= 0x00FFFFFF; 
				ul_read_word_buff |= (pbyStreamData[i+3] << 24);

				if ( (ul_read_word_buff & ul_masking_word_seq) == ul_h264_result_word_seq_PPS ) 
				{
					// PPS has been detected. 
					l_pps_found = 1;
					break;
				}

				// Continue to find the pps in next loop
			}
		}
	}

	if ( l_pps_found == 1 )
	{
		// Now, let's start to find the next sync word to find the end position of Seq. Header

		if ( *plSeqHeaderSize > 0 )
			i = 0;     // we already find the sps, pps in previous frame
		else
			i = i + 4;
		ul_read_word_buff = 0;
		ul_read_word_buff |= (pbyStreamData[i] << 8);
		ul_read_word_buff |= (pbyStreamData[i+1] << 16);
		ul_read_word_buff |= (pbyStreamData[i+2] << 24);

		for ( ; i < lStreamDataSize - 4; i++ )    
		{
			ul_read_word_buff = ul_read_word_buff >> 8;
			ul_read_word_buff &= 0x00FFFFFF; 
			ul_read_word_buff |= (pbyStreamData[i+3] << 24);

			if ( (ul_read_word_buff & ul_masking_word_sync) == ul_h264_result_word_sync ) 
			{
				long l_cnt_zeros = 0;       // to count extra zeros ahead of "00 00 01"

				// next sync-word has been found.
				l_seq_end_pos = i - 1;      // save the end position of the sequence header (00 00 01 case)

				// any zeros can be added ahead of "00 00 01" sync word by H.264 specification. Count the number of these leading zeros.
				while (1)
				{
					l_cnt_zeros++;

					if(i >= l_cnt_zeros) //ZzaU :: to prevent segmentation fault.
					{
						if ( pbyStreamData[i-l_cnt_zeros] == 0 )    
						{
							l_seq_end_pos = l_seq_end_pos -1;    // decrease the end position of Seq. Header by 1.
						}
						else
							break;
					}
					else
						break;
				}
				
				if ( *plSeqHeaderSize > 0 )
				{
					// we already find the sps, pps in previous frame
					l_seq_length = l_seq_end_pos - l_seq_start_pos + 1;       

					if ( l_seq_length > 0 )
					{
						if ( *plSeqHeaderSize + l_seq_length > MAX_SEQ_HEADER_ALLOC_SIZE ) // check the maximum threshold
							return 0;

						free(*ppbySeqHeaderData);
						*ppbySeqHeaderData = calloc(1 , *plSeqHeaderSize + l_seq_length );     // allocation memory for sequence header array (must free this at the CLOSE step)
						memcpy( (unsigned char*) (*ppbySeqHeaderData) + *plSeqHeaderSize , &pbyStreamData[l_seq_start_pos], l_seq_length);   // save the seq. header to array
						*plSeqHeaderSize = *plSeqHeaderSize + l_seq_length;
					}
					
					return 1;
					
				}
				else
				{
					// calculate the length of the sequence header
					l_seq_length = l_seq_end_pos - l_seq_start_pos + 1;       

					if ( l_seq_length > 0 )
					{
						*ppbySeqHeaderData = calloc(1, l_seq_length );     // allocation memory for sequence header array (must free this at the CLOSE step)
						memcpy( (unsigned char*) (*ppbySeqHeaderData), &pbyStreamData[l_seq_start_pos], l_seq_length);   // save the seq. header to array
						*plSeqHeaderSize = l_seq_length;

						return 1;  // We've found the sequence header successfully
					}
				}
			}

			// Continue to find the sync-word in next loop
		}
	}

	if ( l_sps_found == 1 && l_pps_found == 1)
	{	
		// we found sps and pps, but we couldn't find the next sync word yet
		l_seq_end_pos = lStreamDataSize - 1;
		l_seq_length = l_seq_end_pos - l_seq_start_pos + 1;        // calculate the length of the sequence header

		if ( *plSeqHeaderSize > 0 )
		{
			// we already saved the sps, pps in previous frame
			if ( l_seq_length > 0 )
			{
				if ( *plSeqHeaderSize + l_seq_length > MAX_SEQ_HEADER_ALLOC_SIZE )     // check the maximum threshold
					return 0;

				free(*ppbySeqHeaderData);
				*ppbySeqHeaderData = calloc(1 , *plSeqHeaderSize + l_seq_length );     // allocate memory for sequence header array (must free this at the CLOSE step)
				memcpy( (unsigned char*) (*ppbySeqHeaderData) + *plSeqHeaderSize , &pbyStreamData[l_seq_start_pos], l_seq_length);   // save the seq. header to array
				*plSeqHeaderSize = *plSeqHeaderSize + l_seq_length;
			}

		}
		else
		{
			*ppbySeqHeaderData = calloc(1, l_seq_length );           // allocate memory for sequence header array (must free this at the CLOSE step)
			memcpy( (unsigned char*) (*ppbySeqHeaderData), &pbyStreamData[l_seq_start_pos], l_seq_length);   // save the seq. header to array
			*plSeqHeaderSize = *plSeqHeaderSize + l_seq_length;
		}
	}

	return 0; // We couldn't find the complete sequence header yet. We need to search the next frame data.
}
#endif


static int extract_mpeg4_seqheader(
	 const unsigned char	*pbyData, 
	 long				lDataSize,
	 unsigned char		**ppbySeqHead,
	 long				*plHeadLength
	 )
{
	unsigned long syncword = 0xFFFFFFFF;
	int	start_pos = -1;
	int end_pos = -1;
	int i;

	syncword <<= 8; 
	syncword |= pbyData[0];
	syncword <<= 8; 
	syncword |= pbyData[1];
	syncword <<= 8; 
	syncword |= pbyData[2];

	for(i = 3; i < lDataSize; i++) {
		syncword <<= 8; 
		syncword |= pbyData[i];

		if( (syncword >> 8) == 1 ) {	// 0x 000001??
			if( syncword >= MPEG4_VOL_STARTCODE_MIN &&
				syncword <= MPEG4_VOL_STARTCODE_MAX )
				start_pos = i-3;
			else if( start_pos >= 0 || *plHeadLength > 0 ) {
				if ( syncword == MPEG4_VOP_STARTCODE )
				{
					end_pos = i-3;
					break;
				}
			}
		}
	}
	
#ifdef CHECK_ONLY_HEADER_DATA	
	if (start_pos >= 0 && end_pos == -1) {
		end_pos = lDataSize - start_pos;
	}
#endif

	if( start_pos >= 0 ) {
		if( end_pos >= 0 ) {
			*plHeadLength = end_pos-start_pos;
//			*ppbySeqHead = cdk_malloc( *plHeadLength );     // allocate memory for sequence header array 
//			cdk_memcpy(*ppbySeqHead, pbyData + start_pos, *plHeadLength);
			return 1;
		}
		else {
			*plHeadLength = lDataSize - start_pos;
//			*ppbySeqHead = cdk_malloc( *plHeadLength );     // allocate memory for sequence header array 
//			cdk_memcpy(*ppbySeqHead, pbyData + start_pos, *plHeadLength);
			return 0;
		}
	}
	else if( *plHeadLength > 0 ) {
		if( end_pos < 0 )
			end_pos = lDataSize;

		if ( *plHeadLength + end_pos > MAX_SEQ_HEADER_ALLOC_SIZE ) // check the maximum threshold
			return 0;

//		*ppbySeqHead = cdk_realloc(*ppbySeqHead , *plHeadLength + end_pos);     // re-allocate memory for sequence header array 
//		cdk_memcpy(*ppbySeqHead + *plHeadLength, pbyData, end_pos);
		*plHeadLength += end_pos;
		return 1;
	}

	return 0;
}

char*
print_pic_type( int iVideoType, int iPicType, int iPictureStructure )
{
	switch ( iVideoType )
	{
	case STD_MPEG2 :
		if( iPicType == PIC_TYPE_I )
			return "I :";
		else if( iPicType == PIC_TYPE_P )
			return "P :";
		else if( iPicType == PIC_TYPE_B )
			return "B :";
		else
			return "D :"; //D_TYPE
		break;

	case STD_MPEG4 :
		if( iPicType == PIC_TYPE_I )
			return "I :";
		else if( iPicType == PIC_TYPE_P )
			return "P :";
		else if( iPicType == PIC_TYPE_B )
			return "B :";
		else if( iPicType == PIC_TYPE_B_PB ) //MPEG-4 Packed PB-frame
			return "pB:";
		else
			return "S :"; //S_TYPE
		break;

	case STD_VC1 :
		if( iPictureStructure == 3) 
		{
			// FIELD_INTERLACED
			if( (iPicType>>3) == PIC_TYPE_I )
				return "TF_I   :";	//TOP_FIELD = I	
			else if( (iPicType>>3) == PIC_TYPE_P )
				return "TF_P   :";	//TOP_FIELD = P
			else if( (iPicType>>3) == 2 )
				return "TF_BI  :";	//TOP_FIELD = BI_TYPE
			else if( (iPicType>>3) == 3 )
				return "TF_B   :";	//TOP_FIELD = B_TYPE
			else if( (iPicType>>3) == 4 )
				return "TF_SKIP:";	//TOP_FIELD = SKIP_TYPE
			else
				return "TF_FORBIDDEN :"; //TOP_FIELD = FORBIDDEN

			if( (iPicType&0x7) == PIC_TYPE_I )
				return "BF_I   :";	//BOTTOM_FIELD = I
			else if( (iPicType&0x7) == PIC_TYPE_P )
				return "BF_P   :";	//BOTTOM_FIELD = P
			else if( (iPicType&0x7) == 2 )
				return "BF_BI  :";	//BOTTOM_FIELD = BI_TYPE
			else if( (iPicType&0x7) == 3 )
				return "BF_B   :";	//BOTTOM_FIELD = B_TYPE
			else if( (iPicType&0x7) == 4 )
				return "BF_SKIP:";	//BOTTOM_FIELD = SKIP_TYPE
			else
				return "BF_FORBIDDEN :"; //BOTTOM_FIELD = FORBIDDEN
		}
		else 
		{
			iPicType = iPicType>>3;
			if( iPicType == PIC_TYPE_I )
				return "I   :";
			else if( iPicType == PIC_TYPE_P )
				return "P   :";
			else if( iPicType == 2 )
				return "BI  :";
			else if( iPicType == 3 )
				return "B   :";
			else if( iPicType == 4 )
				return "SKIP:";
			else
				return "FORBIDDEN :"; //FORBIDDEN
		}
		break;
	default:
		if( iPicType == PIC_TYPE_I )
			return "I :";
		else if( iPicType == PIC_TYPE_P )
			return "P :";
		else if( iPicType == PIC_TYPE_B )
			return "B :";
		else
			return "U :"; //Unknown
	}
}

static int DECODER_INIT_NoReordering(tDEC_INIT_PARAMS *pInit)
{
	int ret = 0;
	
	DebugPrint( "DECODER_INIT_NoReordering\n" );
	
	dec_private = (tDEC_PRIVATE*)calloc( 1, sizeof(tDEC_PRIVATE) );
	if( dec_private == NULL ){
		DebugPrint( "calloc fail\n" );
		return 1;
	}
	
	memset(dec_private, 0x00, sizeof(tDEC_PRIVATE));
	memset(&dec_private->pVideoDecodInstance, 0x00, sizeof(_VIDEO_DECOD_INSTANCE_));
	dec_private->nFps = 30;
	dec_private->seq_header_init_error_count = SEQ_HEADER_INIT_ERROR_COUNT;	
	dec_private->ConsecutiveBufferFullCnt = 0;
	dec_private->cntDecError = 0;
	dec_private->pVideoDecodInstance.pVdec_Instance = (void*)vdec_alloc_instance(dec_private->pVideoDecodInstance.gsVDecInit.m_iBitstreamFormat, 0);
	dec_private->pVideoDecodInstance.video_dec_idx = 0;
	dec_private->max_fifo_cnt = VPU_BUFF_COUNT;	
	dec_private->out_index = dec_private->in_index = dec_private->frm_clear = 0;
	dec_private->pVideoDecodInstance.restred_count = 0;
	
#ifdef EXT_V_DECODER_TR_TEST
	dec_private->pVideoDecodInstance.gsextP_frame_cnt = 0;
	dec_private->pVideoDecodInstance.gsextReference_Flag = 1;
#endif
#ifdef CHECK_SEQHEADER_WITH_SYNCFRAME
	dec_private->sequence_header_only = NULL;
	dec_private->sequence_header_size = 0;
	dec_private->need_sequence_header_attachment = 0;
#endif
	
	dec_private->pVideoDecodInstance.isVPUClosed = 1;
	dec_private->isFirst_Frame = 1;
	
	switch(pInit->codecFormat)
	{
		case CODEC_FORMAT_H263:	 dec_private->pVideoDecodInstance.video_coding_type = STD_H263;  	break;
		case CODEC_FORMAT_MPEG4: dec_private->pVideoDecodInstance.video_coding_type = STD_MPEG4;	break;
		case CODEC_FORMAT_H264:  dec_private->pVideoDecodInstance.video_coding_type = STD_AVC;		break;
		case CODEC_FORMAT_RV:	 dec_private->pVideoDecodInstance.video_coding_type = STD_RV;  		break;
		case CODEC_FORMAT_MPEG2: dec_private->pVideoDecodInstance.video_coding_type = STD_MPEG2;	break;
		case CODEC_FORMAT_DIV3:  dec_private->pVideoDecodInstance.video_coding_type = STD_DIV3;		break;
		case CODEC_FORMAT_VC1:	 dec_private->pVideoDecodInstance.video_coding_type = STD_VC1;  	break;
		case CODEC_FORMAT_MJPG:  dec_private->pVideoDecodInstance.video_coding_type = STD_MJPG;		break;
		default: return -1;
	}
	
	// Memo : 2014.10.29 N.Tanaka 抜けを追加>>>>>>>>>>>>>>>>>>>>
	dec_private->frameSearchOrSkip_flag 			= 0;
	dec_private->pVideoDecodInstance.gsVDecInit.m_iBitstreamFormat		= dec_private->pVideoDecodInstance.video_coding_type;
	dec_private->pVideoDecodInstance.gsVDecInit.m_iPicWidth 			= pInit->picWidth;
	dec_private->pVideoDecodInstance.gsVDecInit.m_iPicHeight			= pInit->picHeight;
	dec_private->pVideoDecodInstance.gsVDecInit.m_bEnableVideoCache 	= 0;
	dec_private->pVideoDecodInstance.gsVDecInit.m_bFilePlayEnable		= 1;
	dec_private->pVideoDecodInstance.container_type 					= pInit->container_type;
	// <<<<<<<<<<<<<<<<<<<<
	
	// 基本的にHWDecodeのみになるはずなので、SWかの判断をせずにHWの時の処理を移植
	dec_private->pVideoDecodInstance.gsVDecInit.m_bCbCrInterleaveMode	= 1;
	
	// Memo : 2014.10.29 N.Tanaka 抜けを追加>>>>>>>>>>>>>>>>>>>>
	dec_private->pVideoDecodInstance.gspfVDec = vdec_vpu;
	// <<<<<<<<<<<<<<<<<<<<
	
	{
		memset(&dec_private->pVideoDecodInstance.dec_disp_info_input, 0x00, sizeof(dec_disp_info_input_t));
		dec_private->pVideoDecodInstance.dec_disp_info_input.m_iStdType = dec_private->pVideoDecodInstance.gsVDecInit.m_iBitstreamFormat;		
		dec_private->pVideoDecodInstance.dec_disp_info_input.m_iFmtType = dec_private->pVideoDecodInstance.container_type;
		
		if(dec_private->pVideoDecodInstance.container_type == CONTAINER_AVI || dec_private->pVideoDecodInstance.container_type == CONTAINER_MP4)
		{				
			DebugPrint("TimeStampType = CDMX_PTS_MODE");
			dec_private->pVideoDecodInstance.dec_disp_info_input.m_iTimeStampType	= CDMX_PTS_MODE;			
		}
		else
		{
			DebugPrint("TimeStampType = CDMX_DTS_MODE");		
			dec_private->pVideoDecodInstance.dec_disp_info_input.m_iTimeStampType = CDMX_DTS_MODE;
		}
		disp_pic_info ( CVDEC_DISP_INFO_INIT, (void*)&dec_private->pVideoDecodInstance.dec_disp_info_ctrl, (void*)dec_private->pVideoDecodInstance.dec_disp_info,(void*)&dec_private->pVideoDecodInstance.dec_disp_info_input, dec_private->nFps);
	}
	
	dec_private->pVideoDecodInstance.gsVDecUserInfo.bitrate_mbps = 10;
	dec_private->pVideoDecodInstance.gsVDecUserInfo.frame_rate = 30;
	dec_private->pVideoDecodInstance.gsVDecUserInfo.m_bJpegOnly = 0;
	
	// Linuxになって、この設定がなくなっている。実際にはre-ordering無効設定が必要なので後で確認
//	dec_private->pVideoDecodInstance.gsVDecInit.m_bWFDPlayEnable = 1;

/*
	// [vdec_k.cを見た感じ、今回の設定の仕方はこんな感じかな？]
	// extFunction は AndroidOSのifdefの時っぽいなぁ・・・
	if( (pInst->extFunction & EXT_FUNC_NO_BUFFER_DELAY) != 0x0 )
	{
		DebugPrint("[VDEC_K] : No BufferDelay Mode....");
		pInst->gsVpuDecInit_Info.gsVpuDecInit.m_uiDecOptFlags |= (1<<2);
	}
*/
	
	// 一応これでビルドは通ったけどいいのかな？
	dec_private->pVideoDecodInstance.gsVDecUserInfo.extFunction = EXT_FUNC_NO_BUFFER_DELAY;
	
	if( (ret = dec_private->pVideoDecodInstance.gspfVDec( VDEC_INIT, NULL, &dec_private->pVideoDecodInstance.gsVDecInit, &dec_private->pVideoDecodInstance.gsVDecUserInfo, dec_private->pVideoDecodInstance.pVdec_Instance )) < 0 )	
	{
		DebugPrint( "[VDEC_INIT] [Err:%d] video decoder init", ret );

		if(ret != -VPU_ENV_INIT_ERROR) //to close vpu!!
			dec_private->pVideoDecodInstance.isVPUClosed = 0;			
		return ret;
	}
	dec_private->pVideoDecodInstance.isVPUClosed = 0;
	dec_private->pVideoDecodInstance.restred_count = 0;
	
	return 0;
	
}

static void DECODER_CLOSE(void)
{
	int ret;
	
	if(dec_private->pVideoDecodInstance.isVPUClosed == 0)
	{
		DebugPrint( "[VDEC_CLOSE]  video decoder Deinit" );
		if( (ret = dec_private->pVideoDecodInstance.gspfVDec( VDEC_CLOSE, NULL, NULL, &dec_private->pVideoDecodInstance.gsVDecOutput, dec_private->pVideoDecodInstance.pVdec_Instance )) < 0 )
		{
			DebugPrint( "[VDEC_CLOSE] [Err:%4d] video decoder Deinit", ret );
		}
		dec_private->pVideoDecodInstance.isVPUClosed = 1;
	}

#ifdef RESTORE_DECODE_ERR
	if(dec_private->seqHeader_backup != NULL)
		free(dec_private->seqHeader_backup);
#endif

#ifdef CHECK_SEQHEADER_WITH_SYNCFRAME
	if(dec_private->sequence_header_only != NULL)
		free(dec_private->sequence_header_only);
#endif

    vdec_release_instance(dec_private->pVideoDecodInstance.pVdec_Instance);

	if(dec_private) {
		free(dec_private);
		dec_private = NULL;
	}
}

static int DECODER_DEC( tDEC_FRAME_INPUT *pInput, tDEC_FRAME_OUTPUT *pOutput, tDEC_RESULT *pResult )
{
	int ret = 0;
	int nLen = 0;
	int decode_result;	
	unsigned int input_offset = 0;	
	unsigned char retry_input = 0;
	dec_disp_info_t dec_disp_info_tmp;
	
	memset(pOutput, 0x00, sizeof(tDEC_FRAME_OUTPUT));
	memset(pResult, 0x00, sizeof(tDEC_RESULT));
	
	if(dec_private->pVideoDecodInstance.video_coding_type == STD_AVC)
	{
		unsigned char *p;
		p = pInput->inputStreamAddr;
		
		if(p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x00 && p[3] == 0x01 && p[4] == 0x00 && p[5] == 0x00 && p[6] == 0x00 && p[7] == 0x01)
		{
			input_offset = 4;
			DebugPrint("Double NAL-Start Code!!");
		}
		else if(p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x00 && p[3] == 0x01 && p[4] == 0x00 && p[5] == 0x00 && p[6] == 0x01)
		{
			input_offset = 3;
			DebugPrint("remove 00 00 01 behind NAL-Start Code!!");
			p[3] = 0x00;
		}
	}
	
	dec_private->pVideoDecodInstance.gsVDecInput.m_pInp[PA] =  dec_private->pVideoDecodInstance.gsVDecInput.m_pInp[VA] = pInput->inputStreamAddr + input_offset;
	dec_private->pVideoDecodInstance.gsVDecInput.m_iInpLen  = pInput->inputStreamSize - input_offset;
	
	if(!dec_private->isSequenceHeaderDone)
	{	
		int width, height;

#ifdef RESTORE_DECODE_ERR
		if(dec_private->cntDecError != 0){
			dec_private->pVideoDecodInstance.restred_count++;
			DebugPrint("%d'th start to restore decode error count(%d)", dec_private->pVideoDecodInstance.restred_count, dec_private->cntDecError);
			pInput->seek = 1;
		}
#endif

		if(dec_private->pVideoDecodInstance.isVPUClosed == 1)
		{
			DebugPrint("codec will be reInit, %d", dec_private->pVideoDecodInstance.isVPUClosed);
			if( (ret = dec_private->pVideoDecodInstance.gspfVDec( VDEC_INIT, NULL, &dec_private->pVideoDecodInstance.gsVDecInit, &dec_private->pVideoDecodInstance.gsVDecUserInfo, dec_private->pVideoDecodInstance.pVdec_Instance )) < 0 )	
			{
				DebugPrint( "[VDEC_INIT] [Err:%d] video decoder init", ret );
			
				if(ret != -VPU_ENV_INIT_ERROR) { //to close vpu!!
					dec_private->pVideoDecodInstance.isVPUClosed = 0;			
					VideoDecErrorProcess( -RETCODE_CODEC_EXIT );
				}
				return ret;
			}
			dec_private->pVideoDecodInstance.isVPUClosed = 0;
		}

#ifdef RESTORE_DECODE_ERR
		if(dec_private->cntDecError != 0 && dec_private->seqHeader_backup != NULL)
		{
			dec_private->pVideoDecodInstance.gsVDecInput.m_pInp[PA] = dec_private->pVideoDecodInstance.gsVDecInput.m_pInp[VA] = dec_private->seqHeader_backup;
			dec_private->pVideoDecodInstance.gsVDecInput.m_iInpLen	= dec_private->seqHeader_len;
		}
#endif

#ifdef CHECK_SEQHEADER_WITH_SYNCFRAME
		if(dec_private->pVideoDecodInstance.video_coding_type == STD_AVC)
		{
			if(0 >= extract_h264_seqheader((const unsigned char *)dec_private->pVideoDecodInstance.gsVDecInput.m_pInp[VA], dec_private->pVideoDecodInstance.gsVDecInput.m_iInpLen, &dec_private->sequence_header_only, &dec_private->sequence_header_size)){
				pResult->no_frame_output = 1;
				if( dec_private->sequence_header_size > 0 ) {
					dec_private->need_sequence_header_attachment = 1;
				}

				DebugPrint("[%d'th frame with only sequence frame (%d: %d bytes)] VPU want sequence_header frame with sync frame!", SEQ_HEADER_INIT_ERROR_COUNT - dec_private->seq_header_init_error_count, 
															dec_private->sequence_header_size);
				return 1;
			}
			else
			{
				if(dec_private->need_sequence_header_attachment)
				{
					unsigned char *temp_addr = (unsigned char *)dec_private->pVideoDecodInstance.gsVDecInput.m_pInp[VA];
					dec_private->pVideoDecodInstance.gsVDecInput.m_pInp[PA] = vpu_getBitstreamBufAddr(PA, dec_private->pVideoDecodInstance.pVdec_Instance);
					dec_private->pVideoDecodInstance.gsVDecInput.m_pInp[VA] = vpu_getBitstreamBufAddr(VA, dec_private->pVideoDecodInstance.pVdec_Instance);

					memcpy(dec_private->pVideoDecodInstance.gsVDecInput.m_pInp[VA], dec_private->sequence_header_only, dec_private->sequence_header_size);
					memcpy(dec_private->pVideoDecodInstance.gsVDecInput.m_pInp[VA]+ dec_private->sequence_header_size, temp_addr, dec_private->pVideoDecodInstance.gsVDecInput.m_iInpLen);
					dec_private->pVideoDecodInstance.gsVDecInput.m_iInpLen	+= dec_private->sequence_header_size;
				}
				else
				{
					dec_private->need_sequence_header_attachment = 1;
				}
			}
		}
		else 
#endif
		if(dec_private->pVideoDecodInstance.video_coding_type == STD_MPEG4)
		{
			if(0 == extract_mpeg4_seqheader((const unsigned char *)dec_private->pVideoDecodInstance.gsVDecInput.m_pInp[VA], dec_private->pVideoDecodInstance.gsVDecInput.m_iInpLen, &dec_private->sequence_header_only, &dec_private->sequence_header_size)){
				pResult->no_frame_output = 1;
				DebugPrint("MPEG4 :: not sequence data %d", dec_private->pVideoDecodInstance.gsVDecInput.m_iInpLen);
				return 1;
			}			
		}
		

		dec_private->max_fifo_cnt = VPU_BUFF_COUNT;
		vpu_set_additional_refframe_count(dec_private->max_fifo_cnt - 1, dec_private->pVideoDecodInstance.pVdec_Instance);

		if( (ret = dec_private->pVideoDecodInstance.gspfVDec( VDEC_DEC_SEQ_HEADER, NULL, &dec_private->pVideoDecodInstance.gsVDecInput, &dec_private->pVideoDecodInstance.gsVDecOutput, dec_private->pVideoDecodInstance.pVdec_Instance )) < 0 )
		{
			if(dec_private->seq_header_init_error_count != 0)
				dec_private->seq_header_init_error_count--;
			
			if ( (dec_private->seq_header_init_error_count == 0) || (ret == -RETCODE_INVALID_STRIDE) || (ret == -VPU_NOT_ENOUGH_MEM) )
			{
				DebugPrint( "[VDEC_DEC_SEQ_HEADER] [Err:%d]", ret );
				return -1;
			}
			else
			{
				if(ret == -RETCODE_CODEC_EXIT || ret == -RETCODE_MULTI_CODEC_EXIT_TIMEOUT)
				{
					DebugPrint("codec is exited, %d", dec_private->pVideoDecodInstance.isVPUClosed);
					if(dec_private->pVideoDecodInstance.isVPUClosed == 0)
					{
						if( (ret = dec_private->pVideoDecodInstance.gspfVDec( VDEC_CLOSE, NULL, NULL, &dec_private->pVideoDecodInstance.gsVDecOutput, dec_private->pVideoDecodInstance.pVdec_Instance )) < 0 )
						{
							DebugPrint( "[VDEC_CLOSE] [Err:%4d] video decoder Deinit", ret );
						}
						dec_private->pVideoDecodInstance.isVPUClosed = 1;
					}
				}
				DebugPrint("skip seq header frame, data len %d", dec_private->pVideoDecodInstance.gsVDecInput.m_iInpLen);
				DebugPrint( "[VDEC_DEC_SEQ_HEADER - %d] retry %d using next frame!", ret, SEQ_HEADER_INIT_ERROR_COUNT - dec_private->seq_header_init_error_count);
				pResult->no_frame_output = 1;
				return 1;
			}
		}
#ifdef RESTORE_DECODE_ERR
		else
		{
			if(dec_private->seqHeader_backup == NULL)
			{
				dec_private->seqHeader_backup = calloc(1,dec_private->pVideoDecodInstance.gsVDecInput.m_iInpLen);
				memcpy(dec_private->seqHeader_backup, dec_private->pVideoDecodInstance.gsVDecInput.m_pInp[VA], dec_private->pVideoDecodInstance.gsVDecInput.m_iInpLen);
				dec_private->seqHeader_len = dec_private->pVideoDecodInstance.gsVDecInput.m_iInpLen;
				dec_private->cntDecError = 0;

				DebugPrint("backup seq_header(%d) data to restore decode", dec_private->seqHeader_len);
			}
			DebugPrint("success seq_header(%d)", dec_private->seqHeader_len);
		}
#endif
		if( dec_private->pVideoDecodInstance.gsVDecInit.m_iBitstreamFormat == STD_AVC)
		{	
			width = (dec_private->pVideoDecodInstance.gsVDecOutput.m_pInitialInfo->m_iPicWidth- dec_private->pVideoDecodInstance.gsVDecOutput.m_pInitialInfo->m_iAvcPicCrop.m_iCropLeft - dec_private->pVideoDecodInstance.gsVDecOutput.m_pInitialInfo->m_iAvcPicCrop.m_iCropRight);
			height = (dec_private->pVideoDecodInstance.gsVDecOutput.m_pInitialInfo->m_iPicHeight - dec_private->pVideoDecodInstance.gsVDecOutput.m_pInitialInfo->m_iAvcPicCrop.m_iCropBottom - dec_private->pVideoDecodInstance.gsVDecOutput.m_pInitialInfo->m_iAvcPicCrop.m_iCropTop);
		}
		else
		{
			width = dec_private->pVideoDecodInstance.gsVDecOutput.m_pInitialInfo->m_iPicWidth;
			height = dec_private->pVideoDecodInstance.gsVDecOutput.m_pInitialInfo->m_iPicHeight;
		}
		
		if(dec_private->pVideoDecodInstance.gsVDecInit.m_iPicWidth != width || dec_private->pVideoDecodInstance.gsVDecInit.m_iPicHeight != height)
		{			
			DebugPrint( "Resolution is changed!! %d x %d -> %d x %d \n", dec_private->pVideoDecodInstance.gsVDecInit.m_iPicWidth, dec_private->pVideoDecodInstance.gsVDecInit.m_iPicHeight, width, height);
			dec_private->pVideoDecodInstance.gsVDecInit.m_iPicWidth = width;
			dec_private->pVideoDecodInstance.gsVDecInit.m_iPicHeight = height;
		}
		dec_private->isSequenceHeaderDone = 1;
	}			


//Search I-Frame in case of First-Frame or Seek!!
	if(dec_private->isFirst_Frame || pInput->seek)
	{
		DebugPrint("[SEEK] I-frame Search Mode enable");
		dec_private->ConsecutiveVdecFailCnt = 0; //Reset Consecutive Vdec Fail Counting B060955
		dec_private->frameSearchOrSkip_flag = 1;

		disp_pic_info( CVDEC_DISP_INFO_RESET, (void*)&dec_private->pVideoDecodInstance.dec_disp_info_ctrl, (void*)dec_private->pVideoDecodInstance.dec_disp_info,(void*)&dec_private->pVideoDecodInstance.dec_disp_info_input, dec_private->nFps);		

		if(dec_private->max_fifo_cnt != 0)
		{
			while(dec_private->in_index != dec_private->out_index)
			{
				DebugPrint("DispIdx Clear %d", dec_private->Display_index[dec_private->out_index]);
				if( ( ret = dec_private->pVideoDecodInstance.gspfVDec( VDEC_BUF_FLAG_CLEAR, NULL, &dec_private->Display_index[dec_private->out_index], NULL, dec_private->pVideoDecodInstance.pVdec_Instance ) ) < 0 )
				{
					DebugPrint( "[VDEC_BUF_FLAG_CLEAR] Idx = %d, ret = %d", dec_private->Display_index[dec_private->out_index], ret );
					VideoDecErrorProcess(ret);
					return -1;
				}
				dec_private->out_index = (dec_private->out_index + 1) % dec_private->max_fifo_cnt;
			}
			dec_private->in_index = dec_private->out_index = dec_private->frm_clear = 0;
		}

		dec_private->isFirst_Frame = 0;
	}

//Ready to decode frame!!
	dec_private->pVideoDecodInstance.gsVDecInput.m_iSkipFrameNum = 0;
	dec_private->pVideoDecodInstance.gsVDecInput.m_iFrameSearchEnable = 0;

	switch(dec_private->i_skip_scheme_level)
	{
		case VDEC_SKIP_FRAME_DISABLE:
		case VDEC_SKIP_FRAME_EXCEPT_I:
			dec_private->pVideoDecodInstance.gsVDecInput.m_iSkipFrameMode = dec_private->i_skip_scheme_level;
			break;
		case VDEC_SKIP_FRAME_ONLY_B:
			if(dec_private->i_skip_count == dec_private->i_skip_interval)
			{
				dec_private->pVideoDecodInstance.gsVDecInput.m_iSkipFrameMode = dec_private->i_skip_scheme_level;
				dec_private->pVideoDecodInstance.gsVDecInput.m_iSkipFrameNum = 1000;
			}
			else
			{
				dec_private->pVideoDecodInstance.gsVDecInput.m_iSkipFrameMode = VDEC_SKIP_FRAME_DISABLE;
			}
			break;
		default:
				dec_private->pVideoDecodInstance.gsVDecInput.m_iSkipFrameMode = VDEC_SKIP_FRAME_DISABLE;
			break;
	}
	
	if(dec_private->frameSearchOrSkip_flag == 1 )
	{
		dec_private->pVideoDecodInstance.gsVDecInput.m_iSkipFrameNum = 1;
		//dec_private->gsVDecInput.m_iFrameSearchEnable = 0x001;	//I-frame (IDR-picture for H.264)
		dec_private->pVideoDecodInstance.gsVDecInput.m_iFrameSearchEnable = 0x201;	//I-frame (I-slice for H.264) : Non IDR-picture
		dec_private->pVideoDecodInstance.gsVDecInput.m_iSkipFrameMode = VDEC_SKIP_FRAME_DISABLE;

		if( dec_private->pVideoDecodInstance.gsVDecInput.m_iFrameSearchEnable == 0x001 )
		{
			DebugPrint( "[SEEK] I-frame Search Mode(IDR-picture for H.264) Enable!!!");
		}
		else if( dec_private->pVideoDecodInstance.gsVDecInput.m_iFrameSearchEnable == 0x201 )
		{
			DebugPrint( "[SEEK] I-frame Search Mode(I-slice for H.264) Enable!!!");
		}
	}
	
	if( dec_private->frameSearchOrSkip_flag == 2 )
	{
		dec_private->pVideoDecodInstance.gsVDecInput.m_iSkipFrameNum = 1;
		dec_private->pVideoDecodInstance.gsVDecInput.m_iFrameSearchEnable = 0;
		dec_private->pVideoDecodInstance.gsVDecInput.m_iSkipFrameMode = VDEC_SKIP_FRAME_ONLY_B; 
		DebugPrint( "[SEEK] B-frame Skip Mode Enable!!! \n");
	}
	
	if(dec_private->pVideoDecodInstance.isVPUClosed == 1)
	{
		DebugPrint( "Now VPU has been closed , return " );
		return -1;
	}

	if( (ret = dec_private->pVideoDecodInstance.gspfVDec( VDEC_DECODE, NULL, &dec_private->pVideoDecodInstance.gsVDecInput, &dec_private->pVideoDecodInstance.gsVDecOutput, dec_private->pVideoDecodInstance.pVdec_Instance )) < 0 )
	{
		DebugPrint( "[VDEC_DECODE] [Err:%d] video decode", ret );
		VideoDecErrorProcess(ret);
		return -1;
	}
	
	if(dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodingStatus == VPU_DEC_BUF_FULL) 
	{
		// Current input stream should be used next time.
		if(dec_private->ConsecutiveBufferFullCnt++ > MAX_CONSECUTIVE_VPU_BUFFER_FULL_COUNT) {
			DebugPrint("VPU_DEC_BUF_FULL");
			dec_private->ConsecutiveBufferFullCnt = 0;
			VideoDecErrorProcess(-RETCODE_CODEC_EXIT);
			return -1;
		}

		if (dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iOutputStatus == VPU_DEC_OUTPUT_SUCCESS)
			decode_result = 0; // display Index : processed.
		else
			decode_result = 1; // display Index : not processsed.
	} 
	else 
	{
		dec_private->ConsecutiveBufferFullCnt = 0;

		if(dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iOutputStatus == VPU_DEC_OUTPUT_SUCCESS)
			decode_result = 2; // display Index : proceed.
		else
			decode_result = 3; // display Index : not processsed.
	}
	
	switch(dec_private->i_skip_scheme_level)
	{
		case VDEC_SKIP_FRAME_DISABLE:
			break;
		case VDEC_SKIP_FRAME_EXCEPT_I:
		case VDEC_SKIP_FRAME_ONLY_B:
			if((dec_private->pVideoDecodInstance.gsVDecInput.m_iSkipFrameMode != dec_private->i_skip_scheme_level) || (dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodedIdx == -2))
				dec_private->i_skip_count--;

			if(dec_private->i_skip_count < 0)
				dec_private->i_skip_count = dec_private->i_skip_interval;
			break;
	}
	
//Update TimeStamp!!
	if(dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodingStatus == VPU_DEC_SUCCESS && dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodedIdx >= 0)
	{			
		dec_disp_info_tmp.m_iTimeStamp			= pInput->nTimeStamp;
		dec_disp_info_tmp.m_iFrameType			= dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iPicType;
		dec_disp_info_tmp.m_iPicStructure		= dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iPictureStructure;
		dec_disp_info_tmp.m_iextTimeStamp		= 0;
		dec_disp_info_tmp.m_iM2vFieldSequence	= 0;			
		dec_disp_info_tmp.m_iFrameSize			= dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iConsumedBytes;
		dec_disp_info_tmp.m_iFrameDuration		= 2;
		
		switch( dec_private->pVideoDecodInstance.gsVDecInit.m_iBitstreamFormat)
		{
			case STD_RV:
				dec_disp_info_tmp.m_iextTimeStamp = dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iRvTimestamp;
				if (dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodingStatus == VPU_DEC_SUCCESS ) 
				{
					dec_private->pVideoDecodInstance.dec_disp_info_input.m_iFrameIdx = dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodedIdx;
					disp_pic_info( CVDEC_DISP_INFO_UPDATE, (void*)&dec_private->pVideoDecodInstance.dec_disp_info_ctrl, (void*)&dec_disp_info_tmp, (void*)&dec_private->pVideoDecodInstance.dec_disp_info_input, dec_private->nFps);
				}
				
				break;
				
			case STD_AVC:
				if ( ( dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iM2vProgressiveFrame == 0 && dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iPictureStructure == 0x3 )
					|| dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iInterlacedFrame )
				{
					//pOutputBuffer ->nFlags |= OMX_BUFFERFLAG_INTERLACED_FRAME;
				}

				if( dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iTopFieldFirst == 0)
				{
					//pOutputBuffer ->nFlags |= OMX_BUFFERFLAG_ODD_FIRST_FRAME;
				}

				dec_disp_info_tmp.m_iM2vFieldSequence = 0;
				dec_private->pVideoDecodInstance.dec_disp_info_input.m_iFrameIdx = dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodedIdx;
				disp_pic_info( CVDEC_DISP_INFO_UPDATE, (void*)&dec_private->pVideoDecodInstance.dec_disp_info_ctrl, (void*)&dec_disp_info_tmp, (void*)&dec_private->pVideoDecodInstance.dec_disp_info_input, dec_private->nFps);
				break;
				
			case STD_MPEG2: 					
				if( dec_disp_info_tmp.m_iPicStructure != 3 )
				{
					dec_disp_info_tmp.m_iFrameDuration = 1;
				}
				else if( dec_private->pVideoDecodInstance.gsVDecOutput.m_pInitialInfo->m_iInterlace == 0 )
				{
					if( dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iRepeatFirstField == 0 )
						dec_disp_info_tmp.m_iFrameDuration = 2;
					else
						dec_disp_info_tmp.m_iFrameDuration = ( dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iTopFieldFirst == 0 )?4:6;
				}
				else
				{ 
					//pOutputBuffer ->nFlags |= OMX_BUFFERFLAG_INTERLACED_FRAME;
					/* interlaced sequence */
					if( dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iInterlacedFrame == 0 )
						dec_disp_info_tmp.m_iFrameDuration = 2;
					else
						dec_disp_info_tmp.m_iFrameDuration = ( dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iRepeatFirstField == 0 )?2:3;
				}

				if ( ( dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iM2vProgressiveFrame == 0 && dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iPictureStructure == 0x3 )
					|| dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iInterlacedFrame )
				{
					//ALOGD("Interlaced Frame!!!");
					//pOutputBuffer ->nFlags |= OMX_BUFFERFLAG_INTERLACED_FRAME;
				}

				if( dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iTopFieldFirst == 0)
				{
					//ALOGD("Odd First Frame!!!");
					//pOutputBuffer ->nFlags |= OMX_BUFFERFLAG_ODD_FIRST_FRAME;
				}
				
				dec_disp_info_tmp.m_iM2vFieldSequence = dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iM2vFieldSequence;
				dec_private->pVideoDecodInstance.dec_disp_info_input.m_iFrameIdx = dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodedIdx;
				dec_private->pVideoDecodInstance.dec_disp_info_input.m_iFrameRate = dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iM2vFrameRate;
				disp_pic_info( CVDEC_DISP_INFO_UPDATE, (void*)&dec_private->pVideoDecodInstance.dec_disp_info_ctrl, (void*)&dec_disp_info_tmp, (void*)&dec_private->pVideoDecodInstance.dec_disp_info_input, dec_private->nFps);
				break;
				
			default:
				dec_disp_info_tmp.m_iM2vFieldSequence = 0;
				dec_private->pVideoDecodInstance.dec_disp_info_input.m_iFrameIdx = dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodedIdx;
				disp_pic_info( CVDEC_DISP_INFO_UPDATE, (void*)&dec_private->pVideoDecodInstance.dec_disp_info_ctrl, (void*)&dec_disp_info_tmp, (void*)&dec_private->pVideoDecodInstance.dec_disp_info_input, dec_private->nFps);
				break;
		}
		//current decoded frame info
		
		// [Memo] Linuxに変わって dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iextTimeStamp
		// が怒られるので、無視。
		
		/*
		DebugPrint( "[In - %s][N:%4d][LEN:%6d][RT:%8d] [DecoIdx:%2d][DecStat:%d][FieldSeq:%d][TR:%8d] ", 
						print_pic_type(dec_private->pVideoDecodInstance.gsVDecInit.m_iBitstreamFormat, dec_disp_info_tmp.m_iFrameType, dec_disp_info_tmp.m_iPicStructure),
						dec_private->pVideoDecodInstance.video_dec_idx, pInput->inputStreamSize, (int)(pInput->nTimeStamp),
						dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodedIdx, dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodingStatus,
						dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iM2vFieldSequence, dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iextTimeStamp);
		*/
	}
	else
	{
		// [Memo] Linuxに変わって dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iextTimeStamp
		// が怒られるので、無視。
		
		/*
		DebugPrint( "[Err In - %s][N:%4d][LEN:%6d][RT:%8d] [DecoIdx:%2d][DecStat:%d][FieldSeq:%d][TR:%8d] ", 
						print_pic_type(dec_private->pVideoDecodInstance.>gsVDecInit.m_iBitstreamFormat, dec_disp_info_tmp.m_iFrameType, dec_disp_info_tmp.m_iPicStructure),
						dec_private->pVideoDecodInstance.video_dec_idx, pInput->inputStreamSize, (int)(pInput->nTimeStamp),
						dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodedIdx, dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodingStatus,
						dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iM2vFieldSequence, dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iextTimeStamp);
		*/
	}

//In case that only one field picture is decoded...
#if 1
	if(dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodingStatus == VPU_DEC_SUCCESS_FIELD_PICTURE)
	{
		dec_disp_info_t dec_disp_info_tmp;
		int inTS = pInput->nTimeStamp;
		
		dec_disp_info_tmp.m_iFrameDuration = 1;

		if( dec_private->pVideoDecodInstance.dec_disp_info_ctrl.m_iFmtType  == CONTAINER_MPG )
		{
		//ALOGD("VPU_DEC_SUCCESS_FIELD_PICTURE m_iPTSInterval %d m_iLatestPTS %d inTS %d m_iRamainingDuration %d ",gsMPEG2PtsInfo.m_iPTSInterval , gsMPEG2PtsInfo.m_iLatestPTS,inTS,gsMPEG2PtsInfo.m_iRamainingDuration);
			if( inTS <= dec_private->gsMPEG2PtsInfo.m_iLatestPTS )
				inTS = dec_private->gsMPEG2PtsInfo.m_iLatestPTS + ((dec_private->gsMPEG2PtsInfo.m_iPTSInterval * dec_private->gsMPEG2PtsInfo.m_iRamainingDuration) >> 1);
			dec_private->gsMPEG2PtsInfo.m_iLatestPTS = inTS;
			dec_private->gsMPEG2PtsInfo.m_iRamainingDuration = 1;
		}
		
    #ifdef TS_TIMESTAMP_CORRECTION
		if( dec_private->pVideoDecodInstance.dec_disp_info_ctrl.m_iFmtType == CONTAINER_TS)
		{
			//ALOGD("VPU_DEC_SUCCESS_FIELD_PICTURE m_iPTSInterval %d m_iLatestPTS %d inTS %d m_iRamainingDuration %d ", gsTSPtsInfo.m_iPTSInterval, gsTSPtsInfo.m_iLatestPTS, inTS, gsTSPtsInfo.m_iRamainingDuration);
			if( inTS <= dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iLatestPTS )
				inTS = dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iLatestPTS + ((dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iPTSInterval * dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iRamainingDuration) >> 1);
			dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iLatestPTS = inTS;
			dec_private->pVideoDecodInstance.gsTSPtsInfo.m_iRamainingDuration = 1;
		}
    #endif
	}
#endif

	if( dec_private->frameSearchOrSkip_flag 
		&& dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodedIdx >= 0 
		//&& gsVDecOutput.m_DecOutInfo.m_iDecodingStatus == VPU_DEC_SUCCESS)
		&& (dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodingStatus == VPU_DEC_SUCCESS || dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodingStatus == VPU_DEC_SUCCESS_FIELD_PICTURE))
	{
		// frameType - 0: unknown, 1: I-frame, 2: P-frame, 3:B-frame
		int frameType = get_frame_type_for_frame_skipping( dec_private->pVideoDecodInstance.gsVDecInit.m_iBitstreamFormat, 
														dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iPicType, 
														dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iPictureStructure );

		if( dec_private->pVideoDecodInstance.gsVDecInput.m_iFrameSearchEnable )
		{
			dec_private->frameSearchOrSkip_flag = 2;//I-frame Search Mode disable and B-frame Skip Mode enable
			DebugPrint("[SEEK] I-frame Search Mode disable and B-frame Skip Mode enable");				
		}
		else if( dec_private->pVideoDecodInstance.gsVDecInput.m_iSkipFrameMode == VDEC_SKIP_FRAME_ONLY_B )
		{
			dec_private->frameSearchOrSkip_flag = 0; //B-frame Skip Mode disable
			DebugPrint( "[SEEK] B-frame Skip Mode Disable after P-frame decoding!!!");
		}
	}
	//////////////////////////////////////////////////////////////////////////////////////////
	//ZzaU ? :: width and stride	
	pOutput->picWidth = dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iWidth;
	pOutput->picHeight = dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iHeight;
	pOutput->stride = ((dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iWidth+15)>>4)<<4;
	pOutput->frameFormat = FRAME_BUF_FORMAT_YUV420P;
	//add by yusufu for crop info
	pOutput->crop_left = dec_private->pVideoDecodInstance.gsVDecOutput.m_pInitialInfo->m_iAvcPicCrop.m_iCropLeft;
	pOutput->crop_top = dec_private->pVideoDecodInstance.gsVDecOutput.m_pInitialInfo->m_iAvcPicCrop.m_iCropTop;
	pOutput->crop_right = dec_private->pVideoDecodInstance.gsVDecOutput.m_pInitialInfo->m_iAvcPicCrop.m_iCropRight;
	pOutput->crop_bottom = dec_private->pVideoDecodInstance.gsVDecOutput.m_pInitialInfo->m_iAvcPicCrop.m_iCropBottom;

	if(dec_private->pVideoDecodInstance.gsVDecInit.m_bCbCrInterleaveMode == 1)
		pOutput->frameFormat = FRAME_BUF_FORMAT_YUV420I;

	if (dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iOutputStatus == VPU_DEC_OUTPUT_SUCCESS)
	{
		int i;
		unsigned char *buffer;
		dec_private->ConsecutiveVdecFailCnt = 0; //Reset Consecutive Vdec Fail Counting B060955

		/* physical address */
		buffer = (unsigned char*)pOutput->bufPhyAddr;
		for(i=0;i<3;i++)
			memcpy(buffer+i*4, &dec_private->pVideoDecodInstance.gsVDecOutput.m_pDispOut[PA][i], 4);
		
		/* logical address */
		buffer = (unsigned char*)pOutput->bufVirtAddr;
		for(i=0;i<3;i++)
			memcpy(buffer+i*4, &dec_private->pVideoDecodInstance.gsVDecOutput.m_pDispOut[VA][i], 4);

		//Get TimeStamp!!
		{
			dec_disp_info_t *pdec_disp_info = NULL;
			int dispOutIdx = dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDispOutIdx;
			dec_private->pVideoDecodInstance.dec_disp_info_input.m_iFrameIdx = dispOutIdx;
			disp_pic_info( CVDEC_DISP_INFO_GET, (void*)&dec_private->pVideoDecodInstance.dec_disp_info_ctrl, (void*)&pdec_disp_info, (void*)&dec_private->pVideoDecodInstance.dec_disp_info_input, dec_private->nFps);

			if( pdec_disp_info != (dec_disp_info_t*)0 )
			{
#ifdef JPEG_DECODE_FOR_MJPEG
				if(dec_private->pVideoDecodInstance.gsVDecInit.m_iBitstreamFormat == STD_MJPG)
				{
					pOutput->nTimeStamp = pInput->nTimeStamp;
				}
				else
#endif
				if(dec_private->pVideoDecodInstance.gsVDecInit.m_iBitstreamFormat == STD_RV)
				{
					pOutput->nTimeStamp = (unsigned int)pdec_disp_info->m_iextTimeStamp;
				}
				else// if(omx_private->gsVDecInit.m_iBitstreamFormat == STD_MPEG2)
				{
					pOutput->nTimeStamp = (unsigned int)pdec_disp_info->m_iTimeStamp; //pdec_disp_info->m_iM2vFieldSequence * 1000;
				}

				
				if(dec_private->pVideoDecodInstance.gsVDecInit.m_bEnableUserData)
				{
					print_user_data((unsigned char*)(dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_UserDataAddress[VA]));
				}

				DebugPrint( "[Out - %s][N:%4d][LEN:%6d][RT:%8d] [DispIdx:%2d][OutStat:%d][FieldSeq:%d][TR:%8d] ", 
								print_pic_type(dec_private->pVideoDecodInstance.gsVDecInit.m_iBitstreamFormat, pdec_disp_info->m_iFrameType, pdec_disp_info->m_iPicStructure),
								dec_private->pVideoDecodInstance.video_dec_idx, pdec_disp_info->m_iFrameSize, pdec_disp_info->m_iTimeStamp,
								dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDispOutIdx, dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iOutputStatus,
								pdec_disp_info->m_iM2vFieldSequence, pdec_disp_info->m_iextTimeStamp);
			}
			else
			{
				//exception process!! temp!!
				pOutput->nTimeStamp = pInput->nTimeStamp;
			}
		}

/*ZzaU :: Clear decoded frame-buffer according with sequence-order after it was used!!*/
		if(dec_private->max_fifo_cnt != 0)
		{				
			dec_private->Display_index[dec_private->in_index] = dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDispOutIdx;
			DebugPrint("DispIdx Queue %d", dec_private->Display_index[dec_private->in_index]);
			dec_private->in_index = (dec_private->in_index + 1) % dec_private->max_fifo_cnt;
			
			if(dec_private->in_index == 0 && !dec_private->frm_clear)
				dec_private->frm_clear = 1;

			if(dec_private->frm_clear)
			{
				DebugPrint("Normal DispIdx Clear %d", dec_private->Display_index[dec_private->out_index]);
				if( ( ret = dec_private->pVideoDecodInstance.gspfVDec( VDEC_BUF_FLAG_CLEAR, NULL, &dec_private->Display_index[dec_private->out_index], NULL, dec_private->pVideoDecodInstance.pVdec_Instance ) ) < 0 )
				{
					DebugPrint( "[VDEC_BUF_FLAG_CLEAR] Idx = %d, ret = %d", dec_private->Display_index[dec_private->out_index], ret );
					VideoDecErrorProcess(ret);
					return -1;
				}
				
				dec_private->out_index = (dec_private->out_index + 1) % dec_private->max_fifo_cnt;
			}
		}
		else
		{		
			DebugPrint("@ DispIdx Queue %d", dec_private->Display_index[dec_private->in_index]);
			if( ( ret = dec_private->pVideoDecodInstance.gspfVDec( VDEC_BUF_FLAG_CLEAR, NULL, &dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDispOutIdx, NULL, dec_private->pVideoDecodInstance.pVdec_Instance ) ) < 0 )
			{
				DebugPrint( "[VDEC_BUF_FLAG_CLEAR] Idx = %d, ret = %d", dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDispOutIdx, ret );
				VideoDecErrorProcess(ret);
				return -1;
			}
		}
		dec_private->pVideoDecodInstance.video_dec_idx++;
	}
	else
	{	
		pResult->no_frame_output = 1;
		DebugPrint( "[VDEC_DECODE %d bytes] NO-OUTPUT!! m_iDispOutIdx = %d, m_iDecodedIdx = %d, m_iOutputStatus = %d, m_iDecodingStatus = %d, m_iNumOfErrMBs = %d",
										pInput->inputStreamSize,
										dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDispOutIdx, dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodedIdx,
										dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iOutputStatus, dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iDecodingStatus,
										dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iNumOfErrMBs);
	}
	
	if(dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iOutputStatus != VPU_DEC_OUTPUT_SUCCESS && dec_private->frameSearchOrSkip_flag == 1)
	{	
		dec_private->ConsecutiveVdecFailCnt++;		
		if(dec_private->ConsecutiveVdecFailCnt >= MAX_CONSECUTIVE_VPU_FAIL_COUNT)
		{
			DebugPrint("[VDEC_ERROR]m_iOutputStatus %d %dtimes!!!\n",dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iOutputStatus,dec_private->ConsecutiveVdecFailCnt);
			dec_private->ConsecutiveVdecFailCnt = 0;
			return -1;
		}
	}
	
// To process input stream retry or next frame
	switch (decode_result) {
		case 2 :	// Display Output Success, Decode Success
		case 3 :	// Display Output Failed, Decode Success	
			if ((CONTAINER_TS == dec_private->pVideoDecodInstance.container_type) 
				&& (pInput->inputStreamSize > (dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iConsumedBytes)))
			{
				nLen += dec_private->pVideoDecodInstance.gsVDecOutput.m_DecOutInfo.m_iConsumedBytes;
			}
			else
			{
				nLen += pInput->inputStreamSize;
			}

			if((pInput->inputStreamSize - nLen) > 0)
			{
		#if defined(TCC_93XX_INCLUDE) || defined(TCC_88XX_INCLUDE) // until vpu bug is fixed
				if((CONTAINER_TS == dec_private->pVideoDecodInstance.container_type) && (dec_private->pVideoDecodInstance.video_coding_type == STD_AVC))
				{
					if(!dec_private->bUseFrameDefragmentation)
						pResult->need_input_retry = 0;
					else
						pResult->need_input_retry = 1;
				}
		#endif
			}
			break;

		case 0 :	// Display Output Success, Decode Failed Due to Buffer Full
		case 1 :	// Display Output Failed, Decode Failed Due to Buffer Full		
			pResult->need_input_retry = 1;
		default :
			break;
	}
	
	if(pResult->need_input_retry)
	{
		DebugPrint("Retry input-stream");				
	}
	
	return 0;
}
#if 0
static void save_decoded_frame(unsigned char* Y, unsigned char* U, unsigned char *V, int width, int height)
{

	FILE *pFs = NULL;
	char name[100];

	sprintf(name, "/mnt/SD1p1/DecDump.raw");
	if(!pFs){
		pFs = fopen(name, "ab+");
		if (!pFs) {
			DebugPrint("Cannot open '%s'",name);
			return;
		}
	}
	if(pFs){
		fwrite( Y, width*height, 1, pFs);
		fwrite( U, width*height/4, 1, pFs);
		fwrite( V, width*height/4, 1, pFs);
	}
	fclose(pFs);
}
#endif
int tcc_vpudec_init( int width, int height )
{
	int ret = 0;
	tDEC_INIT_PARAMS pInit;

	pInit.codecFormat = CODEC_FORMAT_H264;  //set just for h264
	pInit.container_type = CONTAINER_NONE;
	pInit.picWidth = width;
	pInit.picHeight = height;
	ret = DECODER_INIT_NoReordering(&pInit);
	
	if(ret < 0)
	{
		ErrorPrint( "vpudec_init fail!!\n" );
		return -1;
	}
	return 0;
}

void tcc_vpudec_close(void)
{
	DECODER_CLOSE();
}

int tcc_vpudec_decode(unsigned int *pInputStream, unsigned int *pOutstream)
{
	int ret = 0;
	tDEC_FRAME_INPUT Input;
	tDEC_FRAME_OUTPUT Output;
	tDEC_RESULT Result;

	Input.inputStreamAddr = (unsigned char*)pInputStream[0];
	Input.inputStreamSize = pInputStream[1];
	Input.nTimeStamp = 0;	
	Input.seek = 0;

	//Display_Stream(Input.inputStreamAddr,Input.inputStreamSize);
	
	ret = DECODER_DEC(&Input, &Output, &Result);
	if(ret < 0)
	{
		ErrorPrint( "[Err:%d] video decoder dec", ret );
		return ret;
	}
	if(Result.no_frame_output)
	{
		ErrorPrint( "No_frame_output");
		return -1;
	}
	else
	{
		pOutstream[0] = Output.frameFormat;
		pOutstream[1] = Output.bufPhyAddr[0];   //Physical Y
		pOutstream[2] = Output.bufPhyAddr[1];   //Physical U
		pOutstream[3] = Output.bufPhyAddr[2];   //Physical V
		pOutstream[4] = Output.bufVirtAddr[0];
		pOutstream[5] = Output.bufVirtAddr[1];
		pOutstream[6] = Output.bufVirtAddr[2];
		pOutstream[7] = Output.nTimeStamp; /* TimeStamp of output bitstream, by ms */
		pOutstream[8] = Output.picWidth; /* Picture width, by pixels */
		pOutstream[9] = Output.picHeight; /* Picture height, by pixels */
		pOutstream[10] = Output.stride;
		pOutstream[11] = Output.crop_left;
		pOutstream[12] = Output.crop_top;
		pOutstream[13] = Output.crop_right;
		pOutstream[14] = Output.crop_bottom;
		
		//DebugPrint( "[libH264] pOutstream[1]=0x%08x, pOutstream[2]=0x%08x, pOutstream[3]=0x%08x",
		//				pOutstream[1], pOutstream[2], pOutstream[3] );
		//DebugPrint( "[libH264] pOutstream[4]=0x%08x, pOutstream[5]=0x%08x, pOutstream[6]=0x%08x",
		//				pOutstream[4], pOutstream[5], pOutstream[6] );
		//DebugPrint( "[libH264] pOutstream[13]=0x%08x, pOutstream[14]=0x%08x.",
		//				pOutstream[13],pOutstream[14]);
		
		// Debug
		//	save_decoded_frame( pOutstream[4], pOutstream[5], pOutstream[6], pOutstream[8], pOutstream[9] );
	}

	return ret;
}
