/* SPDX-License-Identifier:Unlicense */

#include <arv.h>
#include <stdio.h>
#include <stdlib.h>

#define N_BUFFERS 8
#define N_FRAMES 16
#define TIMEOUT_US (10 * G_USEC_PER_SEC)

static gboolean
report_error (const char *operation, GError **error)
{
	fprintf (stderr, "%s failed%s%s\n", operation,
		 *error != NULL ? ": " : "", *error != NULL ? (*error)->message : "");
	g_clear_error (error);
	return FALSE;
}

int
main (int argc, char **argv)
{
	ArvBuffer *buffers[N_BUFFERS] = {NULL};
	ArvCamera *camera = NULL;
	ArvStream *stream = NULL;
	GError *error = NULL;
	gint64 deadline;
	guint payload;
	guint completed = 0;
	guint64 empty_polls = 0;
	gboolean started = FALSE;
	gboolean success = FALSE;

	camera = arv_camera_new (argc > 1 ? argv[1] : NULL, &error);
	if (camera == NULL) {
		report_error ("Opening camera", &error);
		goto out;
	}

	stream = arv_camera_create_stream (camera, NULL, NULL, NULL, &error);
	if (stream == NULL) {
		report_error ("Creating stream", &error);
		goto out;
	}
	if (!ARV_IS_GENTL_STREAM (stream)) {
		fprintf (stderr, "Selected camera does not use the Aravis GenTL consumer\n");
		goto out;
	}
	if (!arv_gentl_stream_set_caller_polling (ARV_GENTL_STREAM (stream), TRUE, &error)) {
		report_error ("Selecting caller polling", &error);
		goto out;
	}

	payload = arv_camera_get_payload (camera, &error);
	if (payload == 0 || error != NULL) {
		report_error ("Reading payload size", &error);
		goto out;
	}

	for (guint i = 0; i < N_BUFFERS; i++) {
		void *storage = g_malloc (payload);

		buffers[i] = arv_buffer_new_full (payload, storage, storage, g_free);
		if (!arv_gentl_stream_prepare_buffer (ARV_GENTL_STREAM (stream), buffers[i], &error) ||
		    !arv_gentl_stream_queue_buffer (ARV_GENTL_STREAM (stream), buffers[i], &error)) {
			report_error ("Preparing camera buffer", &error);
			goto out;
		}
	}

	if (!arv_camera_start_acquisition (camera, &error)) {
		report_error ("Starting acquisition", &error);
		goto out;
	}
	started = TRUE;
	deadline = g_get_monotonic_time () + TIMEOUT_US;

	while (completed < N_FRAMES) {
		ArvBuffer *buffer;
		ArvGenTLStreamPollResult result;

		result = arv_gentl_stream_poll_buffer (ARV_GENTL_STREAM (stream), &buffer, &error);
		if (result == ARV_GENTL_STREAM_POLL_EMPTY) {
			empty_polls++;
			if ((empty_polls & 0xffff) == 0 && g_get_monotonic_time () >= deadline) {
				fprintf (stderr, "Timed out waiting for frame %u\n", completed + 1);
				goto out;
			}
			continue;
		}
		if (result == ARV_GENTL_STREAM_POLL_ERROR) {
			report_error ("Polling camera buffer", &error);
			goto out;
		}
		if (arv_buffer_get_status (buffer) != ARV_BUFFER_STATUS_SUCCESS) {
			fprintf (stderr, "Frame %u completed with status %d\n",
				 completed + 1, arv_buffer_get_status (buffer));
			goto out;
		}
		completed++;
		if (!arv_gentl_stream_queue_buffer (ARV_GENTL_STREAM (stream), buffer, &error)) {
			report_error ("Recycling camera buffer", &error);
			goto out;
		}
	}

	success = TRUE;

out:
	if (started && !arv_camera_stop_acquisition (camera, &error)) {
		report_error ("Stopping acquisition", &error);
		success = FALSE;
	}
	for (guint i = 0; i < N_BUFFERS; i++)
		g_clear_object (&buffers[i]);
	g_clear_object (&stream);
	g_clear_object (&camera);

	if (success)
		printf ("Received %u caller-polled frames after %" G_GUINT64_FORMAT " empty polls\n",
			completed, empty_polls);

	return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
