#include <stdio.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/app/gstappsrc.h>
#include <glib.h>
#define STREAMER_EXPORTS
#include "streamer.h"


typedef struct {
	GMainLoop *loop;
	GstElement *pipeline, *source, *parse, *scale, *capsfilter, *conv, *mux, *sink;
	guint sourceid;
	FILE *file;
	GstBus *bus;
	GThread *m_loop_thread;
	guint bus_watch_id;

	gboolean stop;

	gboolean resize, have_data;
	guint in_framerate, in_width, in_height;
	GstBuffer* buffer;

	GMutex mutex;
	GCond cond;
}gst_app_t;
static gst_app_t app;

static void
cb_need_data(GstElement *appsrc,
guint       unused_size,
gpointer    user_data)
{
	static guint white = FALSE, i = 0;
	static GstClockTime timestamp = 0;
	GstBuffer *buffer = NULL;
	GstFlowReturn ret;
	gboolean resize;

	g_mutex_lock(&app.mutex);
	if (!app.have_data) {
		g_debug("Wait for data... ");
		g_cond_wait(&app.cond, &app.mutex);
		g_debug("Done.\n");
	}
	else {
		g_debug("Have data\n");
	}
	buffer = gst_buffer_copy(app.buffer);
	app.have_data = FALSE;
	resize = app.resize;
	app.resize = FALSE;
	g_mutex_unlock(&app.mutex);

	if (!buffer)
		return;

	if (resize) {
		g_print("Actually resizing... ");
		g_object_set(G_OBJECT(app.source), "caps",
			gst_caps_new_simple("video/x-raw",
			"format", G_TYPE_STRING, "RGB",
			"width", G_TYPE_INT, app.in_width,
			"height", G_TYPE_INT, app.in_height,
			"framerate", GST_TYPE_FRACTION, app.in_framerate, 1,
			NULL), NULL);
		g_print("Done resizing.\n");
	}

	if (app.stop) {
		gst_app_src_end_of_stream((GstAppSrc*)user_data);
		return;
	}

	GST_BUFFER_PTS(buffer) = timestamp;
	GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, 30);
	timestamp += GST_BUFFER_DURATION(buffer);

	ret = gst_app_src_push_buffer((GstAppSrc*)appsrc, buffer);
	if (ret != GST_FLOW_OK) {
		g_main_loop_quit(app.loop);
	}

	g_debug("written\n");
}

static gboolean
bus_call(GstBus     *bus,
GstMessage *msg,
gpointer    data)
{
	GMainLoop *loop = (GMainLoop *)data;

	switch (GST_MESSAGE_TYPE(msg)) {

	case GST_MESSAGE_EOS:
		g_print("End of stream\n");
		g_main_loop_quit(loop);
		break;

	case GST_MESSAGE_ERROR: {
								gchar  *debug;
								GError *error;

								gst_message_parse_error(msg, &error, &debug);
								g_free(debug);

								g_printerr("Error: %s\n", error->message);
								g_error_free(error);

								g_main_loop_quit(loop);
								break;
	}
	default:
		break;
	}

	return TRUE;
}

static void
main_loop_run(gpointer data)
{
	g_print("Thread started\n");
	g_main_loop_run(app.loop);
	g_print("Returned, stopping playback\n");
}

gboolean streamer_init() {
	GError* e;
	gboolean res;

	res = gst_init_check(NULL, NULL, &e);
	if (!res) {
		g_printerr(e->message);
	}

	g_mutex_init(&app.mutex);
	return res;
}

void streamer_feed(guint w, guint h, guint8 fill) {
	gssize size;

	g_mutex_lock(&app.mutex);

	if (w != app.in_width || h != app.in_height) {
		app.resize = TRUE;
		app.in_width = w;
		app.in_height = h;
	}
	size = app.in_width * app.in_height * 3;

	if (app.resize) {
		/* This doesn't work if size is more than buffer's size */
		//gst_buffer_resize(app.buffer, 0, size);
		if (app.buffer)
			gst_buffer_unref(app.buffer);
		app.buffer = gst_buffer_new_and_alloc(size);
	}

	/* this makes the image black/white */
	gst_buffer_memset(app.buffer, 0, fill, size);
	app.have_data = TRUE;

	g_mutex_unlock(&app.mutex);
	g_cond_signal(&app.cond);
}

gboolean streamer_run(guint in_framerate, guint out_width, guint out_height) {
	g_cond_init(&app.cond);
	app.loop = g_main_loop_new(NULL, FALSE);
	app.in_width = 0;
	app.in_height = 0;
	app.in_framerate = in_framerate;
	app.resize = FALSE;
	app.have_data = FALSE;

	app.pipeline = gst_pipeline_new("video-player");
	app.source = gst_element_factory_make("appsrc", "mysrc");
	app.scale = gst_element_factory_make("videoscale", "video-scale");
	app.capsfilter = gst_element_factory_make("capsfilter", "caps-filter");
	app.conv = gst_element_factory_make("jpegenc", "jpeg-converter");
	app.mux = gst_element_factory_make("avimux", "avi-muxer");
	app.sink = gst_element_factory_make("filesink", "file-sink");

	g_assert(app.pipeline);
	g_assert(app.source);
	g_assert(app.scale);
	g_assert(app.capsfilter);
	g_assert(app.conv);
	g_assert(app.mux);
	g_assert(app.sink);

	app.bus = gst_pipeline_get_bus(GST_PIPELINE(app.pipeline));
	app.bus_watch_id = gst_bus_add_watch(app.bus, bus_call, app.loop);
	gst_object_unref(app.bus);

	g_object_set(G_OBJECT(app.source),
		"stream-type", 0,
		"format", GST_FORMAT_TIME, NULL);
	g_signal_connect(app.source, "need-data", G_CALLBACK(cb_need_data), app.source);
	//g_signal_connect(source, "enough-data", G_CALLBACK(stop_feed), NULL);

	g_object_set(G_OBJECT(app.source), "caps",
		gst_caps_new_simple("video/x-raw",
		"format", G_TYPE_STRING, "RGB",
		"width", G_TYPE_INT, out_width,
		"height", G_TYPE_INT, out_height,
		"framerate", GST_TYPE_FRACTION, in_framerate, 1,
		NULL), NULL);
	g_object_set(G_OBJECT(app.sink), "location", "out.avi", NULL);

	gst_bin_add_many(GST_BIN(app.pipeline),
		app.source, app.scale, app.capsfilter, app.conv, app.mux, app.sink, NULL);

	g_object_set(G_OBJECT(app.capsfilter), "caps",
		gst_caps_new_simple("video/x-raw",
		"format", G_TYPE_STRING, "RGB",
		"width", G_TYPE_INT, out_width,
		"height", G_TYPE_INT, out_height,
		"framerate", GST_TYPE_FRACTION, in_framerate, 1,
		NULL), NULL);
	gst_element_link_many(app.source, app.scale, app.capsfilter,
		app.conv, app.mux, app.sink, NULL);

	gst_element_set_state(app.pipeline, GST_STATE_PLAYING);

	g_print("Running...\n");

	if ((app.m_loop_thread = g_thread_new("mainloop", (GThreadFunc)main_loop_run, NULL)) == NULL){
		g_print("ERROR: cannot start loop thread");
		return FALSE;
	}

	return TRUE;
}

void streamer_stop() {
	g_mutex_lock(&app.mutex);
	app.have_data = FALSE;
	g_mutex_unlock(&app.mutex);
	g_cond_signal(&app.cond);

	gst_element_set_state(app.pipeline, GST_STATE_NULL);

	g_print("Deleting pipeline\n");
	gst_object_unref(GST_OBJECT(app.pipeline));
	g_source_remove(app.bus_watch_id);
	g_main_loop_unref(app.loop);
}

int
main(int   argc,
char *argv[])
{
	int i;
	gulong t = gst_util_uint64_scale_int(GST_SECOND, 1, 30000);
	g_print("usleep time: %ul\n", t);

	streamer_init();
	streamer_run(30, 1024 / 2, 600 / 2);
	for (i = 0; i < 30 * 5; i++) {
		streamer_feed(1024, 600, 0x00 + i);
		g_usleep(t);
	}
	g_print("Resizing input...\n");
	for (i = 0; i < 30 * 5; i++) {
		streamer_feed(1024 / 2, 600 / 2, 0xFF - i);
		g_usleep(t);
	}
	g_print("Resizing input...\n");
	for (i = 0; i < 30 * 5; i++) {
		streamer_feed(1024 / 4, 600 / 4, 0x00 + i);
		g_usleep(t);
	}
	streamer_stop();
	return 0;
}
