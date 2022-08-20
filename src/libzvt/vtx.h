/*  vtx.h - Zed's Virtual Terminal
 *  Copyright (C) 1998  Michael Zucchi
 *
 *  Screen definitions and structures
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

#ifndef _VTX_H
#define _VTX_H

#include "lists.h"
#include "vt.h"

/* DO NOT include the toolkit */
/*#include <gtk/gtk.h> */

#include <regex.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* defines for screen update routine */
#define UPDATE_CHANGES 0x00	/* only update changed areas */
#define UPDATE_REFRESH 0x01	/* just refersh all */
#define UPDATE_SCROLLBACK 0x02	/* if in scrollback mode, make sure everything is redrawn */

typedef enum {
  VT_SELTYPE_NONE=0,		/* selection inactive */
  VT_SELTYPE_CHAR,		/* selection by char */
  VT_SELTYPE_WORD,		/* selection by word */
  VT_SELTYPE_LINE,		/* selection by whole line */
  VT_SELTYPE_MAGIC		/* 'magic' or 'active tag' select */
} VT_SELTYPE;

#define VT_SELTYPE_MASK 0xff
#define VT_SELTYPE_BYEND 0x8000
#define VT_SELTYPE_BYSTART 0x4000
#define VT_SELTYPE_MOVED 0x2000 /* has motion occured? */

typedef enum {
  VT_SCROLL_ALWAYS=0,		/* normal display */
  VT_SCROLL_SOMETIMES,		/* with 'fast scroll' background pixmap */
  VT_SCROLL_NEVER		/* with transparency pixmap or fixed pixmap */
} VT_SCROLLTYPE;

/* 'X' extension data for VT terminal emulation */
struct _vtx
{
  struct vt_em vt;

  /* ALL FOLLOWING FIELDS ARE TO BE CONSIDERED PRIVATE - USE AT OWN RISK */

  /* when updating, background colour matches for whole contents of line */
  unsigned int back_match:1;

  /* selection stuff */
  uint32 *selection_data;		/* actual selection */
  int selection_size;

  /* 256 bits of word class characters (assumes a char is 8 bits or more) */
  unsigned char wordclass[32];

  /* true if something selected */
  int selected;

  /* if selection active, what type? (by char/word/line) */
  VT_SELTYPE selectiontype;

  int selstartx, selstarty;
  int selendx, selendy;

  /* previously rendered values */
  int selstartxold, selstartyold;
  int selendxold, selendyold;

  /* rendering callbacks */
  void (*draw_text)(void *user_data, struct vt_line *line, int row, int col, int len, int attr);
  void (*scroll_area)(void *user_data, int firstrow, int count, int offset, int fill);
  /* set the cursor on/off, return old state */
  int  (*cursor_state)(void *user_data, int state);
  void (*selection_changed ) (void *user_data);

  /* added in gnome-libs 1.0.10
     ... this shouldn't break bin compatibility? */
  struct vt_list magic_list;	/* a list of magic select structs */
  struct vt_match *matches;	/* blocks on the current screen */
  int magic_matched;		/* have we tried to match? */
  struct vt_match *match_shown; /* currently highlighted match */

  unsigned char scroll_type;	/* how we scroll (see VT_SCROLLTYPE enum) */

};

  /* an application-defined match function, storage */
struct vt_magic_match {
  struct vt_magic_match *next;
  struct vt_magic_match *prev;

  char *regex;			/* actual regex string */
  regex_t preg;			/* compiled regex string, for speed */
  uint32 highlight_mask;	/* masked used to highlight this match visually */
  void *user_data;		/* user data for this match */
};

  /* a match */
struct vt_match {
  struct vt_match *next;	/* in single-linked list of matches */
  struct vt_magic_match *match;	/* which match it matches */
  char *matchstr;		/* which string it matches */
  struct vt_match_block *blocks; /* the actual blocks in this match */
};

  /* an actual block of text which matches */
struct vt_match_block {
  struct vt_match_block *next;	/* in single-linked list of blocks */
  struct vt_line *line;		/* line of this block */
  struct vt_line *saveline;	/* saved, when highlighting using colour */
  unsigned int lineno;		/* line number of this block */
  unsigned int start;		/* start/end of block, in characters */
  unsigned int end;
};

/* from update.c */
char *vt_get_selection   (struct _vtx *vx, int size, int *len);
void vt_clear_selection  (struct _vtx *vx);
void vt_fix_selection    (struct _vtx *vx);
void vt_draw_selection   (struct _vtx *vx);
void vt_update_rect      (struct _vtx *vx, int fill, int sx, int sy, int ex, int ey);
void vt_update           (struct _vtx *vt, int state);
void vt_draw_cursor      (struct _vtx *vx, int state);
void vt_set_wordclass    (struct _vtx *vx, unsigned char *s);
int  vt_get_attr_at      (struct _vtx *vx, int col, int row);

  /* the match routines */
void vt_free_match_blocks(struct _vtx *vx);
void vt_getmatches	 (struct _vtx *vx);
void vt_match_clear	 (struct _vtx *vx, char *regex);
struct vt_match *vt_match_check(struct _vtx *vx, int x, int y);
void vt_match_highlight	 (struct _vtx *vx, struct vt_match *m);
			 
struct _vtx *vtx_new     (int width, int height, void *user_data);
void vtx_destroy         (struct _vtx *vx);

#ifdef __cplusplus
	   }
#endif /* __cplusplus */


#endif

