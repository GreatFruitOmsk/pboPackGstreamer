import commands
import os

compiler = ARGUMENTS.get('compiler', '')
if compiler in['msvc', 'mingw']:		
	if compiler=='mingw':
		env = Environment(tools=['mingw'])
		env.Tool('crossmingw', toolpath = ['scons-tools'])
	else:
		env =Environment()
	print env['ENV']['PATH']
	gstreamer_path=ARGUMENTS.get('gstreamer', '')	
	env.Append(
		CPPPATH=map(lambda s: os.path.join(gstreamer_path, s), [
			'include',
			'include/gstreamer-1.0',
			'include/glib-2.0',
			'lib/glib-2.0/include'
		]),
		LIBPATH=[
			os.path.join(gstreamer_path, 'lib')	
		],
		LIBS=['glib-2.0', 'gobject-2.0', 'gstreamer-1.0', 'gstapp-1.0'],
		CFLAGS = ['-m64'],
		LINKFLAGS = ['-m64'],
		TARGET_ARCH = 'x86_64',
		TARGET_OS = 'Win32',	
	)
else:
	env = Environment()
	env.ParseConfig('pkg-config --libs --cflags gstreamer-1.0 gstreamer-app-1.0')
env.Program(['streamer.c'])
env.SharedLibrary(['streamer.c'])