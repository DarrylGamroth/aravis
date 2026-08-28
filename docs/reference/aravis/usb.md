Title: USB Devices

# USB

## Permissions

By default, USB devices permissions may not be sufficient to allow any user to
access the USB3 cameras. These permissions can be changed by using an udev rule
file. There is a file example in [Aravis
sources](https://github.com/AravisProject/aravis/blob/main/src/aravis.rules).
This file must be placed in `/etc/udev/rules.d` directory (The exact location
may depend on the distribution you are using). This file only contains
declarations for a couple of vendors. If you want to add an entry with the
vendor of your camera, the output of `lsusb` command will give you the vendor
id, which is the first 4 digits of the ID field.

## Performance

Aravis uses the asynchronous libusb API by default. The mode can be selected
with [method@Aravis.Camera.uv_set_usb_mode].

`arv-viewer` and `arv-camera-test` can use the asynchronous API if `usb-mode`
option is set to `async`. Similarly, the GStreamer plugin is using the
asynchronous API if `usb-mode` property is set to `async`.

## Progressive buffer access

Native USB3 Vision streams publish receive progress through
`arv_stream_get_buffer_progress()`. For an active ordinary image payload,
`committed_size` is the contiguous prefix whose USB bulk transfers have
completed. The stream does not write those bytes again for the current frame.
This works with both synchronous and asynchronous USB modes. In asynchronous
mode, Aravis tracks transfer completion order and does not publish bytes beyond
an incomplete earlier transfer.

The maximum USB transfer size determines the publication granularity. A smaller
value can make complete rows available sooner, but increases transfer and
callback overhead. Set it with
`arv_uv_device_set_maximum_transfer_size()` before creating the stream. A caller
must retain ownership of the buffer, process only complete application-level
units inside the committed prefix, and wait for normal stream completion before
requeuing it. Multipart, extended chunk, and GenDC payloads do not currently
support progressive access.
