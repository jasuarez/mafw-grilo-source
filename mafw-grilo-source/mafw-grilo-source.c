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

#include "config.h"

#include <glib.h>
#include <gmodule.h>

#include <libmafw/mafw.h>

#include "mafw-grilo-source.h"

#define MAFW_GRILO_SOURCE_PLUGIN_NAME "MAFW-Grilo-Source"
#define MAFW_GRILO_SOURCE_NAME "MAFW Grilo source"
#define MAFW_GRILO_SOURCE_UUID "grilo"


G_DEFINE_TYPE(MafwGriloSource, mafw_grilo_source, MAFW_TYPE_SOURCE);

#define MAFW_GRILO_SOURCE_GET_PRIVATE(object)				\
  (G_TYPE_INSTANCE_GET_PRIVATE ((object), MAFW_TYPE_GRILO_SOURCE,	\
                                MafwGriloSourcePrivate))

struct _MafwGriloSourcePrivate {
  gint foo;
};

typedef struct {
  MafwRegistry* registry;
  guint next_browse_id;
} MafwGriloSourcePlugin;

static MafwGriloSourcePlugin plugin = { NULL, 0 };

static void mafw_grilo_source_init(MafwGriloSource* self);
static void mafw_grilo_source_class_init(MafwGriloSourceClass* klass);

static guint mafw_grilo_source_browse(MafwSource *source,
                                      const gchar *object_id,
                                      gboolean recursive,
                                      const MafwFilter *filter,
                                      const gchar *sort_criteria,
                                      const gchar *const *metadata_keys,
                                      guint skip_count,
                                      guint item_count,
                                      MafwSourceBrowseResultCb browse_cb,
                                      gpointer user_data);
static gboolean mafw_grilo_source_cancel_browse(MafwSource *source,
                                                guint browse_id,
                                                GError **error);

static void mafw_grilo_source_get_metadata(MafwSource *source,
                                           const gchar *object_id,
                                           const gchar *const *metadata_keys,
                                           MafwSourceMetadataResultCb cb,
                                           gpointer user_data);

G_MODULE_EXPORT MafwPluginDescriptor mafw_grilo_source_plugin_description = {
  { .name = MAFW_GRILO_SOURCE_PLUGIN_NAME },
  .initialize = mafw_grilo_source_initialize,
  .deinitialize = mafw_grilo_source_deinitialize,
};

gboolean mafw_grilo_source_initialize(MafwRegistry *registry,
                                      GError **error)
{
  MafwGriloSource *source;

  g_debug("Mafw Grilo plugin initializing");

  g_assert (!plugin.registry);
  plugin.registry = g_object_ref(registry);

  source = mafw_grilo_source_new();
  mafw_registry_add_extension(registry, MAFW_EXTENSION(source));

  return TRUE;
}

void mafw_grilo_source_deinitialize(GError **error)
{
  g_assert (plugin.registry);
  g_object_unref (plugin.registry);
  plugin.registry = NULL;
}

static void mafw_grilo_source_init(MafwGriloSource *self)
{
  MafwGriloSourcePrivate *priv = NULL;

  g_return_if_fail(MAFW_IS_GRILO_SOURCE(self));
  priv = self->priv = MAFW_GRILO_SOURCE_GET_PRIVATE(self);
}

static void mafw_grilo_source_class_init(MafwGriloSourceClass *klass)
{
  GObjectClass *gobject_class;
  MafwSourceClass *source_class;

  g_return_if_fail(klass != NULL);

  gobject_class = G_OBJECT_CLASS(klass);
  source_class = MAFW_SOURCE_CLASS(klass);

  g_type_class_add_private(gobject_class, sizeof(MafwGriloSourcePrivate));

  source_class->browse = mafw_grilo_source_browse;
  source_class->cancel_browse = mafw_grilo_source_cancel_browse;
  source_class->get_metadata = mafw_grilo_source_get_metadata;
}

/*----------------------------------------------------------------------------
  Public API
  ----------------------------------------------------------------------------*/

MafwGriloSource *mafw_grilo_source_new(void)
{
  return g_object_new(MAFW_TYPE_GRILO_SOURCE,
                      "plugin", MAFW_GRILO_SOURCE_PLUGIN_NAME,
                      "uuid", MAFW_GRILO_SOURCE_UUID,
                      "name", MAFW_GRILO_SOURCE_NAME,
                      NULL);
}

/**
 * See mafw_source_browse() for more information.
 */
static guint mafw_grilo_source_browse(MafwSource *source,
				      const gchar *object_id,
				      gboolean recursive,
				      const MafwFilter *filter,
				      const gchar *sort_criteria,
				      const gchar *const *metadata_keys,
				      guint skip_count,
				      guint item_count,
				      MafwSourceBrowseResultCb browse_cb,
				      gpointer user_data)
{
  if (browse_cb != NULL) {
    GError *error = NULL;
    g_set_error(&error, MAFW_EXTENSION_ERROR,
                MAFW_EXTENSION_ERROR_UNSUPPORTED_OPERATION,
                "Not implemented");
    browse_cb(source, MAFW_SOURCE_INVALID_BROWSE_ID, 0, 0, NULL, NULL,
              user_data, error);
    g_error_free(error);
  }
  return MAFW_SOURCE_INVALID_BROWSE_ID;
}

static gboolean mafw_grilo_source_cancel_browse(MafwSource *source,
                                                guint browse_id,
                                                GError **error)
{
  if (error) {
    g_set_error(error, MAFW_EXTENSION_ERROR,
                MAFW_EXTENSION_ERROR_UNSUPPORTED_OPERATION,
                "Not implemented");
  }
  return FALSE;
}

static void mafw_grilo_source_get_metadata(MafwSource *source,
                                           const gchar *object_id,
                                           const gchar *const *metadata_keys,
                                           MafwSourceMetadataResultCb
                                           metadata_cb,
                                           gpointer user_data)
{
  if (metadata_cb != NULL) {
    GError *error = NULL;
    g_set_error(&error, MAFW_EXTENSION_ERROR,
                MAFW_EXTENSION_ERROR_UNSUPPORTED_OPERATION,
                "Not implemented");
    metadata_cb(source, object_id, NULL, user_data, error);
    g_error_free(error);
  }
}
