# aravissink (GStreamer sink)

`aravissink` is a GStreamer sink element that behaves like a simple GigE Vision camera. It listens for GVCP control (discovery, register/memory access), accepts stream destination settings, and sends GVSP packets using incoming GStreamer video buffers as the pixel payload.

## Capabilities

- GVCP responder on UDP port 3956 (discovery + read/write register/memory).
- GVSP streaming to the client address/port written by the control channel.
- No packet resend, events, or action commands.

## Supported formats

The sink accepts these raw formats:

- `GRAY8` -> `Mono8`
- `GRAY16_LE` -> `Mono16`
- `RGB` -> `RGB8`

Other formats should be converted upstream with `videoconvert`.

## Properties

- `interface` (string): Interface name or IPv4 address to bind (default `127.0.0.1`).
- `serial` (string): Device serial number exposed via GVCP discovery.
- `genicam` (string): Path to GenICam XML file to expose.
- `default-width` (uint): Default width before caps negotiation (default `640`).
- `default-height` (uint): Default height before caps negotiation (default `480`).
- `default-pixel-format` (string): Default pixel format before caps negotiation (`Mono8`, `Mono16`, `RGB8`; default `Mono16`).

## Default GenICam XML

Use `docs/aravissink-default.xml` as a starter GenICam XML. It maps the supported
GStreamer caps to the corresponding PFNC pixel formats:

- `GRAY8` -> `Mono8` (`0x01080001`)
- `GRAY16_LE` -> `Mono16` (`0x01100007`)
- `RGB` -> `RGB8` (`0x02180014`)

## Example pipelines

Stream 640x512 Mono16 from a test pattern:

```
gst-launch-1.0 videotestsrc ! \
  video/x-raw,format=GRAY16_LE,width=640,height=512,framerate=30/1 ! \
  aravissink interface=eth0 genicam=/path/to/genicam.xml
```

Stream RGB:

```
gst-launch-1.0 videotestsrc ! \
  video/x-raw,format=RGB,width=640,height=512,framerate=30/1 ! \
  aravissink interface=eth0 genicam=/path/to/genicam.xml
```

## Notes

- The sink uses the fake camera register map (same addresses as `arv-fake-camera.xml`), so your GenICam XML should match those registers.
- Stream destination is controlled by the client writing `ARV_GVBS_STREAM_CHANNEL_0_IP_ADDRESS_OFFSET` and `ARV_GVBS_STREAM_CHANNEL_0_PORT_OFFSET`.
- Once caps are negotiated, the sink updates width/height/pixel format to match the negotiated caps.

## Multiple instances

GVCP listens on UDP port 3956, so only one `aravissink` can bind to a given IP address. You can run multiple instances on the same machine if each one binds to a different IP address. Common approaches:

- Use IP aliases on one NIC (e.g., `eth0:1`, `eth0:2`), and set `interface=` per sink.
- Run each sink in a network namespace or container with its own IP.

Each instance will respond to discovery on its bound address and should expose a unique `serial` value so clients can distinguish devices.
