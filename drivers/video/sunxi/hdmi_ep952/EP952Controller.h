/******************************************************************************\

          (c) Copyright Explore Semiconductor, Inc. Limited 2005
                           ALL RIGHTS RESERVED 

--------------------------------------------------------------------------------

 Please review the terms of the license agreement before using this file.
 If you are not an authorized user, please destroy this source code file  
 and notify Explore Semiconductor Inc. immediately that you inadvertently 
 received an unauthorized copy.  

--------------------------------------------------------------------------------

  File        :  EP952Controller.h

  Description :  Head file of EP952Controller.

\******************************************************************************/

#ifndef EP952CONTROLLER_H
#define EP952CONTROLLER_H

#include "EP952_If.h"
#include "EP952api.h"

#if Enable_HDCP
#include "HDCP.h"
#endif

#include "Edid.h"

#define VERSION_MAJOR             52   
#define VERSION_MINOR              1     

#define INT_enable		1
#define INT_disable		0
#define INT_OPEN_DRAIN	1
#define INT_PUSH_PULL	0
#define INT_High		1
#define INT_Low			0

typedef enum {
	TXS_Search_EDID,
	TXS_Wait_Upstream,
	TXS_Stream,
	TXS_HDCP
} TX_STATE;

typedef struct _EP952C_REGISTER_MAP {

	unsigned char		System_Status;		

#if Enable_HDCP
	unsigned char		HDCP_Status;			
	unsigned char		HDCP_State;
	unsigned char		HDCP_AKSV[5];
	unsigned char		HDCP_BKSV[5];
	unsigned char		HDCP_BCAPS3[3];
	unsigned char		HDCP_KSV_FIFO[5*16];
	unsigned char		HDCP_SHA[20];
	unsigned char		HDCP_M0[8];
#endif

	unsigned char		EDID_Read[128 * EDID_MAX_BLOCK_COUNT];
	unsigned char		EDID_ASFreq;
	unsigned char		EDID_AChannel;
	//unsigned char		EDID_VideoDataAddr;
	//unsigned char		EDID_AudioDataAddr;
	//unsigned char		EDID_SpeakerDataAddr;
	//unsigned char		EDID_VendorDataAddr;

	unsigned char		System_Configuration;

	unsigned char		Video_Interface[2];		//		
	unsigned char		Video_Input_Format[2];	//	
	unsigned char 		Video_Output_Format;	//

	unsigned char		Audio_Interface;		//		
	unsigned char		Audio_Input_Format;		//

	unsigned char		Video_change;
	unsigned char		Audio_change;

	unsigned char		Content_Type;

} EP952C_REGISTER_MAP, *PEP952C_REGISTER_MAP;

typedef enum {
	CONTENT_TYPE_NONE = -1,
	CONTENT_TYPE_GRAPHICS,
	CONTENT_TYPE_PHOTO,
	CONTENT_TYPE_CINEMA,
	CONTENT_TYPE_GAME
} CONTENT_TYPE;

#define STATIC_CONTENT_TYPE CONTENT_TYPE_GAME

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

typedef void (*EP952C_CALLBACK)(void);

void EP952Controller_Initial(PEP952C_REGISTER_MAP pEP952C_RegMap);

void EP952Controller_Task(void);

void EP952Controller_Timer(void);

void EP952_Audio_reg_set(void);
void EP952_Video_reg_set(void);

void EP952_EXTINT_init(unsigned char INT_Enable, unsigned char INT_OD, unsigned char INT_POL);
void EP_HDMI_DumpMessage(void);

// -----------------------------------------------------------------------------
#endif

