# GVCP Proxy (Host-Side)

This project includes a small GVCP responder intended to run on a host CPU while an external device (for example, an FPGA) already emits GVSP payloads. The proxy implements the control plane (GVCP) and exposes a register map to clients (Aravis, GenICam tools) while the data plane (GVSP) remains on the external device.

The proxy lives in `src/arvgvcpproxy.c` and uses a pluggable backend defined in `src/arvgvcpproxy-backend.h`.

## What it does

- Listens on UDP port 3956 for GVCP commands.
- Responds to discovery, read/write register, and read/write memory.
- Maintains control-channel privilege and heartbeat timeout behavior.
- Exposes a backend interface so register/memory access can be routed to your hardware.

## What it does not do

- GVSP resend or GVCP packet resend.
- Events, action commands, pending-ack, or extended status codes.
- Any image streaming (GVSP remains external).

## Build and run

The proxy is built as part of the normal Meson build. After install, the executable is:

```
arv-gvcp-proxy-0.10
```

Example:

```
arv-gvcp-proxy-0.10 \
  --interface eth0 \
  --serial GVCP01 \
  --genicam /path/to/genicam.xml \
  --backend fake
```

### Options

- `--interface`: Interface name or IPv4 address to bind GVCP sockets.
- `--serial`: Device serial number (used for discovery data).
- `--genicam`: GenICam XML file to expose to clients.
- `--backend`: Backend name. Currently: `fake`/`memory` (in-memory, `ArvFakeCamera`-based).
- `--debug`: Aravis debug domains.

## Backend interface

The proxy uses a small backend vtable to access registers/memory and optionally set the local IPv4 address in the register map.

See `src/arvgvcpproxy-backend.h` for the interface:

- `read_memory(address, size, buffer)`
- `write_memory(address, size, buffer)`
- `read_register(address, value)`
- `write_register(address, value)`
- `stream_config_changed_ex(stream_ip, stream_port, packet_size, mac, is_multicast)`
- `set_inet_address(address)`
- `destroy()`

To add a new backend:

1) Implement the vtable functions.
2) Add a constructor in `arv_gvcp_proxy_backend_new` in `src/arvgvcpproxy-backend.c`.
3) Build and run with `--backend <name>`.

## Minimal register behavior

Clients (Aravis) will typically:

- Discover the device (GVCP discovery).
- Read the XML URL to load GenICam.
- Write `ControlChannelPrivilege` to obtain control.
- Write stream destination IP and port (stream channel 0 registers).
- Write `AcquisitionStart` to begin streaming.

Your backend should ensure those GVBS registers are implemented. To forward stream destination settings to PL logic, implement `stream_config_changed_ex` and apply the values whenever the GVBS stream registers are updated.

## Multicast notes

If a client writes a multicast destination (224.0.0.0/4) into the stream channel IP register, GVSP should be sent to the corresponding multicast MAC address. The proxy does not enforce unicast vs multicast, so your backend can:

- accept multicast IPs in `ARV_GVBS_STREAM_CHANNEL_0_IP_ADDRESS_OFFSET`,
- compute the multicast MAC (`01:00:5e:xx:xx:xx` from the lower 23 bits of the IP),
- program the PL sender accordingly.

The proxy helper uses `stream_config_changed_ex` to pass the computed MAC and multicast flag to your backend.

GVCP remains unicast; only GVSP becomes multicast. The receiver must join the multicast group (IGMP) and the network must allow multicast.
