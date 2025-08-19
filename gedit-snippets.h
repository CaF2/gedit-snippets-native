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
#pragma once

#include <libpeas/peas-extension-base.h>
#include <libpeas/peas-object-module.h>

G_BEGIN_DECLS

#define GEDIT_TYPE_SNIPPETS_PLUGIN        (gedit_snippets_plugin_get_type())
#define GEDIT_SNIPPETS_PLUGIN(o)          (G_TYPE_CHECK_INSTANCE_CAST((o), GEDIT_TYPE_SNIPPETS_PLUGIN, GeditSnippetsPlugin))
#define GEDIT_SNIPPETS_PLUGIN_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), GEDIT_TYPE_SNIPPETS_PLUGIN, GeditSnippetsPluginClass))
#define GEDIT_IS_SNIPPETS_PLUGIN(o)       (G_TYPE_CHECK_INSTANCE_TYPE((o), GEDIT_TYPE_SNIPPETS_PLUGIN))
#define GEDIT_IS_SNIPPETS_PLUGIN_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE((k), GEDIT_TYPE_SNIPPETS_PLUGIN))
#define GEDIT_SNIPPETS_PLUGIN_GET_CLASS(o)(G_TYPE_INSTANCE_GET_CLASS((o), GEDIT_TYPE_SNIPPETS_PLUGIN, GeditSnippetsPluginClass))

typedef struct _GeditSnippetsPlugin        GeditSnippetsPlugin;
typedef struct _GeditSnippetsPluginClass   GeditSnippetsPluginClass;
typedef struct _GeditSnippetsPluginPrivate GeditSnippetsPluginPrivate;

struct _GeditSnippetsPlugin
{
    PeasExtensionBase parent;

    GeditSnippetsPluginPrivate *priv;
};

struct _GeditSnippetsPluginClass
{
    PeasExtensionBaseClass parent_class;
};

typedef struct Tab_position_object
{
	size_t in_blob, start, end, abs_start;
	char *content;
	size_t number_of_objects; ///< number of $i found
}Tab_position_object;

GType gedit_snippets_plugin_get_type(void) G_GNUC_CONST;

G_MODULE_EXPORT
void peas_register_types(PeasObjectModule *module);

G_END_DECLS
