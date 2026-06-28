#!/usr/bin/env bash
# dis.sh <symbol-substring> [maxlines] — disassemble first matching symbol (Intel)
B="$(dirname "$0")/iD.x86_64"; [ -f "$B" ] || B=/tmp/claude-1000/-home-pascal-Dokumente-Repositorys-MixiD/1fe76c2e-8cdf-4cb9-a5a8-bf4f7b65749a/scratchpad/iD.x86_64
sym=$(llvm-nm "$B" 2>/dev/null | grep -E " T | t " | awk '{print $3}' | grep -F "$1" | head -1)
[ -z "$sym" ] && { echo "no symbol matching: $1"; exit 1; }
echo "## $sym"
llvm-objdump --x86-asm-syntax=intel --no-show-raw-insn -d --disassemble-symbols="$sym" "$B" 2>/dev/null | tail -n +6 | head -"${2:-60}"
