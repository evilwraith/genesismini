/******************************************************************************\

          (c) Copyright Explore Semiconductor, Inc. Limited 2005
                           ALL RIGHTS RESERVED

--------------------------------------------------------------------------------

 Please review the terms of the license agreement before using this file.
 If you are not an authorized user, please destroy this source code file
 and notify Explore Semiconductor Inc. immediately that you inadvertently
 received an unauthorized copy.

--------------------------------------------------------------------------------

  File        :  EP952Controller.c

  Description :  EP952Controller program
                 Control SFR directory and use HCI functions

  Codeing     :  Shihken

\******************************************************************************/


// standard Lib
//#include <stdlib.h>
//#include <string.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <asm/memory.h>
#include <asm/unistd.h>
#include "asm-generic/int-ll64.h"
#include "linux/kernel.h"
#include "linux/mm.h"
#include "linux/semaphore.h"
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>   //wake_up_process()
#include <linux/kthread.h> //kthread_create()??ï¿½ï¿½|kthread_run()
#include <linux/err.h> //IS_ERR()??ï¿½ï¿½|PTR_ERR()
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <mach/sys_config.h>
#include <mach/platform.h>


#include "EP952api.h"
#include "Edid.h"
#include "DDC_If.h"
#include "EP952Controller.h"
#include "EP952SettingsData.h"


//
// Defines
//


#if Enable_HDCP
// HDCP Key
unsigned char HDCP_Key[64][8];
#endif

//
// Global State and Flags
//

// System flags
unsigned char is_Cap_HDMI;
unsigned char is_Cap_YCC444;
unsigned char is_Cap_YCC422;
unsigned char is_Forced_Output_RGB;

unsigned char is_Hot_Plug;
unsigned char is_Connected;
unsigned char is_ReceiverSense;
unsigned char ChkSum, ConnectionState;
unsigned char EP952_Debug = 0;
unsigned char edid_retry_count;

#define MAX_EDID_RETRY_COUNT 3

#if Enable_HDCP
unsigned char is_HDCP_Info_BKSV_Rdy;
#endif


// System Data
unsigned char HP_ChangeCount;
unsigned char PowerUpCount;
TX_STATE TX_State;
VDO_PARAMS Video_Params;
ADO_PARAMS Audio_Params;
PEP952C_REGISTER_MAP pEP952C_Registers;

void EP952_HDCP_Reset(void);
void TXS_RollBack_Stream(void);

#if Enable_HDCP
void TXS_RollBack_HDCP(void);
#endif

//--------------------------------------------------------------------------------------------------------------------

void EP952Controller_Initial(PEP952C_REGISTER_MAP pEP952C_RegMap)
{
	DBG("EP952 Code Version : %d.%d\n", (int)VERSION_MAJOR, (int)VERSION_MINOR );

	// Save the Logical Hardware Assignment
	pEP952C_Registers = pEP952C_RegMap;

	// Hardware Reset
	EP_EP952_Reset();

	// Initial IIC address
	EP952_IIC_Initial();

	// Software power down
	HDMI_Tx_Power_Down();

	// Enable Audio Mute and Video Mute
	HDMI_Tx_Mute_Enable();

	// Reset Variables
	is_Cap_HDMI = 0;
	is_Cap_YCC444 = is_Cap_YCC422 = 0;
	is_Forced_Output_RGB = 0;
	is_Connected = 1;
	TX_State = TXS_Search_EDID;
	HP_ChangeCount = 0;
	PowerUpCount = 0;
	is_ReceiverSense = 0;

	// Reset all EP952 parameters
	pEP952C_Registers->System_Status = 0;
	pEP952C_Registers->EDID_ASFreq = 0;
	pEP952C_Registers->EDID_AChannel = 0;
	pEP952C_Registers->Video_change = 0;
	pEP952C_Registers->Audio_change = 0;
	pEP952C_Registers->System_Configuration = 0;
	pEP952C_Registers->Video_Input_Format[1]= 0;
	pEP952C_Registers->Video_Output_Format = 0;		// Auto select output
//	pEP952C_Registers->Video_Output_Format = 0x03;	// Forced set RGB out
	pEP952C_Registers->Content_Type = 0;

#if Enable_HDCP

	// Initial HDCP Info
	pEP952C_Registers->HDCP_Status = 0;
	pEP952C_Registers->HDCP_State = 0;
	memset(pEP952C_Registers->HDCP_AKSV, 0x00, sizeof(pEP952C_Registers->HDCP_AKSV));
	memset(pEP952C_Registers->HDCP_BKSV, 0x00, sizeof(pEP952C_Registers->HDCP_BKSV));
	memset(pEP952C_Registers->HDCP_BCAPS3, 0x00, sizeof(pEP952C_Registers->HDCP_BCAPS3));
	memset(pEP952C_Registers->HDCP_KSV_FIFO, 0x00, sizeof(pEP952C_Registers->HDCP_KSV_FIFO));
	memset(pEP952C_Registers->HDCP_SHA, 0x00, sizeof(pEP952C_Registers->HDCP_SHA));
	memset(pEP952C_Registers->HDCP_M0, 0x00, sizeof(pEP952C_Registers->HDCP_M0));

	// Set Revocation List address
	HDCP_Extract_BKSV_BCAPS3(pEP952C_Registers->HDCP_BKSV);
	HDCP_Extract_FIFO((unsigned char*)pEP952C_Registers->HDCP_KSV_FIFO, sizeof(pEP952C_Registers->HDCP_KSV_FIFO));
	HDCP_Stop();

#else

	// Disable HDCP
	HDMI_Tx_HDCP_Disable();

#endif

	// HDCP KEY reset
	EP952_HDCP_Reset();

	// Info Frame Reset
	EP952_Info_Reset();

}

void EP952_HDCP_Reset(void)
{
#if Enable_HDCP
	int i;

	//////////////////////////////////////////////////////////////////
	// Read HDCP Key for EEPROM
	if(HDMI_Tx_read_AKSV(pEP952C_Registers->HDCP_AKSV)) {
		pEP952C_Registers->System_Status &= ~EP_TX_System_Status__KEY_FAIL;
		DBG("HDCP Check AKSV PASS\n");
		return;
	}

	status = HDMI_Tx_Get_Key((unsigned char *)HDCP_Key); // read HDCP Key from EEPROM
	DBG("Read HDCP Key = 0x%02X\n",(int)status);
	pEP952C_Registers->System_Status &= ~EP_TX_System_Status__KEY_FAIL;

	// Check HDCP key and up load the key
	if(status) {
		// Do not upload the default Key!
		pEP952C_Registers->System_Status |= EP_TX_System_Status__KEY_FAIL;
		pEP952C_Registers->System_Configuration |= EP_TX_System_Configuration__HDCP_DIS;
		DBG("No HDCP Key - Disable HDCP function\n");
	}
	else {
		// Check HDCP key and up load the key
		ChkSum = 0;
		for(i=0; i<328; ++i) {
			ChkSum += *((unsigned char *)HDCP_Key+i);
		}
		DBG("HDCP Key Check Sum 0x%02X\n", (int)ChkSum);

		if(HDCP_Key[3][7] != 0x50 || HDCP_Key[12][7] != 0x01 || ChkSum != 0x00) {
			pEP952C_Registers->System_Status |= EP_TX_System_Status__KEY_FAIL;
			pEP952C_Registers->System_Configuration |= EP_TX_System_Configuration__HDCP_DIS;
			ERR("HDCP Key Check failed! - Disable HDCP function\n");
		}
		else {
			// Upload the key 0-39
			for(i=0; i<40; ++i) {
				DDC_Data[0] = (unsigned char)i;
				status |= EP952_Reg_Write(EP952_Key_Add, DDC_Data, 1);
				memcpy(DDC_Data,&HDCP_Key[i][0],7);
				status |= EP952_Reg_Write(EP952_Key_Data, DDC_Data, 7);
			}
			// Read and check
			for(i=0; i<40; ++i) {
				DDC_Data[0] = (unsigned char)i;
				status |= EP952_Reg_Write(EP952_Key_Add, DDC_Data, 1);
				status |= EP952_Reg_Read(EP952_Key_Data, DDC_Data, 7);
				if((memcmp(DDC_Data,&HDCP_Key[i][0],7) != 0) || status) {
					// Test failed
					pEP952C_Registers->System_Status |= EP_TX_System_Status__KEY_FAIL;
					pEP952C_Registers->System_Configuration |= EP_TX_System_Configuration__HDCP_DIS;
					ERR("HDCP Key Check failed! - Disable HDCP function\n");
					break;
				}
			}
			// Upload final KSV 40
			DDC_Data[0] = 40;
			status |= EP952_Reg_Write(EP952_Key_Add, DDC_Data, 1);
			memcpy(DDC_Data,&HDCP_Key[40][0],7);
			status |= EP952_Reg_Write(EP952_Key_Data, DDC_Data, 7);
			// Read back and check
	    	if(!HDMI_Tx_read_AKSV(pEP952C_Registers->HDCP_AKSV)) {
				// Test failed
				pEP952C_Registers->System_Status |= EP_TX_System_Status__KEY_FAIL;
				pEP952C_Registers->System_Configuration |= EP_TX_System_Configuration__HDCP_DIS;
				ERR("HDCP Check KSV failed! - Disable HDCP function\n");
			}
		}
	}

#else

	pEP952C_Registers->System_Status |= EP_TX_System_Status__KEY_FAIL;
	pEP952C_Registers->System_Configuration |= EP_TX_System_Configuration__HDCP_DIS;
	DBG("User define - Disable HDCP function\n");

#endif

}

void EP952Controller_Timer(void)
{

#if Enable_HDCP
	if(TX_State == TXS_HDCP) HDCP_Timer();
#endif

}

void EP952Controller_Task(void)
{
	int ignore_edid = 0;
	unsigned char cnc_flags = 0;

	// Polling check Hot-Plug status
	ConnectionState = HDMI_Tx_HTPLG();

	is_Hot_Plug = (ConnectionState == 1)? 1:0;
	is_ReceiverSense = HDMI_Tx_RSEN();

	if(is_Connected != ((ConnectionState)?1:0) )
	{
		if (!is_Connected) {
				DBG("#####################\n");
				DBG("#   HDMI - Connect  #\n");
				DBG("#####################\n");
		}
		if(HP_ChangeCount++ >= 5)  // Hotplug continuous low 500ms	(10ms x 50)
		{
			HP_ChangeCount = 0;

			is_Connected = ((ConnectionState)?1:0);

			if(!is_Connected){

				DBG("#####################\n");
				DBG("# HDMI - Disconnect #\n");
				DBG("#####################\n");

				EDID_InvalidateVIC();

				// power down EP952 Tx
				HDMI_Tx_Power_Down();

				DBG("\nState Transist: Power Down -> [TXS_Search_EDID]\n");
				TX_State = TXS_Search_EDID;
				edid_retry_count = 0;
			}
		}
	}
	else {
		HP_ChangeCount = 0;
	}

	if(EP952_Debug){

		EP952_Debug = 0;

		EP_HDMI_DumpMessage();	// dump EP952 register value for debug
	}


	/////////////////////////////////////////////////////////////////////////////////////////////////
	// Update EP952 Registers according to the System Process
	//
	//DBG("###TX_State=%d\n", TX_State);
	switch(TX_State)
	{
		case TXS_Search_EDID:

			if(is_Hot_Plug && is_ReceiverSense) {

				unsigned char EDID_DDC_Status;

				// clear EDID buffer
				memset(pEP952C_Registers->EDID_Read, 0x0, sizeof(pEP952C_Registers->EDID_Read));

				// Read EDID
				DBG("#####################\n");
				DBG("#  BEGIN EDID DUMP  #\n");
				DBG("#####################\n");
				EDID_DDC_Status = Downstream_Rx_read_EDID(pEP952C_Registers->EDID_Read);

				if(EDID_DDC_Status) {
					WARN("EDID read failed 0x%02X\n", (int)EDID_DDC_Status);
					edid_retry_count++;
					if (edid_retry_count < MAX_EDID_RETRY_COUNT) {
						WARN("Retrying EDID read\n");
						break;
					} else {
						WARN("Max retries reached, ignoring EDID\n");
						ignore_edid = 1;
					}
				}

				memset(VIC_bitmap, 0, sizeof(VIC_bitmap));
				if (!ignore_edid && EDID_ParseVIC(pEP952C_Registers->EDID_Read, VIC_bitmap)) {
					if (pEP952C_Registers->EDID_Read[126] != 0) {
						ERR("Failed to parse EDID\n");
					}
					memset(VIC_bitmap, 0, sizeof(VIC_bitmap));
				}

				EDID_ValidateVIC();

				// check EDID
				is_Cap_HDMI = !ignore_edid && EDID_GetHDMICap(pEP952C_Registers->EDID_Read);

				if (!ignore_edid) {
					if (EDID_GetCNCFlags(pEP952C_Registers->EDID_Read, &cnc_flags)) {
						ERR("error looking for CNC flags in EDID\n");
						cnc_flags = 0;
					} else {
						DBG("read CNC flags: %x\n", (int)cnc_flags);
					}
				}

				if (STATIC_CONTENT_TYPE != CONTENT_TYPE_NONE) {
					if ((cnc_flags & (1 << STATIC_CONTENT_TYPE)) != 0) {
						pEP952C_Registers->Content_Type = (0x4 | STATIC_CONTENT_TYPE);
						DBG("Sink supports content type %d\n", STATIC_CONTENT_TYPE);
					} else {
						pEP952C_Registers->Content_Type = 0;
						DBG("Sink does not support content type %d\n", STATIC_CONTENT_TYPE);
					}
				}

				if(is_Cap_HDMI) {
					DBG("EDID : Support HDMI");

					// Default Capability
					is_Cap_YCC444 =	0;
					is_Cap_YCC422 = 0;
					pEP952C_Registers->EDID_ASFreq = 0x07;
					pEP952C_Registers->EDID_AChannel = 1;

					if(!EDID_DDC_Status) {

						if(pEP952C_Registers->EDID_Read[131] & 0x20) {	// Support YCC444
							is_Cap_YCC444 = 1;
							DBG(" YCC444");
						}
						if(pEP952C_Registers->EDID_Read[131] & 0x10) {	// Support YCC422
							is_Cap_YCC422 = 1;
							DBG(" YCC422");
						}
						DBG("\n");

						// Audio
						pEP952C_Registers->EDID_ASFreq = EDID_GetPCMFreqCap(pEP952C_Registers->EDID_Read);
						DBG("EDID : ASFreq = 0x%02X\n",(int)pEP952C_Registers->EDID_ASFreq);

						// Optional
						//pEP952C_Registers->EDID_VideoDataAddr = 0x00;
						//pEP952C_Registers->EDID_AudioDataAddr = 0x00;
						//pEP952C_Registers->EDID_SpeakerDataAddr = 0x00;
						//pEP952C_Registers->EDID_VendorDataAddr = 0x00;

						//pEP952C_Registers->EDID_VideoDataAddr = EDID_GetDataBlockAddr(pEP952C_Registers->EDID_Read, 0x40);
						//pEP952C_Registers->EDID_AudioDataAddr = EDID_GetDataBlockAddr(pEP952C_Registers->EDID_Read, 0x20);
						//pEP952C_Registers->EDID_SpeakerDataAddr = EDID_GetDataBlockAddr(pEP952C_Registers->EDID_Read, 0x80);
						//pEP952C_Registers->EDID_VendorDataAddr = EDID_GetDataBlockAddr(pEP952C_Registers->EDID_Read, 0x60);
					}
				}
				else {
					DBG("EDID : Support DVI(RGB) only\n");
					is_Cap_YCC444 =	0;
					is_Cap_YCC422 = 0;
					pEP952C_Registers->EDID_ASFreq = 0;
					pEP952C_Registers->EDID_AChannel = 0;
				}

				DBG("#####################\n");
				DBG("#   END EDID DUMP   #\n");
				DBG("#####################\n");

				is_Connected = 1; // HDMI is connected

				DBG("\nState Transit: Read EDID -> [TXS_Wait_Upstream]\n");
				TX_State = TXS_Wait_Upstream;
			}
			break;

		case TXS_Wait_Upstream:

			if(pEP952C_Registers->Video_Input_Format[0] != 0)
			{
				// power down Tx
				HDMI_Tx_Power_Down();

				// update EP952 register setting
				EP952_Audio_reg_set();
				EP952_Video_reg_set();

				EP952_Debug = 1;

				// Power Up Tx
				HDMI_Tx_Power_Up();

				DBG("\nState Transist: Power Up -> [TXS_Stream]\n");
				TX_State = TXS_Stream;
			}
			break;

		case TXS_Stream:

			if(pEP952C_Registers->Audio_change){	// Audio change

				DBG("--- Audio source change ---\n");
				EP952_Audio_reg_set();
				pEP952C_Registers->Audio_change = 0;
				EP952_Debug = 1;
			}

			if(pEP952C_Registers->Video_change){	// Video change

				TXS_RollBack_Stream();
				pEP952C_Registers->Video_change = 0;

				if(pEP952C_Registers->Video_Input_Format[0] != 0){
					DBG("\nState Transit: Video Source Change -> [TXS_Wait_Upstream]\n");
					TX_State = TXS_Wait_Upstream;
				}
				else{
					DBG("\nState Transit: VIC = %d -> [TXS_Search_EDID]\n",(int)pEP952C_Registers->Video_Input_Format[0]);
					TX_State = TXS_Search_EDID;
					edid_retry_count = 0;
				}
			}

			if(!is_Connected) {						// HDMI	not connected

				DBG("\nTXS_Stream: HDMI is not Connected\n");
				TXS_RollBack_Stream();
				TX_State = TXS_Search_EDID;
				edid_retry_count = 0;
			}

#if Enable_HDCP
			else if(!(pEP952C_Registers->System_Configuration & EP_TX_System_Configuration__HDCP_DIS) && is_ReceiverSense) {

				if(!is_HDCP_Info_BKSV_Rdy) {
					// Get HDCP Info
			    	if(!Downstream_Rx_read_BKSV(pEP952C_Registers->HDCP_BKSV)) {
						pEP952C_Registers->HDCP_Status = HDCP_ERROR_AKSV;
					}
					pEP952C_Registers->HDCP_BCAPS3[0] = Downstream_Rx_BCAPS();
					is_HDCP_Info_BKSV_Rdy = 1;
				}

				// Enable mute
				DBG("Mute first for HDCP Authentication start\n");
				HDMI_Tx_Mute_Enable();

				DBG("\nState Transist: Start HDCP -> [TXS_HDCP]\n");
				TX_State = TXS_HDCP;
			}
#endif
			break;

#if Enable_HDCP

		case TXS_HDCP:

			if(pEP952C_Registers->Audio_change){	// Audio change

				DBG("--- Audio source change ---\n");
				EP952_Audio_reg_set();
				pEP952C_Registers->Audio_change = 0;
				EP952_Debug = 1;
			}

			if(pEP952C_Registers->Video_change){	// Video change

				TXS_RollBack_HDCP();
				TXS_RollBack_Stream();
				pEP952C_Registers->Video_change = 0;

				if(pEP952C_Registers->Video_Input_Format[0] != 0){
					DBG("\nState Transit: Video Source Change -> [TXS_Wait_Upstream]\n");
					TX_State = TXS_Wait_Upstream;
				}
				else{
					DBG("\nState Transit: VIC = %d -> [TXS_Search_EDID]\n",(int)pEP952C_Registers->Video_Input_Format[0]);
					TX_State = TXS_Search_EDID;
					edid_retry_count = 0;
				}
			}

			if(!is_Connected) {						// HDMI	not connected

				TXS_RollBack_HDCP();
				TXS_RollBack_Stream();
				TX_State = TXS_Search_EDID;
				edid_retry_count = 0;
			}
			else {
				pEP952C_Registers->HDCP_State = HDCP_Authentication_Task(is_ReceiverSense && is_Connected/*is_Hot_Plug*/);
				pEP952C_Registers->HDCP_Status = HDCP_Get_Status();
				if(pEP952C_Registers->HDCP_Status != 0)
				{
					ERR(("HDCP_Status = 0x%02X\n",(int)pEP952C_Registers->HDCP_Status);

					TXS_RollBack_HDCP();
					TXS_RollBack_Stream();
					TX_State = TXS_Search_EDID;
					edid_retry_count = 0;
				}
			}
			break;
#endif

		default:
			DBG("TX_State=%d\n", TX_State);
	}
}

void EP952_Video_reg_set(void)
{
	DBG("\n ========== EP952 Video Parameter setting ==========\n");

	// Mute Control
	HDMI_Tx_Mute_Enable();

	if(pEP952C_Registers->Video_Input_Format[0] != 0)
	{
		// HDMI Mode
		if(!is_Cap_HDMI ) {
			HDMI_Tx_DVI();	// Set to DVI output mode (The Info Frame and Audio Packets would not be send)
			is_Forced_Output_RGB = 1;
		}
		else {
			HDMI_Tx_HDMI();	// Set to HDMI output mode
		}

		///////////////////////////////////////////////////////////////////////
		// Update Video Params
		//
		DBG("pEP952 Video_Interface[0] = 0x%02X\n",(int)pEP952C_Registers->Video_Interface[0]);
		DBG("pEP952 Video_Interface[1] = 0x%02X\n",(int)pEP952C_Registers->Video_Interface[1]);
		DBG("pEP952 Video_Output_Format = 0x%02X \n",(int)pEP952C_Registers->Video_Output_Format );
		DBG("pEP952 Video_Input_Format[0] = 0x%02X \n",(int)pEP952C_Registers->Video_Input_Format[0] );

		// Video Interface
		Video_Params.Interface = pEP952C_Registers->Video_Interface[0];

		// Video Timing
		if(pEP952C_Registers->Video_Input_Format[0] < 35) {
			Video_Params.VideoSettingIndex = pEP952C_Registers->Video_Input_Format[0];
		}
		else{
			Video_Params.VideoSettingIndex = 0;
			DBG("ERROR: pEP952 Video_Input_Format[0] = 0x%02X OVER CEA-861-B SPEC\n",(int)pEP952C_Registers->Video_Input_Format[0]);
		}

		// Select Sync Mode
		Video_Params.SyncMode = (pEP952C_Registers->Video_Interface[1] & EP_TX_Video_Interface_Setting_1__SYNC) >> 2;

		// Select Color Space
		switch(Video_Params.VideoSettingIndex) {
			case  4: case  5: case 16: case 19: case 20: case 31: case 32:
			case 33: case 34: 													// HD Timing
				Video_Params.ColorSpace = COLORSPACE_709;
				break;

			default:
				if(Video_Params.VideoSettingIndex) { 							// SD Timing
					Video_Params.ColorSpace = COLORSPACE_601;
				}
				else {															// IT Timing
					Video_Params.ColorSpace = COLORSPACE_709;
				}
				break;
		}

		// Forced Output RGB Format
		if(pEP952C_Registers->Video_Output_Format == 0x03) {
			is_Forced_Output_RGB = 1;
		}

		// Set In and Output Color Format
		switch(pEP952C_Registers->Video_Interface[1] & EP_TX_Video_Interface_Setting_1__VIN_FMT) {

			default:
			case EP_TX_Video_Interface_Setting_1__VIN_FMT__RGB:	 	// input is RGB
				Video_Params.FormatIn = COLORFORMAT_RGB;
				Video_Params.FormatOut = COLORFORMAT_RGB;
				break;

			case EP_TX_Video_Interface_Setting_1__VIN_FMT__YCC444: 	// input is YCC444
				Video_Params.FormatIn = COLORFORMAT_YCC444;
				if(!is_Forced_Output_RGB && is_Cap_YCC444) {
					Video_Params.FormatOut = COLORFORMAT_YCC444;
				}
				else {
					Video_Params.FormatOut = COLORFORMAT_RGB;
				}
				break;

			case EP_TX_Video_Interface_Setting_1__VIN_FMT__YCC422: 	// inut is YCC422
				Video_Params.FormatIn = COLORFORMAT_YCC422;
				if(!is_Forced_Output_RGB && is_Cap_YCC422) {
					Video_Params.FormatOut = COLORFORMAT_YCC422;
				}
				else {
					Video_Params.FormatOut = COLORFORMAT_RGB;
				}
				break;
		}
		// AFAR
		Video_Params.AFARate = 0;

		// SCAN
		Video_Params.SCAN = 0;

		Video_Params.ContentType = pEP952C_Registers->Content_Type;
		// Update EP952 Video Registers
		HDMI_Tx_Video_Config(&Video_Params);

		// mute control
		HDMI_Tx_Mute_Disable();
	}
	else
	{
		WARN("Tx Mute Enable (VIC = %d) ###\n"
			,(int)pEP952C_Registers->Video_Input_Format[0]);
	}

	// clear flag
	pEP952C_Registers->Video_change = 0;

}

void EP952_Audio_reg_set(void)
{
	DBG("\n ========== EP952 Audio Parameter setting ==========\n");

	// Mute Control
	HDMI_Tx_AMute_Enable();

	if( (pEP952C_Registers->Audio_Input_Format != 0) && (pEP952C_Registers->Video_Input_Format[0] != 0))
	{
		///////////////////////////////////////////////////////////////////////
		// Update Audio Params
		//
		DBG("pEP952 Audio_Interface = 0x%02X\n",(int)pEP952C_Registers->Audio_Interface);
		DBG("pEP952 Audio_Input_Format = 0x%02X\n",(int)pEP952C_Registers->Audio_Input_Format);

		Audio_Params.Interface = pEP952C_Registers->Audio_Interface & 0x0F; // IIS, WS_M, WS_POL, SCK_POL
		Audio_Params.VideoSettingIndex = pEP952C_Registers->Video_Input_Format[0]; //Video_Params.VideoSettingIndex;

		// Audio Channel Number
		Audio_Params.ChannelNumber = 1; // 2 ch

		// Update VFS
		if(Audio_Params.VideoSettingIndex < EP952_VDO_Settings_IT_Start) {
			Audio_Params.VFS = 1;
		}
		else {
			Audio_Params.VFS = 0;
		}

		Audio_Params.NoCopyRight = 0;

		// Write Frequency info (Use ADO_FREQ or Auto)
		switch( pEP952C_Registers->Audio_Input_Format & EP_TX_Audio_Input_Format__ADO_FREQ ) {

			case EP_TX_Audio_Input_Format__ADO_FREQ__32000Hz:
				Audio_Params.InputFrequency = ADSFREQ_32000Hz;
				Audio_Params.ADSRate = 0; // Disable Down Sample
				break;

			default:
			case EP_TX_Audio_Input_Format__ADO_FREQ__44100Hz:
				Audio_Params.InputFrequency = ADSFREQ_44100Hz;
				Audio_Params.ADSRate = 0; // Disable Down Sample
				break;

			case EP_TX_Audio_Input_Format__ADO_FREQ__48000Hz:
				Audio_Params.InputFrequency = ADSFREQ_48000Hz;
				Audio_Params.ADSRate = 0; // Disable Down Sample
				break;

			case EP_TX_Audio_Input_Format__ADO_FREQ__88200Hz:
				Audio_Params.InputFrequency = ADSFREQ_88200Hz;
				if(pEP952C_Registers->EDID_ASFreq & 0x08) { // 88.2kHz
					Audio_Params.ADSRate = 0; // Disable Down Sample
				}
				else {
					Audio_Params.ADSRate = 1; // Enable Down Sample 1/2
				}
				break;

			case EP_TX_Audio_Input_Format__ADO_FREQ__96000Hz:
				Audio_Params.InputFrequency = ADSFREQ_96000Hz;
				if(pEP952C_Registers->EDID_ASFreq & 0x10) { // 96kHz
					Audio_Params.ADSRate = 0; // Disable Down Sample
				}
				else {
					if(pEP952C_Registers->EDID_ASFreq & 0x04) { // 48kHz
						Audio_Params.ADSRate = 1; // Enable Down Sample 1/2
					}
					else {
						Audio_Params.ADSRate = 2; // Enable Down Sample 1/3
					}
				}
				break;

			case EP_TX_Audio_Input_Format__ADO_FREQ__176400Hz:
				Audio_Params.InputFrequency = ADSFREQ_176400Hz;
				if(pEP952C_Registers->EDID_ASFreq & 0x20) { // 176kHz
					Audio_Params.ADSRate = 0; // Disable Down Sample
				}
				else {
					if(pEP952C_Registers->EDID_ASFreq & 0x08) { // 88.2kHz
						Audio_Params.ADSRate = 1; // Enable Down Sample 1/2
					}
					else {
						Audio_Params.ADSRate = 3; // Enable Down Sample 1/4
					}
				}
				break;

			case EP_TX_Audio_Input_Format__ADO_FREQ__192000Hz:
				Audio_Params.InputFrequency = ADSFREQ_192000Hz;
				if(pEP952C_Registers->EDID_ASFreq & 0x40) { // 192kHz
					Audio_Params.ADSRate = 0; // Disable Down Sample
				}
				else {
					if(pEP952C_Registers->EDID_ASFreq & 0x10) { // 96kHz
						Audio_Params.ADSRate = 1; // Enable Down Sample 1/2
				}
				else {
						Audio_Params.ADSRate = 3; // Enable Down Sample 1/4
					}
				}
				break;
		}

		// Update EP952 Audio Registers
		HDMI_Tx_Audio_Config(&Audio_Params);

		// mute control
		HDMI_Tx_AMute_Disable();
	}
	else
	{
		WARN("[Warning]: Audio Mute Enable (Audio Sample Frequency = %d, VIC = %d)\n"
			,(int)pEP952C_Registers->Audio_Input_Format
			,(int)pEP952C_Registers->Video_Input_Format[0]);
	}

	// clear flag
	pEP952C_Registers->Audio_change = 0;
}

void TXS_RollBack_Stream(void)
{
	DBG("\nState Rollback: Power Down -> [TXS_Search_EDID]\n");

	// Power Down
	HDMI_Tx_Power_Down();

#if Enable_HDCP
	// Reset HDCP Info
	memset(pEP952C_Registers->HDCP_BKSV, 0x00, sizeof(pEP952C_Registers->HDCP_BKSV));
	is_HDCP_Info_BKSV_Rdy = 0;
#endif

}

#if Enable_HDCP
void TXS_RollBack_HDCP(void)
{
	DBG("\nState Rollback: Stop HDCP -> [TXS_Stream]\n");

	HDCP_Stop();
	pEP952C_Registers->HDCP_Status = 0;
	pEP952C_Registers->HDCP_State = 0;

}
#endif

//----------------------------------------------------------------------------------------------------------------------
void EP952_EXTINT_init(unsigned char INT_Enable, unsigned char INT_OD, unsigned char INT_POL)
{
	if(INT_Enable)
	{
		// monitor sense is set to HTPLG detect
		EP952_Reg_Set_Bit(EP952_General_Control_1, EP952_General_Control_1__TSEL_HTP);

		if(INT_OD == INT_OPEN_DRAIN)
		{
			// INT pin is open drain output (need external pull high)
			EP952_Reg_Set_Bit(EP952_General_Control_1, EP952_General_Control_1__INT_OD);
		}
		else
		{
			// INT pin is push-pull output
			EP952_Reg_Clear_Bit(EP952_General_Control_1, EP952_General_Control_1__INT_OD);
		}

		if(INT_POL == INT_High)
		{
			// INT pin is high when interrupt
			EP952_Reg_Set_Bit(EP952_General_Control_1, EP952_General_Control_1__INT_POL);
		}
		else
		{
			// INT pin is low when interrupt
			EP952_Reg_Clear_Bit(EP952_General_Control_1, EP952_General_Control_1__INT_POL);
		}

		// monitor sense is enable
		EP952_Reg_Set_Bit(EP952_General_Control_2, EP952_General_Control_2__MIFE);
	}
	else
	{
		// monitor sense is Disable
		EP952_Reg_Clear_Bit(EP952_General_Control_2, EP952_General_Control_2__MIFE);
	}

}


void EP_HDMI_DumpMessage(void)
{
	unsigned char temp_R = 0xFF, reg_addr = 0;

	printk("[EP952 Register value]\n");
	printk("    -0 -1 -2 -3 -4 -5 -6 -7 -8 -9 -A -B -C -D -E -F\n");
	printk("    -----------------------------------------------");
	for(reg_addr=0; reg_addr<=0x88; reg_addr++)
	{
		EP952_Reg_Read(reg_addr, &temp_R, 1);

		if(reg_addr%16 == 0)
		{
			printk("\n%02X| ",(int)((reg_addr/16)<<4));
		}
		printk("%02X ",(int)temp_R);

	}
	printk("\n");
	printk("    -----------------------------------------------\n");
}


