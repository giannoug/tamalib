#
# TamaLIB - A hardware agnostic first-gen Tamagotchi emulation library
#
# Host test harness: builds the same runner against the current sources and
# against a reference commit, then compares their state hashes.
#
#   make check ROM_DIR=/path/to/roms        compare with REF, run self-tests
#   make check ROM_DIR=... REF=<commit>     compare with another revision
#   make bench ROM_DIR=...                  speed 0 benchmark, best of 3
#   make warn                               -Wall -Wextra -Werror build
#
# ROM_DIR holds tama.bin (P1) and tamag2.bin (P2), plus optionally
# tamaang.bin and tamamot.bin; it must not be inside the repository.
# Missing ROMs are skipped. REF_ROMS/REF_SECS (environment) select what
# check.sh compares, LIB_DEFS passes defines to the library under test.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#

# Reference implementation to compare against (any git revision)
REF ?= ce304d5
# Directory holding tama.bin (P1) and tamag2.bin (P2), never committed
ROM_DIR ?= roms
# Seconds of emulated time for the benchmark
BENCH_SECS ?= 3600

CC ?= gcc
CFLAGS ?= -O2 -g
WARNFLAGS = -Wall -Wextra -Werror -Wno-unused-parameter
# Extra defines for the library under test
LIB_DEFS ?=

B = build
LIB_SRCS = cpu.c hw.c tamalib.c
LIB_HDRS = cpu.h hw.h hal.h tamalib.h
LIB_FILES = $(LIB_SRCS) $(LIB_HDRS)

CUR_DIR = $(B)/cur
REF_DIR = $(B)/ref-$(REF)

# The library under test is built with and without its optional features
CUR_BIN = $(B)/tamatest
NOLUT_BIN = $(B)/tamatest_nolut
REF_BIN = $(REF_DIR)/tamatest
CUR_DEFS = -DOP_LOOKUP_TABLE $(LIB_DEFS)

.PHONY: all check bench warn clean

# Do not leave empty reference sources behind if git show fails
.DELETE_ON_ERROR:

all: $(CUR_BIN) $(NOLUT_BIN) $(REF_BIN)

# Current sources (copied so that hal.h finds ../hal_types.h)
$(CUR_DIR)/tamalib/%: ../%
	@mkdir -p $(dir $@)
	cp $< $@

$(CUR_DIR)/hal_types.h: ../hal_types.h.template
	@mkdir -p $(dir $@)
	cp $< $@

# Reference sources, extracted from git
$(REF_DIR)/tamalib/%:
	@mkdir -p $(dir $@)
	git -C .. show $(REF):$* > $@

$(REF_DIR)/hal_types.h:
	@mkdir -p $(dir $@)
	git -C .. show $(REF):hal_types.h.template > $@

$(CUR_BIN): tamatest.c $(addprefix $(CUR_DIR)/tamalib/,$(LIB_FILES)) $(CUR_DIR)/hal_types.h
	$(CC) $(CFLAGS) $(CUR_DEFS) -I$(CUR_DIR) -o $@ tamatest.c $(addprefix $(CUR_DIR)/tamalib/,$(LIB_SRCS))

$(NOLUT_BIN): tamatest.c $(addprefix $(CUR_DIR)/tamalib/,$(LIB_FILES)) $(CUR_DIR)/hal_types.h
	$(CC) $(CFLAGS) $(LIB_DEFS) -I$(CUR_DIR) -o $@ tamatest.c $(addprefix $(CUR_DIR)/tamalib/,$(LIB_SRCS))

$(REF_BIN): tamatest.c $(addprefix $(REF_DIR)/tamalib/,$(LIB_FILES)) $(REF_DIR)/hal_types.h
	$(CC) $(CFLAGS) -DREF_BUILD -I$(REF_DIR) -o $@ tamatest.c $(addprefix $(REF_DIR)/tamalib/,$(LIB_SRCS))

# The library under test must build without warnings
warn: $(addprefix $(CUR_DIR)/tamalib/,$(LIB_FILES)) $(CUR_DIR)/hal_types.h
	for f in $(LIB_SRCS); do \
		$(CC) $(CFLAGS) $(CUR_DEFS) $(WARNFLAGS) -c -o /dev/null $(CUR_DIR)/tamalib/$$f || exit 1; \
		$(CC) $(CFLAGS) $(LIB_DEFS) $(WARNFLAGS) -c -o /dev/null $(CUR_DIR)/tamalib/$$f || exit 1; \
	done

check: all warn
	./check.sh $(CUR_BIN) $(REF_BIN) $(ROM_DIR)
	./check.sh $(NOLUT_BIN) $(REF_BIN) $(ROM_DIR)

# Best (shortest wall time) of BENCH_RUNS runs, each in its own process
BENCH_ROMS ?= tama tamag2
BENCH_RUNS ?= 3
# Benchmark the current library with the HALT fast-forward (ignored by the reference)
BENCH_ARGS ?= ff

bench: $(CUR_BIN) $(NOLUT_BIN) $(REF_BIN)
	@for rom in $(BENCH_ROMS); do \
		for bin in $(REF_BIN) $(NOLUT_BIN) $(CUR_BIN); do \
			for i in $$(seq $(BENCH_RUNS)); do \
				$$bin bench $(ROM_DIR)/$$rom.bin $(BENCH_SECS) $(BENCH_ARGS); \
			done | sort -t, -k3 -n | head -1 | sed "s|^|$$bin: |"; \
		done; \
	done

clean:
	rm -rf $(B)
