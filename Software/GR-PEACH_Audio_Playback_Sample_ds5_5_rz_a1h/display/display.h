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

#ifndef DISPLAY_H
#define DISPLAY_H

#include "r_typedefs.h"
#include "system.h"

/*--- Macro definition ---*/
#define DSP_STACK_SIZE      (2048u)     /* Stack size of display thread */

/* The maximum length of display character string. */
#define DSP_DISP_STR_MAX_LEN        (64)
/* The maximum length of input character string by the command-line. */
#define DSP_CMD_INPT_STR_MAX_LEN    (63)

/* The baud rate of the serial port for PC communication. */
#define DSP_PC_COM_BAUDRATE         (9600)

/*--- User defined types ---*/
typedef enum {
    DSP_MAILID_DUMMY = 0,
    DSP_MAILID_CYCLE_IND,   /* Cyclic notice */
    DSP_MAILID_CMD_STR,     /* Notifies display thread of input string. */
    DSP_MAILID_PRINT_STR,   /* Notifies display thread of output string. */
    DSP_MAILID_PLAY_TIME,   /* Notifies display thread of playback time. */
    DSP_MAILID_PLAY_INFO,   /* Notifies display thread of playback information. */
    DSP_MAILID_PLAY_MODE,   /* Notifies display thread of repeat mode. */
    DSP_MAILID_FILE_NAME,   /* Notifies display thread of file name. */
    DSP_MAILID_HELP,        /* Requests display thread to display help message. */
    DSP_MAILID_NUM
} DSP_MAIL_ID;

/* These data are used in all display modules. */
typedef struct {
    uint32_t        disp_mode;      /* Display mode */
    SYS_PlayStat    play_stat;      /* Playback status */
    uint32_t        track_id;       /* Track number */
    uint32_t        play_time;      /* Playback time (sec) */
    uint32_t        total_time;     /* Total playback time (sec) */
    uint32_t        samp_freq;      /* Sampling frequency (Hz) */
    uint32_t        channel;        /* Channel number */
    bool            repeat_mode;    /* Repeat mode */
    char_t          file_name[DSP_DISP_STR_MAX_LEN];/* Character string of file name */
    char_t          dspl_str[DSP_DISP_STR_MAX_LEN]; /* Display character string */
} dsp_com_ctrl_t;

/* These data are used only in the terminal-output module. */
typedef struct {
    bool        edge_fin_inpt;      /* Completion status of the input by the command-line.*/
                                    /* [true = input completion, false = input now] */
    char_t      inpt_str[DSP_CMD_INPT_STR_MAX_LEN];/* Input character string by the command-line. */
} dsp_trm_ctrl_t;

/* These data are used only in the TFT module. */
typedef struct {
    int32_t         disp_phase_no;  /* The making phase of the display image */
} dsp_tft_ctrl_t;


/** Display Thread
 *
 *  @param argument Pointer to the thread function as start argument.
 */
void dsp_thread(void const *argument);

/** Notifies the display thread of the song information (file number, play time, total play time, play state).
 *
 *  @param play_stat Playback state
 *                     Stopped : SYS_PLAYSTAT_STOP
 *                     Playing : SYS_PLAYSTAT_PLAY
 *                     Paused : SYS_PLAYSTAT_PAUSE
 *  @param file_no File number
 *                   1 to 999
 *  @param play_time Playback time (in seconds)
 *                     0 to 359999
 *                     * 0 hour, 0 minute, 0 second to 99 hours, 59 minutes, 59 seconds
 *  @param total_time Total play time (in seconds)
 *                      0 to 359999
 *                      * 0 hour, 0 minute, 0 second to 99 hours, 59 minutes, 59 seconds
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool dsp_notify_play_time(const SYS_PlayStat play_stat, const uint32_t file_no, 
                            const uint32_t play_time, const uint32_t total_time);

/** Notifies the display thread of the song information (file number, sampling frequency, and number of channels).
 *
 *  @param file_no File number
 *                   1 to 999
 *  @param sample_freq Sampling frequency (Hz)
 *                       22050, 24000, 32000, 44100, 48000, 64000, 88200, 96000
 *  @param channel_num Number of channels
 *                       1, 2
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool dsp_notify_play_info(const uint32_t file_no, 
                    const uint32_t sample_freq, const uint32_t channel_num);

/** Notifies the display thread of the playback mode (repeat mode).
 *
 *  @param rep_mode Repeat mode
 *                    Repeat mode OFF : false
 *                    Repeat mode ON : true
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool dsp_notify_play_mode(const bool rep_mode);

/** Notifies the display thread of the file name.
 *
 *  @param p_str File name string
 *                 * The string must be terminated by '\0'.
 *                   The character code must be the local character code. Since the end of
 *                   a string is identified by the presence of '\0', a file name of 
 *                   multi-byte code may not be displayed correctly. 
 *                   The maximum length of the string that the display thread can notify
 *                   is 64 bytes including '\0'.
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     The argument p_str is set to NULL.
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool dsp_notify_file_name(const char_t * const p_str);

/** Notifies the display thread of the string to be output on the terminal.
 *
 *  @param p_str String to be output on the terminal
 *                 * The string must be terminated by '\0'.
 *                   The character code must be the local character code. Since the end of
 *                   a string is identified by the presence of '\0', a file name of
 *                   multi-byte code may not be displayed correctly. 
 *                   The maximum length of the string that the display thread can notify
 *                   is 64 bytes including '\0'.
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     The argument p_str is set to NULL.
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool dsp_notify_print_string(const char_t * const p_str);

/** Notifies the display thread of the command line input string.
 *  * Used to echo back the string entered from the command line. 
 *
 *  @param p_str Command line input string
 *                 * The string must contain no control characters and terminate with '\0'.
 *                   The character code must be the local character code. Since the end of
 *                   a string is identified by the presence of '\0', a file name of
 *                   multi-byte code may not be displayed correctly.
 *                   The maximum length of the string that the display thread can notify
 *                   is 63 bytes including '\0'.
 *
 *  @param flag_fin Input completion flag
 *                   Middle of input : false
 *                   Input complete : true
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     The argument p_str is set to NULL.
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool dsp_notify_input_string(const char_t * const p_str, const bool flag_fin);

/** Requests the display thread to display help message.
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool dsp_req_help(void);

#endif /* DISPLAY_H */
