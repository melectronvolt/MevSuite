#!/bin/bash
set -eu
# Test Linux avec un serveur PipeWire privé, sans accès aux cartes son du poste.
# Argument : chemin du binaire mevsono_uitests.
: "${1:?chemin de mevsono_uitests}"
if [[ $1 != --session ]]; then
    runtime=$(mktemp -d)
    trap 'rm -rf "$runtime"' EXIT
    cat > "$runtime/bus.conf" <<'BUS'
<busconfig>
  <type>session</type><listen>unix:tmpdir=/tmp</listen>
  <policy context="default"><allow own="*"/><allow send_destination="*"/><allow receive_sender="*"/></policy>
</busconfig>
BUS
    export XDG_RUNTIME_DIR="$runtime" PIPEWIRE_RUNTIME_DIR="$runtime" PULSE_SERVER="unix:$runtime/pulse/native"
    export XDG_CONFIG_HOME="$runtime/config" XDG_STATE_HOME="$runtime/state" XDG_CACHE_HOME="$runtime/cache"
    unset PIPEWIRE_REMOTE
    dbus-run-session --config-file="$runtime/bus.conf" -- bash "$0" --session "$@"
    exit
fi
shift
pipewire >"$XDG_RUNTIME_DIR/pipewire.log" 2>&1 &
pw=$!
wireplumber --profile=policy >"$XDG_RUNTIME_DIR/wp.log" 2>&1 &
wp=$!
pipewire-pulse >"$XDG_RUNTIME_DIR/pulse.log" 2>&1 &
pulse=$!
trap 'kill "$pulse" "$wp" "$pw" 2>/dev/null || true; wait || true' EXIT
for i in {1..50}; do pactl info >/dev/null 2>&1 && break; sleep .1; done
pactl load-module module-null-sink sink_name=sono_tp sink_properties=device.description=MeVSonoOutput
pactl load-module module-remap-source master=sono_tp.monitor source_name=sono_mic source_properties=device.description=USB-Mic
pactl set-default-source sono_mic
pactl set-default-sink sono_tp
QT_QPA_PLATFORM=offscreen MEVSONO_AUDIO_VIRTUEL=1 "$1" enregistre_sur_micro_virtuel "${@:2}"
