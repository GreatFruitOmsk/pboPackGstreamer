#ifdef _MSC_VER
	#ifdef STREAMER_EXPORTS
	#define STREAMER_EXPORT __declspec(dllexport) extern
	#else
	#define STREAMER_EXPORT__declspec(dllimport) extern
	#endif
#else 
	#define STREAMER_EXPORT
#endif

/*
  libStreamer uses GStreamer to convert and stream video passed as a sequence of RGB frames.
  It can write its output to an AVI file or transmit over TCP.
  Sample usage:
	streamer_init();
	streamer_run(fps, w, h, "tcp");
	// or: streamer_run(fps, w, h, "out.avi");
	for (i = 0; i < fps* 50; i++) {
		memset(frame,  0x00+i, w*h*3);
		streamer_feed(w, h, frame);
		g_usleep(t);
	}

  The stream transmitted over TCP can be played back like this:
  gst-launch-1.0 -v tcpclientsrc ! jpegdec ! videoconvert ! autovideosink
*/

/*
  Start streaming.
	in_framerate	Input and output stream framerate, frames per second
	out_width		Output video width (will stay the same regardless of input)
	out_height		Output video height (will stay the same regardless of input)
	out_fname		Output filename. Pass "tcp" instead if streaming over TCP is needed.
  Returns FALSE if an error occured, TRUE otherwise.
*/
gboolean STREAMER_EXPORT 
streamer_run(guint in_framerate, guint out_width, guint out_height, const gchar* out_fname);

/*
  Initialize library. Call this before any other operations.
  Returns FALSE if an error occured, TRUE otherwise.
*/ 
gboolean STREAMER_EXPORT 
streamer_init();

/*
  Feed the streamer with a frame. Resize input if needed.
	w		Frame width
	h		Frame height
	frame	RGB data
*/
void STREAMER_EXPORT
streamer_feed(guint w, guint h, guint8* frame);

/*
  Stop streaming and destroy the pipeline.
*/
void STREAMER_EXPORT 
streamer_stop();

void STREAMER_EXPORT
streamer_set_rotation(gint r);

typedef void(*StreamerCallback)();
typedef void(*StreamerDataCallback)(guint8* d, gssize s);

void STREAMER_EXPORT
streamer_set_ready_callback(StreamerCallback);

void STREAMER_EXPORT
streamer_set_input_callback(StreamerCallback);

void STREAMER_EXPORT
streamer_set_output_callback(StreamerDataCallback);