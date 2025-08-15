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
#include "gedit-snippets-configure-window.h"
#include "gedit-snippets-configuration.h"

static void on_snippet_selected(GtkTreeSelection *selection, gpointer user_data)
{
	SnippetDialogData *data = user_data;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data->textview));

	if (gtk_tree_selection_get_selected(selection, &model, &iter))
	{
		gchar *snippet_text;
		gtk_tree_model_get(model, &iter, 1, &snippet_text, -1);
		gtk_text_buffer_set_text(buffer, snippet_text, -1);
		g_free(snippet_text);
	}
}

static void on_add_snippet(GtkButton *button, gpointer user_data)
{
	SnippetDialogData *data = user_data;
	GtkTreeIter iter;
	gtk_list_store_append(data->store, &iter);
	gtk_list_store_set(data->store, &iter, 0, "New Snippet", 1, "// Code here", -1);
}

static void on_remove_snippet(GtkButton *button, gpointer user_data)
{
	SnippetDialogData *data = user_data;
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data->treeview));
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected(selection, &model, &iter))
	{
		gtk_list_store_remove(data->store, &iter);
	}
}

static void on_text_changed(GtkTextBuffer *buffer, gpointer user_data)
{
	SnippetDialogData *data = user_data;
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data->treeview));
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected(selection, &model, &iter))
	{
		GtkTextIter start, end;
		gchar *new_text;

		gtk_text_buffer_get_bounds(buffer, &start, &end);
		new_text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

		gtk_list_store_set(data->store, &iter, 1, new_text, -1);
		g_free(new_text);
	}
}

static void on_rename_snippet(GtkMenuItem *menuitem, gpointer user_data)
{
	SnippetDialogData *data = user_data;
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data->treeview));
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected(selection, &model, &iter))
	{
		gchar *current_name;
		gtk_tree_model_get(model, &iter, 0, &current_name, -1);

		GtkWidget *dialog = gtk_dialog_new_with_buttons(
			"Rename Snippet",
			GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(data->treeview))),
			GTK_DIALOG_MODAL,
			"_OK", GTK_RESPONSE_OK,
			"_Cancel", GTK_RESPONSE_CANCEL,
			NULL);

		GtkWidget *entry = gtk_entry_new();
		gtk_entry_set_text(GTK_ENTRY(entry), current_name);
		gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry);
		gtk_widget_show_all(dialog);

		if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
		{
			const gchar *new_name = gtk_entry_get_text(GTK_ENTRY(entry));
			gtk_list_store_set(data->store, &iter, 0, new_name, -1);
		}

		gtk_widget_destroy(dialog);
		g_free(current_name);
	}
}

static void on_save_snippet(GtkMenuItem *menuitem, gpointer user_data)
{
	SnippetDialogData *data = user_data;
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data->treeview));
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected(selection, &model, &iter))
	{
		gchar *name, *content;
		gtk_tree_model_get(model, &iter, 0, &name, 1, &content, -1);

		g_message("Saving snippet '%s' with content:\n%s", name, content);

		g_free(name);
		g_free(content);
	}
}

static gboolean on_treeview_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	if (event->type == GDK_BUTTON_PRESS && event->button == 3) // Right click
	{
		GtkTreeView *treeview = GTK_TREE_VIEW(widget);
		GtkTreeSelection *selection = gtk_tree_view_get_selection(treeview);
		GtkTreePath *path;

		// Find which row was clicked
		if (gtk_tree_view_get_path_at_pos(treeview,
		                                  (gint)event->x, (gint)event->y,
		                                  &path, NULL, NULL, NULL))
		{
			// Select that row
			gtk_tree_selection_unselect_all(selection);
			gtk_tree_selection_select_path(selection, path);
			gtk_tree_path_free(path);
		}

		// Now build and show the menu
		GtkWidget *menu = gtk_menu_new();
		GtkWidget *rename_item = gtk_menu_item_new_with_label("Rename");
		GtkWidget *save_item = gtk_menu_item_new_with_label("Save");

		g_signal_connect(rename_item, "activate", G_CALLBACK(on_rename_snippet), user_data);
		g_signal_connect(save_item, "activate", G_CALLBACK(on_save_snippet), user_data);

		gtk_menu_shell_append(GTK_MENU_SHELL(menu), rename_item);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), save_item);
		gtk_widget_show_all(menu);

		gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);

		return TRUE; // Event handled
	}
	return FALSE;
}

void create_snippet_dialog(GtkWidget *parent)
{
	GtkWidget *dialog, *content_area, *treeview, *textview, *scrolled_window, *hbox, *vbox, *button_add, *button_remove;
	GtkListStore *store;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;
	SnippetDialogData *data = g_new0(SnippetDialogData, 1);

	dialog = gtk_dialog_new_with_buttons("Manage Snippets", GTK_WINDOW(parent), GTK_DIALOG_MODAL,
	                                     "_Close", GTK_RESPONSE_CLOSE, NULL);
	content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	// Create a horizontal box to contain the treeview and textview
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_container_add(GTK_CONTAINER(content_area), hbox);

	// Left side - Snippet list
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 5);

	// Snippet list store
	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	data->store = store;

	// TreeView
	treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_signal_connect(treeview, "button-press-event", G_CALLBACK(on_treeview_button_press), data);
	data->treeview = treeview;

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Snippet", renderer, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
	gtk_box_pack_start(GTK_BOX(vbox), treeview, TRUE, TRUE, 5);

	// Buttons
	button_add = gtk_button_new_with_label("Add");
	button_remove = gtk_button_new_with_label("Remove");
	gtk_box_pack_start(GTK_BOX(vbox), button_add, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(vbox), button_remove, FALSE, FALSE, 2);

	// Right side - Text editor
	scrolled_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_size_request(scrolled_window, 300, 200);
	gtk_box_pack_start(GTK_BOX(hbox), scrolled_window, TRUE, TRUE, 5);

	textview = gtk_text_view_new();
	data->textview = textview;
	gtk_container_add(GTK_CONTAINER(scrolled_window), textview);

	if(GLOBAL_SNIPPETS)
	{
		for(int i=0;i<GLOBAL_SNIPPETS->len;i++)
		{
			SnippetBlock *sblk=g_ptr_array_index(GLOBAL_SNIPPETS,i);
			
			for(int j=0;j<sblk->nodes->len;j++)
			{
				SnippetTranslation *tmp=g_ptr_array_index(sblk->nodes,j);
				
				g_autoptr(GString) label_string=g_string_sized_new(10);
				
				for(int k=0;k<tmp->programming_languages->len;k++)
				{
					const char *langauage=g_ptr_array_index(tmp->programming_languages,k);
					
					g_string_append_printf(label_string,"%s%s",k==0?"":",",langauage);
				}
				
				g_string_append_printf(label_string,": %s",tmp->from);
				
				GtkTreeIter iter;
				gtk_list_store_append(data->store, &iter);
				gtk_list_store_set(data->store, &iter, 0, label_string->str, 1, tmp->to, -1);
			}
		}
	}

	// Connect signals
	g_signal_connect(selection, "changed", G_CALLBACK(on_snippet_selected), data);
	g_signal_connect(button_add, "clicked", G_CALLBACK(on_add_snippet), data);
	g_signal_connect(button_remove, "clicked", G_CALLBACK(on_remove_snippet), data);
	g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview)), "changed", G_CALLBACK(on_text_changed), data);

	gtk_widget_show_all(dialog);
	g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);
}

