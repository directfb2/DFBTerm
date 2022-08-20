/*  vt.h - Zed's Virtual Terminal
 *  Copyright (C) 1998  Michael Zucchi
 *
 *  Virtual terminal emulation definitions and structures
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

#ifndef _ZVT_VT_H_
#define _ZVT_VT_H_

#include <unistd.h>
#include <sys/types.h>

#include "lists.h"

/* for utf-8 input support */
#define ZVT_UTF 1

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* defines for screen update routine */
#define UPDATE_CHANGES    0x00	/* only update changed areas */
#define UPDATE_REFRESH    0x01	/* just refersh all */
#define UPDATE_SCROLLBACK 0x02	/* if in scrollback mode, make sure everything is redrawn */

/* 32 bit unsigned int */
typedef unsigned int uint32;

/* defines for VT argument processing, also used for textual arguments */
#define VTPARAM_MAXARGS   5	/* maximum number of arguments */
#define VTPARAM_ARGMAX   20	/* number of characters in each arg maximum */
#define VTPARAM_INTARGS  20	/* maximum args for integers, should be less than MAXARGS*ARGMAX bytes! */

struct vt_line {
  struct vt_line *next;		/* next 'vt' line */
  struct vt_line *prev;		/* prev 'vt' line */
  int line;			/* the line number for this line */
  int width;			/* width of this line */
  int modcount;			/* how many modifications since last update */
  uint32 data[1];		/* the line data follows this structure */
};

/* macro for computing the size of vt_line structures */
#define VT_LINE_SIZE(width) (sizeof(struct vt_line) + (sizeof(uint32) * (width)))

/* type of title to set with callback */
typedef enum {
  VTTITLE_WINDOWICON=0,		/* set both window title and icon name */
  VTTITLE_ICON,			/* set icon name */
  VTTITLE_WINDOW,		/* set window title */
  VTTITLE_XPROPERTY		/* set X property */
} VTTITLE_TYPE;

/* note: bit 0x80000000 is free for another attribute */
#define VTATTR_BOLD       0x40000000
#define VTATTR_UNDERLINE  0x20000000
#define VTATTR_BLINK      0x10000000
#define VTATTR_REVERSE    0x08000000
#define VTATTR_CONCEALED  0x04000000

/* all attributes mask, and no-attributes mask */
#define VTATTR_MASK	  0xffff0000
#define VTATTR_DATAMASK	  (~VTATTR_MASK)
#define VTATTR_CLEARMASK  (~(VTATTR_BOLD|VTATTR_UNDERLINE|VTATTR_BLINK|VTATTR_REVERSE))

/* bitmasks for colour map information */
#define VTATTR_FORECOLOURM 0x03e00000
#define VTATTR_BACKCOLOURM 0x001f0000
#define VTATTR_FORECOLOURB 21
#define VTATTR_BACKCOLOURB 16

/* 'clear' character and attributes of default clear character */
#define VTATTR_CLEAR (16<<VTATTR_FORECOLOURB)|(17<<VTATTR_BACKCOLOURB)|0x0000

struct vt_em {
  int cursorx, cursory;		/* cursor position in characters */
  int width, height;		/* width/height in characters */
  int scrolltop;		/* line from which scrolling occurs */
  int scrollbottom;		/* line after which scrolling occurs */

  pid_t childpid;		/* child process id */
  int childfd;			/* child pty file descriptor (read/write) */
  int keyfd;  			/* keystrokes get written here (normally the
				   same as childfd */
  void *pty_tag;		/* Tag used to talk to the gnome-pty-helper */
  int msgfd;			/* "it's dead" messages come through here */

  int savex,savey;		/* saved cursor position */
  uint32 savemode,saveattr;	/* saved mode too */
  unsigned char *saveremaptable;
  struct vt_line *savethis;

  int cx, cy;			/* cursor position in pixels */
  int sx, sy;			/* width and height pixels */

  unsigned char *remaptable;	/* chracter remapping table. 0 = dont remap */

  int Gx;			/* current character set mapping */
  unsigned char *G[4];		/* Gx character set mappings */

  uint32 attr;			/* current char attributes.  This
				 is a bitfield.  bottom 16 bits = ' ' */

  uint32 mode;			/* vt modes.  see below */

  /* most of this snot isn't needed anymore, its here for backward compatability */
  /* remove the _dummy values at next major release number */
  union {
    struct {
      unsigned char *args_dummy[VTPARAM_MAXARGS]; /* run-time emulation arguments */
      char args_mem[VTPARAM_MAXARGS*VTPARAM_ARGMAX];
      unsigned char **argptr_dummy;
      char *outptr;
      char *outend_dummy;
    } txt;
    struct {
      unsigned int intargs[VTPARAM_INTARGS];
      unsigned int intarg;
    } num;
  } arg;

  int argcnt;

  int state;			/* current parse state */

  struct vt_line *this_line;	/* the current line */

  struct vt_list lines;		/* double linked list of lines */
  struct vt_list lines_back;	/* 'last rendered' buffer.  used to optimise updates */
  struct vt_list lines_alt;	/* alternate screen */

  /* scroll back stuff */
  struct vt_list scrollback;	/* double linked list of scrollback lines */
  int scrollbacklines;		/* total scroll back lines */
  int scrollbackoffset;		/* viewing offset */
  int scrollbackold;		/* old scrollback offset */
  int scrollbackmax;		/* maximum scrollbacklines, after this total is reached,
				   old lines are discarded */
  void (*ring_my_bell)(void *user_data);	/* ring my bell ... */
  void (*change_my_name)(void *user_data, char *name, VTTITLE_TYPE type);	/* ring my bell ... */

  void *user_data;		/* opaque external data handle for callbacks */

#ifdef ZVT_UTF
  union {
    struct {
      uint32 wchar;		/* current wide char, for some states */
      int shiftchar;		/* the first char of a mb char, shifted, scanning for input bits */
      int shift;		/* the amount to shift 'shiftchar' to get valid data/map to wchar */
    } utf8;
  } decode;
  int coding;			/* data encoding method */
#endif
};

#ifdef ZVT_UTF
  /* possible supported encoding methods */
#define ZVT_CODE_ISOLATIN1 0
#define ZVT_CODE_UTF8 1
#endif

#define VTMODE_INSERT 0x00000001 /* insert mode active */
#define VTMODE_SEND_MOUSE 0x02	/* compatability flag, so apps can check for send-mouse mode,
				   new apps should check VTMODE_SEND_MOUSE_MASK */
#define VTMODE_WRAPOFF 0x04	/* wrap screenmode? (default = on) */
#define VTMODE_APP_CURSOR 0x00000008 /* application cursor keys */
#define VTMODE_RELATIVE 0x10	/* relative origin mode */
#define VTMODE_APP_KEYPAD 0x20	/* application keypad on */
#define VTMODE_SEND_MOUSE_PRESS 0x42 /* send mouse press, include bit 1 for compatability */
#define VTMODE_SEND_MOUSE_BOTH 0x82 /* send mouse press & release */
#define VTMODE_SEND_MOUSE_MASK 0xc2 /* mask of options for mouse reports, bit 1 is used for compatability */
#define VTMODE_BLANK_CURSOR 0x100 /* cursor is blanked */
#define VTMODE_ALTSCREEN 0x80000000 /* on alternate screen? */

/* some useful macro's for working with the line contents */
#define VT_BLANK(n) ((n)==0 || (n)==9 || (n)==32)
#define VT_BMASK(n) ((n) & (VTATTR_FORECOLOURM|VTATTR_BACKCOLOURM|VTATTR_REVERSE|VTATTR_UNDERLINE))
#define VT_ASCII(n) ((((n)&VTATTR_DATAMASK)==0 || ((n)&VTATTR_DATAMASK)==9)?32:((n)&VTATTR_DATAMASK))
#define VT_THRESHHOLD (4)

struct vt_em *vt_init           (struct vt_em *vt, int width, int height);
void          vt_destroy        (struct vt_em *vt);
void          vt_resize         (struct vt_em *vt, int width, int height,
			         int pixwidth, int pixheight);
void          vt_parse_vt       (struct vt_em *vt, char *ptr, int length);
void          vt_swap_buffers   (struct vt_em *vt);
pid_t  	      vt_forkpty        (struct vt_em *vt, int do_uwtmp_log);
int   	      vt_readchild      (struct vt_em *vt, char *buffer, int len);
int   	      vt_writechild     (struct vt_em *vt, char *buffer, int len);
int   	      vt_report_button  (struct vt_em *vt, int down, int button, int qual,
			         int x, int y);
void  	      vt_scrollback_set (struct vt_em *vt, int lines);
int   	      vt_killchild      (struct vt_em *vt, int signal);
int   	      vt_closepty       (struct vt_em *vt);
void	      vt_reset_terminal (struct vt_em *vt, int hard);

#ifdef __cplusplus
	   }
#endif /* __cplusplus */


#endif /* _ZVT_VT_H_ */
