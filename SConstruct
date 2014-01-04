import commands

env = Environment()
target = ARGUMENTS.get('target', '')
if target=='windows':
	env.Tool('crossmingw', toolpath = ['scons-tools'])
	env.Append(
		CPPPATH=[
			'/media/artem/DATA/Work/SDK/gstreamer/1.0/x86_64/include',
			'/media/artem/DATA/Work/SDK/gstreamer/1.0/x86_64/include/gstreamer-1.0',
			'/media/artem/DATA/Work/SDK/gstreamer/1.0/x86_64/include/glib-2.0',
			'/media/artem/DATA/Work/SDK/gstreamer/1.0/x86_64/lib/glib-2.0/include'
		],
		LIBPATH=[
			'/media/artem/DATA/Work/SDK/gstreamer/1.0/x86_64/lib'
		],
		LIBS=['glib-2.0', 'gobject-2.0', 'gstreamer-1.0', 'gstapp-1.0']
	)
else:
	env.ParseConfig('pkg-config --libs --cflags gstreamer-1.0 gstreamer-app-1.0')
env.Program(['streamer.c'])