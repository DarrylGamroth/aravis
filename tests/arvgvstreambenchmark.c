/* SPDX-License-Identifier:Unlicense */

#include <arv.h>
#include <glib.h>
#include <string.h>

static gint n_frames = 2000;
static gint n_warmup_frames = 200;
static gint n_buffers = 32;
static gint width = 1024;
static gint height = 1024;
static gint packet_size = 8192;
static gint64 packet_delay_ns = 10000;
static double frame_rate = 100.0;
static gboolean no_packet_socket = FALSE;
static gint packet_socket_block_size = 1 << 21;
static gint packet_socket_block_count = 16;
static gint packet_socket_block_timeout_ms = 5;

static void
fill_constant_pattern (ArvBuffer *buffer, void *user_data, guint32 exposure_time_us,
		       guint32 gain, ArvPixelFormat pixel_format)
{
	gsize size;
	void *data = (void *) arv_buffer_get_data (buffer, &size);

	(void) user_data;
	(void) exposure_time_us;
	(void) gain;
	(void) pixel_format;
	memset (data, 0xa5, size);
}

static void
check_no_error (GError **error, const char *operation)
{
	if (*error != NULL)
		g_error ("%s: %s", operation, (*error)->message);
}

int
main (int argc, char **argv)
{
	GOptionEntry entries[] = {
		{ "frames", 'n', 0, G_OPTION_ARG_INT, &n_frames, "Measured frames", "N" },
		{ "warmup", 'w', 0, G_OPTION_ARG_INT, &n_warmup_frames, "Warmup frames", "N" },
		{ "buffers", 'b', 0, G_OPTION_ARG_INT, &n_buffers, "Announced image buffers", "N" },
		{ "width", 0, 0, G_OPTION_ARG_INT, &width, "Image width", "PIXELS" },
		{ "height", 0, 0, G_OPTION_ARG_INT, &height, "Image height", "PIXELS" },
		{ "packet-size", 0, 0, G_OPTION_ARG_INT, &packet_size, "GVSP packet size", "BYTES" },
		{ "packet-delay", 0, 0, G_OPTION_ARG_INT64, &packet_delay_ns,
		  "Delay between GVSP packets", "NANOSECONDS" },
		{ "frame-rate", 0, 0, G_OPTION_ARG_DOUBLE, &frame_rate, "Camera frame rate", "HZ" },
		{ "no-packet-socket", 0, 0, G_OPTION_ARG_NONE, &no_packet_socket,
		  "Force the standard UDP socket backend", NULL },
		{ "packet-socket-block-size", 0, 0, G_OPTION_ARG_INT, &packet_socket_block_size,
		  "TPACKET_V3 block size", "BYTES" },
		{ "packet-socket-block-count", 0, 0, G_OPTION_ARG_INT, &packet_socket_block_count,
		  "TPACKET_V3 ring block count", "N" },
		{ "packet-socket-block-timeout", 0, 0, G_OPTION_ARG_INT, &packet_socket_block_timeout_ms,
		  "TPACKET_V3 block retirement timeout", "MILLISECONDS" },
		{ NULL }
	};
	GOptionContext *context;
	ArvGvFakeCamera *simulator;
	ArvCamera *camera;
	ArvStream *stream;
	GError *error = NULL;
	gint64 start_us = 0;
	gint64 stop_us;
	gsize payload;
	guint64 completed;
	guint64 failures;
	guint64 underruns;
	guint64 packet_socket_active;
	guint64 packet_socket_ring_size;
	guint64 packet_socket_blocks;
	guint64 packet_socket_polls;
	guint64 packet_socket_poll_timeouts;
	guint64 packet_socket_packets;
	guint64 packet_socket_drops;
	guint64 packet_socket_freezes;
	guint64 packet_socket_malformed_packets;
	guint frame_index;
	guint i;

	context = g_option_context_new ("- profile the Aravis GVSP receive path on loopback");
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_parse (context, &argc, &argv, &error);
	check_no_error (&error, "Failed to parse options");

	if (n_frames <= 0 || n_warmup_frames < 0 || n_buffers <= 0 || width <= 0 || height <= 0 ||
	    packet_size <= 0 || packet_delay_ns < 0 || frame_rate <= 0.0 ||
	    packet_socket_block_size <= 0 || packet_socket_block_count <= 0 ||
	    packet_socket_block_timeout_ms < 0)
		g_error ("Frames, buffers, dimensions, packet size, and frame rate must be positive");

	/* Do not enumerate installed GenTL producers or physical interfaces. */
	arv_select_interface ("GigEVision");
	arv_set_fake_camera_genicam_filename (GENICAM_FILENAME);
	arv_gv_interface_set_discovery_interface_name ("lo");

	simulator = arv_gv_fake_camera_new ("127.0.0.1", "GVPerf");
	g_assert_true (ARV_IS_GV_FAKE_CAMERA (simulator));
	arv_fake_camera_set_fill_pattern (arv_gv_fake_camera_get_fake_camera (simulator),
					  fill_constant_pattern, NULL, NULL);
	camera = arv_camera_new ("Aravis-GVPerf", &error);
	check_no_error (&error, "Failed to open the fake GV camera");
	g_assert_true (ARV_IS_CAMERA (camera));

	arv_camera_set_region (camera, 0, 0, width, height, &error);
	check_no_error (&error, "Failed to set image dimensions");
	arv_camera_set_frame_rate (camera, frame_rate, &error);
	check_no_error (&error, "Failed to set frame rate");
	arv_camera_gv_set_packet_size (camera, packet_size, &error);
	check_no_error (&error, "Failed to set packet size");
	arv_camera_gv_set_packet_delay (camera, packet_delay_ns, &error);
	check_no_error (&error, "Failed to set packet delay");
	if (no_packet_socket)
		arv_camera_gv_set_stream_options (camera, ARV_GV_STREAM_OPTION_PACKET_SOCKET_DISABLED);

	stream = arv_camera_create_stream (camera, NULL, NULL, NULL, &error);
	check_no_error (&error, "Failed to create the stream");
	g_assert_true (ARV_IS_STREAM (stream));
	g_object_set (stream,
		      "socket-buffer", ARV_GV_STREAM_SOCKET_BUFFER_AUTO,
		      "packet-socket-block-size", (guint) packet_socket_block_size,
		      "packet-socket-block-count", (guint) packet_socket_block_count,
		      "packet-socket-block-timeout", (guint) packet_socket_block_timeout_ms,
		      NULL);
	payload = arv_camera_get_payload (camera, &error);
	check_no_error (&error, "Failed to query payload size");

	for (i = 0; i < (guint) n_buffers; i++)
		arv_stream_push_buffer (stream, arv_buffer_new (payload, NULL));

	arv_camera_start_acquisition (camera, &error);
	check_no_error (&error, "Failed to start acquisition");

	for (frame_index = 0; frame_index < (guint) (n_warmup_frames + n_frames); frame_index++) {
		ArvBuffer *buffer = arv_stream_timeout_pop_buffer (stream, 5 * G_TIME_SPAN_SECOND);

		if (buffer == NULL)
			g_error ("Timed out waiting for frame %u", frame_index);
		if (arv_buffer_get_status (buffer) != ARV_BUFFER_STATUS_SUCCESS ||
		    arv_buffer_get_data (buffer, NULL) == NULL ||
		    arv_buffer_get_image_width (buffer) != (guint) width ||
		    arv_buffer_get_image_height (buffer) != (guint) height)
			g_error ("Frame %u failed the correctness check (status %u)",
				 frame_index, arv_buffer_get_status (buffer));
		if (frame_index == (guint) n_warmup_frames)
			start_us = g_get_monotonic_time ();
		arv_stream_push_buffer (stream, buffer);
	}

	stop_us = g_get_monotonic_time ();
	arv_camera_stop_acquisition (camera, &error);
	check_no_error (&error, "Failed to stop acquisition");
	arv_stream_get_statistics (stream, &completed, &failures, &underruns);
	packet_socket_active = arv_stream_get_info_uint64_by_name (stream, "packet_socket_active");
	packet_socket_ring_size = arv_stream_get_info_uint64_by_name (stream, "packet_socket_ring_size");
	packet_socket_blocks = arv_stream_get_info_uint64_by_name (stream, "n_packet_socket_blocks");
	packet_socket_polls = arv_stream_get_info_uint64_by_name (stream, "n_packet_socket_polls");
	packet_socket_poll_timeouts =
		arv_stream_get_info_uint64_by_name (stream, "n_packet_socket_poll_timeouts");
	packet_socket_packets = arv_stream_get_info_uint64_by_name (stream, "n_packet_socket_packets");
	packet_socket_drops = arv_stream_get_info_uint64_by_name (stream, "n_packet_socket_drops");
	packet_socket_freezes = arv_stream_get_info_uint64_by_name (stream, "n_packet_socket_freezes");
	packet_socket_malformed_packets =
		arv_stream_get_info_uint64_by_name (stream, "n_packet_socket_malformed_packets");

	g_print ("backend_requested=%s frames=%d warmup_frames=%d payload_bytes=%" G_GSIZE_FORMAT
		 " elapsed_seconds=%.6f frames_per_second=%.3f completed=%" G_GUINT64_FORMAT
		 " failures=%" G_GUINT64_FORMAT " underruns=%" G_GUINT64_FORMAT
		 " packet_socket_active=%" G_GUINT64_FORMAT
		 " packet_socket_ring_size=%" G_GUINT64_FORMAT
		 " packet_socket_blocks=%" G_GUINT64_FORMAT
		 " packet_socket_polls=%" G_GUINT64_FORMAT
		 " packet_socket_poll_timeouts=%" G_GUINT64_FORMAT
		 " packet_socket_packets=%" G_GUINT64_FORMAT
		 " packet_socket_drops=%" G_GUINT64_FORMAT
		 " packet_socket_freezes=%" G_GUINT64_FORMAT
		 " packet_socket_malformed_packets=%" G_GUINT64_FORMAT "\n",
		 no_packet_socket ? "udp" : "auto", n_frames, n_warmup_frames, payload,
		 (stop_us - start_us) / 1000000.0,
		 n_frames * 1000000.0 / (stop_us - start_us),
		 completed, failures, underruns, packet_socket_active, packet_socket_ring_size,
		 packet_socket_blocks, packet_socket_polls, packet_socket_poll_timeouts,
		 packet_socket_packets, packet_socket_drops, packet_socket_freezes,
		 packet_socket_malformed_packets);
	g_assert_cmpuint (failures, ==, 0);
	g_assert_cmpuint (underruns, ==, 0);

	g_object_unref (stream);
	g_object_unref (camera);
	g_object_unref (simulator);
	g_option_context_free (context);
	arv_shutdown ();

	return EXIT_SUCCESS;
}
