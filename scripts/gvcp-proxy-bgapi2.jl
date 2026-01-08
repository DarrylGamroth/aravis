using BGAPI2

function main()
    serial = get(ENV, "GVCP_SERIAL", "GVCP01")
    width = parse(Int, get(ENV, "GVCP_WIDTH", "640"))
    height = parse(Int, get(ENV, "GVCP_HEIGHT", "480"))
    offset_x = parse(Int, get(ENV, "GVCP_OFFSET_X", "0"))
    offset_y = parse(Int, get(ENV, "GVCP_OFFSET_Y", "0"))
    pixel_format = get(ENV, "GVCP_PIXEL_FORMAT", "Mono16")

    gentl_path = get(ENV, "BGAPI2_GENTL_PATH", "")
    if !isempty(gentl_path)
        if isfile(gentl_path)
            ENV["GENICAM_GENTL64_PATH"] = dirname(gentl_path)
        else
            ENV["GENICAM_GENTL64_PATH"] = gentl_path
        end
    end

    if !haskey(ENV, "GENICAM_GENTL64_PATH") || isempty(ENV["GENICAM_GENTL64_PATH"])
        println("GENICAM_GENTL64_PATH is not set; BGAPI2 discovery may fail")
    end

    found = false

    for system in BGAPI2.SystemList()
        open(system)
        try
            for interface in BGAPI2.InterfaceList(system)
                open(interface)
                try
                    for device in BGAPI2.DeviceList(interface, 100)
                        open(device)
                        try
                            if BGAPI2.serial_number(device) != serial
                                continue
                            end
                            found = true

                            BGAPI2.int!(BGAPI2.remote_node(device, "Width"), width)
                            BGAPI2.int!(BGAPI2.remote_node(device, "Height"), height)
                            BGAPI2.int!(BGAPI2.remote_node(device, "OffsetX"), offset_x)
                            BGAPI2.int!(BGAPI2.remote_node(device, "OffsetY"), offset_y)
                            BGAPI2.value!(BGAPI2.remote_node(device, "PixelFormat"), pixel_format)

                            BGAPI2.remote_node(device, "AcquisitionStart") |> BGAPI2.execute
                            sleep(0.5)
                            BGAPI2.remote_node(device, "AcquisitionStop") |> BGAPI2.execute
                            break
                        finally
                            close(device)
                        end
                    end
                finally
                    close(interface)
                end
                if found
                    break
                end
            end
        finally
            close(system)
            BGAPI2.release(system)
        end
        if found
            break
        end
    end

    if !found
        error("Device not found: $serial")
    end
end

main()
