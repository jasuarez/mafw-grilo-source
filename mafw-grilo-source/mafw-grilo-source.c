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
#include <string.h>

#include <libmafw/mafw.h>
#include <grilo.h>

#include "mafw-grilo-source.h"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "mafw-grilo-source"

#define MAFW_GRILO_SOURCE_PLUGIN_NAME "MAFW-Grilo-Source"


G_DEFINE_TYPE (MafwGriloSource, mafw_grilo_source, MAFW_TYPE_SOURCE);

#define MAFW_GRILO_SOURCE_GET_PRIVATE(object)				\
  (G_TYPE_INSTANCE_GET_PRIVATE ((object), MAFW_TYPE_GRILO_SOURCE,	\
                                MafwGriloSourcePrivate))

#define MAFW_GRILO_SOURCE_ERROR (mafw_grilo_source_error_quark ())
#define MAFW_PROPERTY_GRILO_SOURCE_BROWSE_METADATA_MODE "browse-metadata-mode"
#define MAFW_PROPERTY_GRILO_SOURCE_RESOLVE_METADATA_MODE "resolve-metadata-mode"
#define MAFW_PROPERTY_GRILO_SOURCE_DEFAULT_MIME "default-mime"

typedef enum
  {
    MAFW_GRILO_SOURCE_METADATA_MODE_FAST,
    MAFW_GRILO_SOURCE_METADATA_MODE_NORMAL,
    MAFW_GRILO_SOURCE_METADATA_MODE_FULL,
  } MafwGriloSourceMetadataMode;

struct _MafwGriloSourcePrivate
{
  GrlMediaPlugin *grl_source;
  guint next_browse_id;
  GrlMetadataResolutionFlags browse_metadata_mode;
  GrlMetadataResolutionFlags resolve_metadata_mode;
  GHashTable *browse_requests;
  gchar *default_mime;
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

typedef struct
{
  MafwGriloSource *mafw_grilo_source;
  MafwSourceBrowseResultCb mafw_browse_cb;
  gpointer mafw_user_data;
  guint mafw_browse_id;
  guint grl_browse_id;
  guint index;
} BrowseCbInfo;

typedef struct
{
  MafwGriloSource *mafw_grilo_source;
  MafwSourceMetadataResultCb mafw_metadata_cb;
  gpointer mafw_user_data;
  gchar *mafw_object_id;
} MetadataCbInfo;

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

  /* Only sources that implement browse are of interest */
  supported_ops =
    grl_metadata_source_supported_operations (GRL_METADATA_SOURCE (user_data));
  if (!supported_ops & GRL_OP_BROWSE)
    {
      g_message ("discarded: %s (browse %s, metadata %s)",
                 grl_media_plugin_get_id (GRL_MEDIA_PLUGIN (user_data)),
                 supported_ops & GRL_OP_BROWSE ? "yes" : "no",
                 supported_ops & GRL_OP_METADATA ? "yes" : "no");
      return;
    }

  mafw_grilo_source = mafw_grilo_source_new (GRL_MEDIA_PLUGIN (user_data));
  plugin.grl_sources =
    g_slist_prepend (plugin.grl_sources, g_object_ref (mafw_grilo_source));

  mafw_registry = mafw_registry_get_instance ();
  mafw_registry_add_extension (mafw_registry,
                               MAFW_EXTENSION (mafw_grilo_source));

  g_debug ("loaded: %s (browse %s, metadata %s)",
           grl_media_plugin_get_id (GRL_MEDIA_PLUGIN (user_data)),
           supported_ops & GRL_OP_BROWSE ? "yes" : "no",
           supported_ops & GRL_OP_METADATA ? "yes" : "no");
}

static gint
compare_mafw_grilo_source (gconstpointer data1, gconstpointer data2)
{
  MafwGriloSource *mafw_source1 = MAFW_GRILO_SOURCE (data1);

  return (gconstpointer) mafw_source1->priv->grl_source - data2;
}

static void
cancel_pending_operations (MafwGriloSource *mafw_grilo_source)
{
  GList *list, *current;

  list = g_hash_table_get_values (mafw_grilo_source->priv->browse_requests);

  for (current = list; current; current = g_list_next (current))
    {
      BrowseCbInfo *browse_cb_info = current->data;

      mafw_source_cancel_browse (MAFW_SOURCE (mafw_grilo_source),
                                 browse_cb_info->mafw_browse_id, NULL);
    }

  g_list_free (list);
}

static void
source_removed_cb (GrlPluginRegistry *registry, gpointer user_data)
{
  GSList *link;

  link = g_slist_find_custom (plugin.grl_sources, user_data,
                              compare_mafw_grilo_source);

  if (link)
    {
      MafwRegistry *mafw_registry;

      cancel_pending_operations (MAFW_GRILO_SOURCE (link->data));

      mafw_registry = mafw_registry_get_instance ();
      mafw_registry_remove_extension (mafw_registry,
                                      MAFW_EXTENSION (link->data));

      g_object_unref (link->data);
      plugin.grl_sources =
        g_slist_remove_link (plugin.grl_sources, link);
      g_slist_free_1 (link);
    }
}

static void
initialize_media_types (void)
{
  /* This is a hack to ensure that the GrlMedia* are registered to be
     used when deserializing any object of that type */
  GType type;
  type = grl_media_box_get_type ();
  type = grl_media_audio_get_type ();
  type = grl_media_video_get_type ();
  type = grl_media_image_get_type ();
}

static gboolean
mafw_grilo_source_initialize (MafwRegistry *mafw_registry,
                              GError **error)
{
  GrlPluginRegistry *grl_registry;

  g_debug ("Mafw Grilo plugin initializing");

  initialize_media_types ();

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
destroy_browse_cb_info (gpointer user_data)
{
  BrowseCbInfo *browse_cb_info = user_data;

  g_object_unref (browse_cb_info->mafw_grilo_source);
  g_free (browse_cb_info);
}

static const gchar *
get_default_mime (MafwGriloSource *source)
{
  /* The mime is needed so that the apps can properly filter by them
     when showing the results. As in some sources mime is a slow key
     and we do not want to slow things down, we prefer to write this
     HACK and set a default mime type depending on the source. This
     way we always have a fallback. We could just say it is video, but
     then in the interfaces showing icons, we would se an ugly video
     icon for music or even they could be played like that. */

  if (G_UNLIKELY (!source->priv->default_mime))
    {
      if (strcmp (mafw_extension_get_uuid (MAFW_EXTENSION (source)),
                  "grl_jamendo") == 0 ||
          strcmp (mafw_extension_get_uuid (MAFW_EXTENSION (source)),
                  "grl_shoutcast") == 0)
        {
          source->priv->default_mime =
            g_strdup (MAFW_METADATA_VALUE_MIME_AUDIO);
        }
      else
        {
          source->priv->default_mime =
            g_strdup (MAFW_METADATA_VALUE_MIME_VIDEO);
        }
    }

  return source->priv->default_mime;
}

static void
mafw_grilo_source_init (MafwGriloSource *self)
{
  MafwGriloSourcePrivate *priv = NULL;

  g_return_if_fail (MAFW_IS_GRILO_SOURCE (self));
  priv = self->priv = MAFW_GRILO_SOURCE_GET_PRIVATE (self);
  priv->grl_source = NULL;
  priv->next_browse_id = 1;
  priv->browse_metadata_mode = GRL_RESOLVE_FAST_ONLY;
  priv->resolve_metadata_mode = GRL_RESOLVE_NORMAL;
  priv->browse_requests =
    g_hash_table_new_full (g_int_hash, g_int_equal, NULL,
                           destroy_browse_cb_info);
  priv->default_mime = NULL;

  mafw_extension_add_property(MAFW_EXTENSION(self),
                              MAFW_PROPERTY_GRILO_SOURCE_BROWSE_METADATA_MODE,
                              G_TYPE_UINT);
  mafw_extension_add_property(MAFW_EXTENSION(self),
                              MAFW_PROPERTY_GRILO_SOURCE_RESOLVE_METADATA_MODE,
                              G_TYPE_UINT);
  mafw_extension_add_property(MAFW_EXTENSION(self),
                              MAFW_PROPERTY_GRILO_SOURCE_DEFAULT_MIME,
                              G_TYPE_STRING);
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
dispose (GObject *object)
{
  MafwGriloSource *source = MAFW_GRILO_SOURCE (object);

  g_object_unref (source->priv->grl_source);

  G_OBJECT_CLASS (mafw_grilo_source_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
  MafwGriloSource *source = MAFW_GRILO_SOURCE (object);

  g_hash_table_destroy (source->priv->browse_requests);
  g_free (source->priv->default_mime);

  G_OBJECT_CLASS (mafw_grilo_source_parent_class)->finalize (object);
}

static GQuark
mafw_grilo_source_error_quark (void)
{
  return g_quark_from_static_string ("mafw-grilo-source-error-quark");
}

static void
mafw_grilo_source_get_property (MafwExtension *self,
                                const gchar *key,
                                MafwExtensionPropertyCallback callback,
                                gpointer user_data)
{
  MafwGriloSource *source = MAFW_GRILO_SOURCE (self);
  GValue *value = NULL;
  GError *error = NULL;

  g_return_if_fail (MAFW_IS_GRILO_SOURCE (self));
  g_return_if_fail (callback != NULL);
  g_return_if_fail (key != NULL);

  if (strcmp (key, MAFW_PROPERTY_GRILO_SOURCE_BROWSE_METADATA_MODE) == 0)
    {
      value = g_new0 (GValue, 1);
      g_value_init (value, G_TYPE_UINT);
      switch (source->priv->browse_metadata_mode)
        {
        case GRL_RESOLVE_FAST_ONLY:
          g_value_set_uint (value, MAFW_GRILO_SOURCE_METADATA_MODE_FAST);
          break;
        case GRL_RESOLVE_NORMAL:
          g_value_set_uint (value, MAFW_GRILO_SOURCE_METADATA_MODE_NORMAL);
          break;
        case GRL_RESOLVE_FULL:
          g_value_set_uint (value, MAFW_GRILO_SOURCE_METADATA_MODE_FULL);
          break;
        default:
          g_assert_not_reached ();
        }
    }
  else if (strcmp (key, MAFW_PROPERTY_GRILO_SOURCE_RESOLVE_METADATA_MODE) == 0)
    {
      value = g_new0 (GValue, 1);
      g_value_init (value, G_TYPE_UINT);
      switch (source->priv->resolve_metadata_mode)
        {
        case GRL_RESOLVE_FAST_ONLY:
          g_value_set_uint (value,
                            MAFW_GRILO_SOURCE_METADATA_MODE_FAST);
          break;
        case GRL_RESOLVE_NORMAL:
          g_value_set_uint (value,
                            MAFW_GRILO_SOURCE_METADATA_MODE_NORMAL);
          break;
        case GRL_RESOLVE_FULL:
          g_value_set_uint (value,
                            MAFW_GRILO_SOURCE_METADATA_MODE_FULL);
          break;
        default:
          g_assert_not_reached ();
        }
    }
  else if (strcmp (key, MAFW_PROPERTY_GRILO_SOURCE_RESOLVE_METADATA_MODE) == 0)
    {
      value = g_new0 (GValue, 1);
      g_value_init (value, G_TYPE_STRING);
      g_value_set_string (value, source->priv->default_mime);
    }
  else
    {
      /* Unsupported property */
      error = g_error_new(MAFW_GRILO_SOURCE_ERROR,
                          MAFW_EXTENSION_ERROR_GET_PROPERTY,
                          "Unsupported property");
    }

  callback (self, key, value, user_data, error);
}

static void
mafw_grilo_source_set_property (MafwExtension *self,
                                const gchar *key,
                                const GValue *value)
{
  MafwGriloSource *source = MAFW_GRILO_SOURCE (self);

  g_return_if_fail (MAFW_IS_GRILO_SOURCE (self));
  g_return_if_fail (key != NULL);

  if (strcmp (key, MAFW_PROPERTY_GRILO_SOURCE_BROWSE_METADATA_MODE) == 0)
    {
      switch (g_value_get_uint (value))
        {
        case MAFW_GRILO_SOURCE_METADATA_MODE_FAST:
          source->priv->browse_metadata_mode = GRL_RESOLVE_FAST_ONLY;
          break;
        case MAFW_GRILO_SOURCE_METADATA_MODE_NORMAL:
          source->priv->browse_metadata_mode = GRL_RESOLVE_NORMAL;
          break;
        case MAFW_GRILO_SOURCE_METADATA_MODE_FULL:
          source->priv->browse_metadata_mode = GRL_RESOLVE_FULL;
          break;
        default:
          g_warning ("Wrong metadata mode: %d", g_value_get_uint (value));
        }
    }
  else if (strcmp (key, MAFW_PROPERTY_GRILO_SOURCE_RESOLVE_METADATA_MODE) == 0)
    {
      switch (g_value_get_uint (value))
        {
        case MAFW_GRILO_SOURCE_METADATA_MODE_FAST:
          source->priv->resolve_metadata_mode = GRL_RESOLVE_FAST_ONLY;
          break;
        case MAFW_GRILO_SOURCE_METADATA_MODE_NORMAL:
          source->priv->resolve_metadata_mode = GRL_RESOLVE_NORMAL;
          break;
        case MAFW_GRILO_SOURCE_METADATA_MODE_FULL:
          source->priv->resolve_metadata_mode = GRL_RESOLVE_FULL;
          break;
        default:
          g_warning ("Wrong metadata mode: %d", g_value_get_uint (value));
        }
    }
  else if (strcmp (key, MAFW_PROPERTY_GRILO_SOURCE_RESOLVE_METADATA_MODE) == 0)
    {
      gchar *old_string = source->priv->default_mime;
      source->priv->default_mime = g_value_dup_string (value);
      g_free (old_string);
    }
  else
    {
      return;
    }

  mafw_extension_emit_property_changed (self, key, value);
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
  gobject_class->dispose = dispose;
  gobject_class->finalize = finalize;

  MAFW_EXTENSION_CLASS(klass)->get_extension_property =
    mafw_grilo_source_get_property;
  MAFW_EXTENSION_CLASS(klass)->set_extension_property =
    mafw_grilo_source_set_property;

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
mafw_keys_to_grl_keys (MafwGriloSource *mafw_source,
                       const gchar *const *metadata_keys)
{
  GList *keys = NULL;
  gint i;
  gboolean wildcard = FALSE;

  g_return_val_if_fail (metadata_keys != NULL, NULL);

  for (i = 0; metadata_keys[i] != NULL; i++)
    {
      if (strcmp (metadata_keys[i], MAFW_SOURCE_KEY_WILDCARD) == 0)
        {
          g_list_free (keys);

          keys = g_list_copy ((GList *) grl_metadata_source_supported_keys (GRL_METADATA_SOURCE (mafw_source->priv->grl_source)));
          g_debug ("Converting \"*\" to grilo\n");

          wildcard = TRUE;
        }
      else
        {
          if (G_UNLIKELY (!keys))
            {
              keys = grl_metadata_key_list_new (GRL_METADATA_KEY_ID, NULL);
            }

          if (G_LIKELY (!wildcard))
            {

#define MAFW_KEY_TO_GRL_KEY(mafw_key, grl_key)                        \
              (strcmp (metadata_keys[i], mafw_key) == 0) {              \
                keys = g_list_prepend (keys, GRLKEYID_TO_POINTER(grl_key)); \
                g_debug ("Converting %s to grilo\n", mafw_key);         \
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
        }
    }

  return keys;
}

static GHashTable *
mafw_keys_from_grl_media (MafwGriloSource *mafw_source, GrlMedia *grl_media)
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
            if (!G_VALUE_HOLDS_STRING (value) || g_value_get_string (value)) \
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

  /* We set this independently of it coming in the data or not,
     because in some sources, it can be a slow key and it does not
     come even if we had requested it. */
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
      else
        {
          g_debug ("Setting default mime\n");
          mafw_metadata_add_str (mafw_metadata_keys, MAFW_METADATA_KEY_MIME,
                                             get_default_mime (mafw_source));
        }
    }

  g_list_free (keys);

  return mafw_metadata_keys;
}

static void
grl_browse_cb (GrlMediaSource *grl_source,
               guint grl_browse_id,
               GrlMedia *grl_media,
               guint remaining,
               gpointer user_data,
               const GError *error)
{
  BrowseCbInfo *browse_cb_info = user_data;
  gchar *mafw_object_id = NULL;
  GHashTable *mafw_metadata_keys = NULL;

  if (grl_media)
    {
      const gchar *mafw_uuid;

      mafw_uuid = mafw_extension_get_uuid (MAFW_EXTENSION (browse_cb_info->
                                                           mafw_grilo_source));

      mafw_object_id =
        grl_media_serialize (grl_media, mafw_uuid);
      mafw_metadata_keys = mafw_keys_from_grl_media (browse_cb_info->
                                                     mafw_grilo_source,
                                                     grl_media);
    }

  browse_cb_info->mafw_browse_cb (MAFW_SOURCE (browse_cb_info->
                                               mafw_grilo_source),
                                  browse_cb_info->mafw_browse_id,
                                  remaining,
                                  browse_cb_info->index,
                                  mafw_object_id,
                                  mafw_metadata_keys,
                                  browse_cb_info->mafw_user_data,
                                  error);

  g_free (mafw_object_id);
  if (mafw_metadata_keys)
    {
      g_hash_table_unref (mafw_metadata_keys);
    }

  if (!remaining || error)
    {
      /* we don't free the info, we just remove it from the hash table
         and it will free it for us */
      g_hash_table_remove (browse_cb_info->mafw_grilo_source->priv->
                           browse_requests,
                           &(browse_cb_info->mafw_browse_id));
    }
}

static void
grl_metadata_cb (GrlMediaSource *source,
                 GrlMedia *grl_media,
                 gpointer user_data,
                 const GError *error)
{
  MetadataCbInfo *metadata_cb_info = user_data;
  GHashTable *mafw_metadata_keys = NULL;

  if (grl_media)
    {
      mafw_metadata_keys = mafw_keys_from_grl_media (metadata_cb_info->
                                                     mafw_grilo_source,
                                                     grl_media);
    }

  metadata_cb_info->mafw_metadata_cb (MAFW_SOURCE (metadata_cb_info->
                                                   mafw_grilo_source),
                                      metadata_cb_info->mafw_object_id,
                                      mafw_metadata_keys,
                                      metadata_cb_info->mafw_user_data,
                                      error);

  if (mafw_metadata_keys)
    {
      g_hash_table_unref (mafw_metadata_keys);
    }
  g_free (metadata_cb_info->mafw_object_id);
  g_free (metadata_cb_info);
}

static void
grl_browse_metadata_cb (GrlMediaSource *grl_source,
                        guint grl_browse_id,
                        GrlMedia *grl_media,
                        guint remaining,
                        gpointer user_data,
                        const GError *error)
{
  if (!remaining)
    {
      grl_metadata_cb (grl_source, grl_media, user_data, error);
    }
  else
    {
      g_warning ("Getting metadata with grl_media_source_browse and we have "
                 "remaining results");
    }
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
  GrlMedia *grl_media;
  BrowseCbInfo *browse_cb_info;
  GList *grl_keys;

  g_return_val_if_fail (browse_cb, MAFW_SOURCE_INVALID_BROWSE_ID);

  browse_cb_info = g_new0 (BrowseCbInfo, 1);

  browse_cb_info->mafw_grilo_source = MAFW_GRILO_SOURCE (g_object_ref (source));
  browse_cb_info->mafw_browse_cb = browse_cb;
  browse_cb_info->mafw_user_data = user_data;
  browse_cb_info->mafw_browse_id =
    browse_cb_info->mafw_grilo_source->priv->next_browse_id++;

  grl_media = grl_media_deserialize (object_id);

  grl_keys = mafw_keys_to_grl_keys (MAFW_GRILO_SOURCE (source), metadata_keys);

  g_hash_table_insert (browse_cb_info->mafw_grilo_source->priv->browse_requests,
                       &(browse_cb_info->mafw_browse_id),
                       browse_cb_info);

  browse_cb_info->grl_browse_id =
    grl_media_source_browse (GRL_MEDIA_SOURCE (browse_cb_info->
                                               mafw_grilo_source->priv->
                                               grl_source),
                             grl_media,
                             grl_keys,
                             skip_count,
                             item_count ? item_count : G_MAXUINT,
                             GRL_RESOLVE_IDLE_RELAY |
                             browse_cb_info->mafw_grilo_source->priv->
                             browse_metadata_mode,
                             grl_browse_cb,
                             browse_cb_info);

  g_list_free (grl_keys);

  return browse_cb_info->mafw_browse_id;
}

static gboolean
mafw_grilo_source_cancel_browse (MafwSource *source,
                                 guint browse_id,
                                 GError **error)
{
  BrowseCbInfo *browse_cb_info;
  MafwGriloSource *mafw_grilo_source = MAFW_GRILO_SOURCE (source);

  browse_cb_info =
    g_hash_table_lookup (mafw_grilo_source->priv->browse_requests, &browse_id);

  if (browse_cb_info)
    {
      grl_media_source_cancel (GRL_MEDIA_SOURCE (mafw_grilo_source->priv->
                                                 grl_source),
                               browse_cb_info->grl_browse_id);
      /* We don't need to free anything here as grilo will call the
         browse callback and everything will be freed in that
         moment */
    }
  /* I wonder if we should just silent ignore it and not reporting any
     error */
  else if (error)
    {
      g_set_error (error, MAFW_SOURCE_ERROR,
                   MAFW_SOURCE_ERROR_INVALID_BROWSE_ID,
                   "Browse not active. Could not cancel.");
    }

  return browse_cb_info != NULL;
}

static void
mafw_grilo_source_get_metadata (MafwSource *source,
                                const gchar *object_id,
                                const gchar *const *metadata_keys,
                                MafwSourceMetadataResultCb
                                metadata_cb,
                                gpointer user_data)
{
  MetadataCbInfo *metadata_cb_info;
  GrlMedia *grl_media;
  GList *grl_keys;
  GrlSupportedOps supported_ops;

  g_return_if_fail (metadata_cb);

  metadata_cb_info = g_new0 (MetadataCbInfo, 1);

  metadata_cb_info->mafw_grilo_source = MAFW_GRILO_SOURCE (source);
  metadata_cb_info->mafw_metadata_cb = metadata_cb;
  metadata_cb_info->mafw_user_data = user_data;
  metadata_cb_info->mafw_object_id = g_strdup (object_id);

  grl_media = grl_media_deserialize (object_id);
  grl_keys = mafw_keys_to_grl_keys (MAFW_GRILO_SOURCE (source), metadata_keys);

  supported_ops =
    grl_metadata_source_supported_operations (GRL_METADATA_SOURCE (user_data));
  if (supported_ops & GRL_OP_METADATA)
    {
      g_debug ("getting metadata with source_metadata");
      grl_media_source_metadata (GRL_MEDIA_SOURCE (metadata_cb_info->
                                                   mafw_grilo_source->
                                                   priv->grl_source),
                                 grl_media, grl_keys,
                                 GRL_RESOLVE_IDLE_RELAY |
                                 metadata_cb_info->mafw_grilo_source->priv->
                                 resolve_metadata_mode,
                                 grl_metadata_cb,
                                 metadata_cb_info);
    }
  else
    {
      g_debug ("getting metadata with source_browse");
      grl_media_source_browse (GRL_MEDIA_SOURCE (metadata_cb_info->
                                                 mafw_grilo_source->priv->
                                                 grl_source),
                               grl_media, grl_keys, 0, 1,
                               GRL_RESOLVE_IDLE_RELAY |
                               metadata_cb_info->mafw_grilo_source->priv->
                               resolve_metadata_mode,
                               grl_browse_metadata_cb,
                               metadata_cb_info);
    }

  g_list_free (grl_keys);
}
