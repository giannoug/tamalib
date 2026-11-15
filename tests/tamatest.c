/*
 * TamaLIB - A hardware agnostic first-gen Tamagotchi emulation library
 *
 * Host test harness: runs ROMs headless with a fake HAL and a fake clock.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "tamalib/tamalib.h"

#define TICK_HZ					32768
#define PROGRAM_SIZE				8192

#define SKIP					77

/* Fake HAL */

static timestamp_t fake_now = 0;
static u32_t get_ts_calls = 0;
static uint64_t trace_hash;
static u8_t lcd[LCD_HEIGHT][LCD_WIDTH];

static uint64_t fnv(uint64_t h, uint32_t v)
{
	int i;

	for (i = 0; i < 4; i++) {
		h ^= (v >> (i * 8)) & 0xFF;
		h *= 0x100000001b3ULL;
	}

	return h;
}

#define FNV_INIT				0xcbf29ce484222325ULL

static void trace(uint32_t tag, uint32_t a, uint32_t b)
{
	trace_hash = fnv(fnv(fnv(trace_hash, tag), a), b);
}

static void * hal_malloc(u32_t size) { return malloc(size); }
static void hal_free(void *ptr) { free(ptr); }
static void hal_halt(void) {}
static bool_t hal_is_log_enabled(log_level_t level) { return 0; }
static void hal_log(log_level_t level, char *buff, ...) {}
static void hal_update_screen(void) {}
static int hal_handler(void) { return 0; }

static void hal_sleep_until(timestamp_t ts)
{
	/* The fake clock jumps to the deadline */
	if ((int32_t) (ts - fake_now) > 0) {
		fake_now = ts;
	}
}

static timestamp_t hal_get_timestamp(void)
{
	get_ts_calls++;
	return fake_now;
}

static void hal_set_lcd_matrix(u8_t x, u8_t y, bool_t val)
{
	lcd[y][x] = val;
	trace(1, (x << 8) | y, val);
}

static void hal_set_lcd_icon(u8_t icon, bool_t val)
{
	trace(2, icon, val);
}

static void hal_set_frequency(u32_t freq)
{
	trace(3, freq, 0);
}

static void hal_play_frequency(bool_t en)
{
	trace(4, en, 0);
}

static hal_t hal = {
	.malloc = &hal_malloc,
	.free = &hal_free,
	.halt = &hal_halt,
	.is_log_enabled = &hal_is_log_enabled,
	.log = &hal_log,
	.sleep_until = &hal_sleep_until,
	.get_timestamp = &hal_get_timestamp,
	.update_screen = &hal_update_screen,
	.set_lcd_matrix = &hal_set_lcd_matrix,
	.set_lcd_icon = &hal_set_lcd_icon,
	.set_frequency = &hal_set_frequency,
	.play_frequency = &hal_play_frequency,
	.handler = &hal_handler,
};

/* ROM loading */

static u12_t program[PROGRAM_SIZE];
static const char *rom_dir = "roms";

/* Returns 0 on success, -1 if the ROM is absent or invalid */
static int load_rom_file(const char *path)
{
	FILE *f;
	unsigned char buf[2];
	long size;
	int i = 0;

	memset(program, 0, sizeof(program));

	f = fopen(path, "rb");
	if (f == NULL) {
		return -1;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size != 12288 && size != 16384) {
		fclose(f);
		return -1;
	}

	while (i < PROGRAM_SIZE && fread(buf, 1, 2, f) == 2) {
		program[i++] = ((buf[0] << 8) | buf[1]) & 0xFFF;
	}

	fclose(f);
	return 0;
}

static int load_rom(const char *name)
{
	char path[1024];

	snprintf(path, sizeof(path), "%s/%s.bin", rom_dir, name);
	return load_rom_file(path);
}

/* Synthetic program (no ROM needed): enables all clock timer interrupts, then
 * HALTs. Each interrupt runs an ISR that clears the factor flags, increments
 * M(0x00), optionally toggles the CPU clock between OSC1 and OSC3, then
 * returns to the HALT.
 */
#define SYNTH_ISR_ADDR				0x102

static void synth_halt_isr(u12_t *prog, bool_t osc3)
{
	memset(prog, 0, PROGRAM_SIZE * sizeof(u12_t));

	prog[0x100] = 0x020; // JP   #0x20
	prog[0x102] = 0x040; // JP   #0x40 (clock timer vector)

	prog[0x120] = 0xE0F; // LD   A #0xF
	prog[0x121] = 0xE80; // LD   XP A
	prog[0x122] = 0xB10; // LD   X #0x10 (X = 0xF10)
	prog[0x123] = 0xE2F; // LD   MX #0xF (all clock timer interrupts)
	prog[0x124] = 0xF48; // EI
	prog[0x125] = 0xFF8; // HALT
	prog[0x126] = 0x025; // JP   #0x25

	prog[0x140] = 0xB00; // LD   X #0x00 (X = 0xF00)
	prog[0x141] = 0xEC2; // LD   A MX (clear factor flags)
	prog[0x142] = 0xF60; // INC  M(#0x00)
	if (osc3) {
		prog[0x143] = 0xB70; // LD   X #0x70 (X = 0xF70)
		prog[0x144] = 0xEC2; // LD   A MX
		prog[0x145] = 0xD08; // XOR  A #0x8 (toggle OSC1/OSC3)
		prog[0x146] = 0xEC8; // LD   MX A
	} else {
		prog[0x143] = 0xFFB; // NOP5
		prog[0x144] = 0xFFB; // NOP5
		prog[0x145] = 0xFFB; // NOP5
		prog[0x146] = 0xFFB; // NOP5
	}
	prog[0x147] = 0xF48; // EI
	prog[0x148] = 0xFDF; // RET
}

/* Synthetic program copying the K10-K13 input port to M(0x010) in a loop */
static void synth_k1_read(u12_t *prog)
{
	memset(prog, 0, PROGRAM_SIZE * sizeof(u12_t));

	prog[0x100] = 0x020; // JP   #0x20

	prog[0x120] = 0xE0F; // LD   A #0xF
	prog[0x121] = 0xE80; // LD   XP A
	prog[0x122] = 0xB42; // LD   X #0x42 (X = 0xF42)
	prog[0x123] = 0x810; // LD   Y #0x10 (Y = 0x010)
	prog[0x124] = 0xEC2; // LD   A MX
	prog[0x125] = 0xECC; // LD   MY A
	prog[0x126] = 0x024; // JP   #0x24
}

/* Emulation helpers */

static state_t *st;

static void emu_init(u32_t ts_freq, u8_t speed)
{
	fake_now = 0;
	trace_hash = FNV_INIT;
	memset(lcd, 0, sizeof(lcd));

	tamalib_register_hal(&hal);
	tamalib_init(program, NULL, ts_freq);
	tamalib_set_speed(speed);
	tamalib_set_exec_mode(EXEC_MODE_RUN);
	st = tamalib_get_state();
}

/* Wrap-safe "tick counter has reached target" */
static int tick_reached(u32_t target)
{
	return (int32_t) (*st->tick_counter - target) >= 0;
}

/* Button schedule: press button k%3 at (3k+1) s, release 0.25 s later */
typedef struct {
	u32_t next;
	u32_t k;
	u8_t pressed;
} btn_sched_t;

static void btn_sched_init(btn_sched_t *s, u32_t start)
{
	s->k = 0;
	s->pressed = 0;
	s->next = start + TICK_HZ;
}

static void btn_sched_apply(btn_sched_t *s)
{
	while (tick_reached(s->next)) {
		if (!s->pressed) {
			tamalib_set_button((button_t) (s->k % 3), BTN_STATE_PRESSED);
			s->pressed = 1;
			s->next += TICK_HZ / 4;
		} else {
			tamalib_set_button((button_t) (s->k % 3), BTN_STATE_RELEASED);
			s->pressed = 0;
			s->next += 3 * TICK_HZ - TICK_HZ / 4;
			s->k++;
		}
	}
}

/* Runs until the tick counter reaches target, returns the number of steps */
static u32_t run_until(u32_t target, btn_sched_t *btn)
{
	u32_t steps = 0;

	while (!tick_reached(target)) {
		if (btn != NULL) {
			btn_sched_apply(btn);
		}
		tamalib_step();
		steps++;
	}

	return steps;
}

/* Hash of every original state_t field, the memory and the HAL call trace */
static uint64_t state_hash(int with_trace)
{
	uint64_t h = FNV_INIT;
	int i;

	h = fnv(h, *st->pc);
	h = fnv(h, *st->x);
	h = fnv(h, *st->y);
	h = fnv(h, *st->a);
	h = fnv(h, *st->b);
	h = fnv(h, *st->np);
	h = fnv(h, *st->sp);
	h = fnv(h, *st->flags);
	h = fnv(h, *st->tick_counter);
	h = fnv(h, *st->clk_timer_2hz_timestamp);
	h = fnv(h, *st->clk_timer_4hz_timestamp);
	h = fnv(h, *st->clk_timer_8hz_timestamp);
	h = fnv(h, *st->clk_timer_16hz_timestamp);
	h = fnv(h, *st->clk_timer_32hz_timestamp);
	h = fnv(h, *st->clk_timer_64hz_timestamp);
	h = fnv(h, *st->clk_timer_128hz_timestamp);
	h = fnv(h, *st->clk_timer_256hz_timestamp);
	h = fnv(h, *st->prog_timer_timestamp);
	h = fnv(h, *st->prog_timer_enabled);
	h = fnv(h, *st->prog_timer_data);
	h = fnv(h, *st->prog_timer_rld);
	h = fnv(h, *st->call_depth);
	for (i = 0; i < INT_SLOT_NUM; i++) {
		h = fnv(h, st->interrupts[i].factor_flag_reg);
		h = fnv(h, st->interrupts[i].mask_reg);
		h = fnv(h, st->interrupts[i].triggered);
		h = fnv(h, st->interrupts[i].vector);
	}
	h = fnv(h, *st->cpu_halted);
	for (i = 0; i < MEM_BUFFER_SIZE; i++) {
		h = fnv(h, st->memory[i]);
	}

	if (with_trace) {
		h = fnv(h, (u32_t) trace_hash);
		h = fnv(h, (u32_t) (trace_hash >> 32));
	}

	return h;
}

static double wall_now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Commands */

static int cmd_hash(const char *rom, u32_t secs, int btn)
{
	btn_sched_t sched;

	if (load_rom_file(rom)) {
		printf("SKIP %s absent\n", rom);
		return SKIP;
	}

	emu_init(TICK_HZ, 0);
	btn_sched_init(&sched, 0);
	run_until(secs * TICK_HZ, btn ? &sched : NULL);
	printf("ticks=%u hash=%016llx\n", *st->tick_counter, (unsigned long long) state_hash(1));

	return 0;
}

static int cmd_bench(const char *rom, u32_t secs)
{
	const char *name;
	double t0, wall;
	u32_t steps;

	if (load_rom_file(rom)) {
		printf("SKIP %s absent\n", rom);
		return SKIP;
	}

	name = strrchr(rom, '/');
	name = (name != NULL) ? name + 1 : rom;

	/* One run per process: the reference cannot be initialised twice */
	emu_init(TICK_HZ, 0);
	get_ts_calls = 0;
	t0 = wall_now();
	steps = run_until(secs * TICK_HZ, NULL);
	wall = wall_now() - t0;

	printf("%-12s %u s emulated, %10u steps, %6.3f s wall, %6.2f M steps/s, x%6.0f real time, %10u get_timestamp calls\n",
		name, secs, steps, wall, steps / wall / 1e6, secs / wall, get_ts_calls);

	return 0;
}

/* Self-tests: return 0 on pass, 1 on fail, SKIP if a ROM is missing */

static int st_boot_draws(void)
{
	int x, y, on = 0;

	if (load_rom("tama")) {
		return SKIP;
	}

	emu_init(TICK_HZ, 0);
	run_until(10 * TICK_HZ, NULL);

	for (y = 0; y < LCD_HEIGHT; y++) {
		for (x = 0; x < LCD_WIDTH; x++) {
			on += lcd[y][x];
		}
	}

	if (on == 0) {
		printf("  P1 drew nothing in 10 s\n");
		return 1;
	}

	return 0;
}

/* A halted CPU must service pending interrupts: the 32 Hz clock timer
 * interrupt (the 1/2/8 Hz ones coincide with it) fires 32 times per second.
 */
static int st_halt_wakes(void)
{
	u32_t isr = 0;

	synth_halt_isr(program, 0);
	emu_init(TICK_HZ, 0);

	while (!tick_reached(TICK_HZ)) {
		tamalib_step();
		if (*st->pc == SYNTH_ISR_ADDR) {
			isr++;
		}
	}

	if (isr != 32) {
		printf("  %u interrupts serviced in 1 s, want 32\n", isr);
		return 1;
	}

	return 0;
}

/* Loads a ROM by name, or the synthetic program for "synth"/"synth_osc3" */
static int load_program(const char *name)
{
	if (!strcmp(name, "synth")) {
		synth_halt_isr(program, 0);
		return 0;
	} else if (!strcmp(name, "synth_osc3")) {
		synth_halt_isr(program, 1);
		return 0;
	}

	return load_rom(name);
}

/* A second tamalib_init() in the same process must behave like the first */
static int init_twice(const char *name)
{
	uint64_t h1, h2;

	if (load_program(name)) {
		return SKIP;
	}

	emu_init(TICK_HZ, 0);
	run_until(60 * TICK_HZ, NULL);
	h1 = state_hash(1);

	tamalib_release();

	emu_init(TICK_HZ, 0);
	run_until(60 * TICK_HZ, NULL);
	h2 = state_hash(1);

	if (h1 != h2) {
		printf("  %s: second init gives %016llx, first %016llx (tick %u, halted %u)\n", name,
			(unsigned long long) h2, (unsigned long long) h1, *st->tick_counter, *st->cpu_halted);
		return 1;
	}

	return 0;
}

static int st_init_twice_tama(void) { return init_twice("tama"); }
static int st_init_twice_tamaang(void) { return init_twice("tamaang"); }
static int st_init_twice_tamamot(void) { return init_twice("tamamot"); }
static int st_init_twice_synth(void) { return init_twice("synth"); }

/* tamalib_reset() mid-run must behave like a fresh init (input pins being
 * external, released buttons must still read as released)
 */
static int reset_like_init(const char *name)
{
	btn_sched_t sched;
	uint64_t h1, h2;

	if (load_program(name)) {
		return SKIP;
	}

	emu_init(TICK_HZ, 0);
	run_until(60 * TICK_HZ, NULL);
	h1 = state_hash(1);

	tamalib_release();

	emu_init(TICK_HZ, 0);
	btn_sched_init(&sched, 0);
	run_until(30 * TICK_HZ, &sched);
	tamalib_set_button(BTN_LEFT, BTN_STATE_RELEASED);
	tamalib_set_button(BTN_MIDDLE, BTN_STATE_RELEASED);
	tamalib_set_button(BTN_RIGHT, BTN_STATE_RELEASED);
	tamalib_reset();
	trace_hash = FNV_INIT;
	run_until(60 * TICK_HZ, NULL);
	h2 = state_hash(1);

	if (h1 != h2) {
		printf("  %s: reset gives %016llx, fresh init %016llx\n", name,
			(unsigned long long) h2, (unsigned long long) h1);
		return 1;
	}

	return 0;
}

static int st_reset_like_init_tama(void) { return reset_like_init("tama"); }
static int st_reset_like_init_synth(void) { return reset_like_init("synth"); }

/* The K10-K13 input port must read back the K10-K13 pin levels */
static int st_k1_port_reads_pins(void)
{
	u32_t i;

	synth_k1_read(program);
	emu_init(TICK_HZ, 0);

	cpu_set_input_pin(PIN_K10, PIN_STATE_HIGH);
	cpu_set_input_pin(PIN_K11, PIN_STATE_LOW);
	cpu_set_input_pin(PIN_K12, PIN_STATE_HIGH);
	cpu_set_input_pin(PIN_K13, PIN_STATE_HIGH);
	for (i = 0; i < 100; i++) {
		tamalib_step();
	}

	if (GET_RAM_MEMORY(st->memory, 0x010) != 0xD) {
		printf("  K10-K13 port reads 0x%X, want 0xD\n", GET_RAM_MEMORY(st->memory, 0x010));
		return 1;
	}

	return 0;
}

static const struct {
	const char *name;
	int (*fn)(void);
} selftests[] = {
	{"k1_port_reads_pins", &st_k1_port_reads_pins},
	{"init_twice_tama", &st_init_twice_tama},
	{"init_twice_tamaang", &st_init_twice_tamaang},
	{"init_twice_tamamot", &st_init_twice_tamamot},
	{"init_twice_synth", &st_init_twice_synth},
	{"reset_like_init_tama", &st_reset_like_init_tama},
	{"reset_like_init_synth", &st_reset_like_init_synth},
	{"boot_draws", &st_boot_draws},
	{"halt_wakes", &st_halt_wakes},
	{NULL, NULL},
};

static int cmd_selftest(const char *name)
{
	int i, res;

	for (i = 0; selftests[i].name != NULL; i++) {
		if (!strcmp(selftests[i].name, name)) {
			res = selftests[i].fn();
			printf("%s %s\n", (res == 0) ? "PASS" : (res == SKIP) ? "SKIP" : "FAIL", name);
			return res;
		}
	}

	printf("unknown self-test %s\n", name);
	return 2;
}

static void usage(void)
{
	fprintf(stderr,
		"usage: tamatest hash <rom> <secs> <btn 0|1>\n"
		"       tamatest bench <rom> <secs>\n"
		"       tamatest selftest <name> <rom dir>\n"
		"       tamatest list\n");
}

int main(int argc, char **argv)
{
	int i;

	if (argc >= 5 && !strcmp(argv[1], "hash")) {
		return cmd_hash(argv[2], atoi(argv[3]), atoi(argv[4]));
	} else if (argc >= 4 && !strcmp(argv[1], "bench")) {
		return cmd_bench(argv[2], atoi(argv[3]));
	} else if (argc >= 4 && !strcmp(argv[1], "selftest")) {
		rom_dir = argv[3];
		return cmd_selftest(argv[2]);
	} else if (argc >= 2 && !strcmp(argv[1], "list")) {
		for (i = 0; selftests[i].name != NULL; i++) {
			printf("%s\n", selftests[i].name);
		}
		return 0;
	}

	usage();
	return 2;
}
