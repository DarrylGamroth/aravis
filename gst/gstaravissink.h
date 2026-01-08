/* Aravis - Digital camera library
 *
 * Copyright © 2025
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ARV_GST_SINK_H
#define ARV_GST_SINK_H

#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS

#define GST_TYPE_ARAVIS_SINK		(gst_aravis_sink_get_type())
#define GST_ARAVIS_SINK(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ARAVIS_SINK,GstAravisSink))
#define GST_ARAVIS_SINK_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ARAVIS_SINK,GstAravisSinkClass))
#define GST_IS_ARAVIS_SINK(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ARAVIS_SINK))
#define GST_IS_ARAVIS_SINK_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ARAVIS_SINK))

typedef struct _GstAravisSink GstAravisSink;
typedef struct _GstAravisSinkClass GstAravisSinkClass;

struct _GstAravisSink {
	GstBaseSink element;
};

struct _GstAravisSinkClass {
	GstBaseSinkClass parent_class;
};

GType gst_aravis_sink_get_type (void);

G_END_DECLS

#endif
