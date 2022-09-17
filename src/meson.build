#  This file is part of DFBTerm.
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA

if with_system_libzvt

executable('dfbterm', 'dfbterm.c', include_directories: config_inc, dependencies: [libzvt_dep, lite_dep], install: true)

else

dfbterm_sources = [
  'dfbterm.c',
  'glib.c',
  'libzvt/gnome-login-support.c',
  'libzvt/lists.c',
  'libzvt/subshell.c',
  'libzvt/update.c',
  'libzvt/vt.c'
]

dfbterm_pty_helper_sources = [
  'libzvt/gnome-login-support.c',
  'libzvt/gnome-pty-helper.c',
  'libzvt/gnome-utmp.c'
]

executable('dfbterm',
           dfbterm_sources,
           include_directories: config_inc,
           c_args: '-DPTY_HELPER_DIR="@0@"'.format(dfbtermlibexecdir),
           dependencies: [libutil_dep, lite_dep],
           install: true)

executable('dfbterm-pty-helper',
           dfbterm_pty_helper_sources,
           include_directories: config_inc,
           dependencies: libutil_dep,
           install: true,
           install_dir: dfbtermlibexecdir)

meson.add_install_script(join_paths(meson.source_root(), 'meson_symlink.sh'),
                         'dfbterm-pty-helper',
                         join_paths(dfbtermlibexecdir, 'gnome-pty-helper'))

endif
