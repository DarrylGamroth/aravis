using EGrabber

function main()
    EGrabber.init_library_path!()

    serial = get(ENV, "GVCP_SERIAL", "GVCP01")
    width = parse(Int, get(ENV, "GVCP_WIDTH", "640"))
    height = parse(Int, get(ENV, "GVCP_HEIGHT", "480"))
    offset_x = parse(Int, get(ENV, "GVCP_OFFSET_X", "0"))
    offset_y = parse(Int, get(ENV, "GVCP_OFFSET_Y", "0"))
    pixel_format = get(ENV, "GVCP_PIXEL_FORMAT", "Mono16")

    interface_index = parse(Int, get(ENV, "EGRABBER_INTERFACE_INDEX", "0"))
    device_index = parse(Int, get(ENV, "EGRABBER_DEVICE_INDEX", "0"))
    stream_index = parse(Int, get(ENV, "EGRABBER_STREAM_INDEX", "0"))

    gentl_path = get(ENV, "EGRABBER_GENTL_PATH", "")
    gentl = isempty(gentl_path) ? EGenTL() : EGenTL(path=gentl_path)

    grabber = EGrabber.EGrabber(
        gentl;
        interface=interface_index,
        device=device_index,
        data_stream=stream_index,
    )
    remote = RemoteModule(grabber)

    serial_read = get(remote, "DeviceSerialNumber", String)
    if serial_read != serial
        println("warning: DeviceSerialNumber does not match GVCP_SERIAL")
    end

    set(remote, "Width", width)
    set(remote, "Height", height)
    set(remote, "OffsetX", offset_x)
    set(remote, "OffsetY", offset_y)
    set(remote, "PixelFormat", pixel_format)

    execute(remote, "AcquisitionStart")
    sleep(0.5)
    execute(remote, "AcquisitionStop")
end

main()
