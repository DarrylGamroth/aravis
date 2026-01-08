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
