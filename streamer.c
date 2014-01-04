#include <memory.h>
#include <stdio.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <glib.h>
#define STREAMER_EXPORTS
#include "streamer.h"

typedef struct {
	GMainLoop *loop;
	GstElement *pipeline, *source, *parse, *scale, *capsfilter, *conv, *mux, *sink;
	guint sourceid;
	GstBus *bus;
	GThread *m_loop_thread;
	guint bus_watch_id;

	gboolean stop;

	gboolean resize, have_data;
	guint in_framerate, in_width, in_height, out_width, out_height;
	GstBuffer* buffer;
	gint rotation;

	GMutex mutex;
	GCond cond;

	FILE* outfile;

	StreamerCallback ready_callback, input_callback;
	StreamerDataCallback output_callback;
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

	if(app.input_callback) {
		app.input_callback();
		buffer = gst_buffer_copy(app.buffer);
	} else {
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
	}

	if (!buffer)
		return;

	if (resize) {
		g_warning("Streamer resizing to %dx%d", app.in_width, app.in_height);
		g_object_set(G_OBJECT(app.source), "caps",
			gst_caps_new_simple("video/x-raw",
			"format", G_TYPE_STRING, "RGB",
			"width", G_TYPE_INT, app.in_width,
			"height", G_TYPE_INT, app.in_height,
			"framerate", GST_TYPE_FRACTION, app.in_framerate, 1,
			NULL), NULL);		
	}

	if (app.stop) {
		gst_app_src_end_of_stream((GstAppSrc*)appsrc);
		return;
	}

	GST_BUFFER_PTS(buffer) = timestamp;
	GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, 30);
	timestamp += GST_BUFFER_DURATION(buffer);

	ret = gst_app_src_push_buffer((GstAppSrc*)appsrc, buffer);
	if (ret != GST_FLOW_OK) {
		g_main_loop_quit(app.loop);
	}

	g_print("written\n");
}

static gboolean
bus_call(GstBus     *bus,
GstMessage *msg,
gpointer    data)
{
	GMainLoop *loop = (GMainLoop *)data;
	GstState old_state, new_state;

	switch (GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_STATE_CHANGED:
		gst_message_parse_state_changed (msg, &old_state, &new_state, NULL);

		if(msg->src == GST_OBJECT(app.pipeline) && new_state == GST_STATE_READY) {
			g_print ("Element %s changed state from %s to %s.\n",
				GST_OBJECT_NAME (msg->src),
				gst_element_state_get_name (old_state),
				gst_element_state_get_name (new_state));
			if(app.ready_callback) {
				app.ready_callback();
			}
		}
		break;
	case GST_MESSAGE_EOS:
		g_warning("End of stream\n");
		g_main_loop_quit(loop);
		break;

	case GST_MESSAGE_ERROR: {
		gchar  *debug;
		GError *error;

		gst_message_parse_error(msg, &error, &debug);
		// g_free(debug);

		g_printerr("Error: %s\n", error->message);
		g_printerr("Debug: %s\n", debug);
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
	g_debug("Streamer thread started\n");

	g_main_loop_run(app.loop);
	g_debug("Streamer returned, stopping playback\n");
}

gboolean streamer_init() {
	GError* e;
	gboolean res;

	res = gst_init_check(NULL, NULL, &e);
	if (!res) {
		g_printerr("%s", e->message);
	}

	g_mutex_init(&app.mutex);
	return res;
}

void streamer_feed_sync(guint w, guint h, guint8* frame) {
	gssize size;
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

	gst_buffer_fill(app.buffer, 0, frame, size);
}

void streamer_feed(guint w, guint h, guint8* frame) {
	g_mutex_lock(&app.mutex);

	streamer_feed_sync(w, h, frame);
	app.have_data = TRUE;

	g_mutex_unlock(&app.mutex);
	g_cond_signal(&app.cond);
}

GstFlowReturn on_new_preroll(GstAppSink *appsink, gpointer user_data) {
	GstSample* sample = NULL;
	GstBuffer* buffer;
	GstMemory* memory;
	GstMapInfo info;
	GstClockTime clocktime;

	g_print("on_new_preroll ");
	sample = gst_app_sink_pull_sample (appsink);
	if (sample) {
		g_print("pulled sample\n");
		buffer = gst_sample_get_buffer(sample);
		clocktime = GST_BUFFER_PTS(buffer);
		memory = gst_buffer_get_memory(buffer, 0);
		gst_memory_map(memory, &info, GST_MAP_READ);
		/*
			You can access raw memory at info.data
		*/
		if(app.output_callback)
			app.output_callback(info.data, info.size);
		//fwrite(info.data, 1, info.size, app.outfile);
		
		gst_memory_unmap(memory, &info);
		gst_memory_unref(memory);
		gst_sample_unref(sample);
	}
	return GST_FLOW_OK;
}
 void on_eos(GstAppSink *appsink, gpointer user_data) {
 	g_print("on_eos\n");
 	// return GST_FLOW_OK;
 }


gboolean streamer_run(guint in_framerate, guint out_width, guint out_height, const gchar* out_fname) {
	gboolean tcp = !g_strcmp0(out_fname, "tcp"),
			toapp = !g_strcmp0(out_fname, "app"),
			res;
	static GstAppSinkCallbacks callbacks = {on_eos, NULL, on_new_preroll};

	app.outfile = fopen("out.jpeg", "wb");

	g_cond_init(&app.cond);
	app.loop = g_main_loop_new(NULL, FALSE);
	app.in_width = 0;
	app.in_height = 0;
	app.out_width = out_width;
	app.out_height = out_height;
	app.in_framerate = in_framerate;
	app.resize = FALSE;
	app.have_data = FALSE;

	app.pipeline = gst_pipeline_new("video-streamer");
	app.source = gst_element_factory_make("appsrc", "app-src");
	app.scale = gst_element_factory_make("videoscale", "video-scale");
	app.capsfilter = gst_element_factory_make("capsfilter", "caps-filter");
	app.conv = gst_element_factory_make("jpegenc", "jpeg-converter");	
	if(toapp) {
		app.sink = gst_element_factory_make("appsink", "app-sink");		
		gst_app_sink_set_callbacks(GST_APP_SINK(app.sink), &callbacks, NULL, NULL);
		// gst_app_sink_set_drop(GST_APP_SINK(app.sink), TRUE);
		// g_object_set (G_OBJECT (app.sink), "caps",
  // 			gst_caps_new_simple ("video/x-raw",
		// 				 "format", G_TYPE_STRING, "RGB",
		// 				 "width", G_TYPE_INT, out_width,
		// 				 "height", G_TYPE_INT, out_height,
		// 				 "framerate", GST_TYPE_FRACTION, in_framerate, 1,
		// 				 NULL), NULL);
	} else if(tcp) {
		app.sink = gst_element_factory_make("tcpserversink", "tcp-sink");		
	} else {
		app.mux = gst_element_factory_make("avimux", "avi-muxer");
		g_assert(app.mux);		
		app.sink = gst_element_factory_make("filesink", "file-sink");
	}

	g_assert(app.pipeline);
	g_assert(app.source);
	g_assert(app.scale);
	g_assert(app.capsfilter);	
	g_assert(app.conv);	
	g_assert(app.sink);

	app.bus = gst_pipeline_get_bus(GST_PIPELINE(app.pipeline));
	app.bus_watch_id = gst_bus_add_watch(app.bus, bus_call, app.loop);
	gst_object_unref(app.bus);

	g_object_set(G_OBJECT(app.source),
		"stream-type", 0,
		"format", GST_FORMAT_TIME, NULL);
	g_signal_connect(app.source, "need-data", G_CALLBACK(cb_need_data), NULL);
	//g_signal_connect(source, "enough-data", G_CALLBACK(stop_feed), NULL);

	g_object_set(G_OBJECT(app.source), "caps",
		gst_caps_new_simple("video/x-raw",
		"format", G_TYPE_STRING, "RGB",
		"width", G_TYPE_INT, out_width,
		"height", G_TYPE_INT, out_height,
		"framerate", GST_TYPE_FRACTION, in_framerate, 1,
		NULL), NULL);
	if(tcp || toapp) {
		gst_bin_add_many(GST_BIN(app.pipeline),
			app.source, app.scale, app.capsfilter, app.conv, app.sink, NULL);
	} else {
		g_object_set(G_OBJECT(app.sink), "location", out_fname, NULL);
		gst_bin_add_many(GST_BIN(app.pipeline),
			app.source, app.scale, app.capsfilter, app.conv,  app.mux, app.sink, NULL);
	}

	g_object_set(G_OBJECT(app.capsfilter), "caps",
		gst_caps_new_simple("video/x-raw",
		"format", G_TYPE_STRING, "RGB",
		"width", G_TYPE_INT, out_width,
		"height", G_TYPE_INT, out_height,
		"framerate", GST_TYPE_FRACTION, in_framerate, 1,
		NULL), NULL);
	if(tcp || toapp)
		res = gst_element_link_many(app.source, app.scale, app.capsfilter,
			app.conv, app.sink, NULL);
	else
		res = gst_element_link_many(app.source, app.scale, app.capsfilter,
			app.conv, app.mux, app.sink, NULL);
		
	if(!res) {
		g_printerr("ERROR: linking failed\n");
		return FALSE;
	}

	gst_element_set_state(app.pipeline, GST_STATE_PLAYING);

	g_debug("Running...\n");

	if ((app.m_loop_thread = g_thread_new("mainloop", (GThreadFunc)main_loop_run, NULL)) == NULL){
		g_printerr("ERROR: cannot start loop thread\n");
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

	g_debug("Deleting pipeline\n");
	gst_object_unref(GST_OBJECT(app.pipeline));
	g_source_remove(app.bus_watch_id);
	g_main_loop_unref(app.loop);
}

void streamer_set_rotation(gint r) {
	if(app.rotation != r && app.capsfilter) {
		app.rotation = r;
		switch(r) {
		case 1:
			g_object_set(G_OBJECT(app.capsfilter), "caps",
				gst_caps_new_simple("video/x-raw",
				"format", G_TYPE_STRING, "RGB",
				"width", G_TYPE_INT, app.out_height,
				"height", G_TYPE_INT, app.out_width,
				"framerate", GST_TYPE_FRACTION, app.in_framerate, 1,
				NULL), NULL);
			break;
		default:
			g_object_set(G_OBJECT(app.capsfilter), "caps",
				gst_caps_new_simple("video/x-raw",
				"format", G_TYPE_STRING, "RGB",
				"width", G_TYPE_INT, app.out_width,
				"height", G_TYPE_INT, app.out_height,
				"framerate", GST_TYPE_FRACTION, app.in_framerate, 1,
				NULL), NULL);
			break;
		}
	}
}

int
main(int   argc,
char *argv[])
{
	int i, w=1024, h=600, fps = 30;
	guint8* frame = g_malloc(w*h*4*3);	
	
	/* 1/30 of a second in microseconds */
	gulong t = gst_util_uint64_scale_int(GST_SECOND, 1, fps*1000);	
	
	streamer_init();
	if(!streamer_run(fps, w / 2, h/ 2, "app"))
		return 1;
	for (i = 0; i < fps* 5; i++) {
		memset(frame,  0x00+i, w*h*3);
		streamer_feed(w, h, frame);
		g_usleep(t);
	}

	g_print("Resizing input...\n");
	w/=2; h/=2;
	for (i = 0; i < fps * 5; i++) {
		memset(frame,  0xFF-i, w*h*3);
		streamer_feed(w, h, frame);
		g_usleep(t);
	}

	g_print("Resizing input...\n");
	w/=2; h/=2;
	for (i = 0; i < fps * 5; i++) {
		memset(frame,  0x00+i, w*h*3);
		streamer_feed(w, h, frame);
		g_usleep(t);
	}

	g_print("Resizing input...\n");
	w*=6; h*=5;
	for (i = 0; i < fps * 5; i++) {
		memset(frame,  0x00+i, w*h*3);
		streamer_feed(w, h, frame);
		g_usleep(t);
	}
	streamer_stop();	
	return 0;
}
