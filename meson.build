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

project('DFBTerm', 'c',
        meson_version: '>= 0.61')

dfbtermdatadir    = join_paths(get_option('prefix'), get_option('datadir'),    'dfbterm')
dfbtermlibexecdir = join_paths(get_option('prefix'), get_option('libexecdir'), 'dfbterm')

with_system_libzvt = get_option('system-libzvt')

if with_system_libzvt
  libzvt_dep = dependency('libzvt-2.0', required: false)

  if not libzvt_dep.found()
    warning('The included libzvt will be built.')
    with_system_libzvt = false
  endif
endif

config_h = configuration_data()

if get_option('fontdir') == ''
  config_h.set_quoted('TERMFONTDIR', dfbtermdatadir,        description: 'Terminal font directory.')
else
  config_h.set_quoted('TERMFONTDIR', get_option('fontdir'), description: 'Terminal font directory.')
endif

if not with_system_libzvt
  config_h.set('HAVE_LOGIN_TTY', 1)
  config_h.set('HAVE_OPENPTY',   1)
  config_h.set('HAVE_PTY_H',     1)
  config_h.set('HAVE_SENDMSG',   1)
  config_h.set('HAVE_SETEUID',   1)
  config_h.set('HAVE_UPDWTMPX',  1)
  config_h.set('HAVE_UTMP_H',    1)
  config_h.set('HAVE_UTMPX_H',   1)
  config_h.set('UTMP', 'struct utmpx')

  libutil_dep = meson.get_compiler('c').find_library('util')
endif

configure_file(configuration: config_h, output: 'config.h')

config_inc = include_directories('.')

lite_dep = dependency('lite')

subdir('data')
subdir('src')
