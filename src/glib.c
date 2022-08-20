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

#include <glib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void
g_error( const char *fmt, ... )
{
     va_list args;

     va_start( args, fmt );
     vfprintf( stderr, fmt, args );
     va_end( args );
}

void
g_free( void *mem )
{
     free( mem );
}

void *
g_malloc( unsigned long size )
{
     return malloc( size );
}

void *
g_malloc0( unsigned long size )
{
     return calloc( 1, size );
}

void *
g_realloc( void          *mem,
           unsigned long  size )
{
     return g_realloc( mem, size );
}

GSList *
g_slist_alloc()
{
     GSList *list;

     list = calloc( 1, sizeof(GSList) );

     return list;
}

void
g_slist_free( GSList *list )
{
     GSList *last;

     while (list) {
          last = list;
          list = list->next;
          free( last );
     }
}

GSList *
g_slist_prepend( GSList *list,
                 void   *data )
{
     GSList *new_list;

     new_list = calloc( 1, sizeof(GSList) );
     new_list->data = data;
     new_list->next = list;

     return new_list;
}

GSList *
g_slist_remove( GSList *list,
                void   *data )
{
     GSList *tmp, *prev = NULL;

     tmp = list;
     while (tmp) {
          if (tmp->data == data) {
               if (prev)
                    prev->next = tmp->next;
               else
                    list = tmp->next;
               free( tmp );
               break;
          }
          prev = tmp;
          tmp = prev->next;
     }

     return list;
}

int
g_snprintf( char          *str,
            unsigned long  size,
            const char    *fmt, ... )
{
     int     len;
     va_list args;

     va_start( args, fmt );
     len = vsnprintf( str, size, fmt, args );
     va_end( args );

     return len;
}
