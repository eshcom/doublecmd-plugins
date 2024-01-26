#include <string.h>

#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/video/videooverlay.h>

#include <gdk/gdk.h>
#if defined (GDK_WINDOWING_X11)
#include <gdk/gdkx.h>
#elif defined (GDK_WINDOWING_WIN32)
#include <gdk/gdkwin32.h>
#elif defined (GDK_WINDOWING_QUARTZ)
#include <gdk/gdkquartz.h>
#endif

#include <gdk/gdkkeysyms.h>

#include "wlxplugin.h"


#define DURATION_EMPTY "-:--:--/-:--:--"
#define GST_TIME_MYFORMAT "u:%02u:%02u"
#define GST_TIME_MYARGS(t) \
        GST_CLOCK_TIME_IS_VALID (t) ? \
        (guint) (((GstClockTime)(t)) / (GST_SECOND * 60 * 60)) : 99, \
        GST_CLOCK_TIME_IS_VALID (t) ? \
        (guint) ((((GstClockTime)(t)) / (GST_SECOND * 60)) % 60) : 99, \
        GST_CLOCK_TIME_IS_VALID (t) ? \
        (guint) ((((GstClockTime)(t)) / GST_SECOND) % 60) : 99

gboolean gLoop = FALSE;
gboolean gVis = FALSE;
gboolean gMute = FALSE;
gdouble gVolume = 0.8;
static char gCfgPath[PATH_MAX]; 
static char gFont[PATH_MAX] = ""; 

/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData {
  GstElement *playbin;            /* Our one and only pipeline */

  GtkWidget *slider;              /* Slider widget to keep track of current position */
  GtkWidget *streams_list;        /* Text widget to display info about the streams */
  gulong slider_update_signal_id; /* Signal ID for the slider update signal */

  GtkWidget *volume_slider;
  GtkToolItem *btn_play;
  GtkToolItem *btn_loop;
  GtkToolItem *btn_mute;
  GtkToolItem *btn_info;
  GtkWidget *notebook;
  GtkWidget *lbl_info;
  GtkWidget *lbl_duration;

  GstState state;                 /* Current state of the pipeline */
  gint64 duration;                /* Duration of the clip, in nanoseconds */
  guint timer;                    /* Refresh timer */
  gboolean init_video;
} CustomData;

typedef enum {
  GST_PLAY_FLAG_VIDEO         = (1 << 0), /* We want video output */
  GST_PLAY_FLAG_AUDIO         = (1 << 1), /* We want audio output */
  GST_PLAY_FLAG_TEXT          = (1 << 2), /* We want subtitle output */
  GST_PLAY_FLAG_VIS           = (1 << 3), /* Enable rendering of visualizations when there is no video stream. */
} GstPlayFlags;


/* This function is called when the GUI toolkit creates the physical window that will hold the video.
 * At this point we can retrieve its handler (which has a different meaning depending on the windowing system)
 * and pass it to GStreamer through the XOverlay interface. */
static void realize_cb (GtkWidget *widget, CustomData *data) {
  GdkWindow *window = gtk_widget_get_window (widget);
  guintptr window_handle;

  if (!gdk_window_ensure_native (window))
    g_error ("Couldn't create native window needed for GstXOverlay!");

  /* Retrieve window handler from GDK */
#if defined (GDK_WINDOWING_WIN32)
  window_handle = (guintptr)GDK_WINDOW_HWND (window);
#elif defined (GDK_WINDOWING_QUARTZ)
  window_handle = gdk_quartz_window_get_nsview (window);
#elif defined (GDK_WINDOWING_X11)
  window_handle = GDK_WINDOW_XID (window);
#endif
  /* Pass it to playbin, which implements XOverlay and will forward it to the video sink */
  gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (data->playbin), window_handle);
}

static gboolean duration_scroll_cb (GtkRange *range, GdkEventScroll *event, CustomData *data) {
  gdouble value = gtk_range_get_value (range);

  if (event->direction == GDK_SCROLL_UP) {
    gtk_range_set_value (GTK_RANGE (data->slider), value + 1);
    return TRUE;
  } else if (event->direction == GDK_SCROLL_DOWN) {
    gtk_range_set_value (GTK_RANGE (data->slider), value - 1);
    return TRUE;
  }
  return FALSE;  
}

static gboolean volume_scroll_cb (GtkRange *range, GdkEventScroll *event, CustomData *data) {
  gdouble value = gtk_range_get_value (range);

  if (event->direction == GDK_SCROLL_UP) {
    gtk_range_set_value (GTK_RANGE (data->volume_slider), value + 0.05);
    return TRUE;
  } else if (event->direction == GDK_SCROLL_DOWN) {
    gtk_range_set_value (GTK_RANGE (data->volume_slider), value - 0.05);
    return TRUE;
  }
  return FALSE;
}
static void volume_cb (GtkRange *range, CustomData *data) {
  gdouble value = gtk_range_get_value (range);

  if (value > 0) {
    gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_mute), FALSE);

    if (value > 0.7)
      gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (data->btn_mute), "audio-volume-high");
    else if (value > 0.3)
      gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (data->btn_mute), "audio-volume-medium");
    else if (value > 0)
      gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (data->btn_mute), "audio-volume-low");

    g_object_set (data->playbin, "volume", value, NULL);
  } else
    gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_mute), TRUE);
}

static void mute_cb (GtkToolItem *btn_mute, CustomData *data) {
  if (gtk_toggle_tool_button_get_active (GTK_TOGGLE_TOOL_BUTTON (btn_mute))) {
    gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (btn_mute), "audio-volume-muted");
    g_object_set (data->playbin, "mute", TRUE, NULL);
  } else {
    g_object_set (data->playbin, "mute", FALSE, NULL);

    if (gtk_range_get_value (GTK_RANGE (data->volume_slider)) == 0)
      gtk_range_set_value (GTK_RANGE (data->volume_slider), 0.05);

    volume_cb (GTK_RANGE (data->volume_slider), data);
  }
}

static void info_cb (GtkToolItem *btn_info, CustomData *data) {
  if (gtk_toggle_tool_button_get_active (GTK_TOGGLE_TOOL_BUTTON (btn_info))) {
    gtk_notebook_set_current_page (GTK_NOTEBOOK (data->notebook), 1);
  } else {
    gtk_notebook_set_current_page (GTK_NOTEBOOK (data->notebook), 0);
  }
}

static void playpause_cb (GtkButton *button, CustomData *data) {
  if (data->state == GST_STATE_PLAYING)
    gst_element_set_state (data->playbin, GST_STATE_PAUSED);
  else
    gst_element_set_state (data->playbin, GST_STATE_PLAYING);
}

static gboolean on_button_press (GtkWidget *widget, GdkEventButton *event, CustomData *data) {
  playpause_cb (NULL, data);
  return TRUE;
}

gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, CustomData *data) {
  gdouble value;
  switch (event->keyval) {
    case GDK_KEY_m:
      if (gtk_toggle_tool_button_get_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_mute)))
        gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_mute), FALSE);
      else
        gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_mute), TRUE);
      return TRUE;

    case GDK_KEY_space:
      playpause_cb (NULL, data);
      return TRUE;

    case GDK_KEY_i:
      if (gtk_toggle_tool_button_get_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_info)))
        gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_info), FALSE);
      else
        gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_info), TRUE);
      return TRUE;

    case GDK_KEY_r:
      if (gtk_toggle_tool_button_get_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_loop)))
        gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_loop), FALSE);
      else
        gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_loop), TRUE);
      return TRUE;

    case GDK_KEY_Left:
      value = gtk_range_get_value (GTK_RANGE (data->slider));
      value = value - 1;
      gtk_range_set_value (GTK_RANGE (data->slider), value);
      return TRUE;

    case GDK_KEY_Right:
      value = gtk_range_get_value (GTK_RANGE (data->slider));
      value = value + 1;
      gtk_range_set_value (GTK_RANGE (data->slider), value);
      return TRUE;

    case GDK_KEY_equal:
      value = gtk_range_get_value (GTK_RANGE (data->volume_slider));
      value = value + 0.05;
      gtk_range_set_value (GTK_RANGE (data->volume_slider), value);
      return TRUE;

    case GDK_KEY_minus:
      value = gtk_range_get_value (GTK_RANGE (data->volume_slider));
      value = value - 0.05;
      gtk_range_set_value (GTK_RANGE (data->volume_slider), value);
      return TRUE;
  }
  return FALSE;
}

/* This function is called when the PLAY button is clicked */
/*
static void play_cb (GtkButton *button, CustomData *data) {
  gst_element_set_state (data->playbin, GST_STATE_PLAYING);
}
*/
/* This function is called when the PAUSE button is clicked */
/*
static void pause_cb (GtkButton *button, CustomData *data) {
  gst_element_set_state (data->playbin, GST_STATE_PAUSED);
}
*/
/* This function is called when the STOP button is clicked */
static void stop_cb (GtkButton *button, CustomData *data) {
  gst_element_set_state (data->playbin, GST_STATE_READY);
}

/* This function is called when the main window is closed */
static void delete_event_cb (GtkWidget *widget, GdkEvent *event, CustomData *data) {
  stop_cb (NULL, data);
}

/* This function is called everytime the video window needs to be redrawn (due to damage/exposure,
 * rescaling, etc). GStreamer takes care of this in the PAUSED and PLAYING states, otherwise,
 * we simply draw a black rectangle to avoid garbage showing up. */
static gboolean expose_cb (GtkWidget *widget, GdkEventExpose *event, CustomData *data) {
  if (data->state < GST_STATE_PAUSED) {
    GtkAllocation allocation;
    GdkWindow *window = gtk_widget_get_window (widget);
    cairo_t *cr;

    /* Cairo is a 2D graphics library which we use here to clean the video window.
     * It is used by GStreamer for other reasons, so it will always be available to us. */
    gtk_widget_get_allocation (widget, &allocation);
    cr = gdk_cairo_create (window);
    cairo_set_source_rgb (cr, 0, 0, 0);
    cairo_rectangle (cr, 0, 0, allocation.width, allocation.height);
    cairo_fill (cr);
    cairo_destroy (cr);
  }

  return FALSE;
}

/* This function is called when the slider changes its position. We perform a seek to the
 * new position here. */
static void slider_cb (GtkRange *range, CustomData *data) {
  gdouble value = gtk_range_get_value (GTK_RANGE (data->slider));
  gst_element_seek_simple (data->playbin, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
      (gint64)(value * GST_SECOND));
}

/* This creates all the GTK+ widgets that compose our application, and registers the callbacks */
static GtkWidget *create_ui (GtkWidget *ParentWin, CustomData *data) {
  GtkWidget *main_window;  /* The uppermost window, containing all other windows */
  GtkWidget *video_window; /* The drawing area where the video will be shown */
  GtkWidget *main_box;     /* VBox to hold main_hbox and the controls */
  GtkWidget *controls;     /* HBox to hold the buttons and the slider */
  //GtkToolItem *play_button, *pause_button, *stop_button; /* Buttons */
  GtkToolItem *stop_button;
  GtkWidget *scroll_window;
  GdkColor color;

  main_window = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (ParentWin), main_window);
  g_signal_connect (G_OBJECT (main_window), "delete-event", G_CALLBACK (delete_event_cb), data);

  video_window = gtk_drawing_area_new ();
  gtk_widget_set_double_buffered (video_window, FALSE);
  gtk_widget_add_events (video_window, GDK_BUTTON_PRESS_MASK);
  gdk_color_parse ("black", &color);
  gtk_widget_modify_bg (video_window, GTK_STATE_NORMAL, &color);
  g_signal_connect (video_window, "realize", G_CALLBACK (realize_cb), data);
  g_signal_connect (video_window, "expose_event", G_CALLBACK (expose_cb), data);
  g_signal_connect (G_OBJECT (video_window), "button_press_event", G_CALLBACK (on_button_press), data);

  data->btn_play = gtk_tool_button_new (NULL, NULL);
  gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (data->btn_play), "media-playback-start");
  g_signal_connect (G_OBJECT (data->btn_play), "clicked", G_CALLBACK (playpause_cb), data);
/*
  play_button = gtk_tool_button_new_from_stock (GTK_STOCK_MEDIA_PLAY);
  g_signal_connect (G_OBJECT (play_button), "clicked", G_CALLBACK (play_cb), data);

  pause_button = gtk_tool_button_new_from_stock (GTK_STOCK_MEDIA_PAUSE);
  g_signal_connect (G_OBJECT (pause_button), "clicked", G_CALLBACK (pause_cb), data);
*/
  stop_button = gtk_tool_button_new_from_stock (GTK_STOCK_MEDIA_STOP);
  g_signal_connect (G_OBJECT (stop_button), "clicked", G_CALLBACK (stop_cb), data);

  data->btn_loop = gtk_toggle_tool_button_new ();
  gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (data->btn_loop), "media-playlist-repeat");
  gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_loop), gLoop);

  data->btn_mute = gtk_toggle_tool_button_new ();
  gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON (data->btn_mute), "audio-volume-high");
  g_signal_connect (G_OBJECT (data->btn_mute), "clicked", G_CALLBACK (mute_cb), data);

  data->btn_info = gtk_toggle_tool_button_new ();
  gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (data->btn_info), "dialog-information");
  g_signal_connect (G_OBJECT (data->btn_info), "clicked", G_CALLBACK (info_cb), data);

  data->lbl_info = gtk_label_new (NULL);
  data->lbl_duration = gtk_label_new (DURATION_EMPTY);

  data->slider = gtk_hscale_new_with_range (0, 100, 1);
  gtk_scale_set_draw_value (GTK_SCALE (data->slider), 0);
  gtk_widget_set_can_focus (data->slider, FALSE);
  data->slider_update_signal_id = g_signal_connect (G_OBJECT (data->slider), "value-changed", G_CALLBACK (slider_cb), data);
  g_signal_connect (G_OBJECT (data->slider), "scroll-event", G_CALLBACK (duration_scroll_cb), data);

  data->volume_slider = gtk_hscale_new_with_range (0.05, 1, 0.05);
  gtk_widget_set_can_focus (data->volume_slider, FALSE);
  gtk_scale_set_draw_value (GTK_SCALE (data->volume_slider), 0);
  gtk_widget_set_size_request(data->volume_slider, 100, -1);
  g_signal_connect (G_OBJECT (data->volume_slider), "value-changed", G_CALLBACK (volume_cb), data);
  g_signal_connect (G_OBJECT (data->volume_slider), "scroll-event", G_CALLBACK (volume_scroll_cb), data);

  gtk_range_set_value (GTK_RANGE (data->volume_slider), gVolume);
  gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_mute), gMute);

  scroll_window = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  data->streams_list = gtk_text_view_new ();
  gtk_text_view_set_editable (GTK_TEXT_VIEW (data->streams_list), FALSE);
  gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (data->streams_list), FALSE);

  if (gFont[0] != '\0')
    gtk_widget_modify_font(data->streams_list, pango_font_description_from_string(gFont));

  gtk_container_add (GTK_CONTAINER (scroll_window), data->streams_list);

  controls = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (controls), GTK_WIDGET (data->btn_play), FALSE, FALSE, 2);
/*
  gtk_box_pack_start (GTK_BOX (controls), GTK_WIDGET (play_button), FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), GTK_WIDGET (pause_button), FALSE, FALSE, 2);
*/
  gtk_box_pack_start (GTK_BOX (controls), GTK_WIDGET (stop_button), FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), GTK_WIDGET (data->btn_loop), FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (controls), GTK_WIDGET (data->btn_info), FALSE, FALSE, 2);
  //gtk_box_pack_start (GTK_BOX (controls), GTK_WIDGET (data->lbl_info), TRUE, TRUE, 2);
  gtk_box_pack_end (GTK_BOX (controls), data->volume_slider, FALSE, TRUE, 2);
  gtk_box_pack_end (GTK_BOX (controls), GTK_WIDGET (data->btn_mute), FALSE, TRUE, 2);

  data->notebook = gtk_notebook_new ();
  gtk_notebook_append_page (GTK_NOTEBOOK (data->notebook), video_window, NULL);
  gtk_notebook_append_page (GTK_NOTEBOOK (data->notebook), scroll_window, NULL);
  gtk_notebook_set_tab_pos (GTK_NOTEBOOK (data->notebook), GTK_POS_BOTTOM);
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (data->notebook), FALSE);

  main_box = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (main_box), data->notebook, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (main_box), GTK_WIDGET (data->lbl_info), FALSE, TRUE, 4);
  GtkWidget *duration = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (duration), data->slider, TRUE, TRUE, 0);
  gtk_box_pack_end (GTK_BOX (duration), GTK_WIDGET (data->lbl_duration), FALSE, FALSE, 2);
  gtk_box_pack_start (GTK_BOX (main_box), duration, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (main_box), controls, FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (main_window), main_box);

  gtk_widget_show_all (main_window);

  gtk_widget_realize (video_window);

  return main_window;
}

/* This function is called periodically to refresh the GUI */
static gboolean refresh_ui (CustomData *data) {
  gint64 current = -1;

  /* We do not want to update anything unless we are in the PAUSED or PLAYING states */
  if (data->state < GST_STATE_PAUSED)
    return TRUE;

  /* If we didn't know it yet, query the stream duration */
  if (!GST_CLOCK_TIME_IS_VALID (data->duration)) {
    if (!gst_element_query_duration (data->playbin, GST_FORMAT_TIME, &data->duration)) {
      gtk_label_set_text (GTK_LABEL (data->lbl_duration), DURATION_EMPTY);
      g_printerr ("Could not query current duration.\n");
    } else {
      /* Set the range of the slider to the clip duration, in SECONDS */
      gtk_range_set_range (GTK_RANGE (data->slider), 0, (gdouble)data->duration / GST_SECOND);
    }
  }

  if (gst_element_query_position (data->playbin, GST_FORMAT_TIME, &current)) {
    /* Block the "value-changed" signal, so the slider_cb function is not called
     * (which would trigger a seek the user has not requested) */
    g_signal_handler_block (data->slider, data->slider_update_signal_id);
    /* Set the position of the slider to the current pipeline positoin, in SECONDS */
    gtk_range_set_value (GTK_RANGE (data->slider), (gdouble)current / GST_SECOND);
    /* Re-enable the signal */
    g_signal_handler_unblock (data->slider, data->slider_update_signal_id);
    gchar *string = g_strdup_printf ("%" GST_TIME_MYFORMAT "/%" GST_TIME_MYFORMAT, GST_TIME_MYARGS (current), GST_TIME_MYARGS (data->duration));
    gtk_label_set_text (GTK_LABEL (data->lbl_duration), string);
    g_free(string);
  }
  else
    gtk_label_set_text (GTK_LABEL (data->lbl_duration), DURATION_EMPTY);

  return TRUE;
}

/* This function is called when new metadata is discovered in the stream */
static void tags_cb (GstElement *playbin, gint stream, CustomData *data) {
  /* We are possibly in a GStreamer working thread, so we notify the main
   * thread of this event through a message in the bus */
  gst_element_post_message (playbin,
    gst_message_new_application (GST_OBJECT (playbin),
      gst_structure_new_empty ("tags-changed")));
}

/* This function is called when an error message is posted on the bus */
static void error_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GError *err;
  gchar *debug_info;
  gchar *str;
  GtkTextBuffer *text;

  /* Print error details on the screen */
  gst_message_parse_error (msg, &err, &debug_info);

  text = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->streams_list));

  str = g_strdup_printf ("\nError received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
  gtk_text_buffer_insert_at_cursor (text, str, -1);
  g_free (str);
  str = g_strdup_printf ("Debugging information: %s\n", debug_info ? debug_info : "none");
  gtk_text_buffer_insert_at_cursor (text, str, -1);
  g_free (str);

  //g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
  //g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
  g_clear_error (&err);
  g_free (debug_info);

  gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_info), TRUE);

  /* Set the pipeline to READY (which stops playback) */
  gst_element_set_state (data->playbin, GST_STATE_READY);
}

/* This function is called when an End-Of-Stream message is posted on the bus.
 * We just set the pipeline to READY (which stops playback) */
static void eos_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  //g_print ("End-Of-Stream reached.\n");
  gst_element_set_state (data->playbin, GST_STATE_READY);
  if (gtk_toggle_tool_button_get_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_loop)))
    gst_element_set_state (data->playbin, GST_STATE_PLAYING);
}

/* This function is called when the pipeline changes states. We use it to
 * keep track of the current state. */
static void state_changed_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GstState old_state, new_state, pending_state;
  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->playbin)) {
    data->state = new_state;
    //g_print ("State set to %s\n", gst_element_state_get_name (new_state));
    if (data->state == GST_STATE_PLAYING)
      gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (data->btn_play), "media-playback-pause");
    else
      gtk_tool_button_set_icon_name (GTK_TOOL_BUTTON (data->btn_play), "media-playback-start");

    if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) {
      /* For extra responsiveness, we refresh the GUI as soon as we reach the PAUSED state */
      refresh_ui (data);
    }
  }
}

static void text_add_tags (const GstTagList *list, const gchar *tag, GtkTextBuffer *text) {
  gchar *str, *total_str;
  guint num;
  gdouble fnum;
  gboolean vbool;
  GstDateTime *date;

  if (gst_tag_get_type (tag) == G_TYPE_STRING) {
    if (gst_tag_list_get_string (list, tag, &str)) {
      total_str = g_strdup_printf ("  %s: %s\n", gst_tag_get_nick (tag), str);
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (total_str);
      g_free (str);
    }
  }
  else if (gst_tag_get_type (tag) == G_TYPE_UINT) {
    if (gst_tag_list_get_uint (list, tag, &num)) {
      total_str = g_strdup_printf ("  %s: %'d\n", gst_tag_get_nick (tag), num);
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (total_str);
    }
  }
  else if (gst_tag_get_type (tag) == G_TYPE_DOUBLE) {
    if (gst_tag_list_get_double (list, tag, &fnum)) {
      total_str = g_strdup_printf ("  %s: %.1f\n", gst_tag_get_nick (tag), fnum);
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (total_str);
    }
  }
  else if (gst_tag_get_type (tag) == G_TYPE_BOOLEAN) {
    if (gst_tag_list_get_boolean (list, tag, &vbool) && vbool) {
      total_str = g_strdup_printf ("  %s\n", gst_tag_get_nick (tag));
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (total_str);
    }
  }
  else if (gst_tag_get_type (tag) == GST_TYPE_DATE_TIME) {
    if (gst_tag_list_get_date_time (list, tag, &date)) {
      total_str = g_strdup_printf ("  %s: %s\n", gst_tag_get_nick (tag), gst_date_time_to_iso8601_string(date));
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (total_str);
    }
  }
}

/* Extract metadata from all the streams and write it to the text widget in the GUI */
static void analyze_streams (CustomData *data) {
  gint i;
  GstTagList *tags;
  gchar *total_str, *str;
  gint n_video, n_audio, n_text;
  GtkTextBuffer *text;

  /* Clean current contents of the widget */
  text = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->streams_list));
  GtkTextMark *last_pos = gtk_text_buffer_get_mark (text, "last_pos");
  if (last_pos != NULL) gtk_text_buffer_delete_mark (text, last_pos);
  gtk_text_buffer_set_text (text, "", -1);

  /* Read some properties */
  g_object_get (data->playbin, "n-video", &n_video, NULL);
  g_object_get (data->playbin, "n-audio", &n_audio, NULL);
  g_object_get (data->playbin, "n-text", &n_text, NULL);

  if (!gVis && !data->init_video) {
    gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_info), (n_video == 0));

    if (n_video != 0) {
      data->init_video = TRUE;
      gtk_widget_show (GTK_WIDGET (data->btn_info));
    } else {
      gtk_widget_hide (GTK_WIDGET (data->btn_info));
    }
  }

  total_str = g_strdup_printf ("%d video stream(s), %d audio stream(s), %d text stream(s)\n",
    n_video, n_audio, n_text);
  gtk_text_buffer_insert_at_cursor (text, total_str, -1);
  g_free (total_str);

  for (i = 0; i < n_video; i++) {
    tags = NULL;
    /* Retrieve the stream's video tags */
    g_signal_emit_by_name (data->playbin, "get-video-tags", i, &tags);
    if (tags) {
      total_str = g_strdup_printf ("\nvideo stream %d:\n", i);
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (total_str);
      gst_tag_list_foreach(tags, (GstTagForeachFunc)text_add_tags, text);
      gst_tag_list_free (tags);
    }
  }

  for (i = 0; i < n_audio; i++) {
    tags = NULL;
    /* Retrieve the stream's audio tags */
    g_signal_emit_by_name (data->playbin, "get-audio-tags", i, &tags);
    if (tags) {
      total_str = g_strdup_printf ("\naudio stream %d:\n", i);
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (total_str);
      gst_tag_list_foreach(tags, (GstTagForeachFunc)text_add_tags, text);
      if (gst_tag_list_get_string (tags, GST_TAG_TITLE, &str)) {
        gtk_label_set_text (GTK_LABEL (data->lbl_info), str);
        if (gst_tag_list_get_string (tags, GST_TAG_ARTIST, &str)) {
          gchar *newstr = g_strdup_printf ("%s - %s", str, gtk_label_get_text (GTK_LABEL (data->lbl_info)));
          gtk_label_set_text (GTK_LABEL (data->lbl_info), newstr);
          g_free(newstr);
        }
        g_free(str);
      }
      gst_tag_list_free (tags);
    }
  }

  for (i = 0; i < n_text; i++) {
    tags = NULL;
    /* Retrieve the stream's subtitle tags */
    g_signal_emit_by_name (data->playbin, "get-text-tags", i, &tags);
    if (tags) {
      total_str = g_strdup_printf ("\nsubtitle stream %d:\n", i);
      gtk_text_buffer_insert_at_cursor (text, total_str, -1);
      g_free (total_str);
      gst_tag_list_foreach(tags, (GstTagForeachFunc)text_add_tags, text);
      gst_tag_list_free (tags);
    }
  }
}

/* This function is called when an "application" message is posted on the bus.
 * Here we retrieve the message posted by the tags_cb callback */
static void application_cb (GstBus *bus, GstMessage *msg, CustomData *data) {

  if (g_strcmp0 (gst_structure_get_name (gst_message_get_structure (msg)), "tags-changed") == 0) {
    /* If the message is the "tags-changed" (only one we are currently issuing), update
     * the stream info GUI */
    analyze_streams (data);
  }
}

HWND DCPCALL ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags)
{
  gchar *fileUri;
  CustomData *data;
  GtkWidget *main_window;
  GstStateChangeReturn ret;
  GstBus *bus;
  gint flags;

  /* Initialize GStreamer */
  gst_init (NULL, NULL);

  /* Initialize our data structure */
  data = g_new (CustomData, 1);
  memset (data, 0, sizeof (CustomData));
  data->duration = GST_CLOCK_TIME_NONE;

  /* Create the elements */
  data->playbin = gst_element_factory_make ("playbin", "playbin");

  if (!data->playbin) {
    g_printerr ("Not all elements could be created.\n");
    g_free(data);
    return 0;
  }

  g_object_set (data->playbin, "volume", gVolume, NULL);
  g_object_set (data->playbin, "mute", gMute, NULL);

  /* Set the URI to play */
  fileUri = g_filename_to_uri(FileToLoad, NULL, NULL);
  g_object_set (data->playbin, "uri", fileUri, NULL);
  if (fileUri) g_free(fileUri);

  /* Connect to interesting signals in playbin */
  g_signal_connect (G_OBJECT (data->playbin), "video-tags-changed", (GCallback) tags_cb, &data);
  g_signal_connect (G_OBJECT (data->playbin), "audio-tags-changed", (GCallback) tags_cb, &data);
  g_signal_connect (G_OBJECT (data->playbin), "text-tags-changed", (GCallback) tags_cb, &data);

  g_object_get (data->playbin, "flags", &flags, NULL);
  flags |= GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO | GST_PLAY_FLAG_TEXT;
  if (gVis) flags |= GST_PLAY_FLAG_VIS;
  g_object_set (data->playbin, "flags", flags, NULL);

  /* Create the GUI */
  main_window = create_ui (GTK_WIDGET (ParentWin), data);
  gtk_label_set_text (GTK_LABEL (data->lbl_info), g_path_get_basename (FileToLoad));

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  bus = gst_element_get_bus (data->playbin);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::application", (GCallback)application_cb, data);
  gst_object_unref (bus);

  /* Start playing */
  ret = gst_element_set_state (data->playbin, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data->playbin);
    g_free(data);
    return NULL;
  }

  /* Register a function that GLib will call every second */
  data->timer = g_timeout_add_seconds (1, (GSourceFunc)refresh_ui, data);

  g_object_set_data (G_OBJECT (main_window), "custom-data", data);
  g_signal_connect (G_OBJECT (main_window), "key_press_event", G_CALLBACK (on_key_press), (gpointer)data);
  g_signal_connect (G_OBJECT (data->notebook), "key_press_event", G_CALLBACK (on_key_press), (gpointer)data);

  return main_window;
}

void DCPCALL ListCloseWindow(HWND ListWin)
{
  CustomData *data;

  data = (CustomData *) g_object_get_data (G_OBJECT (ListWin), "custom-data");
  /* Free resources */
  g_source_remove (data->timer);
  gst_element_set_state (data->playbin, GST_STATE_NULL);
  GKeyFile *cfg = g_key_file_new ();
  gLoop = gtk_toggle_tool_button_get_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_loop));
  gMute = gtk_toggle_tool_button_get_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_mute));
  gVolume = gtk_range_get_value (GTK_RANGE (data->volume_slider));

  if (g_key_file_load_from_file (cfg, gCfgPath, G_KEY_FILE_KEEP_COMMENTS, NULL)) {
    g_key_file_set_boolean (cfg, PLUGNAME, "Loop", gLoop);
    g_key_file_set_boolean (cfg, PLUGNAME, "Mute", gMute);
    g_key_file_set_double (cfg, PLUGNAME, "Volume", gVolume);
    g_key_file_save_to_file (cfg, gCfgPath, NULL);
  }

  g_key_file_free (cfg);
  gst_object_unref (data->playbin);
  gtk_widget_destroy (GTK_WIDGET (ListWin));
  g_free (data);
}

int DCPCALL ListLoadNext(HWND ParentWin, HWND PluginWin, char* FileToLoad, int ShowFlags)
{
  gchar *fileUri;
  CustomData *data;
  GstStateChangeReturn ret;

  data = (CustomData *) g_object_get_data (G_OBJECT (PluginWin), "custom-data");
  gtk_range_set_value (GTK_RANGE (data->slider), 0);
  data->duration = GST_CLOCK_TIME_NONE;
  gst_element_set_state (data->playbin, GST_STATE_READY);

  fileUri = g_filename_to_uri (FileToLoad, NULL, NULL);
  g_object_set (data->playbin, "uri", fileUri, NULL);
  gtk_label_set_text (GTK_LABEL (data->lbl_info), g_path_get_basename (FileToLoad));
  data->init_video = FALSE;

  if (fileUri) g_free (fileUri);

  ret = gst_element_set_state (data->playbin, GST_STATE_PLAYING);
  mute_cb (GTK_TOOL_ITEM (data->btn_mute), data);
  return (ret != GST_STATE_CHANGE_FAILURE) ? (LISTPLUGIN_OK) : (LISTPLUGIN_ERROR);
}

void DCPCALL ListGetDetectString(char* DetectString, int maxlen)
{
  g_strlcpy (DetectString, DETECT_STRING, maxlen - 1);
}

int DCPCALL ListSearchText(HWND ListWin, char* SearchString, int SearchParameter)
{
  CustomData *data;
  GtkTextBuffer *text;
  GtkTextMark *last_pos;
  GtkTextIter iter, mstart, mend;
  gboolean found;

  data = (CustomData *) g_object_get_data (G_OBJECT (ListWin), "custom-data");
  gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (data->btn_info), TRUE);
  text = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->streams_list));
  last_pos = gtk_text_buffer_get_mark (text, "last_pos");


  if (last_pos == NULL || SearchParameter & lcs_findfirst) {
    if (SearchParameter & lcs_backwards)
      gtk_text_buffer_get_end_iter (text, &iter);
    else
      gtk_text_buffer_get_start_iter (text, &iter);
  }
  else
    gtk_text_buffer_get_iter_at_mark (text, &iter, last_pos);

  if (SearchParameter & lcs_backwards)
    found = gtk_text_iter_backward_search (&iter, SearchString, GTK_TEXT_SEARCH_TEXT_ONLY, &mend, &mstart, NULL);
  else
    found = gtk_text_iter_forward_search (&iter, SearchString, GTK_TEXT_SEARCH_TEXT_ONLY, &mstart, &mend, NULL);

  if (found) {
    gtk_text_buffer_select_range (text, &mstart, &mend);
    gtk_text_buffer_create_mark (text, "last_pos", &mend, FALSE);
    gtk_text_view_scroll_mark_onscreen (GTK_TEXT_VIEW (data->streams_list),
                                        gtk_text_buffer_get_mark (text, "last_pos"));

  }
  else {
    GtkWidget *dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (ListWin))),
                                                GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                                "\"%s\" not found!", SearchString);
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
    return LISTPLUGIN_ERROR;
  }
  return LISTPLUGIN_OK;
}

int DCPCALL ListSendCommand(HWND ListWin, int Command, int Parameter)
{
  CustomData *data;
  GtkTextBuffer *text;
  GtkTextIter p;

  data = (CustomData *) g_object_get_data (G_OBJECT (ListWin), "custom-data");
  text = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data->streams_list));

  switch (Command) {
    case lc_copy :
      gtk_text_buffer_copy_clipboard (text, gtk_clipboard_get (GDK_SELECTION_CLIPBOARD));
      break;

    case lc_selectall :
      gtk_text_buffer_get_start_iter (text, &p);
      gtk_text_buffer_place_cursor (text, &p);
      gtk_text_buffer_get_end_iter (text, &p);
      gtk_text_buffer_move_mark_by_name (text, "selection_bound", &p);
      break;

    default :
       return LISTPLUGIN_ERROR;
  }
  return LISTPLUGIN_OK;
}

void DCPCALL ListSetDefaultParams(ListDefaultParamStruct* dps)
{
  gboolean is_updcfg = FALSE;
  gchar *cfgdir = g_path_get_dirname (dps->DefaultIniName);
  snprintf (gCfgPath, PATH_MAX, "%s/j2969719.ini", cfgdir);
  g_free (cfgdir);
  GKeyFile *cfg = g_key_file_new ();
  g_key_file_load_from_file (cfg, gCfgPath, G_KEY_FILE_KEEP_COMMENTS, NULL);

  if (!g_key_file_has_key (cfg, PLUGNAME, "MusicVis", NULL)) {
    g_key_file_set_boolean (cfg, PLUGNAME, "MusicVis", gVis);
    is_updcfg = TRUE;
  }
  else
    gVis = g_key_file_get_boolean (cfg, PLUGNAME, "MusicVis", NULL);

  if (!g_key_file_has_key (cfg, PLUGNAME, "Loop", NULL)) {
    g_key_file_set_boolean (cfg, PLUGNAME, "Loop", gLoop);
    is_updcfg = TRUE;
  }
  else
    gLoop = g_key_file_get_boolean (cfg, PLUGNAME, "Loop", NULL);

  if (!g_key_file_has_key (cfg, PLUGNAME, "Mute", NULL)) {
    g_key_file_set_boolean (cfg, PLUGNAME, "Mute", gMute);
    is_updcfg = TRUE;
  }
  else
    gMute = g_key_file_get_boolean (cfg, PLUGNAME, "Mute", NULL);

  if (!g_key_file_has_key (cfg, PLUGNAME, "Volume", NULL)) {
    g_key_file_set_double (cfg, PLUGNAME, "Volume", gVolume);
    is_updcfg = TRUE;
  }
  else
    gVolume = g_key_file_get_double (cfg, PLUGNAME, "Volume", NULL);

  if (g_key_file_has_key (cfg, PLUGNAME, "Font", NULL)) {
    gchar *font = g_key_file_get_string (cfg, PLUGNAME, "Font", NULL);
    g_strlcpy (gFont, font, PATH_MAX);
    g_free (font);
  }

  if (is_updcfg)
    g_key_file_save_to_file (cfg, gCfgPath, NULL);

  g_key_file_free (cfg);
}
