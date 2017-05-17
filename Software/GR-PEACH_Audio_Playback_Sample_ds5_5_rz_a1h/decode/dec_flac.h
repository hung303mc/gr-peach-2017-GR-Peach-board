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

#ifndef DEC_FLAC_H
#define DEC_FLAC_H

#include "r_typedefs.h"
#include "stream_decoder.h"

/*--- User defined types ---*/
typedef struct {
    FLAC__StreamDecoder     *p_decoder;         /* Handle of flac decoder */
    FILE                    *p_file_handle;     /* Handle of flac file */
    uint64_t                decoded_sample;     /* Number of a decoded sample */
    uint64_t                total_sample;       /* Total number of sample */
    uint32_t                sample_rate;        /* Sample rate in Hz */
    uint32_t                channel_num;        /* Number of channels */
    uint32_t                bits_per_sample;    /* bit per sample */
    int32_t                 *p_pcm_buf;         /* Pointer of PCM buffer */
    uint32_t                pcm_buf_num;        /* Size of PCM buffer */
    uint32_t                pcm_buf_used_cnt;   /* Counter of used elements in PCM buffer */
} flac_ctrl_t;

/** Sets the PCM buffer to store decoded data
 *
 *  @param p_flac_ctrl Pointer to the control data of FLAC module.
 *  @param p_buf_addr Pointer to PCM buffer to store decoded data.
 *  @param buf_num Elements number of PCM buffer array.
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
bool flac_set_pcm_buf(flac_ctrl_t * const p_flac_ctrl, 
        int32_t * const p_buf_addr, const uint32_t buf_num);

/** Gets elements number of the decoded data
 *
 *  @param p_flac_ctrl Pointer to the control data of FLAC module.
 *
 *  @returns 
 *    Elements number of the decoded data.
 */
uint32_t flac_get_pcm_cnt(const flac_ctrl_t * const p_flac_ctrl);

/** Gets the playback time
 *
 *  @param p_flac_ctrl Pointer to the control data of FLAC module.
 *
 *  @returns 
 *    Playback time (second).
 */
uint32_t flac_get_play_time(const flac_ctrl_t * const p_flac_ctrl);

/** Gets the total playback time
 *
 *  @param p_flac_ctrl Pointer to the control data of FLAC module.
 *
 *  @returns 
 *    Total playback time (second).
 */
uint32_t flac_get_total_time(const flac_ctrl_t * const p_flac_ctrl);

/** Open the FLAC decoder
 *
 *  @param p_handle Pointer to the handle of FLAC file.
 *  @param p_flac_ctrl Pointer to the control data of FLAC module.
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
bool flac_open(FILE * const p_handle, flac_ctrl_t * const p_flac_ctrl);

/** Decode some audio frames.
 *
 *  @param p_flac_ctrl Pointer to the control data of FLAC module.
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
bool flac_decode(const flac_ctrl_t * const p_flac_ctrl);

/** Close the FLAC decoder
 *
 *  @param p_flac_ctrl Pointer to the control data of FLAC module.
 */
void flac_close(flac_ctrl_t * const p_flac_ctrl);

#endif /* DEC_FLAC_H */
