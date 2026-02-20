/*
   This file is part of DFBTerm.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
*/

#ifndef __GLIB_H__
#define __GLIB_H__

#define gboolean int
#define FALSE    (0)
#define TRUE     (1)

typedef struct _GSList {
     void           *data;
     struct _GSList *next;
} GSList;

typedef struct
{
  char *str;
  int   len;
} GString;

#define MIN(a,b) ((a) < (b) ? (a) : (b))

void     g_error                ( const char *fmt, ... );
void     g_free                 ( void *mem );
char    *g_locale_to_utf8       ( const char *str, ... );
void    *g_malloc               ( unsigned long size );
void    *g_malloc0              ( unsigned long size );
#define  g_new(type, n)         ((type*) g_malloc( sizeof(type) * (n) ))
void    *g_realloc              ( void *mem, unsigned long size );
GSList  *g_slist_prepend        ( GSList *list, void *data );
GSList  *g_slist_remove         ( GSList *list, void *data );
int      g_snprintf             ( char *str, unsigned long size, const char *fmt, ... );
GString *g_string_new           ( const char *str );
GString *g_string_append_unichar( GString *string, char c );
char    *g_string_free          ( GString *string, gboolean free_segment );

#define g_return_if_fail(expr)
#define g_return_val_if_fail(expr,val)

#endif
