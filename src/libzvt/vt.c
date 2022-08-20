/*  vt.c - Zed's Virtual Terminal
 *  Copyright (C) 1998  Michael Zucchi
 *
 *  A virtual terminal emulator.
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

/*
  This module handles the update of the 'off-screen' virtual terminal
*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <sys/ioctl.h>

#include <glib.h>

#include "lists.h"
#include "vt.h"
#include "subshell.h"

/* define to 'x' to enable copius debug of this module */
#define d(x)

/* this one will check nodes aren't 'past' the end of list */
#define n(x)
/*
#define n(x) { if ((x)->next == 0) printf("Bad node, points beyond list: " #x "in %s: line %d\n", __PRETTY_FUNCTION__, __LINE__); }
*/

/* draw selected text (if selected!) */
void vt_draw_text_select(int col, int row, char *text, int len, int attr);

struct vt_line *vt_newline(struct vt_em *vt);

void vt_scroll_up(struct vt_em *vt, int count);
void vt_scroll_down(struct vt_em *vt, int count);
void vt_insert_chars(struct vt_em *vt, int count);
void vt_delete_chars(struct vt_em *vt, int count);
void vt_insert_lines(struct vt_em *vt, int count);
void vt_delete_lines(struct vt_em *vt, int count);
void vt_clear_lines(struct vt_em *vt, int top, int count);
void vt_clear_line_portion(struct vt_em *vt, int start_col, int end_col);

static void vt_resize_lines(struct vt_line *wm, int width, uint32 default_attr);
static void vt_gotoxy(struct vt_em *vt, int x, int y);

static unsigned char vt_remap_dec[256];

#ifdef DEBUG
static void
dump_scrollback(struct vt_em *vt)
{
  struct vt_line *wn, *nn;

  printf("****** scrollback list:\n");
  
  wn=&vt->scrollback.head;
  wn=&vt->lines.head;
  while (wn) {
    printf(" %p: p<: %p, n>:%p\n", wn, wn->prev, wn->next);
    wn = wn->next;
  }
}

static void
vt_dump(struct vt_em *vt)
{
  struct vt_line *wn, *nn;
  int i;

  wn = (struct vt_line *)vt->lines.head;
  nn = wn->next;
  (printf("dumping state of vt buffer:\n"));
  while (nn) {
    /*for (i=0;i<wn->width;i++) {*/
    printf ("%05d: ", wn->line);
    for (i=0;i<80;i++) {
      (printf("%c", wn->data[i]&VTATTR_DATAMASK));
    }
    (printf("\n"));
    wn=nn;
    nn = nn->next;
  }
  (printf("done\n"));
}

#endif

/***********************************************************************
 * Update functions
 */

/*
 *set the screen, either
 *  screen = 0 for main screen
 *         = 1 for alternate screen
 */

static void vt_set_screen(struct vt_em *vt, int screen)
{
  struct vt_line *lh, *lt, *ah, *at;
  int line;

  d(printf("vt_set_screen(%d) called\n", screen));

  if (((vt->mode&VTMODE_ALTSCREEN)?1:0) ^ screen) {

    d(printf("vt_set_screen swapping buffers ... from %d\n", (vt->mode&VTMODE_ALTSCREEN)?1:0));

    /* need to swap 2 list headers.
       tricky bit is catering for all the back pointers? */
    lh = (struct vt_line *)vt->lines.head;
    lt = (struct vt_line *)vt->lines.tailpred;

    ah = (struct vt_line *)vt->lines_alt.head;
    at = (struct vt_line *)vt->lines_alt.tailpred;    

    /* set new head/tail pointers */
    vt->lines.head = (struct vt_listnode *)ah;
    vt->lines.tailpred = (struct vt_listnode *)at;

    vt->lines_alt.head = (struct vt_listnode *)lh;
    vt->lines_alt.tailpred = (struct vt_listnode *)lt;

    /* and link back links */
    ah->prev = (struct vt_line *)&vt->lines.head;
    at->next = (struct vt_line *)&vt->lines.tail;

    lh->prev = (struct vt_line *)&vt->lines_alt.head;
    lt->next = (struct vt_line *)&vt->lines_alt.tail;

    /* and mark all lines as changed/but un-moved */
    at = ah->next;
    line=0;
    while (at) {
      ah->modcount=ah->width;
      ah->line = line++;
      ah = at;
      at = at->next;
    }

    d({
      ah = &vt->lines.head;
      lh = &vt->lines_alt.head;
      while (ah) {
	printf("%p: %p %p   %p: %p %p\n", ah, ah->next, ah->prev, lh, lh->next, lh->prev);
	ah = ah->next;
	lh = lh->next;
      }
    });

    vt->this_line = (struct vt_line *)vt_list_index(&vt->lines, vt->cursory);
    n(vt->this_line);
    if (screen)
      vt->mode |= VTMODE_ALTSCREEN;
    else
      vt->mode &= ~VTMODE_ALTSCREEN;
  }
}  


/**
 * vt_scrollback_set:
 * @vt: the vt
 * @lines: number of lines to set the scrollback to
 *
 * Sets the scrollback buffer size to @lines lines.  This will
 * truncate the scrollback buffer if necessary.
 */
void
vt_scrollback_set(struct vt_em *vt, int lines)
{
  struct vt_line *ln;

  while (vt->scrollbacklines > lines) {
    /* remove the top of list line */
    ln = (struct vt_line *)vt_list_remhead(&vt->scrollback);
    g_free(ln);
    vt->scrollbacklines--;
  }
  vt->scrollbackmax = lines;
}

/*
 * clone the line 'line' and add it to the bottom of
 * the scrollback buffer.
 *
 * if the scrollback buffer is full, discard the oldest line
 *
 * it is up-to the caller to free or 'discard' the old line
 */
static void
vt_scrollback_add(struct vt_em *vt, struct vt_line *wn)
{
  struct vt_line *ln;

  /* create a new scroll-back line */
  ln = g_malloc(VT_LINE_SIZE(wn->width));
  ln->next = NULL;
  ln->prev = NULL;
  ln->width = wn->width;
  ln->modcount = 0;
  memcpy(ln->data, wn->data, wn->width * sizeof(uint32));

  /* add it to the scrollback buffer */
  vt_list_addtail(&vt->scrollback, (struct vt_listnode *)ln);
  ln->line = -1;
  
  /* limit the total number of lines in scrollback */
  if (vt->scrollbacklines >= vt->scrollbackmax) {
    /* remove the top of list line */
    ln = (struct vt_line *)vt_list_remhead(&vt->scrollback);
    g_free(ln);
    
    /* need to track changes to this, even if they're not 'real' */
    if (vt->scrollbackoffset) {
      vt->scrollbackold--;
      if ((-vt->scrollbackoffset) < vt->scrollbackmax)
	vt->scrollbackoffset--;
    }
  } else {
    vt->scrollbacklines++;
    
    /* we've effectively moved the 'old' scrollback position */
    if (vt->scrollbackoffset) {
      vt->scrollbackold--;
      vt->scrollbackoffset--;
    }
  }
}


/**
 * vt_scroll_up:
 * @vt: An initialised &vt_em.
 * @count: Number of lines to scroll.
 *
 * Scrolls the emulator @vt up by @count lines.  The scrolling
 * window is honoured.
 */
void
vt_scroll_up(struct vt_em *vt, int count)
{
  struct vt_line *wn, *nn;
  int i;
  uint32 blank;

  d(printf("vt_scroll_up count=%d top=%d bottom=%d\n", 
	   count, vt->scrolltop, vt->scrollbottom));

  blank = vt->attr & VTATTR_CLEARMASK;

  if (count>vt->height)
    count=vt->height;

  while (count>0) {
    /* first, find the line to remove */
    wn = (struct vt_line *)vt_list_index(&vt->lines, vt->scrolltop);
    if (!wn)
      {
	g_error("could not find line %d\n", vt->scrolltop);
      }

    vt_list_remove((struct vt_listnode *)wn);

    if ((vt->scrolltop==0) && ((vt->mode&VTMODE_ALTSCREEN)==0)) {
      vt_scrollback_add(vt, wn);
    }

    for (i=0;i<wn->width;i++)
      wn->data[i] = blank;

    if (wn->line == -1) {
      wn->modcount = wn->width;	/* make sure a wrap-scrolled line isn't marked clean */
    } else {
      wn->modcount = 0;		/* this speeds it up (heaps) but doesn't work :( */
      wn->line=-1;		/* flag new line */
    }

    /* insert it .. (on bottom of scroll area) */
    nn=(struct vt_line *)vt_list_index(&vt->lines, vt->scrollbottom);
    vt_list_insert(&vt->lines, (struct vt_listnode *)nn, (struct vt_listnode *)wn);

    count--;
  }

  vt->this_line = (struct vt_line *)vt_list_index(&vt->lines, vt->cursory);

  d(printf("vt_scroll_up() done\n"));
}


/**
 * vt_scroll_down:
 * @vt: An initialised &vt_em.
 * @count: Number of lines to scroll.
 *
 * Scrolls the emulator @vt down by @count lines.  The scrolling
 * window is honoured.
 */
void
vt_scroll_down(struct vt_em *vt, int count)
{
  struct vt_line *wn, *nn;
  int i;
  uint32 blank = vt->attr & VTATTR_CLEARMASK;

  d(printf("vt_scroll_down count=%d top=%d bottom=%d\n",
	   count, vt->scrolltop, vt->scrollbottom));

  /* FIXME: do this properly */

  if (count>vt->height)
    count=vt->height;

  while(count>0) {

    /* first, find the line to remove */
    wn=(struct vt_line *)vt_list_index(&vt->lines, vt->scrollbottom);
    vt_list_remove((struct vt_listnode *)wn);
    
    /* clear it */
    for (i=0;i<wn->width;i++) {
      wn->data[i] = blank;
    }
    wn->modcount=0;
    wn->line = -1;		/* flag new line */
    
    /* insert it .. (on bottom of scroll area) */
    nn=(struct vt_line *)vt_list_index(&vt->lines, vt->scrolltop);
    vt_list_insert(&vt->lines, (struct vt_listnode *)nn, (struct vt_listnode *)wn);

    count--;
  }

  vt->this_line = (struct vt_line *)vt_list_index(&vt->lines, vt->cursory);
}

/**
 * vt_insert_chars:
 * @vt: An initialised &vt_em.
 * @count: Number of character to insert.
 *
 * Inserts @count blank characters at the current cursor position.
 */
void
vt_insert_chars(struct vt_em *vt, int count)
{
  int i, j;
  struct vt_line *l;

  d(printf("vt_insert_chars(%d)\n", count));
  l=vt->this_line;

  count=MIN(count,(vt->width-vt->cursorx));

  /* scroll data over count bytes */
  j = (l->width-count)-vt->cursorx;
  for (i=l->width-1;j>0;i--,j--) {
    l->data[i] = l->data[i-count];
  }

  /* clear the rest of the line */
  for (i=vt->cursorx;i<vt->cursorx+count;i++) {
    l->data[i] = vt->attr & VTATTR_CLEARMASK;
  }
  l->modcount+=count;
}

/* if this accesses more than this_line and cursorx, change vt_scroll_left */
void
vt_delete_chars(struct vt_em *vt, int count)
{
  int i, j;
  struct vt_line *l;
  uint32 blank;

  d(printf("vt_delete_chars(%d)\n", count));
  l=vt->this_line;

  /* check input value for validity */
  count=MIN(count,(vt->width-vt->cursorx));

  /* scroll data over count bytes */
  j = (l->width-count)-vt->cursorx;
  for (i=vt->cursorx;j>0;i++,j--) {
    l->data[i] = l->data[i+count];
  }

  /* clear the rest of the line */
  blank = l->data[l->width-1] & VTATTR_CLEARMASK & VTATTR_MASK;
  for (i=l->width-count;i<l->width;i++) {
    l->data[i] = blank;
  }
  l->modcount+=count;
}

/* erase characters */
static void
vt_erase_chars(struct vt_em *vt, int count)
{
  struct vt_line *l;
  int i;

  l = vt->this_line;
  for (i = vt->cursorx; i<vt->cursorx+count && i<l->width;i++)
    l->data[i] = vt->attr & VTATTR_CLEARMASK;
}

void vt_insert_lines(struct vt_em *vt, int count)
{
  struct vt_line *wn, *nn;
  int i;
  uint32 blank = vt->attr & VTATTR_CLEARMASK;

  d(printf("vt_insert_lines(%d) (top = %d bottom = %d cursory = %d)\n",
	   count, vt->scrolltop, vt->scrollbottom, vt->cursory));

  /* FIXME: Do this properly */

  if (count>vt->height)
    count=vt->height;

  while(count>0) {
    /* first, find the line to remove */
    wn=(struct vt_line *)vt_list_index(&vt->lines, vt->scrollbottom);
    vt_list_remove((struct vt_listnode *)wn);
    
    /* clear it */
    for (i=0;i<wn->width;i++) {
      wn->data[i] = blank;
    }
    wn->modcount=0;		/* set as 'unchanged' so the scroll
				   routine can update it.
				   but, if anyone else changes this line, make
				   sure the rest of the screen is updated */
    /*wn->line=vt->cursory;*/		/* also 'fool' it into thinking the
					   line is unchanged */
    wn->line=-1;

    /* insert it .. */
    nn=(struct vt_line *)vt_list_index(&vt->lines, vt->cursory);
    vt_list_insert(&vt->lines, (struct vt_listnode *)nn, (struct vt_listnode *)wn);

    count--;
  }

  vt->this_line = (struct vt_line *)vt_list_index(&vt->lines, vt->cursory);
  n(vt->this_line);
}

void vt_delete_lines(struct vt_em *vt, int count)
{
  struct vt_line *wn, *nn;
  int i;
  uint32 blank = vt->attr & VTATTR_CLEARMASK;

  d(printf("vt_delete_lines(%d)\n", count));
  /* FIXME: do this properly */

  /* range check! */
  if (count>vt->height)
    count=vt->height;

  while (count>0) {
    /* first, find the line to remove */
    wn=(struct vt_line *)vt_list_index(&vt->lines, vt->cursory);
    vt_list_remove((struct vt_listnode *)wn);
    
    /* clear it */
    for (i=0;i<wn->width;i++) {
      wn->data[i] = blank;
    }
    wn->modcount=0;
    /*wn->line=vt->scrollbottom;*/
    wn->line=-1;
    
    /* insert it .. (on bottom of scroll area) */
    nn=(struct vt_line *)vt_list_index(&vt->lines, vt->scrollbottom);
    vt_list_insert(&vt->lines, (struct vt_listnode *)nn, (struct vt_listnode *)wn);
    
    count--;
  }
  vt->this_line = (struct vt_line *)vt_list_index(&vt->lines, vt->cursory);
  n(vt->this_line);
}

void vt_clear_lines(struct vt_em *vt, int top, int count)
{
  struct vt_line *wn, *nn;
  int i;
  uint32 blank=vt->attr&VTATTR_CLEARMASK;

  d(printf("vt_clear_lines(%d, %d)\n", top, count));
  i=1;
  wn=(struct vt_line *)vt_list_index(&vt->lines, top);
  nn=wn->next;
  while(nn && count>=0) {
    for(i=0;i<wn->width;i++) {
      wn->data[i] = blank;
    }
    wn->modcount = wn->width;
    count--;
    wn=nn;
    nn=nn->next;
  }
}

void vt_clear_line_portion(struct vt_em *vt, int start_col, int end_col)
{
  struct vt_line *this_line;
  int i;
  uint32 blank = vt->attr&VTATTR_CLEARMASK;

  d(printf("vt_clear_line_portion()\n"));

  start_col = MIN(start_col, vt->width);
  end_col = MIN(end_col, vt->width);

  this_line = vt->this_line;
  for(i=start_col;i<end_col;i++) {
    this_line->data[i] = blank;
  }
  this_line->modcount+=(this_line->width-vt->cursorx);
}

/*
  resets terminal state completely
*/
void vt_reset_terminal(struct vt_em *vt, int hard)
{

  vt->attr=VTATTR_CLEAR;	/* reset attributes */
  vt->remaptable = 0;		/* no character remapping */
  vt->mode = 0;

  vt->Gx=0;			/* reset all fonts */
  vt->G[0]=0;
  vt->G[1]=vt_remap_dec;
  vt->G[2]=0;
  vt->G[3]=0;

  if (hard) {
    vt->cursorx=0;
    vt->cursory=0;
    vt->this_line = (struct vt_line *)vt->lines.head;
    vt_set_screen(vt, 0);
    vt_clear_lines(vt, 0, vt->height);
  }
}


/********************************************************************************\
 PARSER Callbacks
\********************************************************************************/
static void vt_bell(struct vt_em *vt)
{
  if (vt->ring_my_bell)
    vt->ring_my_bell(vt->user_data);
  d(printf("bell\n"));
}

/* carriage return */
static void vt_cr(struct vt_em *vt)
{
  d(printf("cr \n"));
  vt->cursorx = 0;
}

/* line feed */
static void vt_lf(struct vt_em *vt)
{
  d(printf("lf \n"));

  if ((vt->cursory > vt->scrollbottom
      || vt->cursory < vt->scrollbottom)
      && (vt->cursory < vt->height-1)) {
    vt->cursory++;
    d(printf("new ypos = %d\n", vt->cursory));
    vt->this_line = vt->this_line->next;
  } else {
    vt_scroll_up(vt, 1);
    vt->this_line = (struct vt_line *)vt_list_index(&vt->lines, vt->cursory);
  }
  n(vt->this_line);
}

/* next-line */
static void vt_nl(struct vt_em *vt)
{
  vt_cr(vt);
  vt_lf(vt);
}

/* jump to next tab stop, wrap if needed */
static void vt_tab(struct vt_em *vt)
{
  unsigned char c;
  struct vt_line *l;

  d(printf("tab\n"));

  l = vt->this_line;
  if (vt->cursorx>=vt->width) {
      if (!(vt->mode & VTMODE_WRAPOFF)) {
	  vt->cursorx = 0;
	  vt_lf(vt);			/* goto next line */
      } else
	  return;
  }
  c = l->data[vt->cursorx] & VTATTR_DATAMASK;

  /* dont store tab over a space - will affect attributes */
  if (c == 0) {
    /* We do not store the attribute as tabs are transparent
     * with respect to attributes
     */
    l->data[vt->cursorx] = 9 | (l->data[vt->cursorx]&VTATTR_MASK);
  }

  /* move cursor to new tab position */
  vt->cursorx = (vt->cursorx + 8) & (~7);
  if (vt->cursorx > vt->width) {
      if (vt->mode & VTMODE_WRAPOFF) {
	  vt->cursorx=vt->width-1;
      } else {
	  vt->cursorx = 0;
	  vt_lf(vt);			/* goto next line */
      }
  }
}

/* go to previous tab */
static void vt_backtab(struct vt_em *vt)
{
  if (vt->cursorx)
    vt->cursorx = (vt->cursorx-1) & (~7);
}

static void vt_backspace(struct vt_em *vt)
{
  d(printf("bs \n"));
  if (vt->cursorx>0) {
    vt->cursorx--;
  }
}

static void vt_alt_start(struct vt_em *vt)
{
  d(printf("alternate charset on\n"));
  /* swap in a 'new' charset mapping */
  vt->remaptable = vt->G[1];		/* no character remapping */
}

static void vt_alt_end(struct vt_em *vt)
{
  d(printf("alternate charset off\n"));
  /* restore 'old' charset mapping */
  vt->remaptable = vt->G[0];		/* no character remapping */
}

/* just reverse scroll */
static void vt_scroll_reverse(struct vt_em *vt)
{
  d(printf("reverse scroll line(s)\n"));
  vt_scroll_down(vt, vt->arg.num.intargs[0]?vt->arg.num.intargs[0]:1);
}

/* just forward scroll */
static void vt_scroll_forward(struct vt_em *vt)
{
  d(printf("forward scroll line(s)\n"));
  vt_scroll_up(vt, vt->arg.num.intargs[0]?vt->arg.num.intargs[0]:1);
}

/* insert 'count' columns from 'startx' */
static void vt_insert_columns(struct vt_em *vt, int startx, int count)
{
  struct vt_line *l, *oldthis;
  int oldx;

  oldthis = vt->this_line;
  oldx = vt->cursorx;
  vt->cursorx = startx;
  l = (struct vt_line *)vt->lines.head;
  while (l->next) {
    vt->this_line = l;
    vt_insert_chars(vt, count);
    l = l->next;
  }
  vt->this_line = oldthis;
  vt->cursorx = oldx;
}

static void vt_delete_columns(struct vt_em *vt, int startx, int count)
{
  struct vt_line *l, *oldthis;
  int oldx;

  oldthis = vt->this_line;
  oldx = vt->cursorx;
  vt->cursorx = startx;
  l = (struct vt_line *)vt->lines.head;
  while (l->next) {
    vt->this_line = l;
    vt_delete_chars(vt, count);
    l = l->next;
  }
  vt->this_line = oldthis;
  vt->cursorx = oldx;
}

/* insert column */
static void vt_decic(struct vt_em *vt)
{
  if (vt->cursorx<vt->width)
    vt_insert_columns(vt, vt->cursorx, vt->arg.num.intargs[0]?vt->arg.num.intargs[0]:1);
}

/* delete column */
static void vt_decdc(struct vt_em *vt)
{
  if (vt->cursorx<vt->width)
    vt_delete_columns(vt, vt->cursorx, vt->arg.num.intargs[0]?vt->arg.num.intargs[0]:1);
}

/* scroll right */
static void vt_scroll_right(struct vt_em *vt)
{
  vt_insert_columns(vt, 0, vt->arg.num.intargs[0]?vt->arg.num.intargs[0]:1);
}

/* scroll left */
static void vt_scroll_left(struct vt_em *vt)
{
  vt_delete_columns(vt, 0, vt->arg.num.intargs[0]?vt->arg.num.intargs[0]:1);
}

/* line editing */
static void vt_insert_char(struct vt_em *vt)
{
  switch (vt->state) {
  case 7:
    vt_scroll_left(vt);
    break;
  default:
    d(printf("insert char(s)\n"));
    vt_insert_chars(vt, vt->arg.num.intargs[0]?vt->arg.num.intargs[0]:1);
  }
}

static void vt_delete_char(struct vt_em *vt)
{
  d(printf("delete char(s)\n"));
  vt_delete_chars(vt, vt->arg.num.intargs[0]?vt->arg.num.intargs[0]:1);
}

static void
vt_erase_char(struct vt_em *vt)
{
  vt_erase_chars(vt, vt->arg.num.intargs[0]?vt->arg.num.intargs[0]:1);
}

/* insert lines and scroll down */
static void
vt_insert_line(struct vt_em *vt)
{
  vt_insert_lines(vt, vt->arg.num.intargs[0]?vt->arg.num.intargs[0]:1);
}

/* delete lines and scroll up */
static void
vt_delete_line(struct vt_em *vt)
{
  d(printf("vt_delete_line\n"));

  switch (vt->state) {
  case 1:
    if (vt->cursory > vt->scrolltop) {	/* reverse line feed, not delete */
      d(printf("vt_delete_line: should we try to scroll up?\n"));
      vt->cursory--;
    } else {
      vt_scroll_down(vt, 1);
    }
    break;
  default:
    vt_delete_lines(vt, vt->arg.num.intargs[0]?vt->arg.num.intargs[0]:1);
  }

  vt->this_line = (struct vt_line *)vt_list_index(&vt->lines, vt->cursory);
  n(vt->this_line);
}

/* clear from current cursor position to end of screen */
static void
vt_cleareos(struct vt_em *vt)
{
  d(printf("clear screen/end of screen\n"));

  switch(vt->arg.num.intargs[0]) {
  case 0:			/* clear here to end of screen */
    vt_clear_line_portion(vt, vt->cursorx, vt->this_line->width);
    vt_clear_lines(vt, vt->cursory+1, vt->height);
    break;
  case 1:			/* clear top of screen to here */
    vt_clear_line_portion(vt, 0, vt->cursorx);
    vt_clear_lines(vt, 0, vt->cursory);
    break;
  case 2:			/* clear the whole damn thing */
    vt_clear_lines(vt, 0, vt->height);
    break;
  }
}

static void
vt_clear_lineportion(struct vt_em *vt)
{
  d(printf("Clear part of line\n"));
  switch (vt->arg.num.intargs[0]) {
  case 0:
    vt_clear_line_portion(vt, vt->cursorx, vt->this_line->width);
    break;
  case 1:
    vt_clear_line_portion(vt, 0, vt->cursorx + 1);
    break;
  case 2:
    vt_clear_line_portion(vt, 0, vt->this_line->width);
    break;
  }
  /* ignore bad parameters */
}


/* cursor save/restore */
static void
vt_save_cursor(struct vt_em *vt)
{
  d(printf("save cursor\n"));
  vt->savex = vt->cursorx;
  vt->savey = vt->cursory;
  vt->savemode = vt->mode;
  vt->saveattr = vt->attr;
  vt->saveremaptable = vt->remaptable;
}

static void
vt_restore_cursor(struct vt_em *vt)
{
  d(printf("restore cursor\n"));

  vt->cursorx = vt->savex;
  vt->cursory = vt->savey;
  vt->mode = (vt->savemode & (VTMODE_INSERT | VTMODE_WRAPOFF | VTMODE_APP_CURSOR | VTMODE_RELATIVE));

  vt->attr = vt->saveattr;
  vt->remaptable = vt->saveremaptable;

  /* incase the save/restore was across re-sizes */
  if (vt->cursorx > vt->width)
    vt->cursorx = vt->width;
  if (vt->cursory >= vt->height)
    vt->cursory = vt->height-1;

  vt->this_line = (struct vt_line *)vt_list_index(&vt->lines, vt->cursory);
  n(vt->this_line);
  d(printf("found line %d, %p\n", vt->cursory, vt->this_line));
}

/* cursor movement */
/* in app-cursor mode, this will cause the screen to scroll ? */
static void
vt_up(struct vt_em *vt)
{
  int count;

  d(printf("\n----------------------\n cursor up\n"));

  switch (vt->state) {
  case 7:			/* scroll left instead! */
    vt_scroll_right(vt);
    break;
  default:
    count = vt->arg.num.intargs[0]?vt->arg.num.intargs[0]:1;
    d(printf("cursor up %d, from %d\n", count, vt->cursory));
    vt_gotoxy(vt, vt->cursorx, vt->cursory-count);
  }
}

static void
vt_down(struct vt_em *vt)
{
  int count;

  d(printf("cursor down\n"));
  count = vt->arg.num.intargs[0]?vt->arg.num.intargs[0]:1;
  vt_gotoxy(vt, vt->cursorx, vt->cursory+count);
}

static void
vt_right(struct vt_em *vt)
{
  int count;

  count = vt->arg.num.intargs[0]?vt->arg.num.intargs[0]:1;
  d(printf("cursor right(%d)\n", count));
  vt_gotoxy(vt, vt->cursorx+count, vt->cursory);
}

/*
  this handles ESC.*D
  which is normally "left', but ESCD is "cursor down"
*/
static void
vt_left(struct vt_em *vt)
{
  int count;

  switch (vt->state) {
    case 1:			/* line feed, not left */
    d(printf("vt_left: no, linefeed instead!\n"));
    vt_lf(vt);
    break;
  default:
    count = vt->arg.num.intargs[0]?vt->arg.num.intargs[0]:1;    
    d(printf("cursor left(%d)\n", count));
    vt_gotoxy(vt, vt->cursorx-count, vt->cursory);
  }
}

/*
  goto a specified position, based on integer arguments */
static void
vt_gotoxy(struct vt_em *vt, int x, int y)
{
  int miny,maxy;

  if (vt->mode & VTMODE_RELATIVE) {
    miny=vt->scrolltop;
    maxy=vt->scrollbottom;
  } else {
    miny=0;
    maxy=vt->height;
  }

  if (x < 0)
    x = 0;
  if (y < miny)
    y = miny;
  if (x >= vt->width)
    x = vt->width-1;
  if (y >= maxy)
    y = maxy-1;

  vt->cursory=y;
  vt->cursorx=x;
  d(printf("pos = %d %d\n", vt->cursory, vt->cursorx));

  vt->this_line = (struct vt_line *)vt_list_index(&vt->lines, vt->cursory);
  n(vt->this_line);
}

/* jump to a cursor position */
static void
vt_goto(struct vt_em *vt)
{
  int x,y;

  d(printf("goto position\n"));
  y = vt->arg.num.intargs[0]?vt->arg.num.intargs[0]-1:0;
  if (vt->argcnt>1)
    x = vt->arg.num.intargs[1]?vt->arg.num.intargs[1]-1:0;
  else
    x=0;

  if (vt->mode & VTMODE_RELATIVE)
    y+=vt->scrolltop;

  vt_gotoxy(vt, x, y);
}

/* goto a specific y location */
/* also handles \Ed reset coding method */
static void
vt_gotoabsy(struct vt_em *vt)
{
#ifdef ZVT_UTF
  if (vt->state==1) {
    vt->coding = ZVT_CODE_ISOLATIN1;
  } else {
#endif
    vt_gotoxy(vt, vt->cursorx, vt->arg.num.intargs[0]?vt->arg.num.intargs[0]-1:0);
#ifdef ZVT_UTF
  }
#endif
}

static void
vt_gotoabsx(struct vt_em *vt)
{
  vt_gotoxy(vt, vt->arg.num.intargs[0]?vt->arg.num.intargs[0]-1:0, vt->cursory);
}

/* set character encoding
   \E%n sequence */
static void
vt_char_encoding(struct vt_em *vt)
{
#ifdef ZVT_UTF
  switch (vt->arg.num.intargs[1]) {
  case 'G':
    vt->coding = ZVT_CODE_UTF8;
    vt->decode.utf8.shiftchar = 0;
    break;
  default:
    vt->coding = ZVT_CODE_ISOLATIN1;
  }
#endif
}

/* various mode stuff */
static void
vt_setmode(struct vt_em *vt, int on)
{
  int i;

  d(printf("mode h set\n"));
  for (i=0;i<vt->argcnt;i++) {
    switch (vt->state) {
    case 2:
      switch (vt->arg.num.intargs[i]) {
      case 4:
	d(printf("insert mode\n"));
	if (on)
	  vt->mode |= VTMODE_INSERT;
	else
	  vt->mode &= ~VTMODE_INSERT;
	break;
      }
      break;
    case 6:
      switch(vt->arg.num.intargs[i]) {
      case 1:			/* turn on application cursor keys */
	if (on)
	  vt->mode |= VTMODE_APP_CURSOR;
	else
	  vt->mode &= ~VTMODE_APP_CURSOR;
	break;
      case 6:
	if (on) {
	  vt->mode |= VTMODE_RELATIVE;
	  vt_gotoxy(vt, vt->scrolltop, 0);
	} else {
	  vt->mode &= ~VTMODE_RELATIVE;
	  vt_gotoxy(vt, 0, 0);
	}
	break;
      case 7:
	if (on)
	  vt->mode &= ~VTMODE_WRAPOFF;
	else
	  vt->mode |= VTMODE_WRAPOFF;
	break;
      case 25:			/* cursor invisible/normal */
	if (on)
	  vt->mode &= ~VTMODE_BLANK_CURSOR;
	else
	  vt->mode |= VTMODE_BLANK_CURSOR;
	break;
      case 1047:		/* clear the screen if coming from the alt screen */
	if (on==0 && (vt->mode&&VTMODE_ALTSCREEN))
	  vt_clear_lines(vt, 0, vt->height);
	/* falls through */
      case 47:
	vt_set_screen(vt, on?1:0);
	break;
      case 9:			/* also for DEC private modes */
	d(printf("sending mouse events\n"));
	vt->mode &= ~VTMODE_SEND_MOUSE_MASK;
	if (on)
	  vt->mode |= VTMODE_SEND_MOUSE_PRESS;
	break;
      case 1000:
	d(printf("sending mouse events\n"));
	vt->mode &= ~VTMODE_SEND_MOUSE_MASK;
	if (on)
	  vt->mode |= VTMODE_SEND_MOUSE_BOTH;
	break;
      case 1048:
	if (on)
	  vt_save_cursor(vt);
	else
	  vt_restore_cursor(vt);
	break;
      }
    }
  }
}

static void
vt_modeh(struct vt_em *vt)
{
  vt_setmode(vt, 1);
}

static void
vt_model(struct vt_em *vt)
{
  vt_setmode(vt, 0);
}

static void
vt_modek(struct vt_em *vt)
{
  d(printf("mode k set\n"));
  switch(vt->arg.num.intargs[0]) {
  case 3:
    d(printf("clear tabs\n"));
    /* FIXME: do it */
    break;
  }
}


static void
vt_mode(struct vt_em *vt)
{
  int i, j;
  static int mode_map[] = {0, VTATTR_BOLD, 0, 0,
			   VTATTR_UNDERLINE, VTATTR_BLINK, 0,
			   VTATTR_REVERSE, VTATTR_CONCEALED};

  for (j = 0; j < vt->argcnt; j++) {
    i = vt->arg.num.intargs[j];
    if (i==0 || i==27) {
      vt->attr=VTATTR_CLEAR;
    } else if (i<9) {
      vt->attr |= mode_map[i];	/* add a mode */
    } else if (i>=20 && i <=28) {
      if (i==22) i=21;	/* 22 resets bold, not 21 */
      vt->attr &= ~mode_map[i-20]; /* remove a mode */
    } else if (i>=30 && i <=37) {
      vt->attr = (vt->attr & ~VTATTR_FORECOLOURM) | ((i-30) << VTATTR_FORECOLOURB);
    } else if (i==39) {
      vt->attr = (vt->attr & ~VTATTR_FORECOLOURM) | ((VTATTR_CLEAR) & VTATTR_FORECOLOURM);
    } else if (i>=40 && i <=47) {
      vt->attr = (vt->attr & ~VTATTR_BACKCOLOURM) | ((i-40) << VTATTR_BACKCOLOURB);
    } else if (i==49) {
      vt->attr = (vt->attr & ~VTATTR_BACKCOLOURM) | ((VTATTR_CLEAR) & VTATTR_BACKCOLOURM);
    } else if (i>=90 && i <=97) {
      vt->attr = (vt->attr & ~VTATTR_FORECOLOURM) | ((i-90 + 8) << VTATTR_FORECOLOURB);
    } else if (i>=100 && i <=107) {
      vt->attr = (vt->attr & ~VTATTR_BACKCOLOURM) | ((i-100 + 8) << VTATTR_BACKCOLOURB);
    }
  }
}

static void
vt_keypadon(struct vt_em *vt)
{
  /* appl. keypad 'on' */
  vt->mode |= VTMODE_APP_KEYPAD;
}

static void
vt_keypadoff(struct vt_em *vt)
{
  /* appl. keypad 'off' */
  vt->mode &= ~VTMODE_APP_KEYPAD;
}

/*
 *  sets g0-g3 charset
 */
static void
vt_gx_set(struct vt_em *vt)
{
  int index;
  unsigned char *table;

  index = vt->arg.num.intargs[0]-'(';
  if (index<=3 && index>=0) {
    switch (vt->arg.num.intargs[1]) {
    case '0':
      table = vt_remap_dec;
      break;
    case 'A':			/* turn mapping off for uk or us */
    case 'B':			/* not strictly correct, but easy to fix */
      table = 0;
      break;
    default:			/* unknown char mapping - turn off*/
      table = 0;
    }
    vt->G[index] = table;
    if (index==vt->Gx) {
      vt->remaptable = table;
    }
  } /* else ignore */
}

/*
  device status report
*/
static void
vt_dsr(struct vt_em *vt)
{
  char status[16];

  switch(vt->arg.num.intargs[0]) {
  case 5:			/* report 'ok' status */
    g_snprintf(status, sizeof(status), "\033[0n");
    break;
  case 6:			/* report cursor position */
    g_snprintf(status, sizeof(status), "\033[%d;%dR",
	       vt->cursory+1, vt->cursorx+1);
    break;
  default:
    status[0]=0;
  }
  vt_writechild(vt, status, strlen(status));
}

static void
vt_scroll(struct vt_em *vt)
{
  switch (vt->state) {
  case 2:
    vt->scrolltop = vt->arg.num.intargs[0]?vt->arg.num.intargs[0]-1:0;
    if (vt->argcnt>1)
      vt->scrollbottom = vt->arg.num.intargs[1]?vt->arg.num.intargs[1]-1:0;
    else
      vt->scrollbottom = vt->height-1;
    
    if (vt->scrollbottom >= vt->height)
      vt->scrollbottom = vt->height-1;
    if (vt->scrolltop > vt->scrollbottom)
      vt->scrolltop = vt->scrollbottom;
    
    d(printf("vt_scroll: vt->scrolltop=%d vt->scrollbottom=%d\n",
	    vt->scrolltop, vt->scrollbottom));
    
    vt_gotoxy(vt, 0, vt->scrolltop);
    break;
  }
}

/* for '\E#x' double-height/width, etc modes.
   Not implemented. */
static void
vt_deccharmode (struct vt_em *vt)
{
  /* do nothing!*/
}

/*
 * master soft reset ^[c
 * and report attributes ^[[0c
 */
static void
vt_reset(struct vt_em *vt)
{
#define DEVICE_ATTRIBUTES "\033[?6c"
#define DEVICE_ATTRIBUTES2 "\033[>1;0;0c"

  switch (vt->state) {
  case 2:
    /* report device attributes - vt102 */
    vt_writechild(vt, DEVICE_ATTRIBUTES, sizeof(DEVICE_ATTRIBUTES));
    break;
  case 10:
    /* report device attributes 2 - DUH this should only be in vt220 emulation vt220, firmware version 0.0, rom cart 0 */
    vt_writechild(vt, DEVICE_ATTRIBUTES2, sizeof(DEVICE_ATTRIBUTES2));
    break;
  default:
    vt_reset_terminal(vt, 0);
  }
}

/* function keys - does nothing */
static void
vt_func(struct vt_em *vt)
{
  int i;

  switch (vt->state) {
  case 9:			/* delete columns */
    vt_decdc(vt);
    break;
  default:
    d(printf("function keys\n"));
    switch (vt->arg.num.intargs[0]) {
    case 2:
      d(printf("insert pressed\n"));
      break;
    case 5:
      d(printf("page up pressed\n"));
      break;
    case 6:
      d(printf("page down pressed\n"));
      break;
    default:
      i = vt->arg.num.intargs[0];
      if (i>=11 && i <=20) {
	d(printf("function key %d pressed\n", i-10));
      } else {
	d(printf("Unknown function %d\n", i));
      }
    }
  }
}

/*
  'set text' called, call a callback if we have one

  The callback is called with VTTTILE_* constants and the string
  to set.
*/
static void
vt_set_text(struct vt_em *vt)
{
  char *p;
  int i;

  if (vt->change_my_name) {
    p = strchr(vt->arg.txt.args_mem, ';');
    if (p) {
      *p=0;
      p++;
      i = atoi(vt->arg.txt.args_mem);
      switch(i) {
      case 0:
	i = VTTITLE_WINDOWICON;
	break;
      case 1:
	i = VTTITLE_ICON;
	break;
      case 2:
	i = VTTITLE_WINDOW;
	break;
      case 3:
	i = VTTITLE_XPROPERTY;
	break;
      case 46:			/* log file .. disabled */
      case 50:			/* set font .. disabled too */
	return;
      default:			/* dont care, piss off */
	return;
      }
      /* Order changed to reflect the changed arg. order */
      vt->change_my_name(vt->user_data, p, i);
    }
  }
}

struct vt_jump {
  void (*process)(struct vt_em *vt);	/* process function */
  int modes;			/* modes appropriate */
};

#define VT_LIT 0x01		/* literal */
#define VT_CON 0x02		/* control character */
#define VT_EXB 0x04		/* escape [ sequence */
#define VT_EXO 0x08		/* escape O sequence */
#define VT_ESC 0x10		/* escape "x" sequence */
#define VT_ARG 0x20		/* character is a possible argument to function (only digits!) */
#define VT_EXA 0x40		/* escape x "x" sequence */

#define VT_EBL (VT_EXB|VT_LIT)	/* escape [ or literal */
#define VT_EXL (VT_ESC|VT_LIT)	/* escape "x" or literal */
#define VT_BOL (VT_EBL|VT_EXO)	/* escape [, escape O, or literal */

struct vt_jump vtjumps[] = {
  {0,0}, {0,0}, {0,0}, {0,0},	/* 0: ^@ ^C */
  {0,0}, {0,0}, {0,0}, {vt_bell,VT_CON},	/* 4: ^D ^G */
  {vt_backspace,VT_CON}, {vt_tab,VT_CON}, {vt_lf,VT_CON}, {0,0},	/* 8: ^H ^K */
  {0,0}, {vt_cr,VT_CON}, {vt_alt_start,VT_CON}, {vt_alt_end,VT_CON},	/* c: ^L ^O */
  {0,0}, {0,0}, {0,0}, {0,0},	/* 10: ^P ^S */
  {0,0}, {0,0}, {0,0}, {0,0},	/* 14: ^T ^W */
  {0,0}, {0,0}, {0,0}, {0,0},	/* 18: ^X ^[ */
  {0,0}, {0,0}, {0,0}, {0,0},	/* 1c: ^\ ^] ^^ ^_  */
  {0,VT_LIT}, {0,VT_LIT}, {0,VT_LIT}, {vt_deccharmode,VT_LIT|VT_EXA},	/*  !"# */
  {0,VT_LIT}, {vt_char_encoding,VT_LIT|VT_EXA}, {0,VT_LIT}, {0,VT_LIT},	/* $%&' */
  {vt_gx_set,VT_LIT|VT_EXA}, {vt_gx_set,VT_LIT|VT_EXA}, {vt_gx_set,VT_LIT|VT_EXA}, {vt_gx_set,VT_LIT|VT_EXA},	/* ()*+ */
  {0,VT_LIT}, {0,VT_LIT}, {0,VT_LIT}, {0,VT_LIT},	/* ,-./ */
  {0,VT_LIT|VT_ARG}, {0,VT_LIT|VT_ARG}, {0,VT_LIT|VT_ARG}, {0,VT_LIT|VT_ARG},	/* 0123 */
  {0,VT_LIT|VT_ARG}, {0,VT_LIT|VT_ARG}, {0,VT_LIT|VT_ARG}, {vt_save_cursor,VT_EXL|VT_ARG},	/* 4567 */
  {vt_restore_cursor,VT_EXL|VT_ARG}, {0,VT_LIT|VT_ARG}, {0,VT_LIT}, {0,VT_LIT},	/* 89:; */
  {0,VT_LIT}, {vt_keypadon,VT_EXL}, {vt_keypadoff,VT_EXL}, {0,VT_LIT},	/* <=>? */
  {vt_insert_char,VT_EBL}, {vt_up,VT_BOL}, {vt_down,VT_BOL}, {vt_right,VT_BOL},	/* @ABC */
  {vt_left,VT_BOL|VT_ESC}, {vt_nl,VT_EXL}, {0,VT_LIT}, {vt_gotoabsx,VT_EBL},	/* DEFG */
  {vt_goto,VT_EBL}, {0,VT_LIT}, {vt_cleareos,VT_EBL}, {vt_clear_lineportion,VT_EBL},	/* HIJK */
  {vt_insert_line,VT_EBL}, {vt_delete_line,VT_EBL|VT_ESC}, {0,VT_LIT}, {0,VT_LIT},	/* LMNO */
  {vt_delete_char,VT_EBL}, {0,VT_LIT}, {0,VT_LIT}, {vt_scroll_forward,VT_EBL},	/* PQRS */
  {vt_scroll_reverse,VT_EBL}, {0,VT_LIT}, {0,VT_LIT}, {0,VT_LIT},	/* TUVW */
  {vt_erase_char,VT_EBL}, {0,VT_LIT}, {vt_backtab,VT_EBL}, {0,VT_LIT},	/* XYZ[ */
  {0,VT_LIT}, {0,VT_LIT}, {vt_scroll_reverse,VT_EBL}, {0,VT_LIT},	/* \]^_ */
  {0,VT_LIT}, {0,VT_LIT}, {0,VT_LIT}, {vt_reset,VT_EXL|VT_EXB},	/* `abc */
  {vt_gotoabsy,VT_EBL|VT_ESC}, {0,VT_LIT}, {vt_goto,VT_EBL}, {0,VT_LIT},	/* defg */
  {vt_modeh,VT_EBL}, {0,VT_LIT}, {0,VT_LIT}, {vt_modek,VT_EBL},	/* hijk */
  {vt_model,VT_EBL}, {vt_mode,VT_EBL}, {vt_dsr,VT_EBL}, {0,VT_LIT},	/* lmno */
  {vt_reset,VT_EBL}, {0,VT_LIT}, {vt_scroll,VT_EBL}, {0,VT_LIT},	/* pqrs */
  {0,VT_LIT}, {0,VT_LIT}, {0,VT_LIT}, {0,VT_LIT},	/* tuvw */
  {0,VT_LIT}, {0,VT_LIT}, {0,VT_LIT}, {0,VT_LIT},	/* xyz{ */
  {0,VT_LIT}, {vt_decic,VT_EBL}, {vt_func,VT_EBL}, {0,VT_LIT},	/* |}~? */
};

/**
 * vt_parse_vt:
 * @vt: An initialised &vt_em.
 * @ptr: Text to parse.
 * @length: Length of text available at @ptr
 *
 * Parses a sequence of characters using an xterm compatible
 * terminal state machine.  All state and terminal output is
 * stored in @vt.
 */
void
vt_parse_vt (struct vt_em *vt, char *ptr, int length)
{
  register int c;
  register int state;
  register int mode;
  struct vt_jump *modes = vtjumps;
  char *ptr_end;
  void (*process)(struct vt_em *vt);	/* process function */

  /* states:
   *   0: normal escape mode
   *   1: escape mode.  switch on next character.
   *   2: '[' escape mode (keep finding numbers until a command code found)
   *   3: 'O' escape mode.  switch on next character.
   *   4: ']' escape mode.  swallows until following bell (or newline).
   *   5: '\Exy' sequence.
   *   6: '\E[?nn;...y' escape sequence.
   *   7: '\E[.... X' escape sequence.
   *   8: '\E[!X' escape sequence.
   *   9: '\E[....'X' escape sequence.
   *  10: '\E[>....X' escape sequence.
   *
   *   DO NOT CHANGE THESE STATES!  Some callbacks rely on them.
   */

  state = vt->state;
  ptr_end = ptr + length;
  while (ptr < ptr_end) {

    /* convert to unsigned byte */
    c = (*ptr++) & 0xff;

#ifdef ZVT_UTF

    if (vt->coding == ZVT_CODE_UTF8 && (c&0x80)!=0) {
      if (c<0xc0) {
	/* might be part of a multi-byte content ... */
	if ((vt->decode.utf8.shiftchar & 0x80) == 0) {
	  /* treat this character as a control character/international character.
	     This (0x80-0xc0] should cover the C1 control set and some of the
	     international characters in normal latin 1, but not all :( */
	  mode = modes[c & 0x7f].modes;
	  process = modes[c & 0x7f].process;
	} else {
	  vt->decode.utf8.shiftchar <<= 1;
	  vt->decode.utf8.wchar = (vt->decode.utf8.wchar<<6) | (c&0x3f);
	  if ((vt->decode.utf8.shiftchar & 0x80) == 0) {
	    c = vt->decode.utf8.wchar|((vt->decode.utf8.shiftchar&0x7f)<<vt->decode.utf8.shift);
	    /* and run with it! */
	    vt->decode.utf8.shiftchar=0;

	    /* all extended characters are just literals */
	    mode = VT_LIT;
	    process = 0;
	  } else {
	    vt->decode.utf8.shift+=5;
	    continue;
	  }
	}
      } else {
	vt->decode.utf8.shiftchar=c<<1;
	vt->decode.utf8.wchar=0;
	vt->decode.utf8.shift=4;
	continue;
      }
    } else {
      mode = modes[c & 0x7f].modes;
      process = modes[c & 0x7f].process;
    }
#else
    mode = modes[c & 0x7f].modes;
    process = modes[c & 0x7f].process;
#endif /* ZVT_UTF */

    vt->state = state;		/* so callbacks know their state */
    
    d(printf("state %d: %d $%02x '%c'\n",state, vt->argcnt, c, isprint(c)?c:'.'));
    
    switch (state) {
      
    case 0:
      if (mode & VT_LIT) {
	/* remap character? */
	if (vt->remaptable && c<=0xff)
	  c=vt->remaptable[c];
	
	/* insert mode? */
	if (vt->mode & VTMODE_INSERT)
	  vt_insert_chars(vt, 1);
	
	/* need to wrap? */
	if (vt->cursorx>=vt->width) {
	  if (vt->mode&VTMODE_WRAPOFF)
	    vt->cursorx = vt->width-1;
	  else {
	    vt_lf(vt);
	    vt->cursorx=0;
	  }
	}
	
	/* output character */
	vt->this_line->data[vt->cursorx] = ((vt->attr) & VTATTR_MASK) | c;
	vt->this_line->modcount++;
	/* d(printf("literal %c\n", c)); */
	vt->cursorx++;
      } else if (mode & VT_CON) {
	process(vt);
      } else if (c==27) {
	state=1;
      }
      /* else ignore */
      break;
      
      
      /* received 'esc', next byte */
    case 1:
      if (mode & VT_ESC) {	/* got a \Ex sequence */
	vt->argcnt = 0;
	vt->arg.num.intargs[0] = 0;
	process(vt);
	state=0;
      } else if (c=='[') {
	vt->arg.num.intarg = 0;
	vt->argcnt = 0;
	state = 2;
      } else if (c=='O') {
	state = 3;
      } else if (c==']') {	/* set text parameters, read parameters */
	state = 4;
	vt->arg.txt.outptr = vt->arg.txt.args_mem;
      } else if (mode & VT_EXA) {
	vt->arg.num.intargs[0] = c & 0x7f;
	state = 5;
      } else {
	state = 0;		/* dont understand input */
      }
      break;
      
      
    case 2:
      if (c=='?') {
	state = 6;
	continue;
      } else if (c=='>') {
	state = 10;
	continue;
      } else if (c=='!') {
	state = 8;
	continue;
      }
      /* note -> falls through */
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
      if (mode & VT_ARG) {
	/* accumulate subtotal */
	vt->arg.num.intarg = vt->arg.num.intarg*10+(c-'0');
      } else if ((mode & VT_EXB) || c==';' || c==':') {	/* looking for 'arg;arg;...' */
	if (vt->argcnt < VTPARAM_INTARGS) {
	  vt->arg.num.intargs[vt->argcnt] = vt->arg.num.intarg & 0x7fffffff;
	  vt->argcnt++;
	  vt->arg.num.intarg = 0;
	}
	if (mode & VT_EXB) {
	  process(vt);
	  state=0;
	}
      } else if (mode & VT_CON) {
	process(vt);
      } else if (c==' ') {	/* this expects the next char == VT_EXB.  close enough! */
	state = 7;		/* any other chars should be numbers anyway */
      } else if (c=='\'') {
	state = 9;
      } else {
	d(printf("unknown option '%c'\n", c));
	state=0;		/* unexpected escape sequence - ignore */
      }
      break;
      
      
      /* \EOx */
    case 3:
      if (mode & VT_EXO) {
	vt->arg.num.intargs[0] = 0;
	process(vt);
      }	/* ignore otherwise */
      state=0;
      break;
      
      
      /* \E]..;...BEL */
    case 4:
      if (c==0x07) {
	/* handle output */
	*(vt->arg.txt.outptr)=0;
	vt_set_text(vt);
	d(printf("received text mode: %s\n", vt->arg.txt.args_mem));
	state = 0;
      } else if (c==0x0a) {
	state = 0;		/* abort command */
      } else {
	if (vt->arg.txt.outptr<(vt->arg.txt.args_mem+VTPARAM_MAXARGS*VTPARAM_ARGMAX-2)) /* truncate excessive args */
	  *(vt->arg.txt.outptr)++=c;
      }
      break;
      
      
      /* \E?x */
    case 5:
      vt->arg.num.intargs[1]=c;
      vt->argcnt=0;
      modes[vt->arg.num.intargs[0]].process(vt);
      state=0;
      break;
    }
  }

  vt->state = state;
}

/**
 * vt_newline:
 * @vt: An initialised &vt_em.
 *
 * Allocates a new line using malloc().  The line matches the
 * current terminal size stored in @vt.
 *
 * Return value: A pointer to the newly allocated &vt_line structure.
 */
struct vt_line *vt_newline(struct vt_em *vt)
{
  int i;
  struct vt_line *l;

  l = g_malloc(VT_LINE_SIZE(vt->width));
  l->prev = NULL;
  l->next = NULL;
  l->width = vt->width;
  l->line = -1;
  l->modcount = vt->width;

  for (i = 0; i < vt->width; i++) {
    l->data[i] = vt->attr & VTATTR_CLEARMASK;
  }

  return l;
}

/**
 * vt_init:
 * @vt: Pointer to an uninitialised &vt_em structure.
 * @width: Desired width of the terminal, in characters.
 * @height: Desired height of the terminal, in characters.
 *
 * Initialise a new terminal emulator.
 *
 * Return value: Returns a pointer to the intialised terminal.
 * This will be the same as the value of @vt, passed in.
 */
struct vt_em *
vt_init(struct vt_em *vt, int width, int height)
{
  struct vt_line *vl;
  int i;

  vt_list_new(&vt->lines);
  vt_list_new(&vt->lines_back);
  vt_list_new(&vt->scrollback);
  vt_list_new(&vt->lines_alt);

  vt->width = width;
  vt->height = height;
  vt->scrolltop = 0;
  vt->scrollbottom = height-1;
  vt->attr = VTATTR_CLEAR;	/* default 'clear' character */
  vt->mode = 0;
  vt->remaptable = 0;		/* no character remapping */
  for (i=0;i<height;i++) {
    vl = vt_newline(vt);
    vl->line = i;
    vt_list_addtail(&vt->lines, (struct vt_listnode *)vl);

    vl = vt_newline(vt);
    vl->line = i;
    vt_list_addtail(&vt->lines_back, (struct vt_listnode *)vl);

    vl = vt_newline(vt);
    vl->line = i;
    vt_list_addtail(&vt->lines_alt, (struct vt_listnode *)vl);
  }
  vt->cursorx=0;
  vt->cursory=0;

  vt->childfd = -1;
  vt->childpid = -1;
  vt->keyfd = -1;

  vt->this_line = (struct vt_line *)vt->lines.head;

  vt->scrollbacklines=0;
  vt->scrollbackoffset=0;
  vt->scrollbackold=0;
  vt->scrollbackmax=50;		/* maximum scrollback lines */

  for(i=0;i<256;i++) {		/* initialise dec special char remapping */
    vt_remap_dec[i]=(i>95)&&(i<128)?(i-95):i;
  }

  vt->Gx=0;			/* reset all fonts */
  vt->G[0]=0;
  vt->G[1]=vt_remap_dec;
  vt->G[2]=0;
  vt->G[3]=0;

  vt->ring_my_bell = 0L;
  vt->change_my_name = 0L;

#ifdef ZVT_UTF
  vt->decode.utf8.shiftchar = 0;
  /* we actually start in isolatin1 mode */
  vt->coding = ZVT_CODE_ISOLATIN1;
#endif

  vt->user_data = 0;
  return vt;
}

/**
 * vt_forkpty:
 * @vt: An initialised &vt_em.
 * @do_uwtmp_log: If %TRUE, updates utmp/wtmp records.
 *
 * Start a child process running in the VT emulator using fork, and
 * giving it a pty to communicate through.
 *
 * Return value: The process id of the child process, or -1 on error.
 * See Also: fork(2)
 */
pid_t
vt_forkpty (struct vt_em *vt, int do_uwtmp_log)
{
  char ttyname[256];

  ttyname[0] = '\0';

  if (zvt_init_subshell(vt, ttyname, do_uwtmp_log) == -1)
    return -1;
  
  if (vt->childpid > 0){
    fcntl(vt->childfd, F_SETFL, O_NONBLOCK);
    
    /* FIXME: approx sizes only */
    zvt_resize_subshell (vt->childfd, vt->width, vt->height, vt->width*8, vt->height*8); 
  }

  d(fprintf(stderr, "program started on pid %d, on tty %s\n", vt->childpid, ttyname));
  return vt->childpid;
}

/**
 * vt_writechild:
 * @vt: An initialised &vt_em, which has had a subprocess started using vt_forkpty().
 * @buffer: Data to write.
 * @len: Length of valid data pointed to by @buffer.
 *
 * Write characters to the child process started using vt_forkpty().
 * Because blocking pipes may truncate writes, additional
 * writes may be required, asynchronously.
 *
 * Return value: The number of characters successfully written.
 * See Also: write(2)
 */
int
vt_writechild(struct vt_em *vt, char *buffer, int len)
{
  return write(vt->keyfd, buffer, len);
}

/**
 * vt_readchild:
 * @vt: An initialised &vt_em, which has had a subprocess started using vt_forkpty().
 * @buffer: Buffer to store data.
 * @len: Maximum number of characters to store in @buffer.
 *
 * Read upto @len characters from the child process into @buffer.
 *
 * Return value: The number of characters read into @buffer, or -1 on error.
 * See Also: read(2).
 */
int
vt_readchild(struct vt_em *vt, char *buffer, int len)
{
  return read(vt->childfd, buffer, len);
}

/**
 * vt_killchild:
 * @vt: An initialised &vt_em, which has had a subprocess started using vt_forkpty().
 * @signal: Signal number to send to child process.
 *
 * Send a signal to the child proccess created using vt_forkpty().
 *
 * Return value: Zero if successful, otherwise returns -1 and
 * sets errno.
 * See Also: kill(2), signal(5).
 */
int
vt_killchild(struct vt_em *vt, int signal)
{
  if (vt->childpid!=-1) {
    return kill(vt->childpid, signal);
  }
  errno = ENOENT;
  return -1;
}

/**
 * vt_closepty:
 * @vt: An initialised &vt_em, which has had a subprocess started using vt_forkpty().
 *
 * Close the child connection pty, and invalidates it.  Closes down all
 * pty associated resources, and waits for the child to quit.
 *
 * Return value: Returns the exit status of the child, or -1 if a failure occured.
 */
int
vt_closepty(struct vt_em *vt)
{
  int ret;

  d(printf("vt_closepty called\n"));

  if (vt->childfd != -1)
    ret = zvt_shutdown_subshell (vt);
  else
    ret = -1;
  return ret;
}

/**
 * vt_destroy:
 * @vt: An initialised &vt_em.
 *
 * Destroy all data associated with a given &vt_em structure.
 *
 * It is safe to call this function if a child process has been created
 * using vt_forkpty().
 *
 * After this function returns, @vt is no longer a valid emulator structure
 * and may only be passed to vt_init() for re-initialisation.
 */
void
vt_destroy(struct vt_em *vt)
{
  struct vt_line *wn;

  d(printf("vt_destroy called\n"));

  vt_closepty(vt);

  /* clear out all scrollback memory */
  vt_scrollback_set(vt, 0);

  /* clear all visible lines */
  while ( (wn = (struct vt_line *)vt_list_remhead(&vt->lines)) ) {
    g_free(wn);
  }

  /* and all alternate lines */
  while ( (wn = (struct vt_line *)vt_list_remhead(&vt->lines_alt)) ) {
    g_free(wn);
  }

  /* and all back lines */
  while ( (wn = (struct vt_line *)vt_list_remhead(&vt->lines_back)) ) {
    g_free(wn);
  }

  /* done */
}


static void
vt_resize_lines(struct vt_line *wn, int width, uint32 default_attr)
{
  int i;
  uint32 c;
  struct vt_line *nn;

  nn = wn->next;  
  while (nn) {
    /* d(printf("wn->line=%d wn->width=%d width=%d\n", wn->line, wn->width, width)); */
    
    /* terminal width grew */
    if (wn->width < width) {
      /* get the attribute of the last charactor for fill */
      if (wn->width > 0)
	c = wn->data[wn->width-1] & VTATTR_MASK;
      else
	c = default_attr;
      
      /* resize the line */
      wn = g_realloc(wn, VT_LINE_SIZE(width));
      
      /* re-link line into linked list */
      wn->next->prev = wn;
      wn->prev->next = wn;
      
      /* if the line got bigger, fix it up */
      for (i = wn->width; i < width; i++) {
	wn->data[i] = c;
	wn->modcount++;
      }
	
      wn->width = width;
    }
    
    /* terminal shrunk */
    if (wn->width > width) {
      /* resize the line */
      wn = g_realloc(wn, VT_LINE_SIZE(width));
      
      /* re-link line into linked list */
      wn->next->prev = wn;
      wn->prev->next = wn;
      
      wn->width = width;
    }
    
    wn = nn;
    nn = nn->next;
  }
}


/**
 * vt_resize:
 * @vt: An initalised &vt_em.
 * @width: New terminal width, in characters.
 * @height: New terminal height, in characters.
 * @pixwidth: New terminal width, in pixels.
 * @pixheight: New terminal height, in pixels.
 *
 * Resize the emulator pages to the new size.  The pty is resized too.
 * The scrollback buffer takes part in the resize, but scrollback lines
 * are NOT resized.
 */
void vt_resize(struct vt_em *vt, int width, int height, int pixwidth, int pixheight)
{
  int i, count;
  uint32 c;
  struct vt_line *wn, *nn;

  vt->width = width;

  /* update scroll bottom constant */
  if (vt->scrollbottom == (vt->height-1)) {
    vt->scrollbottom = height-1;
  }

  /* if we just got smaller, discard unused lines
   * (from top of window - move to scrollback)
   */
  if (height < vt->height) {

    count = vt->height - height;

    d(printf("removing %d lines to smaller window\n", count));

    while (count > 0) {

      /* never move the current line into the scrollback buffer, instead
       * switch to a mode where we just dispose of the lines at the bottom
       * of the terminal
       */
      if (vt->cursory==0) {
	if ( (wn = (struct vt_line *)vt_list_remtail(&vt->lines)) )
	  g_free(wn);
	
	/* and for 'alternate' screen */
	if ( (wn = (struct vt_line *)vt_list_remtail(&vt->lines_alt)) )
	  g_free(wn);
	  
	/* repeat for backbuffer */
	if ( (wn = (struct vt_line *)vt_list_remtail(&vt->lines_back)) )
	  g_free(wn);
      } else {
	if ( (wn = (struct vt_line *)vt_list_remhead(&vt->lines)) ) {
	  if ((vt->mode & VTMODE_ALTSCREEN)==0)
	    vt_scrollback_add(vt, wn);
	  g_free(wn);
	}
	
	/* and for 'alternate' screen */
	if ( (wn = (struct vt_line *)vt_list_remhead(&vt->lines_alt)) ) {
	  if ((vt->mode & VTMODE_ALTSCREEN)!=0)
	    vt_scrollback_add(vt, wn);
	  g_free(wn);
	}
	
	/* repeat for backbuffer */
	if ( (wn = (struct vt_line *)vt_list_remhead(&vt->lines_back)) )
	  g_free(wn);
	
	vt->cursory--;
      }
      count--;
    }
    
    /* reset the line index of now-missing lines to -1 */
    count = vt->height - height;
    if ((vt->mode & VTMODE_ALTSCREEN)==0)
      wn = (struct vt_line *)vt->lines.tailpred;
    else
      wn = (struct vt_line *)vt->lines_alt.tailpred;
    while (count && wn->prev) {
      wn->line = -1;
      wn=wn->prev;
      count--;
    }

    /* fix up cursors on resized window */
    if (vt->cursory >= height) {
      vt->cursory = height - 1;
      d(printf("cursor too big, reset\n"));
    }

    if (vt->scrollbottom >= height) {
      vt->scrollbottom = height - 1;
      d(printf("scroll bottom too big, reset\n"));
    }

    if (vt->scrolltop >= height) {
      vt->scrolltop = height - 1;
      d(printf("scroll top too big, reset\n"));
    }

  } else if (height > vt->height) {

    /* if we just got bigger, try and recover scrollback lines into the top
     * of the buffer, or failing that, add new lines to the bottom 
     */
    count = height - vt->height;
    d(printf("adding %d lines to buffer window\n", count));

    for(i = 0; i < count; i++) {
      /* if scrollback exists, remove it from the buffer and add it to the
       * top of the screen 
       */
      if (vt->scrollbacklines > 0) {
	int len, j;

	d(printf("removing scrollback -> top of screen\n"));

	nn = vt_newline(vt);
	wn = (struct vt_line *)vt_list_remtail(&vt->scrollback);
	len = MIN(nn->width, wn->width);
	memcpy(nn->data, wn->data, len * sizeof(uint32));

	/* clear rest of screen (if it exists) with blanks of the
	 * same attributes as the last character
	 */
	c = nn->data[len-1]&VTATTR_MASK;
	for (j = wn->width ; j<nn->width; j++) {
	  nn->data[j]=c;
	}
	g_free(wn);

	vt_list_addhead(&vt->lines, (struct vt_listnode *)nn);
	vt_list_addhead(&vt->lines_alt, (struct vt_listnode *)vt_newline(vt));
	vt_list_addhead(&vt->lines_back, (struct vt_listnode *)vt_newline(vt));

	vt->scrollbacklines--;	/* since we just nuked one */

	if ((-vt->scrollbackoffset)>vt->scrollbacklines){
	  vt->scrollbackoffset++;
	}

	vt->cursory++;		/* fix up cursor */
      } else {
	d(printf("adding line to bottom of screen\n"));
	/* otherwise just add blank lines to the bottom */
	vt_list_addtail(&vt->lines, (struct vt_listnode *)vt_newline(vt));
	vt_list_addtail(&vt->lines_back, (struct vt_listnode *)vt_newline(vt));
	vt_list_addtail(&vt->lines_alt, (struct vt_listnode *)vt_newline(vt));
      } /* if scrollbacklines */
    }
  } /* otherwise width may have changed? */

  vt->height = height;

  /* fix up cursor on resized window */
  if (vt->cursorx >= width) {
    vt->cursorx = width - 1;
    d(printf("cursor x too wide, reset\n"));
  }

  /* now, scan all lines visible, and make them the right width
   * for all 3 'buffers', onscreen, offscreen and alternate
   */
  vt_resize_lines((struct vt_line *) vt->lines.head, width, vt->attr & VTATTR_CLEARMASK);
  vt_resize_lines((struct vt_line *) vt->lines_back.head, width, vt->attr & VTATTR_CLEARMASK);
  vt_resize_lines((struct vt_line *) vt->lines_alt.head, width, vt->attr & VTATTR_CLEARMASK);

  /* re-fix 'this line' pointer */
  vt->this_line = (struct vt_line *) vt_list_index(&vt->lines, vt->cursory);
  n(vt->this_line);
  zvt_resize_subshell(vt->childfd, width, height, pixwidth, pixheight);

#if 0
  {
    printf("resize line no's\n");
    printf("line\talt\tback\n");
    for (i=0;i<height;i++) {
      printf("%d\t%d\t%d\n",
	     ((struct vt_line *)vt_list_index(&vt->lines, i))->line,
	     ((struct vt_line *)vt_list_index(&vt->lines_alt, i))->line,
	     ((struct vt_line *)vt_list_index(&vt->lines_back, i))->line);
    }
  }
#endif

  d(printf("resized to %d,%d, this = %p\n", vt->width, vt->height, vt->this_line));
}

/**
 * vt_report_button:
 * @vt: An initialised &vt_em.
 * @down: Button is a button press.  Otherwise it is a release.
 * @button: Mouse button, from 1 to 3.
 * @qual: Button event qualifiers.  Bitmask - 1 = shift, 8 = meta, 4 = control.
 * @x: The horizontal character position of the mouse event.
 * @y: The vertical character position of the mouse event.
 *
 * Reports a button click to the terminal, if it has requested
 * them to be sent.  The terminal coordinates begin at 0, starting
 * from the top-left corner of the terminal.
 *
 * Return value: Returns %TRUE if the button event was sent.
 */
int vt_report_button(struct vt_em *vt, int down, int button, int qual, int x, int y)
{
  char mouse_info[16];
  int mode = vt->mode & VTMODE_SEND_MOUSE_MASK;
  static char buttonchar[] = " !\"`abc";

  if (y>=0) {
    mouse_info[0]=0;
    switch (mode) {
    case VTMODE_SEND_MOUSE_PRESS: /* press only */
      if (down)
	g_snprintf(mouse_info, sizeof(mouse_info), "\033[M%c%c%c",
		   buttonchar[(button - 1)&7],
		   x+' '+1,
		   y+' '+1);
      break;
    case VTMODE_SEND_MOUSE_BOTH:
      g_snprintf(mouse_info, sizeof(mouse_info), "\033[M%c%c%c",
		 (down?(buttonchar[(button - 1)&7]):(' '+3)) | /* ' ' = lmb, '!' = mid, '"' = rmb, '#' = up, '`' = wheel-down, 'a' = wheel-up */
		 ((qual&1)?4:0) | /* shift */
		 ((qual&8)?8:0) | /* meta */
		 ((qual&4)?16:0), /* control */
		 x+' '+1,
		 y+' '+1);
      break;
    default:
      mouse_info[0] = 0;
      break;
    }
    if (mouse_info[0]) {
      vt_writechild(vt, mouse_info, strlen(mouse_info));
      return 1;
    }
  }
  return 0;
}
