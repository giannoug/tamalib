#!/bin/sh
#
# TamaLIB - A hardware agnostic first-gen Tamagotchi emulation library
#
# Compares the library under test against the reference build, then runs
# the self-tests. Usage: check.sh <tamatest> <reference tamatest> <rom dir>
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

CUR=$1
REF=$2
ROM_DIR=$3
# ROMs compared against the reference (P1 and P2 by default)
REF_ROMS=${REF_ROMS:-"tama tamag2"}
# Emulated durations, in seconds
REF_SECS=${REF_SECS:-"60 3600"}

fail=0

for rom in $REF_ROMS; do
	file=$ROM_DIR/$rom.bin
	if [ ! -f "$file" ]; then
		echo "SKIP ref $rom ($file absent)"
		continue
	fi

	for secs in $REF_SECS; do
		for btn in 0 1; do
			want=$("$REF" hash "$file" "$secs" "$btn")
			got=$("$CUR" hash "$file" "$secs" "$btn")
			case "$got" in
				"ticks="*" hash="*) ;;
				*)
					echo "FAIL ref $rom ${secs}s btn=$btn: no result ($got)"
					fail=1
					continue
					;;
			esac
			if [ "$want" = "$got" ]; then
				echo "PASS ref $rom ${secs}s btn=$btn ($got)"
			else
				echo "FAIL ref $rom ${secs}s btn=$btn: want $want, got $got"
				fail=1
			fi
		done
	done
done

for t in $("$CUR" list); do
	"$CUR" selftest "$t" "$ROM_DIR"
	res=$?
	if [ $res -ne 0 ] && [ $res -ne 77 ]; then
		fail=1
	fi
done

if [ $fail -ne 0 ]; then
	echo "FAILED"
	exit 1
fi

echo "ALL OK"
