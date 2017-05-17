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

#include "mbed.h"
#include "rtos.h"
#include "display.h"
#include "disp_term.h"

/*--- Macro definition ---*/
#define MOVE_CURSOR_TO_LEFT     "\x1b[128D" /* Moves a cursor to the left. */
#define CLEAR_CURSOR_RIGHT      "\x1b[0K"   /* Clears the right side of the cursor position. */
#define CLEAR_CURSOR_LINE       "\x1b[2K"   /* Clears a cursor line. */
#define CLEAR_ALL               "\x1b[2J"   /* Clears all screen. */
#define STR_CR                  "\r\n"

#define MSG_PROMPT               MOVE_CURSOR_TO_LEFT \
                                "T%ld %ld:%02ld:%02ld > "\
                                CLEAR_CURSOR_RIGHT
#define MSG_FILENAME_PREFIX     "File Name = "
#define MSG_FILE_TYPE           "File type      : FLAC"
#define MSG_SAMPLING_FREQ       "Sampling freq. : %ld Hz"
#define MSG_CHANNEL_PREFIX      "Channel        : "
#define MSG_CHANNEL_MONO        "Mono"
#define MSG_CHANNEL_STEREO      "Stereo"
#define MSG_REPEAT_PREFIX       "Repeat Mode = "
#define MSG_MODE_ON             "on"
#define MSG_MODE_OFF            "off"

#define HELP_CMD_NUM            (7u)

/* help information */
#define HELP_INFO_HELP          "help      : Show help information for commands."
#define HELP_INFO_NEXT          "next      : Select the next song."
#define HELP_INFO_PLAYINFO      "playinfo  : Show the song information."
#define HELP_INFO_PLAYPAUSE     "playpause : Control playback/pause."
#define HELP_INFO_PREV          "prev      : Select the previous song."
#define HELP_INFO_REPEAT        "repeat    : Turn on and off the repeat mode."
#define HELP_INFO_STOP          "stop      : Stop playback."

#define MIN_TO_SEC              (60u)
#define HOUR_TO_SEC             (3600u)

static Serial pc_out(USBTX, USBRX);

static void output_prompt(const dsp_com_ctrl_t * const p_com);
static void output_playinfo(const dsp_com_ctrl_t * const p_com);
static void output_help_info(void);
static void clear_current_line(void);
static void output_cr(void);
static void output_string(const char_t * const p_str);

void dsp_init_term(void)
{
    pc_out.baud(DSP_PC_COM_BAUDRATE);
    output_string(CLEAR_ALL);       /* Clears all screen. */
}

void dsp_output_term(const DSP_MAIL_ID mail_id, const dsp_com_ctrl_t * const p_com, 
                                            const dsp_trm_ctrl_t * const p_trm)
{
    bool            is_update;
    const char_t    *p_str;
    
    if ((p_com != NULL) && (p_trm != NULL)) {
        switch(mail_id) {
            case DSP_MAILID_CMD_STR:    /* Input character string by the command-line */
                /* Input character string is displayed after Command Prompt. */
                is_update = true;
                break;
            case DSP_MAILID_PRINT_STR:  /* Character string for the status indication */
                is_update = true;
                clear_current_line();
                output_string(&p_com->dspl_str[0]);
                output_cr();
                break;
            case DSP_MAILID_PLAY_TIME:  /* Playback time */
                /* Playback time is displayed in Command Prompt. */
                is_update = true;
                break;
            case DSP_MAILID_PLAY_INFO:  /* Music information */
                is_update = true;
                clear_current_line();
                output_playinfo(p_com);
                break;
            case DSP_MAILID_PLAY_MODE:  /* Repeat mode */
                is_update = true;
                clear_current_line();
                output_string(MSG_REPEAT_PREFIX);
                if (p_com->repeat_mode == true) {
                    p_str = MSG_MODE_ON;
                } else {
                    p_str = MSG_MODE_OFF;
                }
                output_string(p_str);
                output_cr();
                break;
            case DSP_MAILID_FILE_NAME:   /* File name */
                is_update = true;
                clear_current_line();
                output_string(MSG_FILENAME_PREFIX);
                output_string(&p_com->file_name[0]);
                output_cr();
                break;
            case DSP_MAILID_HELP:        /* Help information */
                is_update = true;
                clear_current_line();
                output_help_info();
                break;
            case DSP_MAILID_CYCLE_IND:  /* Cyclic notice */
            default:                    /* Unexpected cases : mail id was illegal. */
                is_update = false;
                break;
        }
        
        if (is_update == true) {
            /* Outputs the command prompt. */
            output_prompt(p_com);
            
            /* Outputs a input character string. */
            output_string(&p_trm->inpt_str[0]);
            
            if (p_trm->edge_fin_inpt == true) {
                /* Because command input was finished, I move a cursor. */
                /* And outputs the command prompt for next input. */
                output_cr();
                output_prompt(p_com);
            }
        }
    }
}

/** Prints the command prompt
 *
 *  @param p_com Pointer to common data in all module.
 */
static void output_prompt(const dsp_com_ctrl_t * const p_com)
{
    char_t          str_buf[DSP_DISP_STR_MAX_LEN];
    uint32_t        tim;
    uint32_t        hour;
    uint32_t        min;
    uint32_t        sec;

    if (p_com != NULL) {
        tim  = p_com->play_time;
        hour = tim / HOUR_TO_SEC;
        tim  = tim % HOUR_TO_SEC;
        min  = tim / MIN_TO_SEC;
        sec  = tim % MIN_TO_SEC;
        (void) sprintf(str_buf, MSG_PROMPT, p_com->track_id, hour, min, sec);
        output_string(str_buf);
    }
}

/** Prints the file information of the current playback music.
 *
 *  @param p_com Pointer to common data in all module.
 */
static void output_playinfo(const dsp_com_ctrl_t * const p_com)
{
    char_t          str_buf[DSP_DISP_STR_MAX_LEN];
    const char_t *  p_str;
    
    if (p_com != NULL) {
        /* Prints the file type */
        output_string(MSG_FILE_TYPE);
        output_cr();

        /* Prints the sampling frequency */
        (void) sprintf(str_buf, MSG_SAMPLING_FREQ, p_com->samp_freq);
        output_string(str_buf);
        output_cr();

        /* Prints the channel structure */
        output_string(MSG_CHANNEL_PREFIX);
        if (p_com->channel == 1u) {
            p_str = MSG_CHANNEL_MONO;
        } else {
            p_str = MSG_CHANNEL_STEREO;
        }
        output_string(p_str);
        output_cr();
    }
}

/** Prints the command help informations.
 *
 */
static void output_help_info(void)
{
    uint32_t i;
    struct {
        const char_t    *p_help_info;
    } static const info_list[HELP_CMD_NUM] = {
        {   HELP_INFO_HELP        },
        {   HELP_INFO_NEXT        },
        {   HELP_INFO_PLAYINFO    },
        {   HELP_INFO_PLAYPAUSE   },
        {   HELP_INFO_PREV        },
        {   HELP_INFO_REPEAT      },
        {   HELP_INFO_STOP        }
    };

    /* Prints the help information in alphabetical order. */
    for (i = 0u; i < HELP_CMD_NUM; i++) {
        output_string(info_list[i].p_help_info);
        output_cr();
    }
}

/** Clears a cursor line
 *
 */
static void clear_current_line(void)
{
    output_string(MOVE_CURSOR_TO_LEFT);     /* Moves a cursor to the left. */
    output_string(CLEAR_CURSOR_LINE);       /* Clears a cursor line. */
}

/** Prints the CARRIAGE RETURN
 *
 */
static void output_cr(void)
{
    output_string(STR_CR);
}

/** Prints the specified character string
 *
 *  @param p_str Pointer to the character string.
 */
static void output_string(const char_t * const p_str)
{
    if (p_str != NULL) {
        (void) pc_out.puts(p_str);
    }
}
