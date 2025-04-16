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
#include <glib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gedit-snippets-configuration.h"

GPtrArray *GLOBAL_SNIPPETS = NULL;

static gint sort_snippet_block(gconstpointer a, gconstpointer b)
{
  const SnippetBlock *entry1 = *((SnippetBlock **) a);
  const SnippetBlock *entry2 = *((SnippetBlock **) b);

  return entry2->str_len-entry1->str_len;
}

const char *get_programming_language(GeditWindow *window)
{
	GeditTab *tab= gedit_window_get_active_tab(window);
	
	if(tab)
	{
		GeditDocument *doc = gedit_tab_get_document(tab);
	
		GtkTextBuffer *buffer = GTK_TEXT_BUFFER(doc);
		GtkSourceBuffer *sbuffer = GTK_SOURCE_BUFFER(buffer);
		GtkSourceLanguage *language = gtk_source_buffer_get_language(sbuffer);
		if (language)
		{
			return gtk_source_language_get_id(language);
		}
	}
	
	return NULL;
}

static SnippetBlock *get_or_create_block(size_t str_len)
{
	for (guint i = 0; i < GLOBAL_SNIPPETS->len; i++)
	{
		SnippetBlock *block = g_ptr_array_index(GLOBAL_SNIPPETS, i);
		if (block->str_len == str_len)
			return block;
	}

	SnippetBlock *new_block = g_malloc(sizeof(SnippetBlock));
	new_block->str_len = str_len;
	new_block->nodes = g_ptr_array_new_with_free_func((GDestroyNotify)g_free);
	g_ptr_array_add(GLOBAL_SNIPPETS, new_block);

	return new_block;
}

static void process_snippet(xmlNode *node, GStrv programming_languages)
{
	g_autofree char *tag = NULL;
	g_autofree char *text = NULL;
	
	for (xmlNode *child = node->children; child; child = child->next)
	{
		if (child->type == XML_ELEMENT_NODE)
		{
			if (g_strcmp0((const char *)child->name, "tag") == 0)
			{
				tag = (char *)xmlNodeGetContent(child);
			}
			else if (g_strcmp0((const char *)child->name, "text") == 0)
			{
				text = (char *)xmlNodeGetContent(child);
			}
		}
	}

	if (tag && text)
	{
		size_t str_len = strlen(tag);
		SnippetBlock *block = get_or_create_block(str_len);
		SnippetTranslation *entry = g_malloc(sizeof(SnippetTranslation));
		entry->from = g_steal_pointer(&tag);
		entry->to = g_steal_pointer(&text);
		entry->programming_languages = g_ptr_array_new_with_free_func(g_free);
		for (gint i = 0; programming_languages[i] != NULL; i++)
		{
			g_ptr_array_add(entry->programming_languages,g_strdup(programming_languages[i]));
		}
		
		//printf("FROM: %s %s\n",entry->from,entry->to);
		g_ptr_array_add(block->nodes, entry);
	}
}

static void parse_snippet_file(const char *filepath, GStrv programming_languages)
{
	xmlDoc *doc = xmlReadFile(filepath, NULL, 0);
	if (!doc)
		return;

	xmlNode *root = xmlDocGetRootElement(doc);
	for (xmlNode *node = root->children; node; node = node->next)
	{
		if (node->type == XML_ELEMENT_NODE && g_strcmp0((const char *)node->name, "snippet") == 0)
			process_snippet(node,programming_languages);
	}

	xmlFreeDoc(doc);
}

int load_configuration()
{
	GLOBAL_SNIPPETS = g_ptr_array_new_with_free_func((GDestroyNotify)g_free);
	
	g_autofree char *home_config_dir=g_build_filename(g_get_home_dir(), ".config/gedit/snippets/", NULL);
	
	char *dirs[] = {
		home_config_dir,
		"/usr/share/gedit/plugins/snippets/",
		"/usr/local/share/gedit/plugins/snippets/"
	};
	
	const char *const file_suffix=".xml";
	const size_t file_suffix_len=strlen(file_suffix);
	
	for (size_t i = 0; i < G_N_ELEMENTS(dirs); i++)
	{
		g_autoptr(GError) error=NULL;
		g_autoptr(GDir) dir = g_dir_open(dirs[i], 0, &error);
		if (!dir)
		{
			//printf("DIR error: %s\n",error->message);
			continue;
		}

		const gchar *filename;
		while ((filename = g_dir_read_name(dir)))
		{
			//printf("READ: %s\n",filename);
			if (g_str_has_suffix(filename, file_suffix))
			{
				gsize len = strlen(filename) - file_suffix_len;
				g_autofree char *file_language_name=g_strndup(filename, len);
				
				g_auto(GStrv) possible_languages=g_strsplit(file_language_name,"_",-1);
				g_autofree char *filepath = g_build_filename(dirs[i], filename, NULL);
				parse_snippet_file(filepath,possible_languages);
			}
		}
	}

	g_ptr_array_sort(GLOBAL_SNIPPETS,sort_snippet_block);

	return 0;
}

