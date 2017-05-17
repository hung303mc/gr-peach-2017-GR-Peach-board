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

#include "key.h"
#include "key_cmd.h"
#include "system.h"

/*--- Macro definition of key thread ---*/
#define PROC_CYCLE_SW       (10u)   /* The process cycle of SW module */
#define PROC_CYCLE_TFT      (50u)   /* The process cycle of TFT module */
#define PROC_CYCLE_CMD      (2u)    /* The process cycle of command-line module */
#define PROC_CYCLE_REFRESH  (50u)   /* Refresh cycle of counter */
#define UNIT_TIME_MS        (2u)

#define PROC_CNT_SW         (PROC_CYCLE_SW / UNIT_TIME_MS)      /* Counter for 10ms period */
#define PROC_CNT_TFT        (PROC_CYCLE_TFT / UNIT_TIME_MS)     /* Counter for 50ms period */
#define PROC_CNT_CMD        (PROC_CYCLE_CMD / UNIT_TIME_MS)     /* Counter for 2ms period */
#define PROC_CNT_REFRESH    (PROC_CYCLE_REFRESH / UNIT_TIME_MS) /* Counter for 50ms period */

/*--- Macro definition of SW module ---*/
#define SW0_ACTIVE_LEVEL    (0)
#define SW0_DECISION_TIME   (50u)   /* Time until the decision of the input status. */
#define SW0_DECISION_CNT    (SW0_DECISION_TIME / PROC_CYCLE_SW) /* Counter for 50ms period */

/*--- User defined types ---*/
/* Control data of SW module */
typedef struct {
    uint32_t        sampling_count; /* Sampling count for decision of input. */
    bool            current_status; /* Current input status. true=push, false=release. */
} sw_ctrl_t;

/* Control data of TFT module */
typedef struct {
    uint32_t        dummy;
} tft_ctrl_t;

/* Control data of key thread */
typedef struct {
    sw_ctrl_t       sw_data;
    tft_ctrl_t      tft_data;
    cmd_ctrl_t      cmd_data;
} key_ctrl_t;

static void sw_init_proc(sw_ctrl_t * const p_ctrl);
static SYS_KeyCode sw_main_proc(sw_ctrl_t * const p_ctrl);
static void tft_init_proc(tft_ctrl_t * const p_ctrl);
static SYS_KeyCode tft_main_proc(tft_ctrl_t * const p_ctrl);

void key_thread(void const *argument)
{
    static key_ctrl_t   key_data;
    SYS_KeyCode         key_ev;
    SYS_KeyCode         tmp_ev;
    uint32_t            cnt = 0u;

    UNUSED_ARG(argument);
    
    /* Initializes the control data of key thread. */
    sw_init_proc(&key_data.sw_data);
    tft_init_proc(&key_data.tft_data);
    cmd_init_proc(&key_data.cmd_data);
    while(1) {
        key_ev = SYS_KEYCODE_NON;
        /* Is it a timing of the SW module processing? */
        if((cnt % PROC_CNT_SW) == 0u) {
            /* Executes main process of SW module. */
            tmp_ev = sw_main_proc(&key_data.sw_data);
            if(tmp_ev != SYS_KEYCODE_NON) {
                key_ev = tmp_ev;
            }
        }
        /* Is it a timing of TFT module processing? */
        if((cnt % PROC_CNT_TFT) == 0u) {
            /* Executes main process of TFT module. */
            tmp_ev = tft_main_proc(&key_data.tft_data);
            if(tmp_ev != SYS_KEYCODE_NON) {
                if(key_ev == SYS_KEYCODE_NON) {
                    /* There is no input from other modules. */
                    key_ev = tmp_ev;
                }
            }
        }
        /* Is it a timing of command-line module processing? */
        if((cnt % PROC_CNT_CMD) == 0u) {
            /* Executes main process of command-line module. */
            tmp_ev = cmd_main_proc(&key_data.cmd_data);
            if(tmp_ev != SYS_KEYCODE_NON) {
                if(key_ev == SYS_KEYCODE_NON) {
                    /* There is no input from other modules. */
                    key_ev = tmp_ev;
                }
            }
        }
        /* Is it a refresh timing of the counter? */
        if(cnt >= PROC_CNT_REFRESH) {
            cnt = 0u;
        }
        /* When the event occurs, this mail is sent to main thread. */
        if(key_ev != SYS_KEYCODE_NON) {
            (void) sys_notify_key_input(key_ev);
        }
        Thread::wait(UNIT_TIME_MS);
        cnt++;
    }
}

/** Initialises SW module
 *
 *  @param p_ctrl Pointer to the control data of SW module.
 */
static void sw_init_proc(sw_ctrl_t * const p_ctrl)
{
    if (p_ctrl != NULL) {
        p_ctrl->sampling_count = 0u;
        p_ctrl->current_status = false;
    }
}

/** Executes the main processing of SW module
 *
 *  @param p_ctrl Pointer to the control data of SW module.
 *
 *  @returns 
 *    Key code.
 */
static SYS_KeyCode sw_main_proc(sw_ctrl_t * const p_ctrl)
{
    SYS_KeyCode         key_ev = SYS_KEYCODE_NON;
    int32_t             pin_level;
    static DigitalIn    sw0(P6_0);

    if (p_ctrl != NULL) {
        pin_level = sw0.read();
        if (pin_level == SW0_ACTIVE_LEVEL) {
            /* SW0 is pushed. */
            if (p_ctrl->sampling_count < SW0_DECISION_CNT) {
                p_ctrl->sampling_count++;
                if (p_ctrl->sampling_count == SW0_DECISION_CNT) {
                    key_ev = SYS_KEYCODE_PLAYPAUSE;
                }
            }
            p_ctrl->current_status = true;
        } else {
            /* SW0 is released. */
            p_ctrl->sampling_count = 0u;
            p_ctrl->current_status = false;
        }
    }
    return key_ev;
}

/** Initialises TFT module
 *
 *  @param p_ctrl Pointer to the control data of TFT module.
 */
static void tft_init_proc(tft_ctrl_t * const p_ctrl)
{
    if (p_ctrl != NULL) {
        /* DO NOTHING */
    }
}

/** Executes the main processing of TFT module
 *
 *  @param p_ctrl Pointer to the control data of TFT module.
 *
 *  @returns 
 *    Key code.
 */
static SYS_KeyCode tft_main_proc(tft_ctrl_t * const p_ctrl)
{
    SYS_KeyCode     key_ev = SYS_KEYCODE_NON;

    if (p_ctrl != NULL) {
        /* DO NOTHING */
    }
    return key_ev;
}
