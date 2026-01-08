/* Aravis - Digital camera library
 *
 * Copyright © 2025
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "gstaravissink.h"

#include <gst/gst.h>
#include <arv.h>
#include <arvgvcpprivate.h>
#include <arvgvspprivate.h>
#include <arvnetworkprivate.h>
#include <gio/gio.h>
#include <string.h>

#define GST_ARAVIS_SINK_DEFAULT_INTERFACE	"127.0.0.1"
#define GST_ARAVIS_SINK_DEFAULT_SERIAL		"ARV-SINK"

#define GST_ARAVIS_SINK_BUFFER_SIZE		2048

GST_DEBUG_CATEGORY_STATIC (aravissink_debug);
#define GST_CAT_DEFAULT aravissink_debug

enum
{
	PROP_0,
	PROP_INTERFACE_NAME,
	PROP_SERIAL_NUMBER,
	PROP_GENICAM_FILENAME,
	PROP_DEFAULT_WIDTH,
	PROP_DEFAULT_HEIGHT,
	PROP_DEFAULT_PIXEL_FORMAT
};

typedef enum {
	ARV_SINK_INPUT_SOCKET_GVCP = 0,
	ARV_SINK_INPUT_SOCKET_GLOBAL_DISCOVERY,
	ARV_SINK_INPUT_SOCKET_SUBNET_DISCOVERY,
	ARV_SINK_N_INPUT_SOCKETS
} ArvSinkInputSocket;

typedef struct {
	GstBaseSink parent;

	char *interface_name;
	char *serial_number;
	char *genicam_filename;

	ArvFakeCamera *camera;
	GMutex camera_mutex;
	GSocket *gvsp_socket;
	GSocket *input_sockets[ARV_SINK_N_INPUT_SOCKETS];
	GPollFD socket_fds[ARV_SINK_N_INPUT_SOCKETS];
	unsigned int n_socket_fds;
	GSocketAddress *controller_address;
	gint64 controller_time;
	GThread *gvcp_thread;
	gint cancel;

	guint16 frame_id;
	guint32 width;
	guint32 height;
	ArvPixelFormat pixel_format;
	guint32 default_width;
	guint32 default_height;
	ArvPixelFormat default_pixel_format;

	guint8 *packet_buffer;
	gsize packet_buffer_size;
} GstAravisSinkPrivate;

G_DEFINE_TYPE_WITH_CODE (GstAravisSink, gst_aravis_sink, GST_TYPE_BASE_SINK,
			 G_ADD_PRIVATE (GstAravisSink))

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
		GST_INFO ("%s address = %s:%d", socket_name, address_string, port);
	else
		GST_INFO ("%s address = %s", socket_name, address_string);
	g_clear_pointer (&address_string, g_free);

	socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, NULL);
	if (!G_IS_SOCKET (socket)) {
		*socket_out = NULL;
		return FALSE;
	}

	socket_address = arv_socket_bind_with_range (socket, inet_address, port, allow_reuse, &error);
	success = G_IS_INET_SOCKET_ADDRESS (socket_address);

	if (error != NULL) {
		GST_WARNING ("Failed to bind %s socket: %s", socket_name, error->message);
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
_handle_control_packet (GstAravisSinkPrivate *priv, GSocket *socket,
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
	guint32 register_value;
	gboolean write_access;
	gboolean success = FALSE;

	g_mutex_lock (&priv->camera_mutex);

	if (priv->controller_address != NULL) {
		gint64 time;
		guint64 elapsed_ms;

		time = g_get_real_time ();
		elapsed_ms = (time - priv->controller_time) / 1000;

		if (elapsed_ms > arv_fake_camera_get_heartbeat_timeout (priv->camera)) {
			g_object_unref (priv->controller_address);
			priv->controller_address = NULL;
			write_access = TRUE;
			GST_WARNING ("[AravisSink::handle_control_packet] Heartbeat timeout");
			arv_fake_camera_set_control_channel_privilege (priv->camera, 0);
		} else {
			write_access = _g_inet_socket_address_is_equal
				(G_INET_SOCKET_ADDRESS (remote_address),
				 G_INET_SOCKET_ADDRESS (priv->controller_address));
		}
	} else {
		write_access = TRUE;
	}

	packet_id = arv_gvcp_packet_get_packet_id (packet, size);
	packet_type = arv_gvcp_packet_get_packet_type (packet, size);

	if (packet_type != ARV_GVCP_PACKET_TYPE_CMD) {
		GST_WARNING ("[AravisSink::handle_control_packet] Unknown packet type");
		g_mutex_unlock (&priv->camera_mutex);
		return FALSE;
	}

	switch (g_ntohs (packet->header.command)) {
		case ARV_GVCP_COMMAND_DISCOVERY_CMD:
			ack_packet = arv_gvcp_packet_new_discovery_ack (packet_id, &ack_packet_size);
			GST_INFO ("[AravisSink::handle_control_packet] Discovery command");
			arv_fake_camera_read_memory (priv->camera, 0, ARV_GVBS_DISCOVERY_DATA_SIZE,
						     &ack_packet->data);
			break;
		case ARV_GVCP_COMMAND_READ_MEMORY_CMD:
			arv_gvcp_packet_get_read_memory_cmd_infos (packet, size, &block_address, &block_size);
			GST_INFO ("[AravisSink::handle_control_packet] Read memory command %d (%d)",
				  block_address, block_size);
			ack_packet = arv_gvcp_packet_new_read_memory_ack (block_address, block_size,
									  packet_id, &ack_packet_size);
			arv_fake_camera_read_memory (priv->camera, block_address, block_size,
						     arv_gvcp_packet_get_read_memory_ack_data (ack_packet));
			break;
		case ARV_GVCP_COMMAND_WRITE_MEMORY_CMD:
			arv_gvcp_packet_get_write_memory_cmd_infos (packet, size, &block_address, &block_size);
			if (!write_access) {
				GST_WARNING ("[AravisSink::handle_control_packet]"
					     " Ignore Write memory command %d (%d) not controller",
					     block_address, block_size);
				break;
			}

			GST_INFO ("[AravisSink::handle_control_packet] Write memory command %d (%d)",
				  block_address, block_size);
			arv_fake_camera_write_memory (priv->camera, block_address, block_size,
						      arv_gvcp_packet_get_write_memory_cmd_data (packet));
			ack_packet = arv_gvcp_packet_new_write_memory_ack (block_address, packet_id,
									   &ack_packet_size);
			break;
		case ARV_GVCP_COMMAND_READ_REGISTER_CMD:
			arv_gvcp_packet_get_read_register_cmd_infos (packet, size, &register_address);
			arv_fake_camera_read_register (priv->camera, register_address, &register_value);
			GST_INFO ("[AravisSink::handle_control_packet] Read register command %d -> %d",
				  register_address, register_value);
			ack_packet = arv_gvcp_packet_new_read_register_ack (register_value, packet_id,
									    &ack_packet_size);

			if (register_address == ARV_GVBS_CONTROL_CHANNEL_PRIVILEGE_OFFSET)
				priv->controller_time = g_get_real_time ();

			break;
		case ARV_GVCP_COMMAND_WRITE_REGISTER_CMD:
			arv_gvcp_packet_get_write_register_cmd_infos (packet, size, &register_address, &register_value);
			if (!write_access) {
				GST_WARNING ("[AravisSink::handle_control_packet]"
					     " Ignore Write register command %d (%d) not controller",
					     register_address, register_value);
				break;
			}

			arv_fake_camera_write_register (priv->camera, register_address, register_value);
			GST_INFO ("[AravisSink::handle_control_packet] Write register command %d -> %d",
				  register_address, register_value);
			ack_packet = arv_gvcp_packet_new_write_register_ack (1, packet_id,
									     &ack_packet_size);
			break;
		default:
			GST_WARNING ("[AravisSink::handle_control_packet] Unknown command");
	}

	if (priv->controller_address == NULL &&
	    arv_fake_camera_get_control_channel_privilege (priv->camera) != 0) {
		g_object_ref (remote_address);
		GST_INFO ("[AravisSink::handle_control_packet] New controller");
		priv->controller_address = remote_address;
		priv->controller_time = g_get_real_time ();
	} else if (priv->controller_address != NULL &&
		   arv_fake_camera_get_control_channel_privilege (priv->camera) == 0) {
		g_object_unref (priv->controller_address);
		GST_INFO ("[AravisSink::handle_control_packet] Controller releases");
		priv->controller_address = NULL;
		priv->controller_time = g_get_real_time ();
	}

	g_mutex_unlock (&priv->camera_mutex);

	if (ack_packet != NULL) {
		g_socket_send_to (socket, remote_address, (char *) ack_packet, ack_packet_size, NULL, NULL);
		g_free (ack_packet);

		success = TRUE;
	}

	return success;
}

static void *
_gvcp_thread (void *user_data)
{
	GstAravisSinkPrivate *priv = user_data;
	GInputVector input_vector;
	unsigned int i;

	input_vector.buffer = g_malloc0 (GST_ARAVIS_SINK_BUFFER_SIZE);
	input_vector.size = GST_ARAVIS_SINK_BUFFER_SIZE;

	while (!g_atomic_int_get (&priv->cancel)) {
		int n_events;

		n_events = g_poll (priv->socket_fds, priv->n_socket_fds, 100);
		if (n_events <= 0)
			continue;

		for (i = 0; i < ARV_SINK_N_INPUT_SOCKETS; i++) {
			GSocket *socket = priv->input_sockets[i];
			GSocketAddress *remote_address = NULL;
			int count;

			if (!G_IS_SOCKET (socket))
				continue;

			arv_gpollfd_clear_one (&priv->socket_fds[i], socket);

			count = g_socket_receive_message (socket, &remote_address,
							  &input_vector, 1, NULL, NULL,
							  NULL, NULL, NULL);
			if (count > 0)
				_handle_control_packet (priv, socket, remote_address, input_vector.buffer, count);

			g_clear_object (&remote_address);
		}
	}

	g_free (input_vector.buffer);

	return NULL;
}

static ArvPixelFormat
_pixel_format_from_string (const char *format_string)
{
	if (g_strcmp0 (format_string, "Mono8") == 0)
		return ARV_PIXEL_FORMAT_MONO_8;
	if (g_strcmp0 (format_string, "Mono16") == 0)
		return ARV_PIXEL_FORMAT_MONO_16;
	if (g_strcmp0 (format_string, "RGB8") == 0)
		return ARV_PIXEL_FORMAT_RGB_8_PACKED;

	return 0;
}

static void
_apply_default_registers (GstAravisSinkPrivate *priv)
{
	if (!ARV_IS_FAKE_CAMERA (priv->camera))
		return;

	g_mutex_lock (&priv->camera_mutex);
	arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_SENSOR_WIDTH, priv->default_width);
	arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_SENSOR_HEIGHT, priv->default_height);
	arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_WIDTH, priv->default_width);
	arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_HEIGHT, priv->default_height);
	arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_X_OFFSET, 0);
	arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_Y_OFFSET, 0);
	arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_BINNING_HORIZONTAL, 1);
	arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_BINNING_VERTICAL, 1);
	arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_PIXEL_FORMAT, priv->default_pixel_format);
	g_mutex_unlock (&priv->camera_mutex);

	priv->width = priv->default_width;
	priv->height = priv->default_height;
	priv->pixel_format = priv->default_pixel_format;
}

static gboolean
gst_aravis_sink_start (GstBaseSink *sink)
{
	GstAravisSinkPrivate *priv = gst_aravis_sink_get_instance_private (GST_ARAVIS_SINK (sink));
	ArvNetworkInterface *iface;
	GSocketAddress *socket_address;
	GInetAddress *inet_address;
	GInetAddress *gvcp_inet_address;
	unsigned int i;
	unsigned int n_socket_fds;

	priv->camera = arv_fake_camera_new_full (priv->serial_number, priv->genicam_filename);
	if (!ARV_IS_FAKE_CAMERA (priv->camera)) {
		GST_ERROR_OBJECT (sink, "Failed to initialize fake camera");
		return FALSE;
	}

	iface = arv_network_get_interface_by_address (priv->interface_name);
	if (iface == NULL)
		iface = arv_network_get_interface_by_name (priv->interface_name);
#ifdef G_OS_WIN32
	if (iface == NULL && g_strcmp0 (priv->interface_name, GST_ARAVIS_SINK_DEFAULT_INTERFACE) == 0)
		iface = arv_network_get_fake_ipv4_loopback ();
#endif
	if (iface == NULL) {
		GST_ERROR_OBJECT (sink, "No network interface with address or name '%s' found.",
				  priv->interface_name);
		g_clear_object (&priv->camera);
		return FALSE;
	}

	socket_address = g_socket_address_new_from_native (arv_network_interface_get_addr (iface),
							   sizeof (struct sockaddr));
	gvcp_inet_address = g_object_ref (g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (socket_address)));
	arv_fake_camera_set_inet_address (priv->camera, gvcp_inet_address);

	if (!_create_and_bind_input_socket (&priv->gvsp_socket, "GVSP",
					    gvcp_inet_address, 0, FALSE, TRUE)) {
		arv_network_interface_free (iface);
		g_clear_object (&gvcp_inet_address);
		g_clear_object (&priv->camera);
		return FALSE;
	}

	if (!_create_and_bind_input_socket (&priv->input_sockets[ARV_SINK_INPUT_SOCKET_GVCP],
					    "GVCP", gvcp_inet_address, ARV_GVCP_PORT, FALSE, FALSE)) {
		arv_network_interface_free (iface);
		g_clear_object (&gvcp_inet_address);
		g_clear_object (&priv->camera);
		g_clear_object (&priv->gvsp_socket);
		return FALSE;
	}

	inet_address = g_inet_address_new_from_string ("255.255.255.255");
	if (!g_inet_address_equal (gvcp_inet_address, inet_address))
		_create_and_bind_input_socket
			(&priv->input_sockets[ARV_SINK_INPUT_SOCKET_GLOBAL_DISCOVERY],
			 "Global discovery", inet_address, ARV_GVCP_PORT, TRUE, FALSE);
	g_clear_object (&inet_address);
	g_clear_object (&socket_address);

	socket_address = g_socket_address_new_from_native (arv_network_interface_get_broadaddr (iface),
							   sizeof (struct sockaddr));
	inet_address = g_object_ref (g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (socket_address)));
	if (!g_inet_address_equal (gvcp_inet_address, inet_address))
		_create_and_bind_input_socket
			(&priv->input_sockets[ARV_SINK_INPUT_SOCKET_SUBNET_DISCOVERY],
			 "Subnet discovery", inet_address, ARV_GVCP_PORT, FALSE, FALSE);
	g_clear_object (&inet_address);
	g_clear_object (&socket_address);

	g_clear_object (&gvcp_inet_address);
	arv_network_interface_free (iface);

	n_socket_fds = 0;
	for (i = 0; i < ARV_SINK_N_INPUT_SOCKETS; i++) {
		GSocket *socket = priv->input_sockets[i];

		if (G_IS_SOCKET (socket)) {
			priv->socket_fds[n_socket_fds].fd = g_socket_get_fd (socket);
			priv->socket_fds[n_socket_fds].events = G_IO_IN;
			priv->socket_fds[n_socket_fds].revents = 0;
			n_socket_fds++;
		}
	}

	priv->n_socket_fds = n_socket_fds;
	arv_gpollfd_prepare_all (priv->socket_fds, n_socket_fds);

	g_atomic_int_set (&priv->cancel, FALSE);
	priv->gvcp_thread = g_thread_new ("aravis-gvcp-sink", _gvcp_thread, priv);

	priv->packet_buffer_size = ARV_GVSP_MAXIMUM_PACKET_SIZE;
	priv->packet_buffer = g_malloc (priv->packet_buffer_size);

	_apply_default_registers (priv);

	return TRUE;
}

static gboolean
gst_aravis_sink_stop (GstBaseSink *sink)
{
	GstAravisSinkPrivate *priv = gst_aravis_sink_get_instance_private (GST_ARAVIS_SINK (sink));
	unsigned int i;

	if (priv->gvcp_thread != NULL) {
		g_atomic_int_set (&priv->cancel, TRUE);
		g_thread_join (priv->gvcp_thread);
		priv->gvcp_thread = NULL;
	}

	arv_gpollfd_finish_all (priv->socket_fds, priv->n_socket_fds);

	for (i = 0; i < ARV_SINK_N_INPUT_SOCKETS; i++)
		g_clear_object (&priv->input_sockets[i]);

	g_clear_object (&priv->gvsp_socket);
	g_clear_object (&priv->controller_address);
	g_clear_object (&priv->camera);

	g_clear_pointer (&priv->packet_buffer, g_free);
	priv->packet_buffer_size = 0;

	return TRUE;
}

static gboolean
gst_aravis_sink_set_caps (GstBaseSink *sink, GstCaps *caps)
{
	GstAravisSinkPrivate *priv = gst_aravis_sink_get_instance_private (GST_ARAVIS_SINK (sink));
	GstStructure *structure;
	ArvPixelFormat pixel_format;
	gint width = 0;
	gint height = 0;
	int depth = 0;
	int bpp = 0;
	const char *format_string;

	structure = gst_caps_get_structure (caps, 0);
	format_string = gst_structure_get_string (structure, "format");

	gst_structure_get_int (structure, "width", &width);
	gst_structure_get_int (structure, "height", &height);
	gst_structure_get_int (structure, "depth", &depth);
	gst_structure_get_int (structure, "bpp", &bpp);

	pixel_format = arv_pixel_format_from_gst_caps (gst_structure_get_name (structure),
						      format_string, bpp, depth);
	if (!pixel_format) {
		GST_ERROR_OBJECT (sink, "Unsupported caps: %" GST_PTR_FORMAT, caps);
		return FALSE;
	}

	priv->pixel_format = pixel_format;
	priv->width = width;
	priv->height = height;

	if (ARV_IS_FAKE_CAMERA (priv->camera)) {
		g_mutex_lock (&priv->camera_mutex);
		arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_SENSOR_WIDTH, width);
		arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_SENSOR_HEIGHT, height);
		arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_WIDTH, width);
		arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_HEIGHT, height);
		arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_X_OFFSET, 0);
		arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_Y_OFFSET, 0);
		arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_BINNING_HORIZONTAL, 1);
		arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_BINNING_VERTICAL, 1);
		arv_fake_camera_write_register (priv->camera, ARV_FAKE_CAMERA_REGISTER_PIXEL_FORMAT, pixel_format);
		g_mutex_unlock (&priv->camera_mutex);
	}

	return TRUE;
}

static GstFlowReturn
gst_aravis_sink_render (GstBaseSink *sink, GstBuffer *buffer)
{
	GstAravisSinkPrivate *priv = gst_aravis_sink_get_instance_private (GST_ARAVIS_SINK (sink));
	GstMapInfo map;
	GSocketAddress *stream_address = NULL;
	GInetAddress *inet_address = NULL;
	guint32 packet_size_register = 0;
	guint32 gv_packet_size;
	guint32 gv_packet_payload;
	guint16 block_id;
	size_t packet_size;
	guint64 timestamp_ns;
	ptrdiff_t offset;
	const guint8 *payload;
	gsize payload_size;
	GError *error = NULL;

	if (!ARV_IS_FAKE_CAMERA (priv->camera))
		return GST_FLOW_ERROR;

	g_mutex_lock (&priv->camera_mutex);
	if (arv_fake_camera_get_control_channel_privilege (priv->camera) == 0 ||
	    arv_fake_camera_get_acquisition_status (priv->camera) == 0) {
		g_mutex_unlock (&priv->camera_mutex);
		return GST_FLOW_OK;
	}

	stream_address = arv_fake_camera_get_stream_address (priv->camera);
	if (!G_IS_SOCKET_ADDRESS (stream_address)) {
		g_mutex_unlock (&priv->camera_mutex);
		return GST_FLOW_OK;
	}

	inet_address = g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (stream_address));
	if (g_inet_address_get_is_any (inet_address) ||
	    g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (stream_address)) == 0) {
		g_mutex_unlock (&priv->camera_mutex);
		g_object_unref (stream_address);
		return GST_FLOW_OK;
	}

	if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
		g_mutex_unlock (&priv->camera_mutex);
		g_object_unref (stream_address);
		return GST_FLOW_ERROR;
	}

	payload = map.data;
	payload_size = map.size;

	arv_fake_camera_read_register (priv->camera, ARV_GVBS_STREAM_CHANNEL_0_PACKET_SIZE_OFFSET,
				       &packet_size_register);
	g_mutex_unlock (&priv->camera_mutex);
	gv_packet_size = (packet_size_register >> ARV_GVBS_STREAM_CHANNEL_0_PACKET_SIZE_POS) &
		ARV_GVBS_STREAM_CHANNEL_0_PACKET_SIZE_MASK;
	if (gv_packet_size == 0)
		gv_packet_size = 1400;
	gv_packet_size = CLAMP (gv_packet_size, ARV_GVSP_MINIMUM_PACKET_SIZE, ARV_GVSP_MAXIMUM_PACKET_SIZE);

	gv_packet_payload = gv_packet_size - ARV_GVSP_PACKET_PROTOCOL_OVERHEAD (FALSE);

	priv->frame_id = (priv->frame_id + 1) % 65536;
	if (priv->frame_id == 0)
		priv->frame_id = 1;

	if (GST_BUFFER_PTS_IS_VALID (buffer))
		timestamp_ns = GST_BUFFER_PTS (buffer);
	else
		timestamp_ns = (guint64) g_get_real_time () * 1000;

	block_id = 0;

	arv_gvsp_packet_new_image_leader (priv->frame_id, block_id, timestamp_ns, priv->pixel_format,
					  priv->width, priv->height, 0, 0, 0, 0,
					  priv->packet_buffer, priv->packet_buffer_size,
					  &packet_size);

	g_socket_send_to (priv->gvsp_socket, stream_address,
			  (const char *) priv->packet_buffer, packet_size, NULL, &error);
	g_clear_error (&error);

	block_id++;
	offset = 0;
	while (offset < (ptrdiff_t) payload_size) {
		size_t data_size = MIN (gv_packet_payload, payload_size - offset);

		arv_gvsp_packet_new_payload (priv->frame_id, block_id, data_size,
					     (guint8 *) payload + offset,
					     priv->packet_buffer, priv->packet_buffer_size,
					     &packet_size);

		g_socket_send_to (priv->gvsp_socket, stream_address,
				  (const char *) priv->packet_buffer, packet_size, NULL, &error);
		g_clear_error (&error);

		offset += data_size;
		block_id++;
	}

	arv_gvsp_packet_new_data_trailer (priv->frame_id, block_id, priv->height,
					  priv->packet_buffer, priv->packet_buffer_size,
					  &packet_size);

	g_socket_send_to (priv->gvsp_socket, stream_address,
			  (const char *) priv->packet_buffer, packet_size, NULL, &error);
	g_clear_error (&error);

	gst_buffer_unmap (buffer, &map);
	g_object_unref (stream_address);

	return GST_FLOW_OK;
}

static void
gst_aravis_sink_finalize (GObject *object)
{
	GstAravisSinkPrivate *priv = gst_aravis_sink_get_instance_private (GST_ARAVIS_SINK (object));

	g_mutex_clear (&priv->camera_mutex);
	g_clear_pointer (&priv->interface_name, g_free);
	g_clear_pointer (&priv->serial_number, g_free);
	g_clear_pointer (&priv->genicam_filename, g_free);

	G_OBJECT_CLASS (gst_aravis_sink_parent_class)->finalize (object);
}

static void
gst_aravis_sink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GstAravisSinkPrivate *priv = gst_aravis_sink_get_instance_private (GST_ARAVIS_SINK (object));
	ArvPixelFormat pixel_format;

	switch (prop_id) {
		case PROP_INTERFACE_NAME:
			g_free (priv->interface_name);
			priv->interface_name = g_value_dup_string (value);
			break;
		case PROP_SERIAL_NUMBER:
			g_free (priv->serial_number);
			priv->serial_number = g_value_dup_string (value);
			break;
		case PROP_GENICAM_FILENAME:
			g_free (priv->genicam_filename);
			priv->genicam_filename = g_value_dup_string (value);
			break;
		case PROP_DEFAULT_WIDTH:
			priv->default_width = g_value_get_uint (value);
			_apply_default_registers (priv);
			break;
		case PROP_DEFAULT_HEIGHT:
			priv->default_height = g_value_get_uint (value);
			_apply_default_registers (priv);
			break;
		case PROP_DEFAULT_PIXEL_FORMAT:
			pixel_format = _pixel_format_from_string (g_value_get_string (value));
			if (pixel_format != 0) {
				priv->default_pixel_format = pixel_format;
				_apply_default_registers (priv);
			} else {
				GST_WARNING_OBJECT (object, "Unsupported default-pixel-format");
			}
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gst_aravis_sink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstAravisSinkPrivate *priv = gst_aravis_sink_get_instance_private (GST_ARAVIS_SINK (object));

	switch (prop_id) {
		case PROP_INTERFACE_NAME:
			g_value_set_string (value, priv->interface_name);
			break;
		case PROP_SERIAL_NUMBER:
			g_value_set_string (value, priv->serial_number);
			break;
		case PROP_GENICAM_FILENAME:
			g_value_set_string (value, priv->genicam_filename);
			break;
		case PROP_DEFAULT_WIDTH:
			g_value_set_uint (value, priv->default_width);
			break;
		case PROP_DEFAULT_HEIGHT:
			g_value_set_uint (value, priv->default_height);
			break;
		case PROP_DEFAULT_PIXEL_FORMAT:
			switch (priv->default_pixel_format) {
				case ARV_PIXEL_FORMAT_MONO_8:
					g_value_set_string (value, "Mono8");
					break;
				case ARV_PIXEL_FORMAT_MONO_16:
					g_value_set_string (value, "Mono16");
					break;
				case ARV_PIXEL_FORMAT_RGB_8_PACKED:
					g_value_set_string (value, "RGB8");
					break;
				default:
					g_value_set_string (value, "Mono8");
					break;
			}
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gst_aravis_sink_class_init (GstAravisSinkClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
	GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS (klass);
	GstCaps *caps;

	gobject_class->set_property = gst_aravis_sink_set_property;
	gobject_class->get_property = gst_aravis_sink_get_property;
	gobject_class->finalize = gst_aravis_sink_finalize;

	g_object_class_install_property (gobject_class,
					 PROP_INTERFACE_NAME,
					 g_param_spec_string ("interface",
							      "Interface name",
							      "Interface name or IP address to listen on",
							      GST_ARAVIS_SINK_DEFAULT_INTERFACE,
							      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (gobject_class,
					 PROP_SERIAL_NUMBER,
					 g_param_spec_string ("serial",
							      "Serial number",
							      "Device serial number",
							      GST_ARAVIS_SINK_DEFAULT_SERIAL,
							      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (gobject_class,
					 PROP_GENICAM_FILENAME,
					 g_param_spec_string ("genicam",
							      "GenICam XML",
							      "GenICam XML file to expose",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (gobject_class,
					 PROP_DEFAULT_WIDTH,
					 g_param_spec_uint ("default-width",
							    "Default Width",
							    "Default width before caps negotiation",
							    1, G_MAXUINT, 640,
							    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (gobject_class,
					 PROP_DEFAULT_HEIGHT,
					 g_param_spec_uint ("default-height",
							    "Default Height",
							    "Default height before caps negotiation",
							    1, G_MAXUINT, 480,
							    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (gobject_class,
					 PROP_DEFAULT_PIXEL_FORMAT,
					 g_param_spec_string ("default-pixel-format",
							      "Default Pixel Format",
							      "Default pixel format (Mono8, Mono16, RGB8)",
							      "Mono16",
							      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	gst_element_class_set_details_simple (element_class,
					      "Aravis Video Sink",
					      "Sink/Video",
					      "Aravis GVCP/GVSP sink (camera simulator)",
					      "Emmanuel Pacaud <emmanuel.pacaud@free.fr>");

	caps = gst_caps_from_string ("video/x-raw, "
				     "format=(string){GRAY8,GRAY16_LE,RGB}, "
				     "width=(int)[1,MAX], height=(int)[1,MAX]");
	gst_element_class_add_pad_template (element_class,
					    gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
	gst_caps_unref (caps);

	basesink_class->start = GST_DEBUG_FUNCPTR (gst_aravis_sink_start);
	basesink_class->stop = GST_DEBUG_FUNCPTR (gst_aravis_sink_stop);
	basesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_aravis_sink_set_caps);
	basesink_class->render = GST_DEBUG_FUNCPTR (gst_aravis_sink_render);

	GST_DEBUG_CATEGORY_INIT (aravissink_debug, "aravissink", 0, "Aravis sink");
}

static void
gst_aravis_sink_init (GstAravisSink *sink)
{
	GstAravisSinkPrivate *priv = gst_aravis_sink_get_instance_private (sink);

	priv->interface_name = g_strdup (GST_ARAVIS_SINK_DEFAULT_INTERFACE);
	priv->serial_number = g_strdup (GST_ARAVIS_SINK_DEFAULT_SERIAL);
	priv->genicam_filename = NULL;
	priv->frame_id = 0;
	priv->pixel_format = ARV_PIXEL_FORMAT_MONO_8;
	priv->default_width = 640;
	priv->default_height = 480;
	priv->default_pixel_format = ARV_PIXEL_FORMAT_MONO_16;
	g_mutex_init (&priv->camera_mutex);
}
