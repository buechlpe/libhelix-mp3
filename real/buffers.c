/* ***** BEGIN LICENSE BLOCK ***** 
 * Version: RCSL 1.0/RPSL 1.0 
 *  
 * Portions Copyright (c) 1995-2002 RealNetworks, Inc. All Rights Reserved. 
 *      
 * The contents of this file, and the files included with this file, are 
 * subject to the current version of the RealNetworks Public Source License 
 * Version 1.0 (the "RPSL") available at 
 * http://www.helixcommunity.org/content/rpsl unless you have licensed 
 * the file under the RealNetworks Community Source License Version 1.0 
 * (the "RCSL") available at http://www.helixcommunity.org/content/rcsl, 
 * in which case the RCSL will apply. You may also obtain the license terms 
 * directly from RealNetworks.  You may not use this file except in 
 * compliance with the RPSL or, if you have a valid RCSL with RealNetworks 
 * applicable to this file, the RCSL.  Please see the applicable RPSL or 
 * RCSL for the rights, obligations and limitations governing use of the 
 * contents of the file.  
 *  
 * This file is part of the Helix DNA Technology. RealNetworks is the 
 * developer of the Original Code and owns the copyrights in the portions 
 * it created. 
 *  
 * This file, and the files included with this file, is distributed and made 
 * available on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER 
 * EXPRESS OR IMPLIED, AND REALNETWORKS HEREBY DISCLAIMS ALL SUCH WARRANTIES, 
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS 
 * FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT. 
 * 
 * Technology Compatibility Kit Test Suite(s) Location: 
 *    http://www.helixcommunity.org/content/tck 
 * 
 * Contributor(s): 
 *  
 * ***** END LICENSE BLOCK ***** */ 

/**************************************************************************************
 * Fixed-point MP3 decoder
 * Jon Recker (jrecker@real.com), Ken Cooke (kenc@real.com)
 * June 2003
 *
 * buffers.c - allocation and freeing of internal MP3 decoder buffers
 *
 * All memory allocation for the codec is done in this file, so if you don't want 
 *  to use other the default system malloc() and free() for heap management this is 
 *  the only file you'll need to change.
 **************************************************************************************/

//#include "hlxclib/stdlib.h"		/* for malloc, free */ 
#include <stdlib.h>
#include <string.h>
#include "coder.h"
#include "app_memory.h"

/*
 * Static decoder state placed in external PSRAM when SPIRAM is available.
 * libhelix's default AllocateBuffers()/FreeBuffers() use malloc()/free(),
 * which on ESP32-S3
 * under Zephyr consume the small picolibc DRAM heap (typically ~22 KB) and
 * fail because the decoder state needs roughly 27 KB.  Keeping the state in
 * PSRAM makes initialization reliable while still allowing it to be cleared
 * and reused across audio_start()/audio_stop() cycles.
 */
static MP3DecInfo s_mp3DecInfo APP_PSRAM_SECTION;
static FrameHeader s_frameHeader APP_PSRAM_SECTION;
static SideInfo s_sideInfo APP_PSRAM_SECTION;
static ScaleFactorInfo s_scaleFactorInfo APP_PSRAM_SECTION;
static HuffmanInfo s_huffmanInfo APP_PSRAM_SECTION;
static DequantInfo s_dequantInfo APP_PSRAM_SECTION;
static IMDCTInfo s_imdctInfo APP_PSRAM_SECTION;
static SubbandInfo s_subbandInfo APP_PSRAM_SECTION;

static int s_decoder_state_initialized = 0;

/**************************************************************************************
 * Function:    ClearBuffer
 *
 * Description: fill buffer with 0's
 *
 * Inputs:      pointer to buffer
 *              number of bytes to fill with 0
 *
 * Outputs:     cleared buffer
 *
 * Return:      none
 *
 * Notes:       slow, platform-independent equivalent to memset(buf, 0, nBytes)
 **************************************************************************************/
static void ClearBuffer(void *buf, int nBytes)
{
	int i;
	unsigned char *cbuf = (unsigned char *)buf;

	for (i = 0; i < nBytes; i++)
		cbuf[i] = 0;

	return;
}

/**************************************************************************************
 * Function:    AllocateBuffers
 *
 * Description: allocate all the memory needed for the MP3 decoder
 *
 * Inputs:      none
 *
 * Outputs:     none
 *
 * Return:      pointer to MP3DecInfo structure (initialized with pointers to all 
 *                the internal buffers needed for decoding, all other members of 
 *                MP3DecInfo structure set to 0)
 *
 * Notes:       if one or more mallocs fail, function frees any buffers already
 *                allocated before returning
 **************************************************************************************/
MP3DecInfo *AllocateBuffers(void)
{
	MP3DecInfo *mp3DecInfo = &s_mp3DecInfo;

	/* Always clear the state so that the DSP primitives see zeros on first use. */
	ClearBuffer(mp3DecInfo, sizeof(MP3DecInfo));

	if (!s_decoder_state_initialized) {
		mp3DecInfo->FrameHeaderPS =     (void *)&s_frameHeader;
		mp3DecInfo->SideInfoPS =        (void *)&s_sideInfo;
		mp3DecInfo->ScaleFactorInfoPS = (void *)&s_scaleFactorInfo;
		mp3DecInfo->HuffmanInfoPS =     (void *)&s_huffmanInfo;
		mp3DecInfo->DequantInfoPS =     (void *)&s_dequantInfo;
		mp3DecInfo->IMDCTInfoPS =       (void *)&s_imdctInfo;
		mp3DecInfo->SubbandInfoPS =     (void *)&s_subbandInfo;
		s_decoder_state_initialized = 1;
	}

	ClearBuffer(mp3DecInfo->FrameHeaderPS,     sizeof(FrameHeader));
	ClearBuffer(mp3DecInfo->SideInfoPS,        sizeof(SideInfo));
	ClearBuffer(mp3DecInfo->ScaleFactorInfoPS, sizeof(ScaleFactorInfo));
	ClearBuffer(mp3DecInfo->HuffmanInfoPS,     sizeof(HuffmanInfo));
	ClearBuffer(mp3DecInfo->DequantInfoPS,     sizeof(DequantInfo));
	ClearBuffer(mp3DecInfo->IMDCTInfoPS,       sizeof(IMDCTInfo));
	ClearBuffer(mp3DecInfo->SubbandInfoPS,     sizeof(SubbandInfo));

	return mp3DecInfo;
}

/**************************************************************************************
 * Function:    FreeBuffers
 *
 * Description: clears the static decoder state so it can be reused later
 *
 * Inputs:      pointer to initialized MP3DecInfo structure (ignored)
 *
 * Outputs:     none
 *
 * Return:      none
 *
 * Notes:       safe to call even if mp3DecInfo is NULL; state lives in PSRAM
 **************************************************************************************/
void FreeBuffers(MP3DecInfo *mp3DecInfo)
{
	(void)mp3DecInfo;

	if (!s_decoder_state_initialized) {
		return;
	}

	ClearBuffer(&s_mp3DecInfo,       sizeof(s_mp3DecInfo));
	ClearBuffer(&s_frameHeader,      sizeof(s_frameHeader));
	ClearBuffer(&s_sideInfo,         sizeof(s_sideInfo));
	ClearBuffer(&s_scaleFactorInfo,  sizeof(s_scaleFactorInfo));
	ClearBuffer(&s_huffmanInfo,      sizeof(s_huffmanInfo));
	ClearBuffer(&s_dequantInfo,      sizeof(s_dequantInfo));
	ClearBuffer(&s_imdctInfo,        sizeof(s_imdctInfo));
	ClearBuffer(&s_subbandInfo,      sizeof(s_subbandInfo));

	s_decoder_state_initialized = 0;
}
