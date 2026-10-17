#!/bin/sh
# Runs tests/gui/run-all.ps1 in the Windows box's logged-on desktop against the RIDE installed
# there, waits for it and prints its summary. A window needs that desktop, and an ssh session has
# none, so a scheduled task with /IT starts it. The scripts go to %TEMP%\ride-gui-scripts and the
# results stay in %TEMP%\ride-gui on the box.
#   tests/gui/run-on-box.sh [host [run-all.ps1 arguments]]   (default: windows)
#   e.g. tests/gui/run-on-box.sh windows -Samples c-demo -Configs Release
BOX=${1:-windows}
[ $# -gt 0 ] && shift
ARGS="$*"
HERE=$(cd "$(dirname "$0")" && pwd)
TMPDIR_BOX=$(ssh -n "$BOX" 'echo %TEMP%' | tr -d '\r')
SCRIPTS="$TMPDIR_BOX\\ride-gui-scripts"
RESULTS="$TMPDIR_BOX\\ride-gui"

ssh -n "$BOX" "if not exist \"$SCRIPTS\" mkdir \"$SCRIPTS\"" || exit 2
scp -q "$HERE/ride-window.ps1" "$HERE/run-all.ps1" "$BOX:$(printf '%s' "$SCRIPTS" | tr '\\' '/')/" || exit 2
ssh -n "$BOX" "if exist \"$RESULTS\\done\" del \"$RESULTS\\done\""
ssh -n "$BOX" "schtasks /create /tn ride-window-test /f /sc once /st 23:59 /it /tr \"powershell -NoProfile -ExecutionPolicy Bypass -File $SCRIPTS\\run-all.ps1 $ARGS\" >nul && schtasks /run /tn ride-window-test >nul" || exit 2
printf '%s\n' "started on $BOX; results in $RESULTS"

# The whole round is a few minutes; an hour is the cap.
i=0
while [ $i -lt 360 ]; do
  sleep 10; i=$((i + 1))
  if ssh -n "$BOX" "if exist \"$RESULTS\\done\" (exit 0) else (exit 1)"; then
    ssh -n "$BOX" "if exist \"$RESULTS\\error.txt\" type \"$RESULTS\\error.txt\""
    ssh -n "$BOX" "if exist \"$RESULTS\\summary.txt\" type \"$RESULTS\\summary.txt\""
    ssh -n "$BOX" "schtasks /delete /tn ride-window-test /f >nul"
    FAILS=$(ssh -n "$BOX" "type \"$RESULTS\\done\"" | tr -d '\r ')
    [ "$FAILS" = 0 ]; exit $?
  fi
done
echo "no result after an hour"; exit 1
