/*  lists.c - Zed's Virtual Terminal
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

#include "lists.h"

#define d(x)

void vt_list_new(struct vt_list *v)
{
  v->head = (struct vt_listnode *)&v->tail;
  v->tail = 0;
  v->tailpred = (struct vt_listnode *)&v->head;
}

/* add node to head of list */
struct vt_listnode *vt_list_addhead(struct vt_list *l, struct vt_listnode *n)
{
  n->next = l->head;
  n->prev = (struct vt_listnode *)&l->head;
  l->head->prev = n;
  l->head = n;
  return n;
}

/* add node to tail of list */
struct vt_listnode *vt_list_addtail(struct vt_list *l, struct vt_listnode *n)
{

  d(printf("adding %08x to tail of list\n", n));
  d(printf("list = \n %08x\n %08x\n %08x\n", l->head, l->tailpred, l->tail));

  n->next = (struct vt_listnode *)&l->tail;
  n->prev = l->tailpred;
  l->tailpred->next = n;
  l->tailpred = n;

  d(printf("list = \n %08x\n %08x\n %08x\n", l->head, l->tailpred, l->tail));

  return n;
}


/* returns true if the list is empty */
int vt_list_empty(struct vt_list *l)
{
  if (l->head == (struct vt_listnode *)&l->tail)
    return 1;
  else
    return 0;
}

/* removes head node of list */
struct vt_listnode *vt_list_remhead(struct vt_list *l)
{
  struct vt_listnode *n;

  if (vt_list_empty(l))
    return 0L;
  else {
    n = l->head;
    n->next->prev = n->prev;
    l->head = n->next;
  }
  return n;
}

/* removes tail node of list */
struct vt_listnode *vt_list_remtail(struct vt_list *l)
{
  struct vt_listnode *n;

  if (vt_list_empty(l)) {
    d(printf("empty list, returning null\n"));
    return 0L;
  } else {
    d(printf("unlinling last element\n"));
    n = l->tailpred;
    n->prev->next = n->next;
    l->tailpred = n->prev;
  }
  return n;
}


/* remove a node from a list 
  must have already been added to the list */
struct vt_listnode *vt_list_remove(struct vt_listnode *n)
{
  n->next->prev = n->prev;
  n->prev->next = n->next;
  return n;
}

/* insert n into list l before node p */
struct vt_listnode *vt_list_insert(struct vt_list *l, struct vt_listnode *p, struct vt_listnode *n)
{
  if (p->next==0L) {    
    vt_list_addtail(l, n);
  } else {
    p->prev->next = n;
    n->prev = p->prev;
    n->next = p;
    p->prev = n;
  }
  return n;
}

/* find a node by numerical index
   if negative, then scan backward, from the list tail
   for negative scans, "node -1" is the first
   for forward scans "node 0" is the first
*/
struct vt_listnode *vt_list_index(struct vt_list *l, int index)
{
  struct vt_listnode *wn, *nn;

  if (index<0) {
    wn = l->tailpred;
    nn = wn->prev;
    index++;
    while (nn && (index<0)) {
      wn=nn;
      nn=nn->prev;
      index++;
    }
  } else {
    wn = l->head;
    nn = wn->next;
    while (nn && index) {
      wn=nn;
      nn=nn->next;
      index--;
    }
  }
  if (index==0) {
    return wn;
  } else {
    return 0;
  }
}
