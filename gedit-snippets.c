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

#include "gedit-snippets-python-handling.h"
#include "gedit-snippets-configure-window.h"
#include "gedit-snippets-configuration.h"

size_t GLOBAL_SNIPPET_START_POS=0;
size_t GLOBAL_SNIPPET_FILTERED_LEN=0;
GHashTable *GLOBAL_POSITION_INFO_HASH_TABLE=NULL;
SnippetTranslation *GLOBAL_CURRENT_SNIPPET_TRANSLATION=NULL;
int GLOBAL_POSITION_STATE=0;
int GLOBAL_EXPAND_INTERNAL_CODE=0;

//define once
GRegex *GLOBAL_REGEX_FIND_VARIABLES=NULL;
GRegex *GLOBAL_REGEX_FIND_ONLY_DOLLAR_VARIABLES=NULL;

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

static gint g_int_compare(gconstpointer a, gconstpointer b)
{
	int ia = GPOINTER_TO_INT(a);
	int ib = GPOINTER_TO_INT(b);
	
	// Special case: 0 goes last
	if (ia == 0 && ib != 0)
		return 1;  // ia > ib
	if (ib == 0 && ia != 0)
		return -1; // ia < ib
	
	if (ia < ib)
	{
		return -1;
	}
	else if (ia > ib)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}


gpointer get_next_tab_position(GHashTable *table, guint index)
{
	g_autoptr(GList) keys = g_hash_table_get_keys(table);
	keys = g_list_sort(keys, (GCompareFunc)g_int_compare); // Sort keys numerically

	gpointer value = NULL;

	if (index >= 0 && index < g_list_length(keys))
	{
		gpointer key = g_list_nth_data(keys, index);
		value = g_hash_table_lookup(table, key);
	}

	return value;
}

static void _tab_position_object_free(Tab_position_object *tpobj)
{
	if (!tpobj)
	{
		return;
	}
	g_free(tpobj->content);
	g_free(tpobj);
}

int reset_globals()
{
	//dont free every time
	if(GLOBAL_SNIPPET_FILTERED_LEN!=0)
	{
		GLOBAL_SNIPPET_START_POS=0;
		GLOBAL_SNIPPET_FILTERED_LEN=0;
		GLOBAL_POSITION_STATE=0;
		GLOBAL_EXPAND_INTERNAL_CODE=0;
		GLOBAL_CURRENT_SNIPPET_TRANSLATION=NULL;
		if(GLOBAL_POSITION_INFO_HASH_TABLE)
		{
			g_hash_table_remove_all(GLOBAL_POSITION_INFO_HASH_TABLE);
		}
	}
	
	return 0;
}

int gstring_append_reformatted_dollar_string(GString *includes, const char *const dinsertion)
{
	g_autoptr(GMatchInfo) match_dollar_info=NULL;

	//Analyze the input
	if (g_regex_match(GLOBAL_REGEX_FIND_ONLY_DOLLAR_VARIABLES, dinsertion, 0, &match_dollar_info))
	{
		const char *dcursor = dinsertion;
		while (g_match_info_matches(match_dollar_info))
		{
			g_autofree char *dmatch = g_match_info_fetch(match_dollar_info, 1);
			gint dstart, dend;
			g_match_info_fetch_pos(match_dollar_info, 0, &dstart, &dend);
			
			size_t dcursor_len=dstart - (dcursor - dinsertion);
			
			g_string_append_len(includes,dcursor,dcursor_len);
			
			long long did_num=g_ascii_strtoll(dmatch,NULL,10);
			
			Tab_position_object *value = g_hash_table_lookup(GLOBAL_POSITION_INFO_HASH_TABLE, GINT_TO_POINTER(did_num));
			
			if(value)
			{
				g_string_append(includes,"'");
				g_string_append(includes,value->content);
				g_string_append(includes,"'");
			}
			
			dcursor = dinsertion + dend;
			g_match_info_next(match_dollar_info, NULL);
		}
		
		if (*dcursor)
		{
			g_string_append(includes,dcursor);
		}
		
//		fprintf(stdout,"%s:%d INCLUDES [%s]\n",__FILE__,__LINE__,includes->str);
	}
	
	return 0;
}

int finalize_fancy_snippet(GtkTextBuffer *buffer)
{
	g_autoptr(GMatchInfo) match_info=NULL;

//	fprintf(stdout,"%s:%d FINALIZE []\n",__FILE__,__LINE__);
	
	const char *const insertion=GLOBAL_CURRENT_SNIPPET_TRANSLATION->to;
	
	g_autoptr(GString) includes=g_string_sized_new(100);
	g_autoptr(GString) result=g_string_sized_new(100);
	
	//Analyze the input
	if (g_regex_match(GLOBAL_REGEX_FIND_VARIABLES, insertion, 0, &match_info))
	{
		const char *cursor = insertion;
		while (g_match_info_matches(match_info))
		{
			g_autofree char *match = g_match_info_fetch(match_info, 0);
			gint start, end;
			g_match_info_fetch_pos(match_info, 0, &start, &end);
			
			size_t cursor_len=start - (cursor - insertion);
			
			// Print text before match
//			printf("Text: %.*s\n", (int)cursor_len, cursor);
			g_string_append_len(result,cursor,cursor_len);
			
//			printf("Match: %s\n",match);
			
			//get the ids of all matches. This will focus on $123 and $<[123]: ... > ${123: ... }
			if(match[0]=='$')
			{
				if(match[1]>='0' && match[1]<='9')
				{
					long long id_num=g_ascii_strtoll(match+1,NULL,10);
					
					Tab_position_object *value = g_hash_table_lookup(GLOBAL_POSITION_INFO_HASH_TABLE, GINT_TO_POINTER(id_num));
					
					if(value)
					{
						g_string_append(result,value->content);
					}
				}
				else if(match[1]=='<')
				{
					//is return;
					if(match[2]=='[')
					{
						const char *tmp=match+3;
						while((*tmp)!=':' && (*tmp)!='\0')
						{
							tmp++;
						}
						
						if((*tmp)!='\0')
						{
							tmp++;
							const size_t tmp_len=strlen(tmp);
						
							g_autoptr(GString) return_res=g_string_sized_new(100);
							g_string_append_len(return_res,tmp,tmp_len-1);
							
							g_autofree char *return_str=translate_python_block(includes->str,return_res->str);
							
							g_string_append(result,return_str);
						}
					}
					//is include
					else
					{
						const size_t match_len=strlen(match);
						
						g_autoptr(GString) includes_tmp=g_string_sized_new(100);
						g_string_append_len(includes_tmp,match+2,match_len-3);
						//g_string_append(includes_tmp,"\n");
						
						gstring_append_reformatted_dollar_string(includes,includes_tmp->str);
					}
				}
				else if(match[1]=='{' && (match[2]>='0' && match[2]<='9'))
				{
					long long id_num=g_ascii_strtoll(match+2,NULL,10);
				
					Tab_position_object *value = g_hash_table_lookup(GLOBAL_POSITION_INFO_HASH_TABLE, GINT_TO_POINTER(id_num));
					
					if(value)
					{
						//fprintf(stdout,"%s:%d STRLEN: [%zu]\n",__FILE__,__LINE__,strlen(value->content));
					
						if(value->content && strlen(value->content)>0)
						{
							g_string_append(result,value->content);
						}
						else
						{
							const char *tmp=match+2;
							while((*tmp)!=':' && (*tmp)!='\0')
							{
								tmp++;
							}
						
							if((*tmp)!='\0')
							{
								tmp++;
							
								g_string_append_len(result,tmp,strlen(tmp)-1);
							}
						}
					}
				}
			}
			
			cursor = insertion + end;
			g_match_info_next(match_info, NULL);
		}

		// Print remaining text after last match
		if (*cursor)
		{
//			printf("Text: %s\n", cursor);
			g_string_append(result,cursor);
		}
		
		size_t insertion_len=GLOBAL_SNIPPET_FILTERED_LEN;
		
		GHashTableIter iter;
		gpointer key, value;

		g_hash_table_iter_init(&iter, GLOBAL_POSITION_INFO_HASH_TABLE);
		while (g_hash_table_iter_next(&iter, &key, &value))
		{
			Tab_position_object *iobj = value;
			
			insertion_len+=strlen(iobj->content);
		}
		
//		fprintf(stdout,"%s:%d TOTAL RESULT= [%s] [%zu]\n",__FILE__,__LINE__,result->str,insertion_len);
		
		gtk_text_buffer_begin_user_action(buffer);
		
		GtkTextIter start_iter;

		gtk_text_buffer_get_iter_at_offset(buffer, &start_iter, GLOBAL_SNIPPET_START_POS);
		
		GtkTextIter end_iter=start_iter;
		
		gtk_text_iter_forward_chars(&end_iter, insertion_len);
		
		// Remove the previous
		gtk_text_buffer_delete(buffer, &start_iter, &end_iter);

		// Now insert your new pythonized string
		gtk_text_buffer_insert(buffer, &start_iter, result->str, -1);
		
		gtk_text_buffer_end_user_action(buffer);
	}
	
	return 0;
}

int init_globals()
{
	if(GLOBAL_POSITION_INFO_HASH_TABLE==NULL)
	{
		GError *error = NULL;
	
		GLOBAL_POSITION_INFO_HASH_TABLE=g_hash_table_new_full(g_direct_hash,g_direct_equal,NULL,(GDestroyNotify)_tab_position_object_free);
		
		const char *pattern = "\\$([0-9]+|<[^>]*>|{[^}]*})";
		
		GLOBAL_REGEX_FIND_VARIABLES = g_regex_new(pattern, G_REGEX_EXTENDED, 0, &error);
		
		if (!GLOBAL_REGEX_FIND_VARIABLES)
		{
			fprintf(stderr, "Regex compilation failed: %s\n", error->message);
			g_error_free(error);
			return -1;
		}
		
		GLOBAL_REGEX_FIND_ONLY_DOLLAR_VARIABLES = g_regex_new("\\$([0-9]+)", G_REGEX_EXTENDED, 0, &error);
		
		if (!GLOBAL_REGEX_FIND_ONLY_DOLLAR_VARIABLES)
		{
			fprintf(stderr, "Regex compilation failed: %s\n", error->message);
			g_error_free(error);
			return -1;
		}
	}
	else
	{
		return reset_globals();
	}
	
	return 0;
}

void move_cursor_n_chars(GtkTextBuffer *buffer, gint n)
{
	GtkTextIter iter;

	// Get current cursor position
	gtk_text_buffer_get_iter_at_mark(buffer, &iter,
		gtk_text_buffer_get_insert(buffer));

	// Move forward or backward N chars
	if (n > 0)
		gtk_text_iter_forward_chars(&iter, n);
	else if (n < 0)
		gtk_text_iter_backward_chars(&iter, -n);

	// Place cursor at new position
	gtk_text_buffer_place_cursor(buffer, &iter);

	// Optional: scroll so it's visible
	//GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
	//gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(gtk_text_buffer_get_tag_table(buffer)), mark);
}

size_t get_position_relative_start(GtkTextBuffer *buffer)
{
	GtkTextIter iter;
	gtk_text_buffer_get_iter_at_mark(buffer,
		                             &iter,
		                             gtk_text_buffer_get_insert(buffer));

	return gtk_text_iter_get_offset(&iter);
}

/**
	1. Add output as blob (no data). but remember the position of all first positions of the ids
	2. Move cursor to the first position in the blob. Add a state for next tabbing (maybe add some special color to know that we are in a special state). 
	   If clicking somewhere, simply cancel everything.
	3. After typing text and press tab. Store the typed text in the ids struct. Move to the next id. If last id is typed, now parse all the text again and insert.
	   this step will most likely need some basic python pre-processing
*/
static int handle_first_insertion(GtkTextBuffer *buffer, GtkTextIter *start, SnippetTranslation *sntran)
{
	GMatchInfo *match_info;
	GError *error = NULL;
	const char *const insertion=sntran->to;
	
	init_globals();
	GLOBAL_CURRENT_SNIPPET_TRANSLATION=sntran;

	g_autofree char *result = g_regex_replace(GLOBAL_REGEX_FIND_VARIABLES, insertion, -1, 0, "", 0, &error);

	if (error)
	{
		fprintf(stderr, "Regex replacement failed: %s\n", error->message);
		g_error_free(error);
		return -1;
	}

	//Analyze the input
	if (g_regex_match(GLOBAL_REGEX_FIND_VARIABLES, insertion, 0, &match_info))
	{
		size_t in_blob_pos=0;
	
		const char *cursor = insertion;
		while (g_match_info_matches(match_info))
		{
			g_autofree char *match = g_match_info_fetch(match_info, 0);
			gint start, end;
			g_match_info_fetch_pos(match_info, 0, &start, &end);
			
			size_t cursor_len=start - (cursor - insertion);
			in_blob_pos+=cursor_len;
			
			// Print text before match
//			printf("Text: %.*s\n", (int)cursor_len, cursor);
//			printf("Match[%zu]: %s\n", in_blob_pos,match);
			
			//get the ids of all matches. This will focus on $123 and $<[123]: ... > ${123: ... }
			if(match[0]=='$')
			{
				int match_pos=-1;
			
				if(match[1]>='0' && match[1]<='9')
				{
					match_pos=1;
				}
				else if(match[1]=='<' && match[2]=='[' && (match[3]>='0' && match[3]<='9'))
				{
					match_pos=3;
				}
				else if(match[1]=='{' && (match[2]>='0' && match[2]<='9'))
				{
					match_pos=2;
				}
				
				if(match[1]=='<' || match[1]=='{')
				{
//					fprintf(stdout,"%s:%d EXP []\n",__FILE__,__LINE__);
					GLOBAL_EXPAND_INTERNAL_CODE=1;
				}
				
				if(match_pos>=0)
				{
					long long id_num=g_ascii_strtoll(match+match_pos,NULL,10);
					
					if(!g_hash_table_contains(GLOBAL_POSITION_INFO_HASH_TABLE,GINT_TO_POINTER(id_num)))
					{
						Tab_position_object *iobj = g_new0(Tab_position_object, 1);
						iobj->in_blob=in_blob_pos;
						iobj->start=start;
						iobj->end=end;
						iobj->number_of_objects=1;
						
						g_hash_table_insert(GLOBAL_POSITION_INFO_HASH_TABLE,GINT_TO_POINTER(id_num),iobj);
					}
					else
					{
						//more than one variable exists, need to expand
						GLOBAL_EXPAND_INTERNAL_CODE=1;
						
						Tab_position_object *iobj = g_hash_table_lookup(GLOBAL_POSITION_INFO_HASH_TABLE,GINT_TO_POINTER(id_num));
						if(iobj)
						{
							iobj->number_of_objects++;
						}
					}
				}
			}
			
			
			cursor = insertion + end;
			g_match_info_next(match_info, NULL);
		}

		// Print remaining text after last match
//		if (*cursor)
//		{
//			printf("Text: %s\n", cursor);
//		}
	}
	
	
	
	size_t GLOBAL_POSITION_INFO_HASH_TABLE_len=g_hash_table_size(GLOBAL_POSITION_INFO_HASH_TABLE);
	
//	fprintf(stdout,"%s:%d NUMBER OF OBJECTS [%zu]\n",__FILE__,__LINE__,GLOBAL_POSITION_INFO_HASH_TABLE_len);

	g_match_info_free(match_info);
	
	GLOBAL_SNIPPET_START_POS=get_position_relative_start(buffer);
	
	//current easy fix
	gtk_text_buffer_insert(buffer, start, result, -1);
	
	GLOBAL_SNIPPET_FILTERED_LEN=strlen(result);
	
	Tab_position_object *first_id_obj=get_next_tab_position(GLOBAL_POSITION_INFO_HASH_TABLE,GLOBAL_POSITION_STATE);
	
	//if has $1 etc
	if(first_id_obj)
	{
		move_cursor_n_chars(buffer, -GLOBAL_SNIPPET_FILTERED_LEN+first_id_obj->in_blob);
		
		size_t current_abs_pos=get_position_relative_start(buffer);
		
		first_id_obj->abs_start=current_abs_pos;
	}
	
	if((size_t)(GLOBAL_POSITION_STATE+1)==GLOBAL_POSITION_INFO_HASH_TABLE_len && GLOBAL_EXPAND_INTERNAL_CODE)
	{
		GLOBAL_EXPAND_INTERNAL_CODE=2;
	}
	
	if((size_t)(GLOBAL_POSITION_STATE+1)<GLOBAL_POSITION_INFO_HASH_TABLE_len)
	{
		GLOBAL_POSITION_STATE++;
//		fprintf(stdout,"%s:%d INCREASED GLOBAL POSITION STATE [%d]\n",__FILE__,__LINE__,GLOBAL_POSITION_STATE);
	}
	
	return 0;
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

int set_content_from_now(GtkTextBuffer *buffer,Tab_position_object *prev_id_pos)
{
	const size_t current_relative_pos=get_position_relative_start(buffer);
				
	GtkTextIter start_iter;
	GtkTextIter end_iter;

	gtk_text_buffer_get_iter_at_offset(buffer, &start_iter, prev_id_pos->abs_start);
	gtk_text_buffer_get_iter_at_offset(buffer, &end_iter, current_relative_pos);

	gchar *text_between = gtk_text_buffer_get_text(buffer, &start_iter, &end_iter, FALSE);

//	fprintf(stdout,"%s:%d Text Between [%s]\n",__FILE__,__LINE__,text_between);
	
	prev_id_pos->content=text_between;
	
	return 0;
}

static gboolean on_key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	GeditSnippetsPlugin *const plugin=user_data;
	
	const char *const programming_language=get_programming_language(plugin->priv->window);
	
	//When you press tab
	if (event->keyval == GDK_KEY_Tab)
	{
		GeditView *view = GEDIT_VIEW(widget);
		GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
		GtkTextIter iter, start;

		/* Get the current cursor position */
		gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
		
		if(GLOBAL_SNIPPETS)
		{
			if(GLOBAL_EXPAND_INTERNAL_CODE==2)
			{
				Tab_position_object *curr_id_pos=get_next_tab_position(GLOBAL_POSITION_INFO_HASH_TABLE,GLOBAL_POSITION_STATE);
				set_content_from_now(buffer,curr_id_pos);
				finalize_fancy_snippet(buffer);
				reset_globals();
				return TRUE;
			}
			else if(GLOBAL_POSITION_STATE<=0)
			{
				for(guint i=0;i<GLOBAL_SNIPPETS->len;i++)
				{
					SnippetBlock *sblk=g_ptr_array_index(GLOBAL_SNIPPETS,i);
					/* Move start to the beginning of the word before the cursor */
					gtk_text_iter_assign(&start, &iter);
					gtk_text_iter_backward_chars(&start,sblk->str_len);

					/* Extract the word before the cursor */
					g_autofree gchar *word = gtk_text_buffer_get_text(buffer, &start, &iter, FALSE);
					
					for(guint j=0;j<sblk->nodes->len;j++)
					{
						SnippetTranslation *tmp=g_ptr_array_index(sblk->nodes,j);
						if (g_strcmp0(word, tmp->from) == 0 && language_exists_in_obj(tmp,programming_language))
						{
							/* Replace "std_head" with the snippet */
							gtk_text_buffer_begin_user_action(buffer);
							
							gtk_text_buffer_delete(buffer, &start, &iter);
							int ret_result=handle_first_insertion(buffer, &start, tmp);
							
							if(ret_result!=0)
							{
								fprintf(stderr,"%s:%d Something went wrong to handle the first insertion.\n",__FILE__,__LINE__);
							}
							
							gtk_text_buffer_end_user_action(buffer);
							return TRUE;  // Stop event propagation
						}
					}
				}
			}
			else
			{
				gtk_text_buffer_begin_user_action(buffer);
				
				Tab_position_object *prev_id_pos=get_next_tab_position(GLOBAL_POSITION_INFO_HASH_TABLE,GLOBAL_POSITION_STATE-1);
				Tab_position_object *curr_id_pos=get_next_tab_position(GLOBAL_POSITION_INFO_HASH_TABLE,GLOBAL_POSITION_STATE);
				
//				const size_t current_relative_pos=get_position_relative_start(buffer);
				
				set_content_from_now(buffer,prev_id_pos);
				
				move_cursor_n_chars(buffer, curr_id_pos->in_blob-prev_id_pos->in_blob);
				
				curr_id_pos->abs_start=get_position_relative_start(buffer);
				
				size_t GLOBAL_POSITION_INFO_HASH_TABLE_len=g_hash_table_size(GLOBAL_POSITION_INFO_HASH_TABLE);
	
//				fprintf(stdout,"%s:%d NUMBER OF OBJECTS [%zu] [%d %d]\n",__FILE__,__LINE__,GLOBAL_POSITION_INFO_HASH_TABLE_len,
//					GLOBAL_POSITION_STATE,GLOBAL_EXPAND_INTERNAL_CODE);
				
				if((size_t)(GLOBAL_POSITION_STATE+1)==GLOBAL_POSITION_INFO_HASH_TABLE_len && GLOBAL_EXPAND_INTERNAL_CODE)
				{
					GLOBAL_EXPAND_INTERNAL_CODE=2;
				}
				
				if((size_t)(GLOBAL_POSITION_STATE+1)<GLOBAL_POSITION_INFO_HASH_TABLE_len)
				{
					GLOBAL_POSITION_STATE++;
//					fprintf(stdout,"%s:%d INCREASED GLOBAL POSITION STATE [%d]\n",__FILE__,__LINE__,GLOBAL_POSITION_STATE);
				}
				else if(GLOBAL_EXPAND_INTERNAL_CODE!=2)
				{
					reset_globals();
				}
				
				gtk_text_buffer_end_user_action(buffer);
			
				return TRUE;  // Stop event propagation
			}
		}
	}
	return FALSE;
}

static gboolean on_button_press_event(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	//GeditSnippetsPlugin *const plugin=user_data;

	if (event->button == 1) // left click
	{
//		g_print("Clicked at %.1f, %.1f in active document window\n",event->x, event->y);

		reset_globals();
	}

	return FALSE; // let Gedit handle normal selection/cursor movement
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
			g_signal_connect(view, "button-press-event",G_CALLBACK(on_button_press_event), user_data);

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
	Py_Initialize();
	
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->dispose = gedit_snippets_plugin_dispose;
	object_class->finalize = gedit_snippets_plugin_finalize;
	object_class->set_property = gedit_snippets_plugin_set_property;
	object_class->get_property = gedit_snippets_plugin_get_property;
	
	configuration_init();
	load_configuration();

	g_object_class_override_property(object_class, PROP_WINDOW, "window");
	g_object_class_override_property(object_class, PROP_APP, "app");
}

static void gedit_snippets_plugin_class_finalize(GeditSnippetsPluginClass *klass)
{
	g_hash_table_destroy(GLOBAL_POSITION_INFO_HASH_TABLE);

	configuration_finalize();

	Py_FinalizeEx();
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
