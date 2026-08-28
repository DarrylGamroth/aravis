#!/bin/sh
# SPDX-License-Identifier:Unlicense

set -eu

unshare_command=$1
ip_command=$2
benchmark=$3

if ! "$unshare_command" --user --map-root-user --net true 2>/dev/null; then
	echo "SKIP: unprivileged user/network namespaces are unavailable"
	exit 77
fi

# The positional parameters are expanded by the shell inside the namespace.
# shellcheck disable=SC2016
output=$("$unshare_command" --user --map-root-user --net sh -c '
	"$1" link set lo up
	exec "$2" -n 100 -w 20 --frame-rate=200 --packet-size=8192
' sh "$ip_command" "$benchmark")

printf '%s\n' "$output"

case "$output" in
	*"failures=0"*"underruns=0"*"packet_socket_active=1"*"packet_socket_drops=0"*"packet_socket_freezes=0"*"packet_socket_malformed_packets=0"*)
		exit 0
		;;
	*)
		echo "Packet-socket benchmark did not report a clean TPACKET_V3 run" >&2
		exit 1
		;;
esac
