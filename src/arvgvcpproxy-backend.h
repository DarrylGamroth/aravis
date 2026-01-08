/* Aravis - Digital camera library
 *
 * Copyright © 2025
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ARV_GVCP_PROXY_BACKEND_H
#define ARV_GVCP_PROXY_BACKEND_H

#include <arvapi.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _ArvGvcpProxyBackend ArvGvcpProxyBackend;

typedef struct {
	gboolean (*read_memory) (ArvGvcpProxyBackend *backend, guint32 address, guint32 size, void *buffer);
	gboolean (*write_memory) (ArvGvcpProxyBackend *backend, guint32 address, guint32 size, const void *buffer);
	gboolean (*read_register) (ArvGvcpProxyBackend *backend, guint32 address, guint32 *value);
	gboolean (*write_register) (ArvGvcpProxyBackend *backend, guint32 address, guint32 value);
	void (*set_inet_address) (ArvGvcpProxyBackend *backend, GInetAddress *address);
	void (*destroy) (ArvGvcpProxyBackend *backend);
} ArvGvcpProxyBackendVTable;

struct _ArvGvcpProxyBackend {
	const ArvGvcpProxyBackendVTable *vtable;
	const char *name;
};

ARV_API ArvGvcpProxyBackend *arv_gvcp_proxy_backend_new (const char *name,
							 const char *serial_number,
							 const char *genicam_filename,
							 GError **error);
ARV_API void arv_gvcp_proxy_backend_free (ArvGvcpProxyBackend *backend);

G_END_DECLS

#endif
