/*  lists.h - Zed's Virtual Terminal
 *  Copyright (C) 1998  Michael Zucchi
 *
 *  Partly based on list handling code by Marcus Comstedt in libami
 *  from amiwm (an implementation of Amiga Exec-style doubly-linked lists).
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public License
 *  as published by the Free Software Foundation; either version 2 of
 *  the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _LISTS_H
#define _LISTS_H

/* generic list functions */
struct vt_listnode {
  struct vt_listnode *next;
  struct vt_listnode *prev;  
};

struct vt_list {
  struct vt_listnode *head;
  struct vt_listnode *tail;
  struct vt_listnode *tailpred;
};

void vt_list_new(struct vt_list *v);
struct vt_listnode *vt_list_addhead(struct vt_list *l, struct vt_listnode *n);
struct vt_listnode *vt_list_addtail(struct vt_list *l, struct vt_listnode *n);
int vt_list_empty(struct vt_list *l);
struct vt_listnode *vt_list_remhead(struct vt_list *l);
struct vt_listnode *vt_list_remtail(struct vt_list *l);
struct vt_listnode *vt_list_remove(struct vt_listnode *n);
struct vt_listnode *vt_list_insert(struct vt_list *l, struct vt_listnode *p, struct vt_listnode *n);
struct vt_listnode *vt_list_index(struct vt_list *l, int index);

#endif /* _LISTS_H */
