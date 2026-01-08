#!/usr/bin/env python3
import collections
import socket
import struct
import sys

GVCP_PORT = 3956

GVCP_COMMANDS = {
    0x0002: "DiscoveryCmd",
    0x0003: "DiscoveryAck",
    0x0004: "ByeCmd",
    0x0005: "ByeAck",
    0x0040: "PacketResendCmd",
    0x0041: "PacketResendAck",
    0x0080: "ReadRegisterCmd",
    0x0081: "ReadRegisterAck",
    0x0082: "WriteRegisterCmd",
    0x0083: "WriteRegisterAck",
    0x0084: "ReadMemoryCmd",
    0x0085: "ReadMemoryAck",
    0x0086: "WriteMemoryCmd",
    0x0087: "WriteMemoryAck",
    0x0089: "PendingAck",
}

GVCP_PACKET_TYPES = {
    0x00: "Ack",
    0x42: "Cmd",
    0x80: "Error",
    0x8F: "UnknownError",
}


def parse_pcap(path):
    with open(path, "rb") as f:
        header = f.read(24)
        if len(header) < 24:
            raise ValueError("pcap header too short")

        magic = header[:4]
        if magic == b"\xd4\xc3\xb2\xa1":
            endian = "<"
        elif magic == b"\xa1\xb2\xc3\xd4":
            endian = ">"
        elif magic == b"\x4d\x3c\xb2\xa1":
            endian = "<"
        elif magic == b"\xa1\xb2\x3c\x4d":
            endian = ">"
        else:
            raise ValueError("unsupported pcap magic")

        packet_header = struct.Struct(endian + "IIII")
        while True:
            raw = f.read(packet_header.size)
            if not raw:
                break
            if len(raw) < packet_header.size:
                break
            _ts_sec, _ts_usec, incl_len, _orig_len = packet_header.unpack(raw)
            data = f.read(incl_len)
            if len(data) < incl_len:
                break
            yield data


def parse_packets(packets):
    udp_counts = collections.Counter()
    gvcp_counts = collections.Counter()
    gvcp_types = collections.Counter()
    register_reads = collections.Counter()
    register_writes = collections.Counter()
    memory_reads = collections.Counter()
    memory_writes = collections.Counter()
    memory_acks = collections.Counter()
    endpoints = collections.Counter()

    for pkt in packets:
        if len(pkt) < 14:
            continue
        eth_type = struct.unpack("!H", pkt[12:14])[0]
        offset = 14
        if eth_type == 0x8100 and len(pkt) >= 18:
            eth_type = struct.unpack("!H", pkt[16:18])[0]
            offset = 18
        if eth_type != 0x0800:
            continue
        if len(pkt) < offset + 20:
            continue
        ip_header = pkt[offset:offset + 20]
        version_ihl = ip_header[0]
        ihl = (version_ihl & 0x0F) * 4
        if len(pkt) < offset + ihl + 8:
            continue
        proto = ip_header[9]
        if proto != 17:
            continue
        src_ip = socket.inet_ntoa(ip_header[12:16])
        dst_ip = socket.inet_ntoa(ip_header[16:20])
        udp_offset = offset + ihl
        udp_header = pkt[udp_offset:udp_offset + 8]
        src_port, dst_port, _length, _checksum = struct.unpack("!HHHH", udp_header)
        payload = pkt[udp_offset + 8:]
        udp_counts[(src_port, dst_port)] += 1
        endpoints[(src_ip, dst_ip, src_port, dst_port)] += 1

        if src_port != GVCP_PORT and dst_port != GVCP_PORT:
            continue

        if len(payload) < 8:
            continue
        packet_type = payload[0]
        command = struct.unpack("!H", payload[2:4])[0]
        size = struct.unpack("!H", payload[4:6])[0]
        data = payload[8:8 + size]

        gvcp_counts[command] += 1
        gvcp_types[packet_type] += 1

        if command == 0x0080 and len(data) >= 4:
            register_reads[struct.unpack("!I", data[:4])[0]] += 1
        elif command == 0x0082 and len(data) >= 8:
            register_writes[struct.unpack("!I", data[:4])[0]] += 1
        elif command == 0x0084 and len(data) >= 8:
            addr = struct.unpack("!I", data[:4])[0]
            length = struct.unpack("!I", data[4:8])[0]
            memory_reads[(addr, length)] += 1
        elif command == 0x0086 and len(data) >= 4:
            memory_writes[struct.unpack("!I", data[:4])[0]] += 1
        elif command in (0x0085, 0x0087) and len(data) >= 4:
            memory_acks[struct.unpack("!I", data[:4])[0]] += 1

    return {
        "udp_counts": udp_counts,
        "gvcp_counts": gvcp_counts,
        "gvcp_types": gvcp_types,
        "register_reads": register_reads,
        "register_writes": register_writes,
        "memory_reads": memory_reads,
        "memory_writes": memory_writes,
        "memory_acks": memory_acks,
        "endpoints": endpoints,
    }


def format_counts(counter, label_map=None, max_items=20):
    lines = []
    for key, count in counter.most_common(max_items):
        name = label_map.get(key, hex(key)) if label_map else str(key)
        lines.append(f"  {name}: {count}")
    return lines


def main():
    if len(sys.argv) != 2:
        print("usage: gvcp-proxy-review.py <pcap>")
        return 1

    path = sys.argv[1]
    try:
        results = parse_packets(parse_pcap(path))
    except Exception as exc:
        print(f"error: {exc}")
        return 1

    total_gvcp = sum(results["gvcp_counts"].values())
    total_udp = sum(results["udp_counts"].values())

    print("GVCP Proxy Capture Summary")
    print("===========================")
    print(f"Total UDP packets: {total_udp}")
    print(f"Total GVCP packets: {total_gvcp}")

    if total_udp:
        print("\nTop UDP ports (src->dst):")
        for line in format_counts(results["udp_counts"], max_items=10):
            print(line)

    if total_gvcp:
        print("\nGVCP packet types:")
        for line in format_counts(results["gvcp_types"], GVCP_PACKET_TYPES):
            print(line)

        print("\nGVCP commands:")
        for line in format_counts(results["gvcp_counts"], GVCP_COMMANDS):
            print(line)

        if results["register_reads"]:
            print("\nRead register addresses:")
            for line in format_counts(results["register_reads"]):
                print(line)

        if results["register_writes"]:
            print("\nWrite register addresses:")
            for line in format_counts(results["register_writes"]):
                print(line)

        if results["memory_reads"]:
            print("\nRead memory (address, size):")
            for (addr, length), count in results["memory_reads"].most_common(20):
                print(f"  {hex(addr)} size={length}: {count}")

        if results["memory_writes"]:
            print("\nWrite memory addresses:")
            for line in format_counts(results["memory_writes"]):
                print(line)

        if results["memory_acks"]:
            print("\nMemory ack addresses:")
            for line in format_counts(results["memory_acks"]):
                print(line)

    else:
        print("\nNo GVCP packets detected. Check your capture filter and interface.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
