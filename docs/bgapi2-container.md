# BGAPI2 Container Runbook

This uses a Podman bridge network so the fake camera has its own IP and the
host runs BGAPI2 as a separate GigE consumer. This avoids the "same IP stack"
issue that BGAPI2 shows on the host network.

## One-time network setup

```
sudo podman network create --subnet 10.10.0.0/24 --gateway 10.10.0.1 gige-test
```

## Start the fake camera (container)

```
sudo podman run --rm --name gige-fake \
  --network gige-test --ip 10.10.0.2 \
  --privileged \
  -v /:/host:ro \
  -w /host/home/dgamroth/workspaces/codex/aravis \
  docker.io/library/debian:stable-slim \
  /bin/bash -lc 'export LD_LIBRARY_PATH=/host/home/dgamroth/workspaces/codex/aravis/build/src:/host/lib/x86_64-linux-gnu:/host/usr/lib/x86_64-linux-gnu; \
    /host/home/dgamroth/workspaces/codex/aravis/build/src/arv-fake-gv-camera-0.10 -i 10.10.0.2 -s GVCP01 -g /host/home/dgamroth/workspaces/codex/aravis/src/arv-fake-camera.xml -d all'
```

## Run BGAPI2 capture (host)

```
BGAPI2_GENTL_PATH=/opt/baumer-gapi-sdk-c/lib/libbgapi2_gige.cti \
GVCP_SERIAL=GVCP01 GVCP_WIDTH=640 GVCP_HEIGHT=512 GVCP_PIXEL_FORMAT=Mono16 \
GVCP_FRAME_COUNT=10 \
julia /home/dgamroth/workspaces/codex/aravis/scripts/gvcp-bgapi2-capture.jl
```

## Helper script (parameterized)

```
ARAVIS_ROOT=/home/dgamroth/workspaces/codex/aravis \
GVCP_FRAME_COUNT=10 \
bash /home/dgamroth/workspaces/codex/aravis/scripts/run-bgapi2-container.sh
```

Supported variables:

```
ARAVIS_ROOT
PODMAN_IMAGE
NETWORK_NAME
NETWORK_SUBNET
NETWORK_GATEWAY
CONTAINER_IP
GVCP_SERIAL
GVCP_WIDTH
GVCP_HEIGHT
GVCP_PIXEL_FORMAT
GVCP_FRAME_COUNT
```

## Cleanup (optional)

```
sudo podman network rm gige-test
```
