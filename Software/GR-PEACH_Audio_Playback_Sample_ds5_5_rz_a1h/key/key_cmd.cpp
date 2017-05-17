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
#include "misratypes.h"

#include "key_cmd.h"
#include "system.h"
#include "display.h"

/*--- Macro definition ---*/
#define CHR_BS              '\b'        /* 0x08: BACKSPACE */
#define CHR_LF              '\n'        /* 0x0A: LINE FEED */
#define CHR_CR              '\r'        /* 0x0D: CARRIAGE RETURN */
#define CHR_SPACE           ' '         /* 0x20: SPACE */
#define PRINT_CHR_MIN       CHR_SPACE   /* Minimum numeric value of printable character. */
#define PRINT_CHR_MAX       '~'         /* Maximum numeric value of printable character. */

/* Command Name */
#define CMD_STOP            "STOP"      /* Stop */
#define CMD_PLAYPAUSE       "PLAYPAUSE" /* Play / Pause */
#define CMD_NEXT            "NEXT"      /* Next track */
#define CMD_PREV            "PREV"      /* Previous track */
#define CMD_PLAYINFO        "PLAYINFO"  /* Play info */
#define CMD_REPEAT          "REPEAT"    /* Repeat */
#define CMD_HELP            "HELP"      /* Help */

#define VALID_CMD_NUM       (7u)

#define MAX_CNT_OF_ARG      (1u)

#define MSG_UNKNOWN_CMD     "command not found"

/*--- User defined types ---*/
typedef struct {
    char_t          argv[MAX_CNT_OF_ARG][CMD_INPUT_MAX_LEN];
    uint32_t        argc;
} split_str_t;

static Serial pc_in(USBTX, USBRX);

static void clear_input_string(cmd_ctrl_t * const p);
static bool read_data(cmd_ctrl_t * const p);
static bool split_input_string(split_str_t * const p,
                        const char_t * const p_inp_str, const uint32_t inp_len);
static SYS_KeyCode parse_input_string(const split_str_t * const p);

void cmd_init_proc(cmd_ctrl_t * const p_ctrl)
{
    if (p_ctrl != NULL) {
        pc_in.baud(DSP_PC_COM_BAUDRATE);
        clear_input_string(p_ctrl);
    }
}

SYS_KeyCode cmd_main_proc(cmd_ctrl_t * const p_ctrl)
{
    SYS_KeyCode     key_ev = SYS_KEYCODE_NON;
    split_str_t     split;
    bool            result;
    
    if (p_ctrl != NULL) {
        result = read_data(p_ctrl);
        if (result == true) {
            /* Decided the input character string from command-line. */
            /* Splits the input character string in argument. */
            result = split_input_string(&split, p_ctrl->inp_str, p_ctrl->inp_len);
            if (result == true) {
                key_ev = parse_input_string(&split);
                if (key_ev == SYS_KEYCODE_NON) {
                    /* The input character string is unknown command. */
                    (void) dsp_notify_print_string(MSG_UNKNOWN_CMD);
                }
            }
            clear_input_string(p_ctrl);
        }
    }
    return key_ev;
}

/** Clears the data of the input character string
 *
 *  @param p Pointer to the control data of command-line module.
 */
static void clear_input_string(cmd_ctrl_t * const p)
{
    if (p != NULL) {
        p->inp_str[0] = '\0';
        p->inp_len = 0u;
    }
}

/** Reads the input character string from command-line
 *
 *  @param p Pointer to the control data of command-line module.
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool read_data(cmd_ctrl_t * const p)
{
    bool            ret = false;
    int32_t         c;

    if (p != NULL) {
        if (pc_in.readable() == true) {
            c = pc_in.getc();
            if ((PRINT_CHR_MIN <= c) && (c <= PRINT_CHR_MAX)) {
                /* The character of "c" variable can print.  */
                /* Checks the length except the null terminal character. */
                if (p->inp_len < ((sizeof(p->inp_str)/sizeof(p->inp_str[0])) - 1u)) {
                    p->inp_str[p->inp_len] = (char_t)c;
                    p->inp_len++;
                    p->inp_str[p->inp_len] = '\0';
                } else {
                    /* Because buffer is full, "c" variable is canceled. */
                }
            } else {
                /* The character of "c" variable can not print.  */
                if ((c == CHR_CR) || (c == CHR_LF) || (c == '\0')) {
                    /* Detected the end of the input from command-line. */
                    ret = true;
                } else if (c == CHR_BS) {
                    /* Deletes one character. */
                    if (p->inp_len > 0u) {
                        p->inp_len--;
                        p->inp_str[p->inp_len] = '\0';
                    }
                } else {
                    /* DO NOTHING */
                }
            }
            /* Sends the input character string to display thread. */
            (void) dsp_notify_input_string(p->inp_str, ret);
        }
    }
    return ret;
}

/** Splits the input character string in argument
 *
 *  @param p Pointer to the structure to store the argument data.
 *  @param p_inp_str Pointer to the input character string.
 *  @param inp_len Length of the input character string. 
 *                 (Excepting the null terminal character.)
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool split_input_string(split_str_t * const p,
                        const char_t * const p_inp_str, const uint32_t inp_len)
{
    bool            ret = false;
    uint32_t        arg_cnt;
    uint32_t        chr_cnt;
    uint32_t        i;
    
    if ((p != NULL) && (p_inp_str != NULL) && (inp_len > 0u)) {
        arg_cnt = 0u;
        chr_cnt = 0u;
        /* Checks the input character string. */
        /* (Including the null terminal character.) */
        for (i = 0u; i < (inp_len + 1u); i++) {
            /* Deletes the white space. */
            if (((int32_t)p_inp_str[i] == CHR_SPACE) || 
                ((int32_t)p_inp_str[i] == '\0')) {
                if (chr_cnt > 0u) {
                    /* Detects the white space after the string. */
                    arg_cnt++;
                    chr_cnt = 0u;
                }
            } else {
                /* Checks the length except the null terminal character. */
                if (chr_cnt < (sizeof(p->argv[0]) - 1u)) {
                    if (arg_cnt < (sizeof(p->argv)/sizeof(p->argv[0]))) {
                        p->argv[arg_cnt][chr_cnt] = p_inp_str[i];
                        p->argv[arg_cnt][chr_cnt + 1u] = '\0';
                    }
                    chr_cnt++;
                }
            }
        }
        if ((arg_cnt > 0u) && 
            (arg_cnt <= (sizeof(p->argv)/sizeof(p->argv[0])))) {
            p->argc = arg_cnt;
            ret = true;
        } else {
            p->argc = 0u;
        }
    }
    return ret;
}

/** Parses the input character string using the argument data
 *
 *  @param p Pointer to the structure of the argument data.
 *
 *  @returns 
 *    Key code.
 */
static SYS_KeyCode parse_input_string(const split_str_t * const p)
{
    SYS_KeyCode         key_ret = SYS_KEYCODE_NON;
    const char_t        *p_str;
    uint32_t            max_len;
    uint32_t            i;
    struct {
        const char_t    *p_cmd;
        SYS_KeyCode     key_ev;
    } static const cmd_list[VALID_CMD_NUM] = {
        {   CMD_STOP,       SYS_KEYCODE_STOP        },
        {   CMD_PLAYPAUSE,  SYS_KEYCODE_PLAYPAUSE   },
        {   CMD_NEXT,       SYS_KEYCODE_NEXT        },
        {   CMD_PREV,       SYS_KEYCODE_PREV        },
        {   CMD_PLAYINFO,   SYS_KEYCODE_PLAYINFO    },
        {   CMD_REPEAT,     SYS_KEYCODE_REPEAT      },
        {   CMD_HELP,       SYS_KEYCODE_HELP        }
    };

    if (p != NULL) {
        if (p->argc == MAX_CNT_OF_ARG) {
            p_str = p->argv[0];
            max_len = sizeof(p->argv[0])/sizeof(p->argv[0][0]);
            for (i = 0u; (i < VALID_CMD_NUM) && (key_ret == SYS_KEYCODE_NON); i++) {
                if (strncasecmp(cmd_list[i].p_cmd, p_str, max_len) == 0) {
                    key_ret = cmd_list[i].key_ev;
                }
            }
        }
    }
    return key_ret;
}
