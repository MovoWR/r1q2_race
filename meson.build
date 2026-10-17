project('r1q2_standalone', 'c',
  meson_version: '>= 1.3.0',
  default_options: [
    'buildtype=release',
    'optimization=2',
    'warning_level=2',
    'b_ndebug=if-release',
    'b_vscrt=static_from_buildtype',
    'default_library=static',
  ],
)

cc = meson.get_compiler('c')
if host_machine.system() != 'windows' or cc.get_id() != 'msvc' or host_machine.cpu_family() not in ['x86', 'x86_64']
  error('The standalone client requires MSVC targeting Windows x86 or x64. Run Meson from a matching developer command prompt.')
endif
standalone_arch = host_machine.cpu_family() == 'x86_64' ? 'x64' : 'x86'
if get_option('b_vscrt') not in ['static_from_buildtype', 'mt', 'mtd']
  error('The standalone client requires a static MSVC runtime (b_vscrt=static_from_buildtype, mt, or mtd).')
endif

python = import('python').find_installation()
if not python.language_version().version_compare('>=3.10')
  error('Python 3.10 or newer is required.')
endif

# Share the native archive, API link and capability checks with CMake.
media_deps = []
if get_option('avcodec')
  if get_option('ffmpeg_root') == ''
    error('avcodec requires an explicit ffmpeg_root prefix.')
  endif
  if get_option('b_vscrt') == 'mtd' or (get_option('b_vscrt') == 'static_from_buildtype' and get_option('buildtype') == 'debug')
    error('avcodec requires /MT; the selected configuration uses /MTd. Select a release static CRT configuration.')
  endif
  ffmpeg_check = run_command(python, files('tools/check_static_ffmpeg.py'),
    '--arch', standalone_arch,
    '--prefix', get_option('ffmpeg_root'), '--compiler', cc.cmd_array()[0],
    '--output-dir', meson.current_build_dir() / 'ffmpeg-check', check: false)
  if ffmpeg_check.returncode() != 0
    error(ffmpeg_check.stderr())
  endif
  ffmpeg_paths = ffmpeg_check.stdout().strip().replace('\r', '').split('\n')
  ffmpeg_libraries = []
  foreach index : range(1, ffmpeg_paths.length())
    ffmpeg_libraries += ffmpeg_paths[index]
  endforeach
  media_deps = [declare_dependency(
    compile_args: ['-DUSE_AVCODEC', '/I' + ffmpeg_paths[0]],
    link_args: ffmpeg_libraries,
  )]
endif

openal_deps = []
if get_option('openal')
  if get_option('openal_root') == ''
    error('openal requires an explicit openal_root prefix.')
  endif
  if get_option('b_vscrt') == 'mtd' or (get_option('b_vscrt') == 'static_from_buildtype' and get_option('buildtype') == 'debug')
    error('openal requires /MT; select a release static CRT configuration.')
  endif
  openal_check = run_command(python, files('tools/check_static_openal.py'),
    '--arch', standalone_arch,
    '--prefix', get_option('openal_root'), '--compiler', cc.cmd_array()[0],
    '--output-dir', meson.current_build_dir() / 'openal-check', check: false)
  if openal_check.returncode() != 0
    error(openal_check.stderr())
  endif
  openal_paths = openal_check.stdout().strip().replace('\r', '').split('\n')
  openal_libraries = []
  foreach index : range(1, openal_paths.length())
    openal_libraries += openal_paths[index]
  endforeach
  openal_deps = [declare_dependency(
    compile_args: ['-DUSE_OPENAL', '-DR1Q2_STATIC_OPENAL', '-DAL_LIBTYPE_STATIC', '/I' + openal_paths[0]],
    link_args: openal_libraries,
  )]
endif

# Match the archives and static dependency profile in CMakeLists.txt.
zlib_project = subproject('zlib',
  default_options: ['default_library=static', 'tests=disabled'],
)
zlib = zlib_project.get_variable('zlib_dep')
png = subproject('libpng',
  default_options: ['default_library=static', 'tests=false'],
).get_variable('libpng_dep')

# The wrap's small build-only patch retains the existing JPEG v6b ABI.
jpeg = subproject('libjpeg-turbo',
  default_options: [
    'default_library=static',
    'turbojpeg=disabled',
    'force_inline=true',
    'tests=disabled',
  ],
).get_variable('jpeg_dep')

http_deps = []
if get_option('http_downloads')
  # Use curl's upstream configuration checks and the same static zlib build.
  cmake = import('cmake')
  curl_options = cmake.subproject_options()
  zlib_source = meson.project_source_root() / 'subprojects/zlib-1.3.2'
  curl_options.add_cmake_defines({
    'BUILD_SHARED_LIBS': false,
    'BUILD_STATIC_LIBS': true,
    'BUILD_CURL_EXE': false,
    'BUILD_EXAMPLES': false,
    'BUILD_TESTING': false,
    'BUILD_LIBCURL_DOCS': false,
    'BUILD_MISC_DOCS': false,
    'CURL_DISABLE_INSTALL': true,
    'CURL_ENABLE_EXPORT_TARGET': false,
    'CURL_STATIC_CRT': true,
    'SHARE_LIB_OBJECT': false,
    'HTTP_ONLY': true,
    'CURL_USE_SCHANNEL': true,
    'CURL_USE_OPENSSL': false,
    'CURL_TARGET_WINDOWS_VERSION': '0x0601',
    'CURL_ZLIB': 'ON',
    'CURL_BROTLI': 'OFF',
    'CURL_ZSTD': 'OFF',
    'CURL_USE_LIBPSL': false,
    'CURL_USE_LIBSSH2': false,
    'USE_LIBIDN2': false,
    'USE_NGHTTP2': false,
    'CURL_USE_CMAKECONFIG': false,
    'ZLIB_INCLUDE_DIR': zlib_source,
    'ZLIB_LIBRARY': zlib_project.get_variable('zlib').full_path().replace('\\', '/'),
  })
  # The CMake importer does not retain includes from imported ZLIB::ZLIB.
  curl_options.append_compile_args('c', '/I' + zlib_source, target: 'libcurl_static')
  curl_project = cmake.subproject('libcurl', options: curl_options)
  http_deps += curl_project.dependency('libcurl_static')
endif

sources = files(
  'client/cl_cin.c',
  'client/cl_http.c',
  'client/cl_media.c',
  'client/cl_movie.c',
  'client/cl_dll.c',
  'client/cl_ents.c',
  'client/cl_fx.c',
  'client/cl_input.c',
  'client/cl_inv.c',
  'client/cl_main.c',
  'client/cl_timing.c',
  'client/cl_translate.c',
  'client/jump/jump_commands.c',
  'client/jump/jump_config.c',
  'client/jump/sh_nerdstats.c',
  'client/jump/sh_netmeter.c',
  'client/jump/jump_network.c',
  'client/jump/jump_widgets.c',
  'client/jump/jump_layout.c',
  'client/jump/jump_layout_native.c',
  'client/jump/jump_editor.c',
  'client/jump/jump_ui_compat.c',
  'client/jump/jump_menu.c',
  'client/jump/jump_script.c',
  'client/jump/jump_ui.c',
  'client/jump/jump_builtin_menu.c',
  'client/jump/jump_menu_services.c',
  'client/jump/jump_sh_commands.c',
  'client/jump/jump_playerconfig.c',
  'client/jump/jump_playermodels.c',
  'client/jump/jump_demos.c',
  'client/jump/jump_display.c',
  'client/jump/jump_video_ui.c',
  'client/jump/jump_browser_ui.c',
  'client/jump/jump_servers.c',
  'client/jump/jump_servers_http.c',
  'client/jump/jump_field.c',
  'client/jump/jump_reminders.c',
  'client/jump/jump_beams.c',
  'client/jump/jump_draw.c',
  'client/jump/strafe_helper_draw.c',
  'client/jump/strafe_efficiency.c',
  'client/jump/strafe_helper_customization.c',
  'client/jump/sh_efficiency_draw.c',
  'client/jump/jump_input.c',
  'client/jump/jump_prediction.c',
  'client/cl_newfx.c',
  'client/cl_parse.c',
  'client/cl_pred.c',
  'client/cl_scrn.c',
  'client/cl_tent.c',
  'client/cl_view.c',
  'client/con_prompt.c',
  'client/console.c',
  'client/keys.c',
  'client/le_physics.c',
  'client/le_util.c',
  'client/menu.c',
  'client/qmenu.c',
  'client/snd_dma.c',
  'client/snd_mem.c',
  'client/snd_ogg.c',
  'client/snd_stream.c',
  'client/snd_music.c',
  'client/snd_music_al.c',
  'client/snd_movie_al.c',
  'client/snd_mix.c',
  'game/m_flash.c',
  'game/q_shared.c',
  'qcommon/cmd.c',
  'qcommon/cmodel.c',
  'qcommon/common.c',
  'qcommon/crc.c',
  'qcommon/cvar.c',
  'qcommon/files.c',
  'qcommon/md4.c',
  'qcommon/mersennetwister.c',
  'qcommon/net_chan.c',
  'qcommon/pmove.c',
  'qcommon/redblack.c',
  'qcommon/unzip.c',
  'qcommon/ioapi.c',
  'server/sv_ccmds.c',
  'server/sv_ents.c',
  'server/sv_game.c',
  'server/sv_init.c',
  'server/sv_main.c',
  'server/sv_send.c',
  'server/sv_user.c',
  'server/sv_world.c',
  'win32/alw_win.c',
  'win32/cd_win.c',
  'win32/conproc.c',
  'win32/in_win.c',
  'win32/net_wins.c',
  'win32/q_shwin.c',
  'win32/qal_win.c',
  'win32/snd_win.c',
  'win32/sys_win.c',
  'win32/vid_dll.c',
  'win32/vid_menu.c',
  'win32/glw_imp.c',
  'win32/glw_display.c',
  'win32/qgl_win.c',
  'ref_gl/gl_jump.c',
  'ref_gl/gl_jump_lava.c',
  'ref_gl/gl_jump_beams.c',
  'ref_gl/gl_core.c',
  'ref_gl/gl_core_state.c',
  'ref_gl/gl_core_shader.c',
  'ref_gl/gl_shader.c',
  'ref_gl/gl_postfx.c',
  'ref_gl/gl_draw.c',
  'ref_gl/gl_image.c',
  'ref_gl/gl_hqx.c',
  'ref_gl/gl_light.c',
  'ref_gl/gl_mesh.c',
  'ref_gl/gl_model.c',
  'ref_gl/gl_rmain.c',
  'ref_gl/gl_rmisc.c',
  'ref_gl/gl_rsurf.c',
  'ref_gl/gl_warp.c',
)

definitions = ['-DWIN32', '-D_WINDOWS', '-DREF_HARD_LINKED', '-DCINEMATICS', '-D_CRT_SECURE_NO_WARNINGS']
if get_option('http_downloads')
  definitions += ['-DUSE_CURL']
endif
if not get_option('chat_translator')
  definitions += ['-DNO_CHAT_TRANSLATOR']
endif
if get_option('core_renderer')
  definitions += ['-DR1Q2_GL_CORE']
endif
version_header = custom_target('race_version',
  output: 'race_version_generated.h',
  command: [python, files('tools/generate_version.py'),
    '--source', meson.project_source_root(), '--output', '@OUTPUT@'],
  build_always_stale: true,
)
definitions += ['-DR1Q2_GENERATED_VERSION']
windows = import('windows')
win32_inc = include_directories('win32', '.')
resources = windows.compile_resources('win32/q2.rc',
  args: definitions,
  include_directories: win32_inc,
  depends: version_header,
  depend_files: files('win32/resource.h', 'win32/q2.ico', 'win32/icon2.ico'),
)

manifest = meson.project_source_root() / 'win32/r1q2.manifest'
system_deps = []
foreach name : ['winmm', 'wsock32', 'kernel32', 'user32', 'gdi32', 'advapi32', 'dxguid', 'dinput8', 'dsound']
  system_deps += cc.find_library(name)
endforeach

executable('r1q2_race', sources, resources, version_header,
  dependencies: [zlib, png, jpeg] + http_deps + media_deps + openal_deps + system_deps,
  include_directories: win32_inc,
  # This also removes Meson's implicit /utf-8, retaining CMake's MSVC code page.
  c_args: definitions + ['/fp:precise', '/validate-charset-'],
  win_subsystem: 'windows',
  link_args: [
    '/MANIFEST:EMBED',
    '/MANIFESTINPUT:' + manifest,
  ],
  link_depends: files('win32/r1q2.manifest'),
  install: true,
  install_dir: '.',
)
