/**
Copyright (c) 2025 Florian Evaldsson

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/

#include <string.h>
#include <glib/gi18n.h>
#include <gedit/gedit-debug.h>
#include <gedit/gedit-utils.h>
#include <gedit/gedit-app.h>
#include <gedit/gedit-window.h>
#include <gedit/gedit-app-activatable.h>
#include <gedit/gedit-window-activatable.h>
#include "gedit-snippets.h"

#include "gedit-snippets-configure-window.h"
#include "gedit-snippets-configuration.h"

static void gedit_app_activatable_iface_init(GeditAppActivatableInterface *iface);
static void gedit_window_activatable_iface_init(GeditWindowActivatableInterface *iface);

struct _GeditSnippetsPluginPrivate
{
	GeditWindow *window;
	GSimpleAction *snippets_action;
	GeditApp *app;
	GeditMenuExtension *menu_ext;

	//GtkTextIter start, end; /* selection */
};

enum
{
	PROP_0,
	PROP_WINDOW,
	PROP_APP
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED(GeditSnippetsPlugin, gedit_snippets_plugin, PEAS_TYPE_EXTENSION_BASE, 0,
                               G_IMPLEMENT_INTERFACE_DYNAMIC(GEDIT_TYPE_APP_ACTIVATABLE, gedit_app_activatable_iface_init)
                               G_IMPLEMENT_INTERFACE_DYNAMIC(GEDIT_TYPE_WINDOW_ACTIVATABLE, gedit_window_activatable_iface_init)
                               G_ADD_PRIVATE_DYNAMIC(GeditSnippetsPlugin))

//////////////////////////////////

static void handle_first_insertion(GtkTextBuffer *buffer, GtkTextIter *start, const char *const insertion)
{
	GMatchInfo *match_info;
	GRegex *regex;
	GError *error = NULL;
	const char *pattern = "\\$([0-9]+|<[^>]*>|{[^}]*})";

	regex = g_regex_new(pattern, G_REGEX_EXTENDED, 0, &error);
	if (!regex)
	{
		fprintf(stderr, "Regex compilation failed: %s\n", error->message);
		g_error_free(error);
		return;
	}

	g_autofree char *result = g_regex_replace(regex, insertion, -1, 0, "", 0, &error);

	if (error)
	{
		fprintf(stderr, "Regex replacement failed: %s\n", error->message);
		g_error_free(error);
		return;
	}

	if (g_regex_match(regex, insertion, 0, &match_info))
	{
		const char *cursor = insertion;
		while (g_match_info_matches(match_info))
		{
			g_autofree char *match = g_match_info_fetch(match_info, 0);
			gint start, end;
			g_match_info_fetch_pos(match_info, 0, &start, &end);

			// Print text before match
			printf("Text: %.*s\n", start - (cursor - insertion), cursor);
			printf("Match: %s\n", match);

			cursor = insertion + end;
			g_match_info_next(match_info, NULL);
		}

		// Print remaining text after last match
		if (*cursor)
		{
			printf("Text: %s\n", cursor);
		}
	}

	g_match_info_free(match_info);
	g_regex_unref(regex);

	gtk_text_buffer_insert(buffer, start, result, -1);
}

gboolean language_exists_in_obj(SnippetTranslation *self, const gchar *target)
{
	GPtrArray *const array=self->programming_languages;

	for (guint i = 0; i < array->len; i++)
	{
		const gchar *item = g_ptr_array_index(array, i);
		if (g_ascii_strcasecmp(item, target) == 0)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static gboolean on_key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	GeditSnippetsPlugin *const plugin=user_data;
	
	const char *const programming_language=get_programming_language(plugin->priv->window);
	
	if (event->keyval == GDK_KEY_Tab)
	{
		GeditView *view = GEDIT_VIEW(widget);
		GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
		GtkTextIter iter, start;

		/* Get the current cursor position */
		gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
		
		if(GLOBAL_SNIPPETS)
		{
			for(int i=0;i<GLOBAL_SNIPPETS->len;i++)
			{
				SnippetBlock *sblk=g_ptr_array_index(GLOBAL_SNIPPETS,i);
				/* Move start to the beginning of the word before the cursor */
				gtk_text_iter_assign(&start, &iter);
				gtk_text_iter_backward_chars(&start,sblk->str_len);

				/* Extract the word before the cursor */
				g_autofree gchar *word = gtk_text_buffer_get_text(buffer, &start, &iter, FALSE);
				
				for(int j=0;j<sblk->nodes->len;j++)
				{
					SnippetTranslation *tmp=g_ptr_array_index(sblk->nodes,j);
					if (g_strcmp0(word, tmp->from) == 0 && language_exists_in_obj(tmp,programming_language))
					{
						/* Replace "std_head" with the snippet */
						gtk_text_buffer_begin_user_action(buffer);
						
						gtk_text_buffer_delete(buffer, &start, &iter);
						handle_first_insertion(buffer, &start, tmp->to);
						
						gtk_text_buffer_end_user_action(buffer);
						return TRUE;  // Stop event propagation
					}
				}
			}
		}
	}
	return FALSE;
}

static void snippets_cb(GAction *action, GVariant *parameter, GeditSnippetsPlugin *plugin)
{
	create_snippet_dialog(GTK_WIDGET(plugin->priv->app));
}

static void update_ui(GeditSnippetsPlugin *plugin)
{
	GeditView *view;

	view = gedit_window_get_active_view(plugin->priv->window);

	g_simple_action_set_enabled(plugin->priv->snippets_action, (view != NULL) && gtk_text_view_get_editable(GTK_TEXT_VIEW(view)));
}

static void gedit_snippets_plugin_app_activate(GeditAppActivatable *activatable)
{
	GeditSnippetsPluginPrivate *priv;
	GMenuItem *item;

	priv = GEDIT_SNIPPETS_PLUGIN(activatable)->priv;
	
	priv->menu_ext = gedit_app_activatable_extend_menu(activatable, "tools-section");

	item = g_menu_item_new(_("Snippets"), "win.snippets");
	gedit_menu_extension_append_menu_item(priv->menu_ext, item);
	g_object_unref(item);
}

static void gedit_snippets_plugin_app_deactivate(GeditAppActivatable *activatable)
{
	GeditSnippetsPluginPrivate *priv;

	priv = GEDIT_SNIPPETS_PLUGIN(activatable)->priv;

	g_clear_object(&priv->menu_ext);
}

static void on_tab_changed(GeditWindow *window, gpointer user_data)
{
	GeditView *view = gedit_window_get_active_view(window);
	if (view)
	{
		void *is_loaded=g_object_get_data(G_OBJECT(view), "sl");
		if(is_loaded==NULL)
		{
			g_object_set_data(G_OBJECT(view), "sl", "y");
			// Ensure each new tab gets the key-press-event handler
			g_signal_connect(view, "key-press-event", G_CALLBACK(on_key_press_event), user_data);
		}
	}
}

static void gedit_snippets_plugin_window_activate(GeditWindowActivatable *activatable)
{
	GeditSnippetsPlugin *plugin = GEDIT_SNIPPETS_PLUGIN(activatable);

	GeditSnippetsPluginPrivate *priv;

	priv = GEDIT_SNIPPETS_PLUGIN(activatable)->priv;

	priv->snippets_action = g_simple_action_new("snippets", NULL);
	g_signal_connect(priv->snippets_action, "activate", G_CALLBACK(snippets_cb), activatable);
	g_action_map_add_action(G_ACTION_MAP(priv->window), G_ACTION(priv->snippets_action));
	
	update_ui(GEDIT_SNIPPETS_PLUGIN(activatable));
	
	g_signal_connect(priv->window, "active-tab-changed", G_CALLBACK(on_tab_changed), plugin);
}

static void gedit_snippets_plugin_window_deactivate(GeditWindowActivatable *activatable)
{
	GeditSnippetsPluginPrivate *priv;

	priv = GEDIT_SNIPPETS_PLUGIN(activatable)->priv;
	g_action_map_remove_action(G_ACTION_MAP(priv->window), "snippets");
}

static void gedit_snippets_plugin_window_update_state(GeditWindowActivatable *activatable)
{
	update_ui(GEDIT_SNIPPETS_PLUGIN(activatable));
}

static void gedit_snippets_plugin_init(GeditSnippetsPlugin *plugin)
{
	plugin->priv = gedit_snippets_plugin_get_instance_private(plugin);
}

static void gedit_snippets_plugin_dispose(GObject *object)
{
	GeditSnippetsPlugin *plugin = GEDIT_SNIPPETS_PLUGIN(object);

	g_clear_object(&plugin->priv->snippets_action);
	g_clear_object(&plugin->priv->window);
	g_clear_object(&plugin->priv->menu_ext);
	g_clear_object(&plugin->priv->app);

	G_OBJECT_CLASS(gedit_snippets_plugin_parent_class)->dispose(object);
}

static void gedit_snippets_plugin_finalize(GObject *object)
{
	G_OBJECT_CLASS(gedit_snippets_plugin_parent_class)->finalize(object);
}

static void gedit_snippets_plugin_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GeditSnippetsPlugin *plugin = GEDIT_SNIPPETS_PLUGIN(object);

	switch (prop_id)
	{
	case PROP_WINDOW:
		plugin->priv->window = GEDIT_WINDOW(g_value_dup_object(value));
		break;
	case PROP_APP:
		plugin->priv->app = GEDIT_APP(g_value_dup_object(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void gedit_snippets_plugin_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GeditSnippetsPlugin *plugin = GEDIT_SNIPPETS_PLUGIN(object);

	switch (prop_id)
	{
	case PROP_WINDOW:
		g_value_set_object(value, plugin->priv->window);
		break;
	case PROP_APP:
		g_value_set_object(value, plugin->priv->app);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void gedit_snippets_plugin_class_init(GeditSnippetsPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->dispose = gedit_snippets_plugin_dispose;
	object_class->finalize = gedit_snippets_plugin_finalize;
	object_class->set_property = gedit_snippets_plugin_set_property;
	object_class->get_property = gedit_snippets_plugin_get_property;
	
	load_configuration();

	g_object_class_override_property(object_class, PROP_WINDOW, "window");
	g_object_class_override_property(object_class, PROP_APP, "app");
}

static void gedit_snippets_plugin_class_finalize(GeditSnippetsPluginClass *klass)
{
}

static void gedit_app_activatable_iface_init(GeditAppActivatableInterface *iface)
{
	iface->activate = gedit_snippets_plugin_app_activate;
	iface->deactivate = gedit_snippets_plugin_app_deactivate;
}

static void gedit_window_activatable_iface_init(GeditWindowActivatableInterface *iface)
{
	iface->activate = gedit_snippets_plugin_window_activate;
	iface->deactivate = gedit_snippets_plugin_window_deactivate;
	iface->update_state = gedit_snippets_plugin_window_update_state;
}

G_MODULE_EXPORT void peas_register_types(PeasObjectModule *module)
{
	gedit_snippets_plugin_register_type(G_TYPE_MODULE(module));

	peas_object_module_register_extension_type(module, GEDIT_TYPE_APP_ACTIVATABLE, GEDIT_TYPE_SNIPPETS_PLUGIN);
	peas_object_module_register_extension_type(module, GEDIT_TYPE_WINDOW_ACTIVATABLE, GEDIT_TYPE_SNIPPETS_PLUGIN);
}
