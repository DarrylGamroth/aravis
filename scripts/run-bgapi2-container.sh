#!/usr/bin/env bash
set -euo pipefail

NETWORK_NAME="${NETWORK_NAME:-gige-test}"
NETWORK_SUBNET="${NETWORK_SUBNET:-10.10.0.0/24}"
NETWORK_GATEWAY="${NETWORK_GATEWAY:-10.10.0.1}"
CONTAINER_IP="${CONTAINER_IP:-10.10.0.2}"
SERIAL="${GVCP_SERIAL:-GVCP01}"
WIDTH="${GVCP_WIDTH:-640}"
HEIGHT="${GVCP_HEIGHT:-512}"
PIXEL_FORMAT="${GVCP_PIXEL_FORMAT:-Mono16}"
FRAME_COUNT="${GVCP_FRAME_COUNT:-10}"
ROOT="${ARAVIS_ROOT:-/home/dgamroth/workspaces/codex/aravis}"
IMAGE="${PODMAN_IMAGE:-docker.io/library/debian:stable-slim}"

FAKE_BIN="$ROOT/build/src/arv-fake-gv-camera-0.10"
GENICAM_XML="$ROOT/src/arv-fake-camera.xml"
CAPTURE_SCRIPT="$ROOT/scripts/gvcp-bgapi2-capture.jl"

if ! sudo podman network exists "$NETWORK_NAME"; then
  sudo podman network create --subnet "$NETWORK_SUBNET" --gateway "$NETWORK_GATEWAY" "$NETWORK_NAME"
fi

sudo podman run --rm --name gige-fake \
  --network "$NETWORK_NAME" --ip "$CONTAINER_IP" \
  --privileged \
  -v /:/host:ro \
  -w "/host$ROOT" \
  "$IMAGE" \
  /bin/bash -lc "export LD_LIBRARY_PATH=/host$ROOT/build/src:/host/lib/x86_64-linux-gnu:/host/usr/lib/x86_64-linux-gnu; \
    /host$FAKE_BIN -i $CONTAINER_IP -s $SERIAL -g /host$GENICAM_XML -d all" &

FAKE_PID=$!
sleep 1

BGAPI2_GENTL_PATH=/opt/baumer-gapi-sdk-c/lib/libbgapi2_gige.cti \
GVCP_SERIAL="$SERIAL" GVCP_WIDTH="$WIDTH" GVCP_HEIGHT="$HEIGHT" \
GVCP_PIXEL_FORMAT="$PIXEL_FORMAT" GVCP_FRAME_COUNT="$FRAME_COUNT" \
julia "$CAPTURE_SCRIPT"

wait "$FAKE_PID"
