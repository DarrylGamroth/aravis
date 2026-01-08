using BGAPI2

function ip_to_u32(ip::AbstractString)
    parts = split(ip, ".")
    if length(parts) != 4
        error("Invalid IPv4 address: $ip")
    end
    value = 0
    for part in parts
        octet = parse(Int, part)
        if octet < 0 || octet > 255
            error("Invalid IPv4 address: $ip")
        end
        value = (value << 8) + octet
    end
    return value
end

function find_device(serial::AbstractString)
    device = nothing
    interface = nothing
    system = nothing
    for sys in BGAPI2.SystemList()
        open(sys)
        try
            for iface in BGAPI2.InterfaceList(sys)
                open(iface)
                try
                    for dev in BGAPI2.DeviceList(iface, 100)
                        open(dev)
                        try
                            if BGAPI2.serial_number(dev) == serial
                                device = dev
                                interface = iface
                                system = sys
                                break
                            end
                        finally
                            if device !== dev
                                close(dev)
                            end
                        end
                    end
                finally
                    if device === nothing
                        close(iface)
                    end
                end
                if device !== nothing
                    break
                end
            end
        finally
            if device === nothing
                close(sys)
                BGAPI2.release(sys)
            end
        end
        if device !== nothing
            break
        end
    end
    return device, interface, system
end

function maybe_set_int(device::BGAPI2.Device, name::AbstractString, value::Integer)
    try
        BGAPI2.int!(BGAPI2.remote_node(device, name), Int64(value))
        println("set ", name, " = ", value)
        return true
    catch err
        println("failed to set ", name, ": ", err)
        return false
    end
end

function maybe_set_stream_int(stream::BGAPI2.DataStream, name::AbstractString, value::Integer)
    try
        BGAPI2.int!(BGAPI2.node(stream, name), Int64(value))
        println("set stream ", name, " = ", value)
        return true
    catch err
        println("failed to set stream ", name, ": ", err)
        return false
    end
end

function maybe_set_string(device::BGAPI2.Device, name::AbstractString, value::AbstractString)
    try
        BGAPI2.value!(BGAPI2.remote_node(device, name), value)
        println("set ", name, " = ", value)
        return true
    catch err
        println("failed to set ", name, ": ", err)
        return false
    end
end

function capture_one()
    gentl_path = get(ENV, "BGAPI2_GENTL_PATH", "")
    if !isempty(gentl_path)
        if isfile(gentl_path)
            ENV["GENICAM_GENTL64_PATH"] = dirname(gentl_path)
        else
            ENV["GENICAM_GENTL64_PATH"] = gentl_path
        end
    end
    try
        BGAPI2.LibBGAPI2.BGAPI2_SetEnv(ENV["GENICAM_GENTL64_PATH"])
    catch
    end

    serial = get(ENV, "GVCP_SERIAL", "GVCP01")
    width = parse(Int, get(ENV, "GVCP_WIDTH", "640"))
    height = parse(Int, get(ENV, "GVCP_HEIGHT", "480"))
    offset_x = parse(Int, get(ENV, "GVCP_OFFSET_X", "0"))
    offset_y = parse(Int, get(ENV, "GVCP_OFFSET_Y", "0"))
    pixel_format = get(ENV, "GVCP_PIXEL_FORMAT", "Mono16")
    stream_ip = get(ENV, "GVCP_STREAM_IP", "")
    stream_port_env = get(ENV, "GVCP_STREAM_PORT", "")
    packet_size_env = get(ENV, "GVCP_PACKET_SIZE", "")

    frame_count = parse(Int, get(ENV, "GVCP_FRAME_COUNT", "10"))
    frame_timeout_ms = parse(Int, get(ENV, "GVCP_FRAME_TIMEOUT_MS", "5000"))

    device, interface, system = find_device(serial)
    if device === nothing
        error("Device not found: $serial")
    end

    try
        datastreamlist = BGAPI2.DataStreamList(device)
        open(datastreamlist[1]) do datastream
            stream_port = 0
            try
                stream_port = Int(BGAPI2.int(BGAPI2.node(datastream, "GevSCPHostPort")))
                println("datastream GevSCPHostPort = ", stream_port)
            catch err
                println("failed to read GevSCPHostPort: ", err)
            end
            if stream_port == 0 && !isempty(stream_port_env)
                stream_port = parse(Int, stream_port_env)
            end

            if !isempty(stream_ip)
                ip_value = ip_to_u32(stream_ip)
                if !maybe_set_stream_int(datastream, "GevSCDA", ip_value)
                    maybe_set_int(device, "GevSCDA", ip_value)
                end
            end
            if stream_port != 0
                if !maybe_set_stream_int(datastream, "GevSCPHostPort", stream_port)
                    maybe_set_int(device, "GevSCPHostPort", stream_port)
                end
            end
            if !isempty(packet_size_env)
                if !maybe_set_stream_int(datastream, "DeviceStreamChannelPacketSize", parse(Int, packet_size_env))
                    maybe_set_int(device, "GevSCPSPacketSize", parse(Int, packet_size_env))
                end
            end

            maybe_set_int(device, "Width", width)
            maybe_set_int(device, "Height", height)
            maybe_set_int(device, "OffsetX", offset_x)
            maybe_set_int(device, "OffsetY", offset_y)
            maybe_set_string(device, "PixelFormat", pixel_format)

            bl = BGAPI2.BufferList(datastream)
            for _ in 1:4
                push!(bl, BGAPI2.Buffer())
            end
            for b in bl
                BGAPI2.queue_buffer(bl, b)
            end
            BGAPI2.start_acquisition_continuous(datastream)
            BGAPI2.remote_node(device, "AcquisitionStart") |> BGAPI2.execute
            for idx in 1:frame_count
                try
                    b = BGAPI2.filled_buffer(datastream, frame_timeout_ms)
                    println("frame ", idx, " id: ", BGAPI2.frame_id(b))
                    println("frame ", idx, " size: ", BGAPI2.size_filled(b))
                    BGAPI2.queue_buffer(bl, b)
                catch e
                    println("capture error on frame ", idx, ": ", e)
                    break
                end
            end
            BGAPI2.remote_node(device, "AcquisitionStop") |> BGAPI2.execute
            BGAPI2.stop_acquisition(datastream)
            BGAPI2.discard_all_buffers(bl)
            empty!(bl)
        end
    finally
        if device !== nothing
            close(device)
        end
        if interface !== nothing
            close(interface)
        end
        if system !== nothing
            close(system)
            BGAPI2.release(system)
        end
    end
end

capture_one()
