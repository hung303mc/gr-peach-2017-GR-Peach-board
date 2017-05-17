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

#ifndef KEY_CMD_H
#define KEY_CMD_H

#include "r_typedefs.h"
#include "system.h"

/*--- Macro definition ---*/
#define CMD_INPUT_MAX_LEN   (32u)   /* Maximum length of the input from command-line. */
                                    /* (Including 'CR' character.) */

/*--- User defined types ---*/
/* Control data of command-line module */
typedef struct {
    char_t          inp_str[CMD_INPUT_MAX_LEN]; /* Input string from command-line. */
    uint32_t        inp_len;                    /* Length of input string. */
} cmd_ctrl_t;

/** Initialises command-line module
 *
 *  @param p_ctrl Pointer to the control data of command-line module.
 */
void cmd_init_proc(cmd_ctrl_t * const p_ctrl);

/** Executes the main processing of command-line module
 *
 *  @param p_ctrl Pointer to the control data of command-line module.
 *
 *  @returns 
 *    Key code.
 */
SYS_KeyCode cmd_main_proc(cmd_ctrl_t * const p_ctrl);

#endif /* KEY_CMD_H */
