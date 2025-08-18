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

#include <glib.h>
#include <gedit/gedit-window.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

G_BEGIN_DECLS

typedef struct XmlFileInformation
{
	xmlDoc *doc;
}XmlFileInformation;

typedef struct SnippetTranslation
{
	char *from; ///< tag in the xml files
	char *to; ///< text in the xml files
	char *description; ///< optional description
	GPtrArray *programming_languages;
	char *filename;
	XmlFileInformation *fileinf;
	xmlNode *child;
}SnippetTranslation;

typedef struct SnippetBlock
{
	size_t str_len;
	GPtrArray *nodes; ///< SnippetTranslation
}SnippetBlock;

int configuration_init();
int configuration_finalize();
int load_configuration();
const char *get_programming_language(GeditWindow *window);

extern GPtrArray *GLOBAL_SNIPPETS;

SnippetBlock *get_or_create_block(size_t str_len);

//static SnippetBlock GLOBAL_SNIPPETS[]={
//	{3,(SnippetTranslation[]){{"prl","fprintf(stdout,\"%s:%d \\n\",__FILE__,__LINE__,);"},{"err","fprintf(stderr,\"%s:%d \\n\",__FILE__,__LINE__,);"},{NULL,NULL}}},
//	{8,(SnippetTranslation[]){{"std_head","typedef struct abc{}abc;"},{NULL,NULL}}}
//};
//
//#define GLOBAL_SNIPPETS_LEN (sizeof(GLOBAL_SNIPPETS)/sizeof(GLOBAL_SNIPPETS[0]))

G_END_DECLS
