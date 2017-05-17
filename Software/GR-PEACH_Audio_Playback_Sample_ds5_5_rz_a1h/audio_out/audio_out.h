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

#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H

#include "r_typedefs.h"

/*--- Macro definition ---*/
#define AUD_STACK_SIZE     (2048u)      /* Stack size of Decode thread */

/*--- User defined types ---*/
typedef void (*AUD_CbDataOut)(const bool result);
typedef void (*AUD_CbAudioData)( const bool result, uint16_t * const p_buf, 
    const uint32_t buf_num, const uint32_t * const p_audio, const uint32_t audio_num);

/** Audio Output Thread
 *
 *  @param argument Pointer to the thread function as start argument.
 */
void aud_thread(void const *argument);

/** Requests the audio out thread to read the SCUX conversion results and output data.
 *
 *  @param p_cb Callback function for notifying the completion of data output preparation
 *              typedef void (*AUD_CbDataOut)( const bool result );
 *              When calling callback function specified in p_cb, specify the following
 *              in the callback function argument result:
 *                result : Execution result; true = Success; false = Failure
 *                * Since the SCUX's asynchronous SRC function is used, an overflow error will occur
 *                  if the data read from the SCUX is delayed. For this reason, the callback function
 *                  must be set up to notify the completion of processing after making settings
 *                  for reading SCUX data. The decode thread starts writing data to the SCUX upon
 *                  receipt of the completion notification through the callback function.
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     The argument p_cb is set to NULL.
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool aud_req_data_out(const AUD_CbDataOut p_cb);

/** Requests the audio out thread to stop reading the SCUX conversion results and to generate silent output.
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool aud_req_zero_out(void);

/** Gets the audio data from the output thread.
 *
 *  @param p_cb Callback function for notifying the completion of data acquisition
 *              typedef void (*AUD_CbAudioData)( const bool result, 
 *                            int16_t * const p_buf, const uint32_t buf_num, 
 *                            const int32_t * const p_audio, const uint32_t audio_num);
 *              When calling callback function specified in p_cb, specify the following
 *              in the callback function arguments result, p_buf, buf_num, p_audio, and audio_num:
 *                result : Execution result; true = Success; false = Failure
 *                p_buf : Pointer to the buffer for storing audio data
 *                        * Although the audio data output is 24 bits long, 16 bits of the data
 *                          are used for display.
 *                        * The data that is specified in the second argument p_buf of this function
 *                          must be placed in p_buf of the callback function as is. 
 *                buf_num : Array size of the area pointed to by p_buf
 *                          * The data that is specified in the third argument buf_num of
 *                            this function must be placed in buf_num of the callback function as is.
 *                p_audio : 10 ms equivalent of audio data (88200 Hz, 2ch, 24 bits)
 *                audio_num : Array size of the area pointed to by p_audio
 *  @param p_buf Pointer to the buffer for storing audio data
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     The argument p_cb is set to NULL.
 *     The argument p_buf is set to NULL.
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool aud_get_audio_data(const AUD_CbAudioData p_cb, uint16_t * const p_buf);

#endif /* AUDIO_OUT_H */
