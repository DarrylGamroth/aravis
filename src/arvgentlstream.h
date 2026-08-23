/* Aravis - Digital camera library
 *
 * Copyright © 2009-2025 Emmanuel Pacaud <emmanuel.pacaud@free.fr>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Xiaoqiang Wang <xiaoqiang.wang@psi.ch>
 */

#ifndef ARV_GENTL_STREAM_H
#define ARV_GENTL_STREAM_H

#if !defined (ARV_H_INSIDE) && !defined (ARAVIS_COMPILATION)
#error "Only <arv.h> can be included directly."
#endif

#include <arvapi.h>
#include <arvtypes.h>
#include <arvstream.h>

G_BEGIN_DECLS

#define ARV_TYPE_GENTL_STREAM (arv_gentl_stream_get_type ())
ARV_API G_DECLARE_FINAL_TYPE (ArvGenTLStream, arv_gentl_stream, ARV, GENTL_STREAM, ArvStream)

typedef enum {
	ARV_GENTL_STREAM_POLL_ERROR = -1,
	ARV_GENTL_STREAM_POLL_EMPTY = 0,
	ARV_GENTL_STREAM_POLL_BUFFER = 1
} ArvGenTLStreamPollResult;

ARV_API gboolean	arv_gentl_stream_set_caller_polling	(ArvGenTLStream *stream,
							 gboolean enabled,
							 GError **error);
ARV_API gboolean	arv_gentl_stream_prepare_buffer		(ArvGenTLStream *stream,
							 ArvBuffer *buffer,
							 GError **error);
ARV_API gboolean	arv_gentl_stream_queue_buffer		(ArvGenTLStream *stream,
							 ArvBuffer *buffer,
							 GError **error);
ARV_API ArvGenTLStreamPollResult
			arv_gentl_stream_poll_buffer		(ArvGenTLStream *stream,
							 ArvBuffer **buffer,
							 GError **error);

G_END_DECLS

#endif
