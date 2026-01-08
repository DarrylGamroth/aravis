import os
import sys

wheel_path = os.environ.get(
    "EGRABBER_WHL",
    "/opt/euresys/egrabber/python/egrabber-25.12.1.16-py2.py3-none-any.whl",
)
if wheel_path and wheel_path not in sys.path:
    sys.path.insert(0, wheel_path)

try:
    from egrabber import *
except ModuleNotFoundError as exc:
    if exc.name == "six":
        print("Missing dependency: six. Install python3-six or pip-install six.")
        raise SystemExit(1)
    raise

serial = os.environ.get("GVCP_SERIAL", "GVCP01")
width = int(os.environ.get("GVCP_WIDTH", "640"))
height = int(os.environ.get("GVCP_HEIGHT", "480"))
offset_x = int(os.environ.get("GVCP_OFFSET_X", "0"))
offset_y = int(os.environ.get("GVCP_OFFSET_Y", "0"))
pixel_format = os.environ.get("GVCP_PIXEL_FORMAT", "Mono16")

interface_index = int(os.environ.get("EGRABBER_INTERFACE_INDEX", "0"))
device_index = int(os.environ.get("EGRABBER_DEVICE_INDEX", "0"))

gentl = EGenTL()
grabber = EGrabber(gentl, interface_index, device_index)

print("InterfaceID:", grabber.interface.get("InterfaceID"))
print("DeviceID:", grabber.device.get("DeviceID"))
print("DeviceSerialNumber:", grabber.remote.get("DeviceSerialNumber"))

if grabber.remote.get("DeviceSerialNumber") != serial:
    print("warning: serial does not match GVCP_SERIAL")

grabber.remote.set("Width", width)
grabber.remote.set("Height", height)
grabber.remote.set("OffsetX", offset_x)
grabber.remote.set("OffsetY", offset_y)
grabber.remote.set("PixelFormat", pixel_format)

grabber.remote.execute("AcquisitionStart")
grabber.remote.execute("AcquisitionStop")
