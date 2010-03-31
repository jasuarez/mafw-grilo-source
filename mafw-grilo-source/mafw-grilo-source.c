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
#include <grilo.h>

#include "mafw-grilo-source.h"

#define MAFW_GRILO_SOURCE_PLUGIN_NAME "MAFW-Grilo-Source"


G_DEFINE_TYPE (MafwGriloSource, mafw_grilo_source, MAFW_TYPE_SOURCE);

#define MAFW_GRILO_SOURCE_GET_PRIVATE(object)				\
  (G_TYPE_INSTANCE_GET_PRIVATE ((object), MAFW_TYPE_GRILO_SOURCE,	\
                                MafwGriloSourcePrivate))

struct _MafwGriloSourcePrivate
{
  GrlMediaPlugin *grl_source;
  guint next_browse_id;
};

typedef struct
{
  GSList *grl_sources;
} MafwGriloSourcePlugin;

static MafwGriloSourcePlugin plugin = { NULL };

enum
  {
    PROP_GRILO_PLUGIN = 1,
  };

static void mafw_grilo_source_init (MafwGriloSource* self);
static void mafw_grilo_source_class_init (MafwGriloSourceClass* klass);
static MafwGriloSource *mafw_grilo_source_new (GrlMediaPlugin *grl_plugin);

static guint mafw_grilo_source_browse (MafwSource *source,
                                       const gchar *object_id,
                                       gboolean recursive,
                                       const MafwFilter *filter,
                                       const gchar *sort_criteria,
                                       const gchar *const *metadata_keys,
                                       guint skip_count,
                                       guint item_count,
                                       MafwSourceBrowseResultCb browse_cb,
                                       gpointer user_data);
static gboolean mafw_grilo_source_cancel_browse (MafwSource *source,
                                                 guint browse_id,
                                                 GError **error);

static void mafw_grilo_source_get_metadata (MafwSource *source,
                                            const gchar *object_id,
                                            const gchar *const *metadata_keys,
                                            MafwSourceMetadataResultCb cb,
                                            gpointer user_data);
static gboolean mafw_grilo_source_initialize (MafwRegistry *mafw_registry,
                                              GError **error);
static void mafw_grilo_source_deinitialize (GError **error);


G_MODULE_EXPORT MafwPluginDescriptor mafw_grilo_source_plugin_description = {
  { .name = MAFW_GRILO_SOURCE_PLUGIN_NAME },
  .initialize = mafw_grilo_source_initialize,
  .deinitialize = mafw_grilo_source_deinitialize,
};


static void
source_added_cb (GrlPluginRegistry *grl_registry, gpointer user_data)
{
  GrlSupportedOps supported_ops;
  MafwGriloSource *mafw_grilo_source;
  MafwRegistry *mafw_registry;

  /* Only sources that implement browse and metadata are of interest */
  supported_ops =
    grl_metadata_source_supported_operations (GRL_METADATA_SOURCE (user_data));
  if (!(supported_ops & GRL_OP_BROWSE &&
        supported_ops & GRL_OP_METADATA))
    {
      return;
    }

  mafw_grilo_source = mafw_grilo_source_new (GRL_MEDIA_PLUGIN (user_data));
  plugin.grl_sources =
    g_slist_prepend (plugin.grl_sources, mafw_grilo_source);

  mafw_registry = mafw_registry_get_instance ();
  mafw_registry_add_extension (mafw_registry,
                               MAFW_EXTENSION (mafw_grilo_source));
}

static void
source_removed_cb (GrlPluginRegistry *registry, gpointer user_data)
{
  GSList *link;

  link = g_slist_find (plugin.grl_sources, user_data);

  if (link)
    {
      g_object_unref (link->data);
      plugin.grl_sources =
        g_slist_remove_link (plugin.grl_sources, link);
    }
}

static gboolean
mafw_grilo_source_initialize (MafwRegistry *mafw_registry,
                              GError **error)
{
  GrlPluginRegistry *grl_registry;

  g_debug ("Mafw Grilo plugin initializing");

  grl_registry = grl_plugin_registry_get_instance ();

  g_signal_connect (grl_registry, "source-added",
                    G_CALLBACK (source_added_cb), NULL);
  g_signal_connect (grl_registry, "source-removed",
                    G_CALLBACK (source_removed_cb), NULL);

  grl_plugin_registry_load_all (grl_registry);

  return TRUE;
}

static void
mafw_grilo_source_deinitialize (GError **error)
{
  g_slist_foreach (plugin.grl_sources, (GFunc) g_object_unref, NULL);
  g_slist_free (plugin.grl_sources);
  plugin.grl_sources = NULL;
}

static void
mafw_grilo_source_init (MafwGriloSource *self)
{
  MafwGriloSourcePrivate *priv = NULL;

  g_return_if_fail (MAFW_IS_GRILO_SOURCE (self));
  priv = self->priv = MAFW_GRILO_SOURCE_GET_PRIVATE (self);
  priv->grl_source = NULL;
  priv->next_browse_id = 1;
}

static void
set_property (GObject *gobject, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
  MafwGriloSource *source = MAFW_GRILO_SOURCE (gobject);

  switch (prop_id)
    {
    case PROP_GRILO_PLUGIN:
      /* Construct-only */
      g_assert (source->priv->grl_source == NULL);
      source->priv->grl_source = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (source, prop_id, pspec);
      break;
    }
}

static void
mafw_grilo_source_class_init (MafwGriloSourceClass *klass)
{
  GObjectClass *gobject_class;
  MafwSourceClass *source_class;

  g_return_if_fail (klass != NULL);

  gobject_class = G_OBJECT_CLASS (klass);
  source_class = MAFW_SOURCE_CLASS (klass);

  g_type_class_add_private (gobject_class, sizeof (MafwGriloSourcePrivate));

  source_class->browse = mafw_grilo_source_browse;
  source_class->cancel_browse = mafw_grilo_source_cancel_browse;
  source_class->get_metadata = mafw_grilo_source_get_metadata;

  gobject_class->set_property = set_property;

  g_object_class_install_property (gobject_class, PROP_GRILO_PLUGIN,
                                   g_param_spec_object ("grl-plugin",
                                                        "Grilo Plugin",
                                                        "The Grilo plugin "
                                                        "object",
                                                        GRL_TYPE_MEDIA_PLUGIN,
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS |
                                                        G_PARAM_WRITABLE));
}

static void
sanitize (gchar *string)
{
  gint i;
  g_return_if_fail (string);

  i=0;
  while (string[i]) {
    switch (string[i]) {
    case '-':
    case ':':
      string[i] = '_';
    break;
    }
    i++;
  }
}

static MafwGriloSource *
mafw_grilo_source_new (GrlMediaPlugin *grl_plugin)
{
  gchar *uuid;

  uuid = g_strdup (grl_media_plugin_get_id (grl_plugin));
  sanitize (uuid);

  return g_object_new (MAFW_TYPE_GRILO_SOURCE,
                       "plugin", MAFW_GRILO_SOURCE_PLUGIN_NAME,
                       "uuid", uuid,
                       "name", grl_media_plugin_get_name (grl_plugin),
                       "grl-plugin", grl_plugin,
                       NULL);
}

static GrlMedia *
grl_media_deserialize (const gchar *object_id)
{
  GrlMedia *grl_media = NULL;
  gchar *serialized_grl_media = NULL;
  gchar *grl_media_type, *grl_media_id;

  if (mafw_source_split_objectid (object_id, NULL, &serialized_grl_media) &&
      serialized_grl_media[0] != '\0')
    {
      /* We search ':' and then we convert the type in NULL terminated and
         prepare the media id */
      grl_media_type = serialized_grl_media;
      grl_media_id = g_strstr_len (serialized_grl_media, -1, ":");
      grl_media_type[grl_media_id - grl_media_type] = '\0';
      grl_media_id++;

      grl_media = g_object_new (g_type_from_name (grl_media_type), NULL);
      grl_media_set_id (grl_media, grl_media_id);
    }

  g_free (serialized_grl_media);

  return grl_media;
}

static gchar *
grl_media_serialize (GrlMedia *grl_media, const gchar *source_id)
{
  const gchar *media_id, *type;

  type = G_OBJECT_TYPE_NAME (grl_media);
  media_id = grl_media_get_id (grl_media);

  return g_strconcat (source_id, "::", type, ":", media_id, NULL);
}

static GList *
mafw_keys_to_grl_keys (const gchar *const *metadata_keys)
{
  GList *keys;
  gint i;

  g_return_val_if_fail (metadata_keys != NULL, NULL);

  keys = grl_metadata_key_list_new (GRL_METADATA_KEY_ID, NULL);

  for (i = 0; metadata_keys[i] != NULL; i++)
    {
#define MAFW_KEY_TO_GRL_KEY(mafw_key, grl_key) \
      (strcmp (metadata_keys[i], mafw_key) == 0) { \
          keys = g_list_prepend (keys, GRLKEYID_TO_POINTER(grl_key)); \
          g_debug ("Converting %s to grilo\n", mafw_key); \
      }

      if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_URI, GRL_METADATA_KEY_URL)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_TITLE, GRL_METADATA_KEY_TITLE)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_ARTIST, GRL_METADATA_KEY_ARTIST)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_ALBUM, GRL_METADATA_KEY_ALBUM)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_GENRE, GRL_METADATA_KEY_GENRE)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_THUMBNAIL, GRL_METADATA_KEY_THUMBNAIL)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_COMPOSER, GRL_METADATA_KEY_AUTHOR)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_DESCRIPTION, GRL_METADATA_KEY_DESCRIPTION)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_LYRICS, GRL_METADATA_KEY_LYRICS)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_DURATION, GRL_METADATA_KEY_DURATION)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_CHILDCOUNT_1, GRL_METADATA_KEY_CHILDCOUNT)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_MIME, GRL_METADATA_KEY_MIME)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_RES_X, GRL_METADATA_KEY_WIDTH)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_RES_Y, GRL_METADATA_KEY_HEIGHT)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_VIDEO_FRAMERATE, GRL_METADATA_KEY_FRAMERATE)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_RATING, GRL_METADATA_KEY_RATING)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_BITRATE, GRL_METADATA_KEY_BITRATE)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_PLAY_COUNT, GRL_METADATA_KEY_PLAY_COUNT)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_LAST_PLAYED, GRL_METADATA_KEY_LAST_PLAYED)
      else if MAFW_KEY_TO_GRL_KEY (MAFW_METADATA_KEY_PAUSED_POSITION, GRL_METADATA_KEY_LAST_POSITION)
      else
        {
          g_message ("MAFW key %s cannot be mapped to Grilo", metadata_keys[i]);
        }
    }

  return keys;
}

static GHashTable *
mafw_keys_from_grl_media (GrlMedia *grl_media)
{
  GHashTable *mafw_metadata_keys;
  GList *keys, *current;

  mafw_metadata_keys = mafw_metadata_new ();

  keys = grl_data_get_keys (GRL_DATA (grl_media));

  for (current = keys; current; current = g_list_next (current))
    {
      GrlKeyID id;
      const GValue *value;

      id = POINTER_TO_GRLKEYID (current->data);
      value = grl_data_get (GRL_DATA (grl_media), id);

      if (value)
        {
#define GRL_KEY_TO_MAFW_KEY(mafw_key, grl_key) \
          (id == grl_key) { \
            g_debug ("Converting %s from grilo\n", mafw_key); \
            mafw_metadata_add_val (mafw_metadata_keys, mafw_key, (GValue*) value); \
          }

          if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_URI, GRL_METADATA_KEY_URL)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_TITLE, GRL_METADATA_KEY_TITLE)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_ARTIST, GRL_METADATA_KEY_ARTIST)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_ALBUM, GRL_METADATA_KEY_ALBUM)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_GENRE, GRL_METADATA_KEY_GENRE)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_THUMBNAIL, GRL_METADATA_KEY_THUMBNAIL)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_COMPOSER, GRL_METADATA_KEY_AUTHOR)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_DESCRIPTION, GRL_METADATA_KEY_DESCRIPTION)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_LYRICS, GRL_METADATA_KEY_LYRICS)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_DURATION, GRL_METADATA_KEY_DURATION)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_CHILDCOUNT_1, GRL_METADATA_KEY_CHILDCOUNT)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_RES_X, GRL_METADATA_KEY_WIDTH)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_RES_Y, GRL_METADATA_KEY_HEIGHT)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_VIDEO_FRAMERATE, GRL_METADATA_KEY_FRAMERATE)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_RATING, GRL_METADATA_KEY_RATING)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_BITRATE, GRL_METADATA_KEY_BITRATE)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_PLAY_COUNT, GRL_METADATA_KEY_PLAY_COUNT)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_LAST_PLAYED, GRL_METADATA_KEY_LAST_PLAYED)
          else if GRL_KEY_TO_MAFW_KEY (MAFW_METADATA_KEY_PAUSED_POSITION, GRL_METADATA_KEY_LAST_POSITION)
     }
   }

  if (GRL_IS_MEDIA_BOX (grl_media))
    {
      g_debug ("Converting mime container from grilo\n");
      mafw_metadata_add_str (mafw_metadata_keys, MAFW_METADATA_KEY_MIME,
                             MAFW_METADATA_VALUE_MIME_CONTAINER);
    }
  else
    {
      const gchar *mime;

      mime = grl_media_get_mime (grl_media);

      if (mime)
        {
          g_debug ("Converting mime from grilo\n");
          mafw_metadata_add_str (mafw_metadata_keys, MAFW_METADATA_KEY_MIME,
                                 mime);
        }
    }

  g_list_free (keys);

  return mafw_metadata_keys;
}

/*----------------------------------------------------------------------------
  Public API
  ----------------------------------------------------------------------------*/

static guint
mafw_grilo_source_browse (MafwSource *source,
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
  if (browse_cb != NULL)
    {
      GError *error = NULL;
      g_set_error (&error, MAFW_EXTENSION_ERROR,
                   MAFW_EXTENSION_ERROR_UNSUPPORTED_OPERATION,
                   "Not implemented");
      browse_cb (source, MAFW_SOURCE_INVALID_BROWSE_ID, 0, 0, NULL, NULL,
                 user_data, error);
      g_error_free (error);
    }
  return MAFW_SOURCE_INVALID_BROWSE_ID;
}

static gboolean
mafw_grilo_source_cancel_browse (MafwSource *source,
                                 guint browse_id,
                                 GError **error)
{
  if (error)
    {
      g_set_error (error, MAFW_EXTENSION_ERROR,
                   MAFW_EXTENSION_ERROR_UNSUPPORTED_OPERATION,
                   "Not implemented");
    }
  return FALSE;
}

static void
mafw_grilo_source_get_metadata (MafwSource *source,
                                const gchar *object_id,
                                const gchar *const *metadata_keys,
                                MafwSourceMetadataResultCb
                                metadata_cb,
                                gpointer user_data)
{
  if (metadata_cb != NULL)
    {
      GError *error = NULL;
      g_set_error (&error, MAFW_EXTENSION_ERROR,
                   MAFW_EXTENSION_ERROR_UNSUPPORTED_OPERATION,
                   "Not implemented");
      metadata_cb (source, object_id, NULL, user_data, error);
      g_error_free (error);
    }
}
