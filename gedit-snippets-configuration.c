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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gedit-snippets-configuration.h"

//@TODO change to a trie and have a file-structure?
GPtrArray *GLOBAL_SNIPPETS = NULL;
GHashTable *GLOBAL_XML_FILE_INFO = NULL;

void snippet_translation_free(SnippetTranslation *self)
{
	g_free(self->from);
	g_free(self->to);
	g_free(self->description);
	
	g_ptr_array_free(self->programming_languages,TRUE);

	g_free(self);
}

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

SnippetBlock *get_or_create_block(size_t str_len)
{
	for (guint i = 0; i < GLOBAL_SNIPPETS->len; i++)
	{
		SnippetBlock *block = g_ptr_array_index(GLOBAL_SNIPPETS, i);
		if (block->str_len == str_len)
			return block;
	}

	SnippetBlock *new_block = g_malloc(sizeof(SnippetBlock));
	new_block->str_len = str_len;
	new_block->nodes = g_ptr_array_new_with_free_func((GDestroyNotify)snippet_translation_free);
	g_ptr_array_add(GLOBAL_SNIPPETS, new_block);

	return new_block;
}

SnippetTranslation *snippet_translation_new()
{
	SnippetTranslation *self = g_new0(SnippetTranslation,1);
	self->programming_languages=g_ptr_array_new_with_free_func(g_free);
	return self;
}

static void process_snippet(xmlNode *node, XmlFileInformation *fileinf, GStrv programming_languages)
{
	g_autofree char *tag = NULL;
	g_autofree char *text = NULL;
	g_autofree char *description = NULL;
	
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
			else if (g_strcmp0((const char *)child->name, "description") == 0)
			{
				description = (char *)xmlNodeGetContent(child);
			}
		}
	}

	if (tag && text)
	{
		const size_t tag_len = strlen(tag);
		SnippetBlock *block = get_or_create_block(tag_len);
		
		SnippetTranslation *entry = snippet_translation_new();
		entry->from = g_steal_pointer(&tag);
		entry->to = g_steal_pointer(&text);
		entry->description = g_steal_pointer(&description);
		for (gint i = 0; programming_languages[i] != NULL; i++)
		{
			g_ptr_array_add(entry->programming_languages,g_strdup(programming_languages[i]));
		}
		entry->fileinf=fileinf;
		entry->child=node;
		//printf("FROM: %s %s\n",entry->from,entry->to);
		g_ptr_array_add(block->nodes, entry);
	}
}

static void parse_snippet_file(const char *filepath, GStrv programming_languages)
{
	XmlFileInformation *fileinf=NULL;

	xmlDoc *doc = xmlReadFile(filepath, NULL, 0);
	if (!doc)
	{
		return;
	}
	
	if(!g_hash_table_contains(GLOBAL_XML_FILE_INFO,filepath))
	{
		fileinf = g_new0(XmlFileInformation,1);
		fileinf->doc=doc;
		fileinf->filename=g_strdup(filepath);
	
		g_hash_table_insert(GLOBAL_XML_FILE_INFO,g_strdup(filepath),fileinf);
	}
	else
	{
		fileinf = g_hash_table_lookup(GLOBAL_XML_FILE_INFO,filepath);
	}

	xmlNode *root = xmlDocGetRootElement(doc);
	for (xmlNode *node = root->children; node; node = node->next)
	{
		if (node->type == XML_ELEMENT_NODE && g_strcmp0((const char *)node->name, "snippet") == 0)
		{
			process_snippet(node,fileinf,programming_languages);
		}
	}
}

int fix_xml_file_from_snippet_translation(SnippetTranslation *self)
{
	const char *langauage="c";

	if(self->programming_languages->len>0)
	{
		langauage=g_ptr_array_index(self->programming_languages,0);
	}

	g_autofree char *preferred_file=g_build_filename(g_get_home_dir(), ".config/gedit/snippets/", langauage, ".xml", NULL);
	
	if(self->fileinf && g_strcmp0(self->fileinf->filename,preferred_file)==0)
	{
		//if it is the same file, do nothing
		return 1;
	}
	
	GHashTableIter hiter;
	gpointer key, value;

	g_hash_table_iter_init(&hiter, GLOBAL_XML_FILE_INFO);
	while (g_hash_table_iter_next(&hiter, &key, &value))
	{
		XmlFileInformation *fileinf=value;
		
		if(g_strcmp0(fileinf->filename,preferred_file)==0)
		{
			self->fileinf=fileinf;
			break;
		}
	}
	
	//did not find the file
	if(!self->fileinf)
	{
		XmlFileInformation *new_fileinf=g_new0(XmlFileInformation,1);
		
		new_fileinf->filename=g_steal_pointer(&preferred_file);
		
		xmlDocPtr doc = xmlNewDoc((const xmlChar*)"1.0");
		xmlNodePtr root = xmlNewNode(NULL, (const xmlChar*)"snippets");
		xmlNewProp(root, (const xmlChar*)"language", (const xmlChar*)langauage);
		xmlDocSetRootElement(doc, root);

		// <snippet>
		xmlNodePtr snippet = xmlNewChild(root, NULL, (const xmlChar*)"snippet", NULL);

		// <tag>if_cpp</tag>
		xmlNewChild(snippet, NULL, (const xmlChar*)"tag", (const xmlChar*)self->from);

		// <text><![CDATA[ ... ]]></text>
		const char *cdata_content = self->to;

		xmlNodePtr text_node = xmlNewChild(snippet, NULL, (const xmlChar*)"text", NULL);
		xmlNodePtr cdata = xmlNewCDataBlock(doc, (const xmlChar*)cdata_content, strlen(cdata_content));
		xmlAddChild(text_node, cdata);

		// <description>if_cpp</description>
		xmlNewChild(snippet, NULL, (const xmlChar*)"description", (const xmlChar*)self->description);

		// Save the document to stdout
		g_autofree xmlChar *xmlbuff;
		int buffersize;
		xmlDocDumpFormatMemoryEnc(doc, &xmlbuff, &buffersize, "utf-8", 1);
		g_print("%s", (char *)xmlbuff);

		new_fileinf->doc=doc;

		self->child=snippet;
		self->fileinf=new_fileinf;
	}
	
	return 0;
}

int save_snippet_translation(SnippetTranslation *self, int options)
{
	g_autofree char *tag = NULL;
	xmlNode *tag_node=NULL;
	g_autofree char *text = NULL;
	xmlNode *text_node=NULL;
	g_autofree char *description = NULL;
	xmlNode *description_node=NULL;
	
	xmlNode *node=self->child;
	
	for (xmlNode *child = node->children; child; child = child->next)
	{
		if (child->type == XML_ELEMENT_NODE)
		{
			if (g_strcmp0((const char *)child->name, "tag") == 0)
			{
				tag = (char *)xmlNodeGetContent(child);
				tag_node=child;
			}
			else if (g_strcmp0((const char *)child->name, "text") == 0)
			{
				text = (char *)xmlNodeGetContent(child);
				text_node=child;
			}
			else if (g_strcmp0((const char *)child->name, "description") == 0)
			{
				description = (char *)xmlNodeGetContent(child);
				description_node=child;
			}
		}
	}
	
	if(g_strcmp0(tag,self->from)!=0)
	{
		if(tag_node)
		{
			xmlNodeSetContent(tag_node, (const xmlChar*)self->from);
		}
		else
		{
			xmlNewChild(node, NULL, (const xmlChar*)"tag", (const xmlChar*)self->from);
		}
	}
	
	if(g_strcmp0(text,self->to)!=0)
	{
		if(text_node)
		{
			xmlNodeSetContent(text_node, (const xmlChar*)self->to);
		}
		else
		{
			xmlNewChild(node, NULL, (const xmlChar*)"text", (const xmlChar*)self->to);
		}
	}
	
	if(g_strcmp0(description,self->to)!=0)
	{
		if(description_node)
		{
			xmlNodeSetContent(description_node, (const xmlChar*)self->description);
		}
		else
		{
			xmlNewChild(node, NULL, (const xmlChar*)"description", (const xmlChar*)self->description);
		}
	}

	XmlFileInformation *fileinf=self->fileinf;

	g_autofree xmlChar *xmlbuff;
	int buffersize;
	xmlDocDumpFormatMemoryEnc(fileinf->doc, &xmlbuff, &buffersize, "utf-8", 1);
	g_print("DUMP: %s", (char *)xmlbuff);
	
	g_print("SAVE TO: %s", fileinf->filename);

	return 0;
}

static void _xml_file_information_free(XmlFileInformation *self)
{
	xmlFreeDoc(self->doc);
	free(self->filename);
	
	free(self);
}

int configuration_init()
{
	GLOBAL_SNIPPETS = g_ptr_array_new_with_free_func((GDestroyNotify)g_free);
	GLOBAL_XML_FILE_INFO = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)_xml_file_information_free);
	
	return 0;
}

int configuration_finalize()
{
	g_ptr_array_free(GLOBAL_SNIPPETS,TRUE);
	g_hash_table_destroy(GLOBAL_XML_FILE_INFO);
	
	return 0;
}

int load_configuration()
{
	g_ptr_array_set_size(GLOBAL_SNIPPETS,0);
	g_hash_table_remove_all(GLOBAL_XML_FILE_INFO);

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

