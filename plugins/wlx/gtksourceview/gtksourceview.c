/*
   http://www.bravegnu.org/gtktext/x561.html
   Joe Arose updated code to gtk3 and gtksourceview-3.0.
*/

#define _GNU_SOURCE

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gtksourceview/gtksourceview.h>
#include <gtksourceview/gtksourceiter.h>
#include <gtksourceview/gtksourcebuffer.h>
#include <gtksourceview/gtksourcelanguage.h>
#include <gtksourceview/gtksourcelanguagemanager.h>
#include <gtksourceview/gtksourcestyleschememanager.h>
#include <dlfcn.h>
#include <limits.h>
#include <string.h>
#include <wctype.h>
#include "wlxplugin.h"

#include <glib/gi18n.h>
#include <locale.h>

#define GETTEXT_PACKAGE "plugins"
#define MARK_NAME "last_pos"
#define BUFF_NAME "srcbuf"
#define LANGMGR_NAME "languages-manager"

#define _detectstring "EXT=\"C\"|EXT=\"H\"|EXT=\"LUA\"|EXT=\"CPP\"|EXT=\"HPP\"|EXT=\"PAS\"|\
EXT=\"CSS\"|EXT=\"SH\"|EXT=\"XML\"|EXT=\"INI\"|EXT=\"DIFF\"|EXT=\"PATCH\"|EXT=\"PO\"|EXT=\"PY\"|\
EXT=\"XSL\"|EXT=\"LPR\"|EXT=\"PP\"|EXT=\"LPI\"|EXT=\"LFM\"|EXT=\"LPK\"|EXT=\"DOF\"|EXT=\"DPR\""

#define CTRL_ALT_MASK (GDK_CONTROL_MASK | GDK_MOD1_MASK)

GtkWrapMode wrap_mode;
gchar *font, *style, *def_lang;

gchar *mask1, *lang1, *mask2, *lang2, *mask3, *lang3, *mask4, *lang4;
gchar *mask5, *lang5, *mask6, *lang6, *mask7, *lang7, *mask8, *lang8;
gboolean force1, force2, force3, force4, force5, force6, force7, force8;

gboolean line_num, hcur_line, draw_spaces, no_cursor;
gint s_tab, p_above, p_below;

GtkTextIter cur_iter;
gboolean init_cur_iter;
static const gchar *INSERT_MARK_NAME = "insert";
static const gchar *SELECT_MARK_NAME = "selection_bound";

enum
{
	NONE_SEARCH,
	L_WORD_SEARCH,
	R_WORD_SEARCH,
	LR_WORD_SEARCH,
	L_NONWORD_SEARCH,
	R_NONWORD_SEARCH,
	LR_NONWORD_SEARCH,
	L_NONSPACE_SEARCH,
	R_NONSPACE_SEARCH,
	LR_NONSPACE_SEARCH
};
enum
{
	NONE_KEY,
	CTRL_KEY,
	CTRL_ALT_KEY
};


static GtkWidget *getFirstChild(GtkWidget *w)
{
	GList *list = gtk_container_get_children(GTK_CONTAINER(w));
	GtkWidget *result = GTK_WIDGET(list->data);
	g_list_free(list);
	return result;
}

static gboolean open_file(GtkSourceBuffer *sBuf, const gchar *filename);


static gboolean is_word_char(gunichar ch, gpointer data)
{
	return iswalnum(ch) || ch == '_' || ch == '-';
}
static gboolean is_word_break(gunichar ch, gpointer data)
{
	return !is_word_char(ch, data);
}
static gboolean is_wordext_break(gunichar ch, gpointer data)
{
	return !is_word_char(ch, data) && ch != '.';
}
static gboolean is_space_char(gunichar ch, gpointer data)
{
	return iswspace(ch);
}

static GtkTextCharPredicate get_pred(const guint search_type,
									 const gboolean extended_word)
{
	GtkTextCharPredicate pred;
	if (search_type == R_WORD_SEARCH ||
		search_type == L_WORD_SEARCH ||
		search_type == LR_WORD_SEARCH)
		if (extended_word)
			pred = (GtkTextCharPredicate)is_wordext_break;
		else
			pred = (GtkTextCharPredicate)is_word_break;
	else if (search_type == L_NONWORD_SEARCH ||
			 search_type == R_NONWORD_SEARCH ||
			 search_type == LR_NONWORD_SEARCH)
		pred = (GtkTextCharPredicate)is_word_char;
	else
		pred = (GtkTextCharPredicate)is_space_char;
	return pred;
}

void on_mark_set(GtkSourceBuffer *sBuf, GtkTextIter *iter,
				 GtkTextMark *mark, gpointer data)
{
	const gchar *mark_name = gtk_text_mark_get_name(mark);
	if (mark_name == NULL)
		return;
	
	gboolean has_selection;
	GtkTextIter cur_start, cur_end;
	has_selection = gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(sBuf),
														 &cur_start, &cur_end);
	
	if (strcmp(mark_name, INSERT_MARK_NAME) == 0)
	{
		if (!has_selection || gtk_text_iter_equal(&cur_start, &cur_end))
		{
			cur_iter = *iter;
			init_cur_iter = TRUE;
		}
	}
	else if (strcmp(mark_name, SELECT_MARK_NAME) == 0)
	{
		// if no selection -> return
		if (!has_selection) return;
		// if no cur_iter -> return (if seach case)
		if (!init_cur_iter) return;
		// if select-all -> return
		if (gtk_text_iter_is_start(&cur_start) &&
			gtk_text_iter_is_end(&cur_end)) return;
		// if empty string -> unselect and return
		if (gtk_text_iter_starts_line(&cur_iter) &&
			gtk_text_iter_ends_line(&cur_iter))
		{
			gtk_text_buffer_select_range(GTK_TEXT_BUFFER(sBuf),
										 &cur_iter, &cur_iter);
			return;
		}
		// if GDK_3BUTTON_PRESS - select full line (default behavior -> so exit)
		GdkEvent *event = gtk_get_current_event();
		//~ g_print("gtksourceview.wlx, event.type = %d\n", event->type);
		if (event != NULL && event->type == GDK_3BUTTON_PRESS)
		{
			gdk_event_free(event);
			return;
		}
		guint mod_key = NONE_KEY;
		GdkModifierType state;
		if (gdk_event_get_state(event, &state))
		{
			if ((state & CTRL_ALT_MASK) == GDK_CONTROL_MASK)
				mod_key = CTRL_KEY;
			else if ((state & CTRL_ALT_MASK) == CTRL_ALT_MASK)
				mod_key = CTRL_ALT_KEY;
		}
		gdk_event_free(event);
		//~ g_print("gtksourceview.wlx, mod_key = %i\n", mod_key);
		
		GtkTextIter new_start, new_end;
		GtkTextIter pre_iter = cur_iter;
		gtk_text_iter_backward_char(&pre_iter);
		
		GtkTextIter left_lim, right_lim;
		gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(sBuf), &left_lim,
										 gtk_text_iter_get_line(&cur_iter));
		right_lim = left_lim;
		gtk_text_iter_forward_to_line_end(&right_lim);
		
		guint search_type = NONE_SEARCH;
		
		//~ calc search_type (with ctrl+alt key pressed)
		//~ (select non-space sequence)
		if (mod_key == CTRL_ALT_KEY)
		{
			if (gtk_text_iter_starts_line(&cur_iter))
				search_type = R_NONSPACE_SEARCH;
			else if (gtk_text_iter_ends_line(&cur_iter))
				search_type = L_NONSPACE_SEARCH;
			else if (!is_space_char(gtk_text_iter_get_char(&cur_iter), NULL) &&
					 is_space_char(gtk_text_iter_get_char(&pre_iter), NULL))
				search_type = R_NONSPACE_SEARCH;
			else if (is_space_char(gtk_text_iter_get_char(&cur_iter), NULL) &&
					 !is_space_char(gtk_text_iter_get_char(&pre_iter), NULL))
				search_type = L_NONSPACE_SEARCH;
			else if (!is_space_char(gtk_text_iter_get_char(&cur_iter), NULL) &&
					 !is_space_char(gtk_text_iter_get_char(&pre_iter), NULL))
				search_type = LR_NONSPACE_SEARCH;
		}
		
		//~ calc search_type
		if (search_type == NONE_SEARCH)
		{
			if (gtk_text_iter_starts_line(&cur_iter))
			{
				if (is_word_char(gtk_text_iter_get_char(&cur_iter), NULL))
					search_type = R_WORD_SEARCH;
				else
					search_type = R_NONWORD_SEARCH;
			}
			else if (gtk_text_iter_ends_line(&cur_iter))
			{
				if (is_word_char(gtk_text_iter_get_char(&pre_iter), NULL))
					search_type = L_WORD_SEARCH;
				else
					search_type = L_NONWORD_SEARCH;
			}
			else if (is_word_char(gtk_text_iter_get_char(&cur_iter), NULL) &&
					 is_word_break(gtk_text_iter_get_char(&pre_iter), NULL))
				search_type = R_WORD_SEARCH;
			else if (is_word_break(gtk_text_iter_get_char(&cur_iter), NULL) &&
					 is_word_char(gtk_text_iter_get_char(&pre_iter), NULL))
				search_type = L_WORD_SEARCH;
			else if (is_word_char(gtk_text_iter_get_char(&cur_iter), NULL))
				search_type = LR_WORD_SEARCH;
			else
				search_type = LR_NONWORD_SEARCH;
		}
		//~ g_print("gtksourceview.wlx, search_type = %i\n", search_type);
		
		//~ apply search_type
		GtkTextCharPredicate pred = get_pred(search_type,
											 (mod_key == CTRL_KEY));
		if (search_type == R_WORD_SEARCH || search_type == R_NONWORD_SEARCH)
		{
			//~ right search
			new_start = cur_iter;
			new_end = new_start;
			gtk_text_iter_forward_find_char(&new_end, pred, NULL, &right_lim);
		}
		else if (search_type == L_WORD_SEARCH || search_type == L_NONWORD_SEARCH)
		{
			//~ left search
			new_end = cur_iter;
			new_start = new_end;
			gtk_text_iter_backward_find_char(&new_start, pred, NULL, &left_lim);
			if (pred(gtk_text_iter_get_char(&new_start), NULL))
				gtk_text_iter_forward_char(&new_start);
		}
		else
		{
			//~ left/right search
			new_start = cur_iter;
			gtk_text_iter_backward_find_char(&new_start, pred, NULL, &left_lim);
			if (pred(gtk_text_iter_get_char(&new_start), NULL))
				gtk_text_iter_forward_char(&new_start);
			new_end = cur_iter;
			gtk_text_iter_forward_find_char(&new_end, pred, NULL, &right_lim);
		}
		if (!gtk_text_iter_equal(&cur_start, &new_start) ||
			!gtk_text_iter_equal(&cur_end, &new_end))
			gtk_text_buffer_select_range(GTK_TEXT_BUFFER(sBuf),
										 &new_start, &new_end);
	}
}

HWND DCPCALL ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags)
{
	GtkWidget *gFix;
	GtkWidget *pScrollWin;
	GtkWidget *sView;
	GtkSourceLanguageManager *lm;
	GtkSourceStyleSchemeManager *scheme_manager;
	GtkSourceStyleScheme *scheme;
	GtkSourceBuffer *sBuf;
	
	gFix = gtk_vbox_new(FALSE, 5);
	gtk_container_add(GTK_CONTAINER(GTK_WIDGET(ParentWin)), gFix);
	
	/* Create a Scrolled Window that will contain the GtkSourceView */
	pScrollWin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pScrollWin),
								   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	
	/* Now create a GtkSourceLanguageManager */
	lm = gtk_source_language_manager_new();
	
	/* and a GtkSourceBuffer to hold text (similar to GtkTextBuffer) */
	sBuf = GTK_SOURCE_BUFFER(gtk_source_buffer_new(NULL));
	g_object_ref(lm);
	g_object_set_data_full(G_OBJECT(sBuf), LANGMGR_NAME, lm,
						   (GDestroyNotify)g_object_unref);
	g_object_set_data_full(G_OBJECT(gFix), BUFF_NAME, sBuf,
						   (GDestroyNotify)g_object_unref);
	
	/* Create the GtkSourceView and associate it with the buffer */
	sView = gtk_source_view_new_with_buffer(sBuf);
	gtk_widget_modify_font(sView, pango_font_description_from_string(font));
	gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(sView), line_num);
	gtk_source_view_set_tab_width(GTK_SOURCE_VIEW(sView), s_tab);
	gtk_source_view_set_highlight_current_line(GTK_SOURCE_VIEW(sView), hcur_line);
	
	if (draw_spaces)
		gtk_source_view_set_draw_spaces(GTK_SOURCE_VIEW(sView),
										GTK_SOURCE_DRAW_SPACES_ALL);
	
	/* Attach the GtkSourceView to the scrolled Window */
	gtk_container_add(GTK_CONTAINER(pScrollWin), GTK_WIDGET(sView));
	gtk_container_add(GTK_CONTAINER(gFix), pScrollWin);
	
	if (!open_file(sBuf, FileToLoad))
	{
		gtk_widget_destroy(GTK_WIDGET(gFix));
		return NULL;
	}
	
	scheme_manager = gtk_source_style_scheme_manager_get_default();
	scheme = gtk_source_style_scheme_manager_get_scheme(scheme_manager, style);
	gtk_source_buffer_set_style_scheme(sBuf, scheme);
	gtk_text_view_set_editable(GTK_TEXT_VIEW(sView), FALSE);
	
	if (no_cursor)
		gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(sView), FALSE);
	
	gtk_text_view_set_pixels_above_lines(GTK_TEXT_VIEW(sView), p_above);
	gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(sView), p_below);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(sView), wrap_mode);
	
	//~ esh: for custom selection algorithm by double-click
	init_cur_iter = FALSE;
	g_signal_connect(sBuf, "mark-set", G_CALLBACK(on_mark_set), NULL);
	
	gtk_widget_show_all(gFix);
	
	return gFix;
}

int DCPCALL ListLoadNext(HWND ParentWin, HWND PluginWin,
						 char* FileToLoad, int ShowFlags)
{
	GtkSourceBuffer *sBuf = g_object_get_data(G_OBJECT(PluginWin), BUFF_NAME);
	gtk_text_buffer_set_text(GTK_TEXT_BUFFER(sBuf), "", 0);
	
	init_cur_iter = FALSE;
	
	if (!open_file(sBuf, FileToLoad))
		return LISTPLUGIN_ERROR;
	
	//~ esh: delete search mark for next/prev doc
	GtkTextMark *last_pos;
	last_pos = gtk_text_buffer_get_mark(GTK_TEXT_BUFFER(sBuf), MARK_NAME);
	if (last_pos != NULL)
		gtk_text_buffer_delete_mark(GTK_TEXT_BUFFER(sBuf), last_pos);
	
	return LISTPLUGIN_OK;
}

static void define_lang(GtkSourceLanguage **lang,
						GtkSourceLanguageManager *lm,
						const gchar *search)
{
	//~ g_print("Search string for define lang: %s\n", search);
	
	if ((*lang == NULL || force1) &&
			 (g_strrstr(mask1, search) != NULL) &&
			 (mask1 != NULL) && (lang1 != NULL))
		*lang = gtk_source_language_manager_get_language(lm, lang1);
	
	else if ((*lang == NULL || force2) &&
			 (g_strrstr(mask2, search) != NULL) &&
			 (mask2 != NULL) && (lang2 != NULL))
		*lang = gtk_source_language_manager_get_language(lm, lang2);
	
	else if ((*lang == NULL || force3) &&
			 (g_strrstr(mask3, search) != NULL) &&
			 (mask3 != NULL) && (lang3 != NULL))
		*lang = gtk_source_language_manager_get_language(lm, lang3);
	
	else if ((*lang == NULL || force4) &&
			 (g_strrstr(mask4, search) != NULL) &&
			 (mask4 != NULL) && (lang4 != NULL))
		*lang = gtk_source_language_manager_get_language(lm, lang4);
	
	else if ((*lang == NULL || force5) &&
			 (g_strrstr(mask5, search) != NULL) &&
			 (mask5 != NULL) && (lang5 != NULL))
		*lang = gtk_source_language_manager_get_language(lm, lang5);
	
	else if ((*lang == NULL || force6) &&
			 (g_strrstr(mask6, search) != NULL) &&
			 (mask6 != NULL) && (lang6 != NULL))
		*lang = gtk_source_language_manager_get_language(lm, lang6);
	
	else if ((*lang == NULL || force7) &&
			 (g_strrstr(mask7, search) != NULL) &&
			 (mask7 != NULL) && (lang7 != NULL))
		*lang = gtk_source_language_manager_get_language(lm, lang7);
	
	else if ((*lang == NULL || force8) &&
			 (g_strrstr(mask8, search) != NULL) &&
			 (mask8 != NULL) && (lang8 != NULL))
		*lang = gtk_source_language_manager_get_language(lm, lang8);
}

static gboolean open_file(GtkSourceBuffer *sBuf, const gchar *filename)
{
	GtkSourceLanguageManager *lm;
	GtkSourceLanguage *lang = NULL;
	GError *err = NULL;
	gboolean reading;
	GtkTextIter iter;
	GIOChannel *io;
	gchar *buffer;
	const gchar *content_type;
	
	g_return_val_if_fail(sBuf != NULL, FALSE);
	g_return_val_if_fail(filename != NULL, FALSE);
	g_return_val_if_fail(GTK_SOURCE_BUFFER(sBuf), FALSE);
	
	/* get the Language for source mimetype */
	lm = g_object_get_data(G_OBJECT(sBuf), LANGMGR_NAME);
	
	GFile *gfile = g_file_new_for_path(filename);
	g_return_val_if_fail(gfile != NULL, FALSE);
	GFileInfo *fileinfo = g_file_query_info(gfile,
											G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
											0, NULL, NULL);
	g_return_val_if_fail(fileinfo != NULL, FALSE);
	
	content_type = g_file_info_get_content_type(fileinfo);
	
	lang = gtk_source_language_manager_guess_language(lm, filename, content_type);
	
	g_object_unref(fileinfo);
	g_object_unref(gfile);
	
	gchar *ext = g_strrstr(filename, ".");
	if (ext != NULL)
	{
		ext = g_strdup_printf("%s;", ext);				// allocate memory
		gchar *l_ext = g_ascii_strdown(ext, -1);		// allocate memory
		g_free(ext);									// free memory
		if (*l_ext != '\0')
			define_lang(&lang, lm, l_ext);
		g_free(l_ext);									// free memory
	}
	
	gchar *fname = g_strrstr(filename, "/");
	if (fname != NULL)
	{
		fname++; // without startswith "/"
		fname = g_strdup_printf("%s;", fname);			// allocate memory
		ext = g_strrstr(fname, ".");
		if (ext != NULL) *ext = '\0';
		gchar *l_fname = g_ascii_strdown(fname, -1);	// allocate memory
		g_free(fname);									// free memory
		if (*l_fname != '\0')
			define_lang(&lang, lm, l_fname);
		g_free(l_fname);								// free memory
	}
	
	//g_return_val_if_fail(lang != NULL, FALSE);
	if ((!lang) && (def_lang != NULL))
		lang = gtk_source_language_manager_get_language(lm, def_lang);
	if (!lang) return FALSE;
	
	gtk_source_buffer_set_language(sBuf, lang);
	g_print("%s [%s]\n", _("Language:"), gtk_source_language_get_name(lang));
	
	/* Now load the file from Disk */
	io = g_io_channel_new_file(filename, "r", &err);
	if (!io)
	{
		g_print("gtksourceview.wlx (%s): %s\n", filename, (err)->message);
		return FALSE;
	}
	
	if (g_io_channel_set_encoding(io, "utf-8", &err) != G_IO_STATUS_NORMAL)
	{
		g_print("gtksourceview.wlx (%s): %s: %s\n", filename,
				_("Failed to set encoding"), (err)->message);
		return FALSE;
	}
	
	gtk_source_buffer_begin_not_undoable_action(sBuf);
	
	//gtk_text_buffer_set_text (GTK_TEXT_BUFFER(sBuf), "", 0);
	buffer = g_malloc(4096);
	reading = TRUE;
	
	gboolean err_convert = FALSE;
	while (reading)
	{
		gsize bytes_read;
		GIOStatus status;
		
		status = g_io_channel_read_chars(io, buffer, 4096, &bytes_read, &err);
		switch (status)
		{
		case G_IO_STATUS_EOF:
			reading = FALSE;
			
		case G_IO_STATUS_NORMAL:
			if (bytes_read == 0) continue;
			
			gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(sBuf), &iter);
			gtk_text_buffer_insert(GTK_TEXT_BUFFER(sBuf), &iter, buffer, bytes_read);
			break;
			
		case G_IO_STATUS_AGAIN:
			continue;
			
		case G_IO_STATUS_ERROR:
			
		default:
			/* because of error in input we clear already loaded text */
			gtk_text_buffer_set_text(GTK_TEXT_BUFFER(sBuf), "", 0);
			
			if ((err)->code == G_CONVERT_ERROR_ILLEGAL_SEQUENCE && !err_convert)
			{
				err_convert = TRUE;
				err = NULL;
				if (g_io_channel_set_encoding(io, "windows-1251", &err) == G_IO_STATUS_NORMAL &&
					g_io_channel_seek_position(io, 0, G_SEEK_SET, &err) == G_IO_STATUS_NORMAL)
					continue;
			}
			g_print("gtksourceview.wlx (%s): %s\n", filename, (err)->message);
			reading = FALSE;
			break;
		}
	}
	g_free(buffer);
	
	gtk_source_buffer_end_not_undoable_action(sBuf);
	g_io_channel_unref(io);
	
	if (err)
	{
		g_error_free(err);
		return FALSE;
	}
	
	gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(sBuf), FALSE);
	
	/* move cursor to the beginning */
	gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(sBuf), &iter);
	gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(sBuf), &iter);
	
	g_object_set_data_full(G_OBJECT(sBuf), "filename", g_strdup(filename),
						   (GDestroyNotify)g_free);
	return TRUE;
}

void DCPCALL ListCloseWindow(HWND ListWin)
{
	gtk_widget_destroy(GTK_WIDGET(ListWin));
}

void DCPCALL ListGetDetectString(char* DetectString, int maxlen)
{
	g_strlcpy(DetectString, _detectstring, maxlen-1);
}

int DCPCALL ListSearchText(HWND ListWin, char* SearchString, int SearchParameter)
{
	GtkSourceBuffer *sBuf;
	GtkTextMark *last_pos;
	GtkTextIter iter, mstart, mend;
	gboolean found;
	
	init_cur_iter = FALSE;
	
	sBuf = g_object_get_data(G_OBJECT(ListWin), BUFF_NAME);
	last_pos = gtk_text_buffer_get_mark(GTK_TEXT_BUFFER(sBuf), MARK_NAME);
	
	if (last_pos == NULL)
		gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(sBuf), &iter);
	else
		gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(sBuf), &iter, last_pos);
	
	if ((SearchParameter & lcs_backwards) && (SearchParameter & lcs_matchcase))
		found = gtk_source_iter_backward_search(&iter, SearchString,
												GTK_SOURCE_SEARCH_TEXT_ONLY,
												&mend, &mstart, NULL);
	else if (SearchParameter & lcs_matchcase)
		found = gtk_source_iter_forward_search(&iter, SearchString,
											   GTK_SOURCE_SEARCH_TEXT_ONLY,
											   &mstart, &mend, NULL);
	else if (SearchParameter & lcs_backwards)
		found = gtk_source_iter_backward_search(&iter, SearchString,
												GTK_SOURCE_SEARCH_TEXT_ONLY |
													GTK_SOURCE_SEARCH_CASE_INSENSITIVE,
												&mend, &mstart, NULL);
	else
		found = gtk_source_iter_forward_search(&iter, SearchString,
											   GTK_SOURCE_SEARCH_TEXT_ONLY |
													GTK_SOURCE_SEARCH_CASE_INSENSITIVE,
											   &mstart, &mend, NULL);
	
	if (found)
	{
		gtk_text_buffer_select_range(GTK_TEXT_BUFFER(sBuf), &mstart, &mend);
		gtk_text_buffer_create_mark(GTK_TEXT_BUFFER(sBuf), MARK_NAME, &mend, FALSE);
		gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(getFirstChild(
														 getFirstChild(GTK_WIDGET(ListWin)))),
										   gtk_text_buffer_get_mark(GTK_TEXT_BUFFER(sBuf),
																	MARK_NAME));
	}
	else
	{
		GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(
															  GTK_WIDGET(ListWin))),
												   GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
												   GTK_BUTTONS_OK, _("\"%s\" not found!"),
												   SearchString);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	}
	return LISTPLUGIN_OK;
}

int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter)
{
	GtkSourceBuffer *sBuf;
	GtkTextIter p;
	
	sBuf = g_object_get_data(G_OBJECT(ListWin), BUFF_NAME);
	
	switch (Command)
	{
	case lc_copy :
		gtk_text_buffer_copy_clipboard(GTK_TEXT_BUFFER(sBuf),
									   gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
		break;
		
	case lc_selectall :
		gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(sBuf), &p);
		gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(sBuf), &p);
		gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(sBuf), &p);
		gtk_text_buffer_move_mark_by_name(GTK_TEXT_BUFFER(sBuf),
										  "selection_bound", &p);
		break;
		
	default :
		return LISTPLUGIN_ERROR;
	}
	return LISTPLUGIN_OK;
}
void DCPCALL ListSetDefaultParams(ListDefaultParamStruct* dps)
{
	Dl_info dlinfo;
	static char cfg_path[PATH_MAX];
	const char* cfg_file = "settings.ini";
	GKeyFile *cfg;
	GError *err = NULL;
	gboolean bval;
	
	// Find in plugin directory
	memset(&dlinfo, 0, sizeof(dlinfo));
	
	if (dladdr(cfg_path, &dlinfo) != 0)
	{
		strncpy(cfg_path, dlinfo.dli_fname, PATH_MAX);
		char *pos = strrchr(cfg_path, '/');
		if (pos)
			strcpy(pos + 1, cfg_file);
		
		setlocale (LC_ALL, "");
		bindtextdomain(GETTEXT_PACKAGE, g_strdup_printf("%s/langs",
							g_path_get_dirname(dlinfo.dli_fname)));
		textdomain(GETTEXT_PACKAGE);
	}
	
	cfg = g_key_file_new();
	
	if (!g_key_file_load_from_file(cfg, cfg_path, G_KEY_FILE_KEEP_COMMENTS, &err))
	{
		g_print("gtksourceview.wlx (%s): %s\n", cfg_path, (err)->message);
		font = "monospace 12";
		style = "classic";
		wrap_mode = GTK_WRAP_NONE;
		line_num = TRUE;
		hcur_line = TRUE;
		draw_spaces = TRUE;
		no_cursor = TRUE;
		s_tab = 8;
		p_above = 0;
		p_below = 0;
	}
	else
	{
		font = g_key_file_get_string(cfg, "Appearance", "Font", NULL);
		if (!font) font = "monospace 12";
		
		style = g_key_file_get_string(cfg, "Appearance", "Style", NULL);
		if (!style) style = "classic";
		
		bval = g_key_file_get_boolean(cfg, "Flags", "LineNumbers", &err);
		line_num = (bval || err);
		if (err) err = NULL;
		
		bval = g_key_file_get_boolean(cfg, "Flags", "HighlightCurLine", &err);
		hcur_line = (bval || err);
		if (err) err = NULL;
		
		bval = g_key_file_get_boolean(cfg, "Flags", "Spaces", &err);
		draw_spaces = (bval || err);
		if (err) err = NULL;
		
		bval = g_key_file_get_boolean(cfg, "Flags", "NoCursor", &err);
		no_cursor = (bval || err);
		if (err) err = NULL;
		
		s_tab = g_key_file_get_integer(cfg, "Appearance", "TabSize", &err);
		if (err)
		{
			s_tab = 8;
			err = NULL;
		}
		
		p_above = g_key_file_get_integer(cfg, "Appearance", "PAbove", &err);
		if (err)
		{
			p_above = 0;
			err = NULL;
		}
		
		p_below = g_key_file_get_integer(cfg, "Appearance", "PBelow", &err);
		if (err)
		{
			p_below = 0;
			err = NULL;
		}
		
		bval = g_key_file_get_boolean(cfg, "Flags", "Wrap", NULL);
		wrap_mode = bval ? GTK_WRAP_WORD : GTK_WRAP_NONE;
		
		def_lang = g_key_file_get_string(cfg, "Appearance", "DefaultLang", NULL);
		
		mask1 = g_key_file_get_string(cfg, "Override1", "Mask", NULL);
		lang1 = g_key_file_get_string(cfg, "Override1", "Lang", NULL);
		force1 = g_key_file_get_boolean(cfg, "Override1", "Force", &err);
		
		mask2 = g_key_file_get_string(cfg, "Override2", "Mask", NULL);
		lang2 = g_key_file_get_string(cfg, "Override2", "Lang", NULL);
		force2 = g_key_file_get_boolean(cfg, "Override2", "Force", &err);
		
		mask3 = g_key_file_get_string(cfg, "Override3", "Mask", NULL);
		lang3 = g_key_file_get_string(cfg, "Override3", "Lang", NULL);
		force3 = g_key_file_get_boolean(cfg, "Override3", "Force", &err);
		
		mask4 = g_key_file_get_string(cfg, "Override4", "Mask", NULL);
		lang4 = g_key_file_get_string(cfg, "Override4", "Lang", NULL);
		force4 = g_key_file_get_boolean(cfg, "Override4", "Force", &err);
		
		mask5 = g_key_file_get_string(cfg, "Override5", "Mask", NULL);
		lang5 = g_key_file_get_string(cfg, "Override5", "Lang", NULL);
		force5 = g_key_file_get_boolean(cfg, "Override5", "Force", &err);
		
		mask6 = g_key_file_get_string(cfg, "Override6", "Mask", NULL);
		lang6 = g_key_file_get_string(cfg, "Override6", "Lang", NULL);
		force6 = g_key_file_get_boolean(cfg, "Override6", "Force", &err);
		
		mask7 = g_key_file_get_string(cfg, "Override7", "Mask", NULL);
		lang7 = g_key_file_get_string(cfg, "Override7", "Lang", NULL);
		force7 = g_key_file_get_boolean(cfg, "Override7", "Force", &err);
		
		mask8 = g_key_file_get_string(cfg, "Override8", "Mask", NULL);
		lang8 = g_key_file_get_string(cfg, "Override8", "Lang", NULL);
		force8 = g_key_file_get_boolean(cfg, "Override8", "Force", &err);
	}
	
	g_key_file_free(cfg);
	
	if (err)
		g_error_free(err);
}
