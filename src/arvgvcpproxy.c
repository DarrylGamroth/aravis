/* Aravis - Digital camera library
 *
 * Copyright © 2025
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "arvgvcpproxy-backend.h"

#include <arvdebug.h>
#include <arvdebugprivate.h>
#include <arvgvcpprivate.h>
#include <arvnetworkprivate.h>
#include <gio/gio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define ARV_GVCP_PROXY_N_INPUT_SOCKETS 3
#define ARV_GVCP_PROXY_BUFFER_SIZE 2048

typedef enum {
	ARV_GVCP_PROXY_INPUT_SOCKET_GVCP = 0,
	ARV_GVCP_PROXY_INPUT_SOCKET_GLOBAL_DISCOVERY,
	ARV_GVCP_PROXY_INPUT_SOCKET_SUBNET_DISCOVERY
} ArvGvcpProxyInputSocket;

typedef struct {
	ArvGvcpProxyBackend *backend;
	GSocket *input_sockets[ARV_GVCP_PROXY_N_INPUT_SOCKETS];
	GPollFD socket_fds[ARV_GVCP_PROXY_N_INPUT_SOCKETS];
	unsigned int n_socket_fds;
	GSocketAddress *controller_address;
	gint64 controller_time;
} ArvGvcpProxy;

static gboolean cancel = FALSE;

static char *arv_option_interface_name = NULL;
static char *arv_option_serial_number = NULL;
static char *arv_option_genicam_file = NULL;
static char *arv_option_backend = NULL;
static char *arv_option_debug_domains = NULL;

static const GOptionEntry arv_option_entries[] =
{
	{ "interface",		'i', 0, G_OPTION_ARG_STRING,
		&arv_option_interface_name,	"Listening interface name or address", "interface"},
	{ "serial",		's', 0, G_OPTION_ARG_STRING,
		&arv_option_serial_number,	"Device serial number", "serial_nbr"},
	{ "genicam",		'g', 0, G_OPTION_ARG_STRING,
		&arv_option_genicam_file,	"XML Genicam file to expose", "genicam_filename"},
	{ "backend",		'b', 0, G_OPTION_ARG_STRING,
		&arv_option_backend,		"Backend name (fake, memory)", "backend"},
	{
		"debug",		'd', 0, G_OPTION_ARG_STRING,
		&arv_option_debug_domains,	NULL,
		"{<category>[:<level>][,...]|help}"
	},
	{ NULL }
};

static void
set_cancel (int signal_id)
{
	cancel = TRUE;
}

static gboolean
_g_inet_socket_address_is_equal (GInetSocketAddress *a, GInetSocketAddress *b)
{
	if (!G_IS_INET_SOCKET_ADDRESS (a) ||
	    !G_IS_INET_SOCKET_ADDRESS (b))
		return FALSE;

	if (g_inet_socket_address_get_port (a) != g_inet_socket_address_get_port (b))
		return FALSE;

	return g_inet_address_equal (g_inet_socket_address_get_address (a),
				     g_inet_socket_address_get_address (b));
}

static guint32
_backend_read_register (ArvGvcpProxyBackend *backend, guint32 address, guint32 fallback)
{
	guint32 value = fallback;

	if (backend->vtable->read_register == NULL)
		return fallback;

	if (!backend->vtable->read_register (backend, address, &value))
		return fallback;

	return value;
}

static gboolean
_backend_read_memory (ArvGvcpProxyBackend *backend, guint32 address, guint32 size, void *buffer)
{
	if (backend->vtable->read_memory == NULL)
		return FALSE;

	return backend->vtable->read_memory (backend, address, size, buffer);
}

static gboolean
_backend_write_memory (ArvGvcpProxyBackend *backend, guint32 address, guint32 size, const void *buffer)
{
	if (backend->vtable->write_memory == NULL)
		return FALSE;

	return backend->vtable->write_memory (backend, address, size, buffer);
}

static void
_backend_write_register (ArvGvcpProxyBackend *backend, guint32 address, guint32 value)
{
	if (backend->vtable->write_register == NULL)
		return;

	backend->vtable->write_register (backend, address, value);
}

static void
_maybe_notify_stream_config (ArvGvcpProxyBackend *backend, guint32 address)
{
	guint32 stream_ip = 0;
	guint32 stream_port = 0;
	guint32 packet_size = 0;
	guint8 mac[6] = {0};
	gboolean is_multicast = FALSE;
	guint32 ip_be;

	if (backend->vtable->stream_config_changed == NULL &&
	    backend->vtable->stream_config_changed_ex == NULL)
		return;

	if (address != ARV_GVBS_STREAM_CHANNEL_0_IP_ADDRESS_OFFSET &&
	    address != ARV_GVBS_STREAM_CHANNEL_0_PORT_OFFSET &&
	    address != ARV_GVBS_STREAM_CHANNEL_0_PACKET_SIZE_OFFSET)
		return;

	if (backend->vtable->read_register == NULL)
		return;

	if (!backend->vtable->read_register (backend, ARV_GVBS_STREAM_CHANNEL_0_IP_ADDRESS_OFFSET, &stream_ip))
		return;
	if (!backend->vtable->read_register (backend, ARV_GVBS_STREAM_CHANNEL_0_PORT_OFFSET, &stream_port))
		return;
	if (!backend->vtable->read_register (backend, ARV_GVBS_STREAM_CHANNEL_0_PACKET_SIZE_OFFSET, &packet_size))
		return;

	ip_be = g_htonl (stream_ip);
	if ((ip_be & 0xf0000000) == 0xe0000000) {
		is_multicast = TRUE;
		mac[0] = 0x01;
		mac[1] = 0x00;
		mac[2] = 0x5e;
		mac[3] = (ip_be >> 16) & 0x7f;
		mac[4] = (ip_be >> 8) & 0xff;
		mac[5] = ip_be & 0xff;
	}

	if (backend->vtable->stream_config_changed != NULL)
		backend->vtable->stream_config_changed (backend, stream_ip, (guint16) stream_port, packet_size);
	if (backend->vtable->stream_config_changed_ex != NULL)
		backend->vtable->stream_config_changed_ex (backend, stream_ip, (guint16) stream_port, packet_size,
							    mac, is_multicast);
}

static void
_proxy_release_controller (ArvGvcpProxy *proxy)
{
	if (proxy->controller_address == NULL)
		return;

	g_object_unref (proxy->controller_address);
	proxy->controller_address = NULL;
	_backend_write_register (proxy->backend, ARV_GVBS_CONTROL_CHANNEL_PRIVILEGE_OFFSET, 0);
}

static gboolean
_handle_control_packet (ArvGvcpProxy *proxy, GSocket *socket,
			GSocketAddress *remote_address,
			ArvGvcpPacket *packet, size_t size)
{
	ArvGvcpPacket *ack_packet = NULL;
	size_t ack_packet_size;
	guint32 block_address;
	guint32 block_size;
	guint16 packet_id;
	guint16 packet_type;
	guint32 register_address;
	guint32 register_value = 0;
	gboolean write_access;
	gboolean success = FALSE;

	if (proxy->controller_address != NULL) {
		gint64 time;
		guint64 elapsed_ms;
		guint32 heartbeat_timeout_ms;

		time = g_get_real_time ();
		elapsed_ms = (time - proxy->controller_time) / 1000;
		heartbeat_timeout_ms = _backend_read_register (proxy->backend,
							       ARV_GVBS_HEARTBEAT_TIMEOUT_OFFSET,
							       3000);

		if (elapsed_ms > heartbeat_timeout_ms) {
			_proxy_release_controller (proxy);
			write_access = TRUE;
			arv_warning_device ("[GvcpProxy::handle_control_packet] Heartbeat timeout");
		} else {
			write_access = _g_inet_socket_address_is_equal
				(G_INET_SOCKET_ADDRESS (remote_address),
				 G_INET_SOCKET_ADDRESS (proxy->controller_address));
		}
	} else {
		write_access = TRUE;
	}

	arv_gvcp_packet_debug (packet, ARV_DEBUG_LEVEL_DEBUG);

	packet_id = arv_gvcp_packet_get_packet_id (packet, size);
	packet_type = arv_gvcp_packet_get_packet_type (packet, size);

	if (packet_type != ARV_GVCP_PACKET_TYPE_CMD) {
		arv_warning_device ("[GvcpProxy::handle_control_packet] Unknown packet type");
		return FALSE;
	}

	switch (g_ntohs (packet->header.command)) {
		case ARV_GVCP_COMMAND_DISCOVERY_CMD:
			ack_packet = arv_gvcp_packet_new_discovery_ack (packet_id, &ack_packet_size);
			arv_info_device ("[GvcpProxy::handle_control_packet] Discovery command");
			_backend_read_memory (proxy->backend, 0, ARV_GVBS_DISCOVERY_DATA_SIZE,
					      &ack_packet->data);
			break;
		case ARV_GVCP_COMMAND_READ_MEMORY_CMD:
			arv_gvcp_packet_get_read_memory_cmd_infos (packet, size, &block_address, &block_size);
			arv_info_device ("[GvcpProxy::handle_control_packet] Read memory command %d (%d)",
					  block_address, block_size);
			ack_packet = arv_gvcp_packet_new_read_memory_ack (block_address, block_size,
									  packet_id, &ack_packet_size);
			_backend_read_memory (proxy->backend, block_address, block_size,
					      arv_gvcp_packet_get_read_memory_ack_data (ack_packet));
			break;
		case ARV_GVCP_COMMAND_WRITE_MEMORY_CMD:
			arv_gvcp_packet_get_write_memory_cmd_infos (packet, size, &block_address, &block_size);
			if (!write_access) {
				arv_warning_device ("[GvcpProxy::handle_control_packet]"
						    " Ignore Write memory command %d (%d) not controller",
						    block_address, block_size);
				break;
			}

			arv_info_device ("[GvcpProxy::handle_control_packet] Write memory command %d (%d)",
					  block_address, block_size);
			_backend_write_memory (proxy->backend, block_address, block_size,
					       arv_gvcp_packet_get_write_memory_cmd_data (packet));
			ack_packet = arv_gvcp_packet_new_write_memory_ack (block_address, packet_id,
									   &ack_packet_size);
			break;
		case ARV_GVCP_COMMAND_READ_REGISTER_CMD:
			arv_gvcp_packet_get_read_register_cmd_infos (packet, size, &register_address);
			if (proxy->backend->vtable->read_register != NULL)
				proxy->backend->vtable->read_register (proxy->backend, register_address, &register_value);
			arv_info_device ("[GvcpProxy::handle_control_packet] Read register command %d -> %d",
					  register_address, register_value);
			ack_packet = arv_gvcp_packet_new_read_register_ack (register_value, packet_id,
									    &ack_packet_size);

			if (register_address == ARV_GVBS_CONTROL_CHANNEL_PRIVILEGE_OFFSET)
				proxy->controller_time = g_get_real_time ();

			break;
		case ARV_GVCP_COMMAND_WRITE_REGISTER_CMD:
			arv_gvcp_packet_get_write_register_cmd_infos (packet, size, &register_address, &register_value);
			if (!write_access) {
				arv_warning_device ("[GvcpProxy::handle_control_packet]"
						    " Ignore Write register command %d (%d) not controller",
						    register_address, register_value);
				break;
			}

			if (proxy->backend->vtable->write_register != NULL)
				proxy->backend->vtable->write_register (proxy->backend, register_address, register_value);
			_maybe_notify_stream_config (proxy->backend, register_address);
			arv_info_device ("[GvcpProxy::handle_control_packet] Write register command %d -> %d",
					  register_address, register_value);
			ack_packet = arv_gvcp_packet_new_write_register_ack (1, packet_id,
									     &ack_packet_size);
			break;
		default:
			arv_warning_device ("[GvcpProxy::handle_control_packet] Unknown command");
	}

	if (ack_packet != NULL) {
		g_socket_send_to (socket, remote_address, (char *) ack_packet, ack_packet_size, NULL, NULL);
		arv_gvcp_packet_debug (ack_packet, ARV_DEBUG_LEVEL_DEBUG);
		g_free (ack_packet);

		success = TRUE;
	}

	if (proxy->controller_address == NULL &&
	    _backend_read_register (proxy->backend, ARV_GVBS_CONTROL_CHANNEL_PRIVILEGE_OFFSET, 0) != 0) {
		g_object_ref (remote_address);
		arv_info_device ("[GvcpProxy::handle_control_packet] New controller");
		proxy->controller_address = remote_address;
		proxy->controller_time = g_get_real_time ();
	} else if (proxy->controller_address != NULL &&
		   _backend_read_register (proxy->backend, ARV_GVBS_CONTROL_CHANNEL_PRIVILEGE_OFFSET, 0) == 0) {
		_proxy_release_controller (proxy);
		arv_info_device ("[GvcpProxy::handle_control_packet] Controller releases");
		proxy->controller_time = g_get_real_time ();
	}

	return success;
}

static gboolean
_create_and_bind_input_socket (GSocket **socket_out, const char *socket_name,
			       GInetAddress *inet_address, unsigned int port,
			       gboolean allow_reuse, gboolean blocking)
{
	GSocket *socket;
	GSocketAddress *socket_address;
	GError *error = NULL;
	gboolean success;
	char *address_string;

	address_string = g_inet_address_to_string (inet_address);
	if (port > 0)
		arv_info_device ("%s address = %s:%d", socket_name, address_string, port);
	else
		arv_info_device ("%s address = %s", socket_name, address_string);
	g_clear_pointer (&address_string, g_free);

	socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, NULL);
	if (!G_IS_SOCKET (socket)) {
		*socket_out = NULL;
		return FALSE;
	}

	socket_address = arv_socket_bind_with_range (socket, inet_address, port, allow_reuse, &error);
	success = G_IS_INET_SOCKET_ADDRESS (socket_address);

	if (error != NULL) {
		arv_warning_device ("Failed to bind %s socket: %s", socket_name, error->message);
		g_clear_error (&error);
	}

	g_clear_object (&socket_address);

	if (success)
		g_socket_set_blocking (socket, blocking);
	else
		g_clear_object (&socket);

	*socket_out = socket;

	return G_IS_SOCKET (socket);
}

static gboolean
_proxy_start (ArvGvcpProxy *proxy, const char *interface_name)
{
	ArvNetworkInterface *iface;
	GSocketAddress *socket_address;
	GInetAddress *inet_address;
	GInetAddress *gvcp_inet_address;
	unsigned int i;
	unsigned int n_socket_fds;

	iface = arv_network_get_interface_by_address (interface_name);
	if (iface == NULL)
		iface = arv_network_get_interface_by_name (interface_name);
#ifdef G_OS_WIN32
	if (iface == NULL && g_strcmp0 (interface_name, "127.0.0.1") == 0)
		iface = arv_network_get_fake_ipv4_loopback ();
#endif
	if (iface == NULL) {
		arv_warning_device ("[GvcpProxy::start] No network interface with address or name '%s' found.",
				    interface_name);
		return FALSE;
	}

	socket_address = g_socket_address_new_from_native (arv_network_interface_get_addr (iface),
							   sizeof (struct sockaddr));
	gvcp_inet_address = g_object_ref (g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (socket_address)));

	if (proxy->backend->vtable->set_inet_address != NULL)
		proxy->backend->vtable->set_inet_address (proxy->backend, gvcp_inet_address);

	_create_and_bind_input_socket
		(&proxy->input_sockets[ARV_GVCP_PROXY_INPUT_SOCKET_GVCP],
		 "GVCP", gvcp_inet_address, ARV_GVCP_PORT, FALSE, FALSE);

	inet_address = g_inet_address_new_from_string ("255.255.255.255");
	if (!g_inet_address_equal (gvcp_inet_address, inet_address))
		_create_and_bind_input_socket
			(&proxy->input_sockets[ARV_GVCP_PROXY_INPUT_SOCKET_GLOBAL_DISCOVERY],
			 "Global discovery", inet_address, ARV_GVCP_PORT, TRUE, FALSE);
	g_clear_object (&inet_address);
	g_clear_object (&socket_address);

	socket_address = g_socket_address_new_from_native (arv_network_interface_get_broadaddr (iface),
							   sizeof (struct sockaddr));
	inet_address = g_object_ref (g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (socket_address)));
	if (!g_inet_address_equal (gvcp_inet_address, inet_address))
		_create_and_bind_input_socket
			(&proxy->input_sockets[ARV_GVCP_PROXY_INPUT_SOCKET_SUBNET_DISCOVERY],
			 "Subnet discovery", inet_address, ARV_GVCP_PORT, FALSE, FALSE);
	g_clear_object (&inet_address);
	g_clear_object (&socket_address);

	g_clear_object (&gvcp_inet_address);

	arv_network_interface_free (iface);

	n_socket_fds = 0;
	for (i = 0; i < ARV_GVCP_PROXY_N_INPUT_SOCKETS; i++) {
		GSocket *socket = proxy->input_sockets[i];

		if (G_IS_SOCKET (socket)) {
			proxy->socket_fds[n_socket_fds].fd = g_socket_get_fd (socket);
			proxy->socket_fds[n_socket_fds].events = G_IO_IN;
			proxy->socket_fds[n_socket_fds].revents = 0;
			n_socket_fds++;
		}
	}

	arv_info_device ("Listening to %d sockets", n_socket_fds);
	proxy->n_socket_fds = n_socket_fds;
	arv_gpollfd_prepare_all (proxy->socket_fds, n_socket_fds);

	return TRUE;
}

static void
_proxy_stop (ArvGvcpProxy *proxy)
{
	unsigned int i;

	arv_gpollfd_finish_all (proxy->socket_fds, proxy->n_socket_fds);

	for (i = 0; i < ARV_GVCP_PROXY_N_INPUT_SOCKETS; i++)
		g_clear_object (&proxy->input_sockets[i]);

	_proxy_release_controller (proxy);
}

int
main (int argc, char **argv)
{
	ArvGvcpProxy proxy;
	GOptionContext *context;
	GError *error = NULL;
	GInputVector input_vector;
	gboolean started;
	unsigned int i;

	memset (&proxy, 0, sizeof (proxy));

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, "GVCP proxy for an external GVSP source.");
	g_option_context_add_main_entries (context, arv_option_entries, NULL);

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_option_context_free (context);
		g_printerr ("Option parsing failed: %s\n", error->message);
		g_error_free (error);
		return EXIT_FAILURE;
	}

	g_option_context_free (context);

	if (!arv_debug_enable (arv_option_debug_domains)) {
		if (g_strcmp0 (arv_option_debug_domains, "help") != 0)
			printf ("Invalid debug selection\n");
		else
			arv_debug_print_infos ();
		return EXIT_FAILURE;
	}

	if (arv_option_interface_name == NULL)
		arv_option_interface_name = g_strdup ("127.0.0.1");

	if (arv_option_serial_number == NULL)
		arv_option_serial_number = g_strdup ("GVCP01");

	proxy.backend = arv_gvcp_proxy_backend_new (arv_option_backend,
						    arv_option_serial_number,
						    arv_option_genicam_file,
						    &error);
	if (proxy.backend == NULL) {
		g_printerr ("Failed to initialize backend: %s\n", error != NULL ? error->message : "Unknown error");
		g_clear_error (&error);
		return EXIT_FAILURE;
	}

	started = _proxy_start (&proxy, arv_option_interface_name);
	if (!started) {
		g_printerr ("Failed to start GVCP proxy\n");
		arv_gvcp_proxy_backend_free (proxy.backend);
		return EXIT_FAILURE;
	}

	signal (SIGINT, set_cancel);

	input_vector.buffer = g_malloc0 (ARV_GVCP_PROXY_BUFFER_SIZE);
	input_vector.size = ARV_GVCP_PROXY_BUFFER_SIZE;

	while (!cancel) {
		int n_events;

		n_events = g_poll (proxy.socket_fds, proxy.n_socket_fds, 1000);
		if (n_events <= 0)
			continue;

		for (i = 0; i < ARV_GVCP_PROXY_N_INPUT_SOCKETS; i++) {
			GSocket *socket = proxy.input_sockets[i];
			int count;

			if (!G_IS_SOCKET (socket))
				continue;

			arv_gpollfd_clear_one (&proxy.socket_fds[i], socket);

			GSocketAddress *remote_address = NULL;
			count = g_socket_receive_message (socket, &remote_address,
							  &input_vector, 1, NULL, NULL,
							  NULL, NULL, NULL);
			if (count > 0) {
				if (_handle_control_packet (&proxy, socket, remote_address,
							    input_vector.buffer, count))
					arv_info_device ("[GvcpProxy::main] Control packet received");
			}
			g_clear_object (&remote_address);
		}
	}

	_proxy_stop (&proxy);
	arv_gvcp_proxy_backend_free (proxy.backend);
	g_free (input_vector.buffer);
	g_clear_pointer (&arv_option_interface_name, g_free);
	g_clear_pointer (&arv_option_serial_number, g_free);
	g_clear_pointer (&arv_option_genicam_file, g_free);
	g_clear_pointer (&arv_option_backend, g_free);

	return EXIT_SUCCESS;
}
