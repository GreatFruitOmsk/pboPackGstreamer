#ifdef _MSC_VER
#ifdef STREAMER_EXPORTS
#define STREAMER_EXPORT __declspec(dllexport) extern
#else
#define STREAMER_EXPORT__declspec(dllimport) extern
#endif
#endif

gboolean STREAMER_EXPORT streamer_run(guint in_framerate, guint out_width, guint out_height);
gboolean STREAMER_EXPORT streamer_init();
void     STREAMER_EXPORT streamer_stop();