/*******************************************************************************
* DISCLAIMER
* This software is supplied by Renesas Electronics Corporation and is only
* intended for use with Renesas products. No other uses are authorized. This
* software is owned by Renesas Electronics Corporation and is protected under
* all applicable laws, including copyright laws.
* THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
* THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT
* LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
* AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED.
* TO THE MAXIMUM EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS
* ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES SHALL BE LIABLE
* FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR
* ANY REASON RELATED TO THIS SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE
* BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
* Renesas reserves the right, without notice, to make changes to this software
* and to discontinue the availability of this software. By using this software,
* you agree to the additional terms and conditions found by accessing the
* following link:
* http://www.renesas.com/disclaimer*
* Copyright (C) 2015 Renesas Electronics Corporation. All rights reserved.
*******************************************************************************/

#ifndef DECODE_H
#define DECODE_H

#include "r_typedefs.h"
#include "USBHostMSD.h"
#include "R_BSP_Scux.h"

/*--- Macro definition ---*/
#define DEC_STACK_SIZE              (2048u)     /* Stack size of Decode thread */
#define DEC_MIN_BLOCK_SIZE          (192u)      /* Minimum block size */
#define DEC_MAX_BLOCK_SIZE          (16384u)    /* Maximum block size */
#define DEC_16BITS_PER_SAMPLE       (16u)       /* Bit count per sample */
#define DEC_24BITS_PER_SAMPLE       (24u)       /* Bit count per sample */
#define DEC_MAX_CHANNEL_NUM         (2u)        /* Maximum number of channel */
#define DEC_OUTPUT_PADDING_BITS     (8u)        /* Padding of lower 8 bits */
#define DEC_SCUX_READ_NUM           (9u)        /* The number of buffuer for SCUX read */

/* Minimum sampling rate in Hz of input file */
#define DEC_INPUT_MIN_SAMPLE_RATE   (SAMPLING_RATE_22050HZ)
/* Maximum sampling rate in Hz of input file */
#define DEC_INPUT_MAX_SAMPLE_RATE   (SAMPLING_RATE_96000HZ)
/* Sampling rate in Hz of audio output */
#define DEC_OUTPUT_SAMPLE_RATE      (SAMPLING_RATE_96000HZ)
/* Channel number of audio output */
#define DEC_OUTPUT_CHANNEL_NUM      (DEC_MAX_CHANNEL_NUM)
/* Bit count per sample of audio output */
#define DEC_OUTPUT_BITS_PER_SAMPLE  (DEC_24BITS_PER_SAMPLE)

/*--- User defined types ---*/
typedef void (*DEC_CbOpen)(const bool result, 
                const uint32_t sample_freq, const uint32_t channel_num);
typedef void (*DEC_CbClose)(void);

/** Decode Thread
 *
 *  @param argument Pointer to the thread function as start argument.
 */
void dec_thread(void const *argument);

/** Instructs the decode thread to open the decoder.
 *
 *  @param p_handle File handle
 *  @param p_cb Callback function for notifying the completion of open processing
 *              typedef void (*DEC_CbOpen)(const bool result, 
 *                            const uint32_t sample_freq, const uint32_t channel_num);
 *              When calling callback function specified in p_cb, specify the following
 *              in the callback function arguments result, sample_freq, and channel_num:
 *                result : Execution result; true = Open is successful, false = Open fails
 *                sample_freq : Sampling frequency of the file to be played back
 *                channel_num : Number of channels for the file to be played back.
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     The argument p_handle is set to NULL.
 *     The argument p_cb is set to NULL.
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool dec_open(FILE * const p_handle, const DEC_CbOpen p_cb);

/** Instructs the decode thread for playback.
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool dec_play(void);

/** Instructs the decode thread for pause.
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool dec_pause_on(void);

/** Instructs the decode thread to exit the pause state.
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool dec_pause_off(void);

/** Instructs the decode thread to stop processing.
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool dec_stop(void);

/** Instructs the decode thread to close the decoder.
 *
 *  @param p_cb Callback function for notifying the completion of close processing
 *              typedef void (*DEC_CbClose)(void);
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     The argument p_cb is set to NULL.
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool dec_close(const DEC_CbClose p_cb);

/** Issues a read request to the SCUX driver.
 *
 *  @param p_data Buffer for storing the read data
 *  @param data_size Number of bytes to read
 *  @param p_data_conf Asynchronous control information 
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     The argument p_data is set to NULL.
 *     The argument data_size is set to 0.
 *     The argument p_data_conf is set to NULL.
 */
bool dec_scux_read(void * const p_data, const uint32_t data_size, 
                            const rbsp_data_conf_t * const p_data_conf);

#endif /* DECODE_H */
