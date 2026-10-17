#!/bin/sh
# RIDE as installed (or $RIDE_BIN): nine programs, x86 through ours and the vendor's, C6000 emulated on vm6747,
# run as RIDE's Run does (R5: asm6x, lnk6x against RTS6x, the .out on sim6747), and linked through TI's lnk6x.
set -u
export MSYS_NO_PATHCONV=1
# Microsoft's link.exe ahead of Git bash's coreutils `link`.
export PATH="$(cygpath -u "$VCToolsInstallDir")bin/Hostx64/x64:$PATH"
RC=${RC_DIR:-/c/meas0923/rc}
rm -f $RC/progs/._*
BIN="${RIDE_BIN:-C:/Program Files/RIDE 5.17/bin}"
P=$RC/progs; W=$RC/w; rm -rf $W; mkdir -p $W; R=$RC/results.txt; : > $R; : > $R.detail
TI=C:/ti/ccsv7/tools/compiler/ti-cgt-c6000_8.2.2; TILIB=C:/Users/GRA/Documents/VM6747/tilib
tool() { case $1 in c) echo c90 C90;; cpp) echo cpp11 CPP11;; shl) echo shalimar SHALIMAR;; esac; }
norm() { tr -d '\r' < "$1" | md5sum | cut -c1-8; }
unsetall() { unset C90_AS C90_LD CPP11_AS CPP11_LD SHALIMAR_AS SHALIMAR_LD C90_TI C90_TILIB CPP11_TI CPP11_TILIB; }
while read name lang srcs; do
  set -- $(tool $lang); exe=$1; V=$2; d=$W/$name; mkdir -p $d
  files=""; for f in $srcs; do files="$files $(cygpath -w $P/$f)"; done
  masm=""; [ $lang = cpp ] && masm="-masm=masm"
  line="$name ($exe):"
  # 1. x86 ours: masm + LINK, as RIDE sets it
  unsetall; export ${V}_AS="$BIN/masm.exe" ${V}_LD="$BIN/link.exe"
  if (cd $d && "$BIN/$exe.exe" $masm $files -o ours.exe > ours.build 2>&1) && [ -f $d/ours.exe ]; then
     (cd $d && ./ours.exe < /dev/null > ours.out 2>&1); e1=$?; o1=$(norm $d/ours.out); line="$line x86-ours=$o1/$e1"
  else o1=FAIL; line="$line x86-ours=BUILD-FAIL"; echo "  $name ours: $(tail -2 $d/ours.build | tr -d '\r')" >> $R.detail; fi
  # 1b. x86 masm with Microsoft's link.exe: which half, if the two disagree
  unsetall; export ${V}_AS="$BIN/masm.exe"
  if (cd $d && "$BIN/$exe.exe" $masm $files -o mix.exe > mix.build 2>&1) && [ -f $d/mix.exe ]; then
     (cd $d && ./mix.exe < /dev/null > mix.out 2>&1); e4=$?; o4=$(norm $d/mix.out); line="$line masm+link.exe=$o4/$e4"
  else o4=FAIL; line="$line masm+link.exe=BUILD-FAIL"; echo "  $name mix: $(tail -2 $d/mix.build | tr -d '\r')" >> $R.detail; fi
  # 2. x86 vendor: ml64 or clang, and link.exe - no masm, no LINK
  unsetall
  if (cd $d && "$BIN/$exe.exe" $files -o vend.exe > vend.build 2>&1) && [ -f $d/vend.exe ]; then
     (cd $d && ./vend.exe < /dev/null > vend.out 2>&1); e2=$?; o2=$(norm $d/vend.out); line="$line x86-vendor=$o2/$e2"
  else o2=FAIL; line="$line x86-vendor=BUILD-FAIL"; echo "  $name vendor: $(tail -2 $d/vend.build | tr -d '\r')" >> $R.detail; fi
  # 3. C6000 on vm6747, as Build > Emulate on vm6747 does: each source to assembly, then the emulator
  unsetall; ss=""; ok=1
  if [ $lang = shl ]; then targ="--target=tms6747"; else targ="-arch tms6747"; fi
  for f in $srcs; do case $f in *.h) continue;; esac; b=$(basename $f); b=${b%.*}
    (cd $d && "$BIN/$exe.exe" -S $targ $(cygpath -w $P/$f) -o c6-$b.s > c6-$b.build 2>&1) || { ok=0; echo "  $name c6 -S $f: $(tail -2 $d/c6-$b.build | tr -d '\r')" >> $R.detail; }
    ss="$ss c6-$b.s"; done
  rt="$BIN/lib/shmrt-tms6747"
  if [ $ok = 1 ]; then if [ $lang = shl ]; then (cd $d && timeout 120 "$BIN/vm6747.exe" $ss "$rt" < /dev/null > c6.out 2>&1); else (cd $d && timeout 120 "$BIN/vm6747.exe" $ss < /dev/null > c6.out 2>&1); fi; e3=$?; o3=$(norm $d/c6.out); line="$line c6000-vm=$o3/$e3"
  else o3=FAIL; line="$line c6000-vm=S-FAIL"; fi
  # 3b. C6000 as RIDE's Run does (R5): the console builds a project of it, links the .out against RTS6x
  # and runs it on sim6747; the program's output is what comes after the "$ sim6747" line.
  r=$d/r5; rm -rf $r; mkdir -p $r; g=""; cp $P/*.h $r/ 2>/dev/null
  for f in $srcs; do cp $P/$f $r/; case $f in *.h) ;; *) g="$g\"$f\",";; esac; done
  printf '{ "arch": "tms6747", "groups": { "Sources": [%s] } }\n' "${g%,}" > $r/project.pro
  (cd $r && timeout 300 "$BIN/RIDEConsole.exe" "$(cygpath -w $r)" --run --config release < /dev/null > run.log 2>&1); e5=$?
  if grep -q '^\$ sim6747 --run' $r/run.log; then
    sed -n '/^\$ sim6747 --run/,/^\[program returned/p' $r/run.log | sed '1d;$d' | tr -d '\r' | grep -v '^CYCLES ' > $d/c6sim.out
    o5=$(norm $d/c6sim.out); line="$line c6000-run=$o5/$e5"
  else o5=FAIL; line="$line c6000-run=FAIL"; echo "  $name run: $(tail -3 $r/run.log | tr -d '\r')" >> $R.detail; fi
  # 4/5. C6000 linked: ASM6x with LNK6x (ours) and with TI's lnk6x (vendor) - c90 and cpp11
  if [ $lang != shl ]; then
    unsetall; export ${V}_AS="$BIN/asm6x.exe" ${V}_TI=$TI ${V}_TILIB=$TILIB ${V}_LD="$BIN/lnk6x.exe"
    (cd $d && "$BIN/$exe.exe" -arch tms6747 $files -o ours.out6 > ours6.build 2>&1) && [ -f $d/ours.out6 ] && l4="ok($(stat -c %s $d/ours.out6))" || { l4=FAIL; echo "  $name lnk6x ours: $(tail -2 $d/ours6.build | tr -d '\r')" >> $R.detail; }
    unset ${V}_LD
    (cd $d && "$BIN/$exe.exe" -arch tms6747 $files -o vend.out6 > vend6.build 2>&1) && [ -f $d/vend.out6 ] && l5="ok($(stat -c %s $d/vend.out6))" || { l5=FAIL; echo "  $name lnk6x TI: $(tail -2 $d/vend6.build | tr -d '\r')" >> $R.detail; }
    line="$line c6000-link ours=$l4 TI=$l5"
  fi
  # agreement: all run outputs the same, and against the recorded expectation where there is one
  agree=yes; for o in $o2 $o3 $o4 $o5; do [ "$o" = "$o1" ] || agree=NO; done
  exp=""; [ -f $P/$name.expected ] && exp=" expected=$(norm $P/$name.expected)"
  echo "$line$exp agree=$agree" | tee -a $R
done < $P/list.txt
echo "=== details" | tee -a $R; cat $R.detail 2>/dev/null | tee -a $R; echo "=== done" | tee -a $R
