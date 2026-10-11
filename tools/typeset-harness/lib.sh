# lib.sh — shared settings for the typeset harness scripts (sourced, not run).
# Every path can be overridden from the environment; see README.md.
H=${H:-${0:A:h}}
REPO=${REPO:-${H:h:h}}
PPS_APP=${PPS_APP:-$REPO/build/Qt_6_11_1_for_macOS_Debug/PinPointStudio.app/Contents/MacOS/PinPointStudio}
TYPESET_SCRATCH=${TYPESET_SCRATCH:-/mnt/swingdata/scratch/typeset-harness}
TYPESET_OUT=${TYPESET_OUT:-$TYPESET_SCRATCH/out}
TYPESET_SLOTS=${TYPESET_SLOTS:-$TYPESET_SCRATCH/slots}
# The fixture: the 4 July 2026 session (7 iron, 15 shots); shot 8 is the one the probes pick.
TYPESET_SESSION=${TYPESET_SESSION:-/mnt/swingdata/Mark-Liversedge/2026-07-04_Mark-Liversedge_Wrist_01}
TYPESET_SETTINGS=${TYPESET_SETTINGS:-$HOME/.config/PinPointStudio/PinPointStudio.ini}
SCREENS=$H/screens

# ensure_slot <name> — prints the slot directory, creating it on first use: a copy of the fixture
# session (without its diagnostics.json.* backups) and of the settings file, so a run never touches
# the real library or Mark's settings, and parallel agents never share state.
ensure_slot() {
  local W=$TYPESET_SLOTS/$1 S=${TYPESET_SESSION:t}
  if [[ ! -d $W/session/$S ]]; then
    echo "Creating slot $W (a copy of $S, once)…" >&2
    mkdir -p $W/session || return 1
    rsync -a --exclude 'diagnostics.json.*' --exclude '.DS_Store' $TYPESET_SESSION $W/session/ || return 1
  fi
  if [[ ! -f $W/cfg/PinPointStudio/PinPointStudio.ini ]]; then
    mkdir -p $W/cfg/PinPointStudio && cp $TYPESET_SETTINGS $W/cfg/PinPointStudio/ || return 1
  fi
  print -r -- $W
}

# screen_json <normal|tall|print|mark|/path.json> — the offscreen platform's screen configuration.
screen_json() {
  case $1 in
    normal|"") print -r -- $SCREENS/screen.json;;
    tall)      print -r -- $SCREENS/screen_tall.json;;
    print)     print -r -- $SCREENS/screen_print.json;;
    mark)      print -r -- $SCREENS/screen_mark.json;;
    *)         print -r -- $1;;
  esac
}

# load_profile <name|target> — sets TARGET, PANEL_KEY, FEED, TAB_PROP, VIEW_PROP, VIEWS, DETAIL from
# panels/<name>.env when it exists; otherwise treats the argument as a raw target
# (an objectName, or key:<stageKey>) with nothing fed and one view called "main".
load_profile() {
  TARGET="" PANEL_KEY="" FEED="" TAB_PROP="" VIEW_PROP="" VIEWS="main" DETAIL=""
  if [[ -f $H/panels/$1.env ]]; then source $H/panels/$1.env
  else TARGET=$1; [[ $1 == key:* ]] && PANEL_KEY=${1#key:}
  fi
}

# The lines worth showing from an app run: guard and probe messages and QML errors.
LOG_FILTER='GUARD|PROBE|TRY|\.qml:[0-9]+|TypeError|ReferenceError|Cannot'
