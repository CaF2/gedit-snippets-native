#pragma once

#include <gtk/gtk.h>

typedef struct {
	GtkWidget *treeview;
	GtkWidget *textview;
	GtkListStore *store;
} SnippetDialogData;

void create_snippet_dialog(GtkWidget *parent);
