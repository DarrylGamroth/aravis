/* Aravis - Digital camera library
 *
 * Copyright © 2025
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "arvgvcpproxy-backend.h"

#include <arvfakecamera.h>
#include <arvdebug.h>
#include <string.h>

typedef struct {
	ArvGvcpProxyBackend base;
	ArvFakeCamera *camera;
} ArvGvcpProxyBackendFake;

static gboolean
_fake_read_memory (ArvGvcpProxyBackend *backend, guint32 address, guint32 size, void *buffer)
{
	ArvGvcpProxyBackendFake *fake = (ArvGvcpProxyBackendFake *) backend;

	return arv_fake_camera_read_memory (fake->camera, address, size, buffer);
}

static gboolean
_fake_write_memory (ArvGvcpProxyBackend *backend, guint32 address, guint32 size, const void *buffer)
{
	ArvGvcpProxyBackendFake *fake = (ArvGvcpProxyBackendFake *) backend;

	return arv_fake_camera_write_memory (fake->camera, address, size, buffer);
}

static gboolean
_fake_read_register (ArvGvcpProxyBackend *backend, guint32 address, guint32 *value)
{
	ArvGvcpProxyBackendFake *fake = (ArvGvcpProxyBackendFake *) backend;

	return arv_fake_camera_read_register (fake->camera, address, value);
}

static gboolean
_fake_write_register (ArvGvcpProxyBackend *backend, guint32 address, guint32 value)
{
	ArvGvcpProxyBackendFake *fake = (ArvGvcpProxyBackendFake *) backend;

	return arv_fake_camera_write_register (fake->camera, address, value);
}

static void
_fake_set_inet_address (ArvGvcpProxyBackend *backend, GInetAddress *address)
{
	ArvGvcpProxyBackendFake *fake = (ArvGvcpProxyBackendFake *) backend;

	arv_fake_camera_set_inet_address (fake->camera, address);
}

static void
_fake_destroy (ArvGvcpProxyBackend *backend)
{
	ArvGvcpProxyBackendFake *fake = (ArvGvcpProxyBackendFake *) backend;

	g_clear_object (&fake->camera);
	g_free (fake);
}

static const ArvGvcpProxyBackendVTable fake_vtable = {
	.read_memory = _fake_read_memory,
	.write_memory = _fake_write_memory,
	.read_register = _fake_read_register,
	.write_register = _fake_write_register,
	.set_inet_address = _fake_set_inet_address,
	.destroy = _fake_destroy
};

static ArvGvcpProxyBackend *
_fake_backend_new (const char *serial_number, const char *genicam_filename, GError **error)
{
	ArvGvcpProxyBackendFake *fake;

	fake = g_new0 (ArvGvcpProxyBackendFake, 1);
	fake->base.vtable = &fake_vtable;
	fake->base.name = "fake";
	fake->camera = arv_fake_camera_new_full (serial_number, genicam_filename);

	if (!ARV_IS_FAKE_CAMERA (fake->camera)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to initialize fake backend");
		g_free (fake);
		return NULL;
	}

	return (ArvGvcpProxyBackend *) fake;
}

ArvGvcpProxyBackend *
arv_gvcp_proxy_backend_new (const char *name,
			    const char *serial_number,
			    const char *genicam_filename,
			    GError **error)
{
	if (name == NULL || strcmp (name, "fake") == 0 || strcmp (name, "memory") == 0)
		return _fake_backend_new (serial_number, genicam_filename, error);

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		     "Unknown backend '%s' (supported: fake, memory)", name);
	return NULL;
}

void
arv_gvcp_proxy_backend_free (ArvGvcpProxyBackend *backend)
{
	if (backend == NULL || backend->vtable == NULL || backend->vtable->destroy == NULL)
		return;

	backend->vtable->destroy (backend);
}
