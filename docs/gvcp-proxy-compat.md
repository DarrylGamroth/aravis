# GVCP Proxy Compatibility Testing (Baumer GAPI, Euresys GigE Link)

This document outlines how to test the GVCP proxy with third‑party GenICam stacks and what features are commonly required.

## Scope

The current GVCP proxy supports:

- Discovery (GVCP discovery + acknowledge)
- Read/write register and memory
- XML URL exposure via GVBS registers
- Control channel privilege + heartbeat timeout
- Stream destination registers (IP/port/packet size)

It does not implement packet resend, events, or action commands.

## Common expectations from vendor stacks

These are the areas that often differ across vendor SDKs:

- GVBS capability register (`ARV_GVBS_GVCP_CAPABILITY_OFFSET`) advertises features.
- CCP ownership handling (control channel privilege behavior).
- Packet resend support (GVCP packet resend command).
- Event channel and event data support.
- Extended status codes / pending acknowledge behavior.
- Additional GVBS registers (link speed, CCP app socket, manifest table, etc.).

You can add support incrementally based on what the SDK actually requests.

## Test plan

### 1) Build and run the GVCP proxy

Run the proxy on your target interface:

```
arv-gvcp-proxy-0.10 --interface eth0 --serial GVCP01 --genicam /path/to/your.xml --backend fake
```

### 2) Capture GVCP traffic

Use tcpdump or Wireshark on the interface that the SDK uses:

```
sudo tcpdump -i eth0 -nn udp port 3956 -w gvcp.pcap
```

### 3) Connect with the vendor SDK

Use the vendor tool or SDK to discover and open the device. Note:

- Does discovery succeed?
- Does the SDK load the GenICam XML?
- Does it set control channel privilege?
- Does it attempt additional GVCP commands (packet resend, events)?

### 4) Inspect the trace

In Wireshark:

- Filter on GVCP: `udp.port == 3956`
- Inspect request/response pairs.
- Look for GVCP errors or missing responses.

Typical indicators of missing features:

- Repeated retries for the same command.
- Commands for packet resend or event setup.
- Read/write accesses to GVBS registers not yet implemented in your backend.

### 5) Add only what’s required

Extend the proxy/backend for the missing features observed. Keep a list of:

- GVCP command types needed.
- Additional GVBS registers used.
- Required capability bits.

## Suggested compatibility checklist

Start with these and only add more if the SDK requests them:

- `ARV_GVBS_GVCP_CAPABILITY_OFFSET` set to include:
  - Write memory
  - Packet resend (only if you plan to implement)
  - Event (only if you plan to implement)
  - Heartbeat disable (optional)
- `ARV_GVBS_CONTROL_CHANNEL_PRIVILEGE_OFFSET` behavior matches CCP ownership.
- `ARV_GVBS_HEARTBEAT_TIMEOUT_OFFSET` is writable if the SDK writes it.
- Stream channel 0 registers:
  - IP address, port, packet size

## What to send back after testing

Capture this info so the proxy can be extended accurately:

- List of GVCP commands observed (by command ID).
- GVBS registers accessed (addresses).
- Any GVCP errors returned by the SDK.
- Whether the SDK times out waiting for a response.

## Automated test loop

Use the scripts below to automate a capture, run both SDKs, and summarize GVCP
traffic. All variables are placeholders and should be adjusted per host.

Run the full loop:

```
INTERFACE=eth0 \
GENICAM=/path/to/your.xml \
SERIAL=GVCP01 \
scripts/gvcp-proxy-test.sh
```

Optional overrides:

- `CAPTURE_FILTER="udp port 3956"` for GVCP-only captures.
- `CAPTURE_TOOL=tshark` to use `tshark` (preferred when in the `wireshark` group).
- `CAPTURE_TOOL=tcpdump` with `TCPDUMP_CMD="sudo tcpdump"` if you need elevated capture.
- `RUN_BGAPI2=0` or `RUN_EGRABBER=0` to skip a SDK.
- `BGAPI2_PROJECT=../BGAPI2.jl` if the Julia package lives elsewhere.
- `BGAPI2_GENTL_PATH=/path/to/baumer.cti` to force a single GenTL producer for BGAPI2 (file or directory).
- `EGRABBER_INTERFACE_INDEX=0` / `EGRABBER_DEVICE_INDEX=0` to select a device.
- `EGRABBER_GENTL_PATH=/opt/euresys/egrabber/lib/x86_64/gigelink.cti` to pick a specific Euresys GenTL producer.
- `BGAPI2_TIMEOUT=30` / `EGRABBER_TIMEOUT=30` to cap SDK runtime (seconds).
- The loop uses the Julia eGrabber wrapper in `../egrabber` (no Python deps needed).

Capture review:

```
python3 scripts/gvcp-proxy-review.py gvcp-proxy.pcap
```

Scripts:

- `scripts/gvcp-proxy-test.sh` runs the proxy, captures pcap with tcpdump, runs the SDK scripts, and calls the reviewer.
- `scripts/gvcp-proxy-review.py` parses pcap and prints GVCP command/register summaries.
- `scripts/gvcp-proxy-bgapi2.jl` and `scripts/gvcp-proxy-egrabber.py` perform minimal node read/write and acquisition start/stop.

## Vendor SDK quick-starts

Use these when you want a repeatable, scriptable test that exercises discovery,
GenICam XML fetch, control channel, and basic register/memory access.

### Baumer BGAPI2.jl (Julia)

Prereqs:

- Install the Baumer GenTL producer and note the CTI path.
- Set `GENICAM_GENTL64_PATH` so BGAPI2 can discover it.

Example script (discovery + basic node read/write + acquisition start/stop):

```julia
using BGAPI2

ENV["GENICAM_GENTL64_PATH"] = "/path/to/baumer/cti"

function find_camera(serial::AbstractString)
    for system in BGAPI2.SystemList()
        open(system)
        for interface in BGAPI2.InterfaceList(system)
            open(interface)
            for device in BGAPI2.DeviceList(interface, 100)
                open(device)
                if BGAPI2.serial_number(device) == serial
                    return device
                end
                close(device)
            end
            close(interface)
        end
        close(system)
        BGAPI2.release(system)
    end
    return nothing
end

device = find_camera("GVCP01")
if device === nothing
    error("Device not found")
end

BGAPI2.int!(BGAPI2.remote_node(device, "Width"), 640)
BGAPI2.int!(BGAPI2.remote_node(device, "Height"), 480)
BGAPI2.int!(BGAPI2.remote_node(device, "OffsetX"), 0)
BGAPI2.int!(BGAPI2.remote_node(device, "OffsetY"), 0)
BGAPI2.value!(BGAPI2.remote_node(device, "PixelFormat"), "Mono16")

BGAPI2.remote_node(device, "AcquisitionStart") |> BGAPI2.execute
sleep(1.0)
BGAPI2.remote_node(device, "AcquisitionStop") |> BGAPI2.execute
```

Notes:

- If you want to test streaming, reuse `get_image` from `../BGAPI2.jl/src/test.jl`.
- Capture GVCP traffic during this run with tcpdump as described above.

### Euresys eGrabber (Python)

Prereqs:

- The wheel is shipped in `/opt/euresys/egrabber/python/`.
- Install it with:

```
python3 -m pip install /opt/euresys/egrabber/python/egrabber-25.12.1.16-py2.py3-none-any.whl
```

Example script (discovery + basic node read/write):

```python
from egrabber import *

gentl = EGenTL()
grabber = EGrabber(gentl, 0, 0)

print("Interface:", grabber.interface.get("InterfaceID"))
print("Device:", grabber.device.get("DeviceID"))
print("Width:", grabber.remote.get("Width"))
print("Height:", grabber.remote.get("Height"))

grabber.remote.set("Width", 640)
grabber.remote.set("Height", 480)
grabber.remote.set("OffsetX", 0)
grabber.remote.set("OffsetY", 0)
grabber.remote.set("PixelFormat", "Mono16")

grabber.remote.execute("AcquisitionStart")
grabber.remote.execute("AcquisitionStop")
```

Notes:

- The `module.set()` / `module.execute()` APIs are documented in the eGrabber
  Python section (`/opt/euresys/egrabber/doc/egrabber.html`).
- Capture GVCP with tcpdump during this run to see which registers the SDK touches.
