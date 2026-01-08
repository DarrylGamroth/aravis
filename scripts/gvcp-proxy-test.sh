#!/usr/bin/env bash
set -euo pipefail

INTERFACE="${INTERFACE:-CHANGE_ME}"
CAPTURE_IFACE="${CAPTURE_IFACE:-$INTERFACE}"
CAPTURE_FILTER="${CAPTURE_FILTER:-udp}"
SERIAL="${SERIAL:-GVCP01}"
GENICAM="${GENICAM:-docs/aravissink-default.xml}"
BACKEND="${BACKEND:-fake}"
PROXY_BIN="${PROXY_BIN:-./build/src/arv-gvcp-proxy-0.10}"
PROXY_LOG="${PROXY_LOG:-gvcp-proxy.log}"
PCAP="${PCAP:-gvcp-proxy.pcap}"
CAPTURE_LOG="${CAPTURE_LOG:-gvcp-proxy-capture.log}"
RUN_BGAPI2="${RUN_BGAPI2:-1}"
RUN_EGRABBER="${RUN_EGRABBER:-1}"
CAPTURE_TOOL="${CAPTURE_TOOL:-auto}"
TCPDUMP_CMD="${TCPDUMP_CMD:-tcpdump}"
TSHARK_CMD="${TSHARK_CMD:-tshark}"
REVIEW_SCRIPT="${REVIEW_SCRIPT:-scripts/gvcp-proxy-review.py}"
JULIA_BIN="${JULIA_BIN:-julia}"
BGAPI2_PROJECT="${BGAPI2_PROJECT:-../BGAPI2.jl}"
BGAPI2_SCRIPT="${BGAPI2_SCRIPT:-scripts/gvcp-proxy-bgapi2.jl}"
BGAPI2_GENTL_PATH="${BGAPI2_GENTL_PATH:-}"
EGRABBER_PROJECT="${EGRABBER_PROJECT:-../egrabber}"
EGRABBER_SCRIPT="${EGRABBER_SCRIPT:-scripts/gvcp-proxy-egrabber.jl}"
BGAPI2_TIMEOUT="${BGAPI2_TIMEOUT:-30}"
EGRABBER_TIMEOUT="${EGRABBER_TIMEOUT:-30}"
TIMEOUT_CMD=""

if [[ "$INTERFACE" == "CHANGE_ME" ]]; then
  echo "Set INTERFACE to the GVCP proxy interface (e.g. eth0)" >&2
  exit 1
fi

resolve_proxy_bin() {
  if [[ -x "$PROXY_BIN" ]]; then
    echo "$PROXY_BIN"
    return 0
  fi
  if command -v "$PROXY_BIN" >/dev/null 2>&1; then
    command -v "$PROXY_BIN"
    return 0
  fi
  if command -v arv-gvcp-proxy-0.10 >/dev/null 2>&1; then
    command -v arv-gvcp-proxy-0.10
    return 0
  fi
  return 1
}

PROXY_BIN_RESOLVED=""
if ! PROXY_BIN_RESOLVED=$(resolve_proxy_bin); then
  echo "Cannot find arv-gvcp-proxy. Build it or set PROXY_BIN." >&2
  exit 1
fi

if [[ "$CAPTURE_TOOL" == "auto" ]]; then
  if command -v "$TSHARK_CMD" >/dev/null 2>&1; then
    CAPTURE_TOOL="tshark"
  elif command -v "$TCPDUMP_CMD" >/dev/null 2>&1; then
    CAPTURE_TOOL="tcpdump"
  else
    echo "Neither tshark nor tcpdump found. Install one or set CAPTURE_TOOL." >&2
    exit 1
  fi
fi

if [[ "$CAPTURE_TOOL" == "tcpdump" ]] && ! command -v "$TCPDUMP_CMD" >/dev/null 2>&1; then
  echo "tcpdump not found. Install it or set TCPDUMP_CMD." >&2
  exit 1
fi

if [[ "$CAPTURE_TOOL" == "tshark" ]] && ! command -v "$TSHARK_CMD" >/dev/null 2>&1; then
  echo "tshark not found. Install it or set TSHARK_CMD." >&2
  exit 1
fi

if command -v timeout >/dev/null 2>&1; then
  TIMEOUT_CMD="timeout"
fi

cleanup() {
  set +e
  if [[ -n "${PROXY_PID:-}" ]]; then
    kill "$PROXY_PID" >/dev/null 2>&1
    wait "$PROXY_PID" >/dev/null 2>&1
  fi
  if [[ -n "${TCPDUMP_PID:-}" ]]; then
    kill "$TCPDUMP_PID" >/dev/null 2>&1
    wait "$TCPDUMP_PID" >/dev/null 2>&1
  fi
}
trap cleanup EXIT INT TERM

rm -f "$PCAP" "$CAPTURE_LOG"
if [[ "$CAPTURE_TOOL" == "tshark" ]]; then
  "$TSHARK_CMD" -i "$CAPTURE_IFACE" -n -F pcap -w "$PCAP" -f "$CAPTURE_FILTER" >"$CAPTURE_LOG" 2>&1 &
else
  "$TCPDUMP_CMD" -i "$CAPTURE_IFACE" -nn -s 0 -w "$PCAP" "$CAPTURE_FILTER" >"$CAPTURE_LOG" 2>&1 &
fi
TCPDUMP_PID=$!

"$PROXY_BIN_RESOLVED" --interface "$INTERFACE" --serial "$SERIAL" --genicam "$GENICAM" --backend "$BACKEND" >"$PROXY_LOG" 2>&1 &
PROXY_PID=$!

sleep 1

if [[ "$RUN_BGAPI2" == "1" ]]; then
  if command -v "$JULIA_BIN" >/dev/null 2>&1 && [[ -f "$BGAPI2_SCRIPT" ]]; then
    if [[ -n "$TIMEOUT_CMD" ]]; then
      BGAPI2_GENTL_PATH="$BGAPI2_GENTL_PATH" "$TIMEOUT_CMD" "$BGAPI2_TIMEOUT" "$JULIA_BIN" --project="$BGAPI2_PROJECT" "$BGAPI2_SCRIPT" || echo "BGAPI2 script failed" >&2
    else
      BGAPI2_GENTL_PATH="$BGAPI2_GENTL_PATH" "$JULIA_BIN" --project="$BGAPI2_PROJECT" "$BGAPI2_SCRIPT" || echo "BGAPI2 script failed" >&2
    fi
  else
    echo "Skipping BGAPI2 (missing julia or $BGAPI2_SCRIPT)" >&2
  fi
fi

if [[ "$RUN_EGRABBER" == "1" ]]; then
  if command -v "$JULIA_BIN" >/dev/null 2>&1 && [[ -f "$EGRABBER_SCRIPT" ]]; then
    if [[ -n "$TIMEOUT_CMD" ]]; then
      "$TIMEOUT_CMD" "$EGRABBER_TIMEOUT" "$JULIA_BIN" --project="$EGRABBER_PROJECT" "$EGRABBER_SCRIPT" || echo "eGrabber script failed" >&2
    else
      "$JULIA_BIN" --project="$EGRABBER_PROJECT" "$EGRABBER_SCRIPT" || echo "eGrabber script failed" >&2
    fi
  else
    echo "Skipping eGrabber (missing julia or $EGRABBER_SCRIPT)" >&2
  fi
fi

sleep 1

if [[ -f "$REVIEW_SCRIPT" ]]; then
  if [[ -s "$PCAP" ]]; then
    python3 "$REVIEW_SCRIPT" "$PCAP" || echo "Review script failed" >&2
  else
    echo "No capture file at $PCAP. Check $CAPTURE_LOG for errors." >&2
  fi
else
  echo "Review script not found at $REVIEW_SCRIPT" >&2
fi
