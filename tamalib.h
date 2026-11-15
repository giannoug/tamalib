/*
 * TamaLIB - A hardware agnostic first-gen Tamagotchi emulation library
 *
 * Copyright (C) 2021 Jean-Christophe Rona <jc@rona.fr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#ifndef _TAMALIB_H_
#define _TAMALIB_H_

#include "cpu.h"
#include "hw.h"
#include "hal.h"

#define tamalib_set_button(btn, state)			hw_set_button(btn, state)

#define tamalib_set_speed(speed)			cpu_set_speed(speed)

/* HALT fast-forward (speed 0 only, disabled by default and on reset)
 * NOTE: When enabled, one step of a halted CPU performs all the following HALT
 * steps that would only increment the tick counter, stopping at the first one
 * that makes a timer expire or that brings the tick counter to limit_tick (or
 * past it). The resulting state is exactly the one plain stepping would give.
 * limit_tick is compared with the tick counter modulo 2^32, so it must be less
 * than 2^31 ticks ahead: refresh it before each step or run, e.g. with
 * min(target, now + 0x7FFFFFFF) for a longer target.
 */
#define tamalib_set_halt_ff(en, limit_tick)		cpu_set_halt_ff(en, limit_tick)

#define tamalib_get_state()				cpu_get_state()
#define tamalib_refresh_hw()				cpu_refresh_hw()

#define tamalib_reset()					cpu_reset()

#define tamalib_add_bp(list, addr)			cpu_add_bp(list, addr)
#define tamalib_free_bp(list)				cpu_free_bp(list)

typedef enum {
	EXEC_MODE_PAUSE,
	EXEC_MODE_RUN,
	EXEC_MODE_STEP,
	EXEC_MODE_NEXT,
	EXEC_MODE_TO_CALL,
	EXEC_MODE_TO_RET,
} exec_mode_t;


void tamalib_release(void);
bool_t tamalib_init(const u12_t *program, breakpoint_t *breakpoints, u32_t freq);

void tamalib_set_framerate(u8_t framerate);
u8_t tamalib_get_framerate(void);

void tamalib_register_hal(hal_t *hal);

void tamalib_set_exec_mode(exec_mode_t mode);

/* NOTE: Only one of these two functions must be used in the main application
 * (tamalib_step() should be used only if tamalib_mainloop() does not fit the
 * main application execution flow).
 */
void tamalib_step(void);
void tamalib_mainloop(void);

#endif /* _TAMALIB_H_ */
