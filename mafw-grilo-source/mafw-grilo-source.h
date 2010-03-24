/*
 * Copyright (C) 2010 Igalia S.L.
 *
 * Contact: Xabier Rodríguez Calvar <xrcalvar@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <libmafw/mafw-source.h>

#ifndef MAFW_GRILO_SOURCE_H
#define MAFW_GRILO_SOURCE_H

G_BEGIN_DECLS

#define MAFW_GRILO_SOURCE_EXTENSION_NAME "mafw-grilo-source"

#define MAFW_TYPE_GRILO_SOURCE			\
	(mafw_grilo_source_get_type ())

#define MAFW_GRILO_SOURCE(obj)						\
	(G_TYPE_CHECK_INSTANCE_CAST ((obj), MAFW_TYPE_GRILO_SOURCE,	\
				     MafwGriloSource))
#define MAFW_IS_GRILO_SOURCE(obj)					\
	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), MAFW_TYPE_GRILO_SOURCE))

#define MAFW_GRILO_SOURCE_CLASS(klass)					\
	(G_TYPE_CHECK_CLASS_CAST((klass), MAFW_TYPE_GRILO_SOURCE,	\
				 MafwGriloSourceClass))

#define MAFW_IS_GRILO_SOURCE_CLASS(klass)				\
	(G_TYPE_CHECK_CLASS_TYPE((klass), MAFW_TYPE_GRILO_SOURCE))

#define MAFW_GRILO_SOURCE_GET_CLASS(obj)				\
	(G_TYPE_INSTANCE_GET_CLASS ((obj), MAFW_TYPE_GRILO_SOURCE,	\
				    MafwGriloSourceClass))

typedef struct _MafwGriloSource MafwGriloSource;
typedef struct _MafwGriloSourceClass MafwGriloSourceClass;
typedef struct _MafwGriloSourcePrivate MafwGriloSourcePrivate;

struct _MafwGriloSource {
	MafwSource parent;
	MafwGriloSourcePrivate *priv;
};

struct _MafwGriloSourceClass {
	MafwSourceClass parent_class;
};

GType mafw_grilo_source_get_type(void);

G_END_DECLS

#endif /* MAFW_GRILO_SOURCE_H */
