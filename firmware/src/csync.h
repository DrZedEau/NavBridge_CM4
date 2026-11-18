/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef CSYNC_H
#define CSYNC_H

#include <stdint.h>

/* TIM1 counts at SYSCLK: all widths below are in these ticks (20.83 ns). */
#define CSYNC_TICKS_PER_US 48u

typedef struct {
    uint8_t  enabled;        /* CM on: inputs and output in use */
    uint8_t  locked;         /* HSYNC at the expected line rate, CSYNC running */
    uint8_t  vsync_seen;     /* VSYNC edges in the last 100 ms */
    uint16_t period_ticks;   /* measured line period */
    uint16_t sync_ticks;     /* normal CSYNC pulse */
    uint16_t broad_ticks;    /* CSYNC pulse while VSYNC is active */
    uint32_t locks;
    uint32_t losses;
    /* Since the previous call: */
    uint8_t  filter;         /* TI1 input filter setting (0..15) */
    uint16_t vs_pos_min;     /* line position (TIM1 ticks) of the VSYNC edges */
    uint16_t vs_pos_max;
    uint32_t vs_now;         /* VSYNC changes applied to the line in progress */
    uint32_t vs_next;        /* ... from the next line */
    uint32_t vs_waited;      /* ... after waiting for the next line to start */
    uint16_t period_min;     /* measured line period spread */
    uint16_t period_max;
} csync_status_t;

void csync_init(void);         /* pins safe (CSYNC low), TIM1 stopped */
void csync_enable(int on);     /* follows the CM 3.3 V rail */
void csync_poll(void);         /* from the main loop, at least every millisecond */
void csync_get_status(csync_status_t *st);  /* also restarts the diagnostics window */
void csync_set_filter(uint32_t filter);     /* 0..15, see CSYNC_INPUT_FILTER */

#endif
