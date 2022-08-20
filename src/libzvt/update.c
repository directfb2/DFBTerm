/*  update.c - Zed's Virtual Terminal
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
  This module handles the update of the 'on-screen' virtual terminal
  from the 'off-screen' buffer.

  It also handles selections, and making them visible, etc.
*/

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <glib.h>

#include <libzvt/lists.h>
#include <libzvt/vt.h>
#include <libzvt/vtx.h>


#define d(x)
/* 'update' line debug */
#define u(x)

/*
  update line 'line' (node 'l') of the vt

  always==1  assumes all line data is stale
  start/end columns to update
*/
static void vt_line_update(struct _vtx *vx, struct vt_line *l, struct vt_line *bl, int line, int always,
			   int start, int end)
{
  int i;
  int run, commonrun;
  int runstart;
  uint32 attr, newattr, oldattr, oldchar, newchar, lastchar;
  /*  struct vt_line *bl;*/
  int sx, ex;			/* start/end selection */
  int force;

  d(printf("updating line %d: ", line));
  d(fwrite(l->data, l->width, 1, stdout));
  d(printf("\n"));

  u(printf("updating line from (%d-%d) ->", start, end));

  /* some sanity checks */
  g_return_if_fail (bl != NULL);
  g_return_if_fail (bl->next != NULL);

  /* work out if selections are being rendered */
  if (vx->selected &&
      (((line >= (vx->selstarty - vx->vt.scrollbackoffset)) && /* normal order select */
      (line <= (vx->selendy - vx->vt.scrollbackoffset))) ||
      ((line <= (vx->selstarty - vx->vt.scrollbackoffset)) && /* start<end */
      (line >= (vx->selendy - vx->vt.scrollbackoffset)))) ) {
    
    /* work out range of selections */
    sx = 0;
    ex = l->width;
    
    if (vx->selstarty<=vx->selendy) {
      if (line == (vx->selstarty-vx->vt.scrollbackoffset))
	sx = vx->selstartx;
      if (line == (vx->selendy-vx->vt.scrollbackoffset))
	ex = vx->selendx;
    } else {
      if (line == (vx->selendy-vx->vt.scrollbackoffset))
	sx = vx->selendx;
      if (line == (vx->selstarty-vx->vt.scrollbackoffset)) {
	ex = vx->selstartx;
      }
    }

    /* check startx<endx, if on the same line, swap if so */
    if ( (sx>ex) &&
	 (line == vx->selstarty-vx->vt.scrollbackoffset) &&
	 (line == vx->selendy-vx->vt.scrollbackoffset) ) {
      int tmp;
      tmp=sx; sx=ex; ex=tmp;
    }
  } else {
    sx = -1;
    ex = -1;
  }
  /* ^^ at this point sx -- ex needs to be inverted (it is selected) */

  /* scan line, checking back vs front, if there are differences, then
     look to render */

  run = 0;
  attr = 0;
  runstart = 0;
  commonrun = 0;
  lastchar = 0;

  /* start out optimistic */
  vx->back_match = 1;

  force = always;

  /* actual size to use is bl->width */
  if (end>bl->width)
    end=bl->width;
  if (start>bl->width)
    start=bl->width;

  for (i=start;i<end;i++) {
    oldchar = bl->data[i];

    /* handle smaller line size, from scrollback */
    if (i<l->width)
      newchar = l->data[i];
    else
      newchar = lastchar;

    /* check for selected block */
    if (i >= sx && i < ex) {
      newchar ^= VTATTR_REVERSE;
    }

    /* used in both if cases */
    newattr = newchar & VTATTR_MASK;

    /* if there are no changes, quit right here ... */
    if (oldchar != newchar || force) {
      bl->data[i] = newchar;
      oldattr = oldchar & VTATTR_MASK;
      if (run) {
	if (newattr == attr) {
	  if (vx->back_match) {
	    if (!VT_BLANK(oldchar & 0xffff) || (VT_BMASK(newattr) != VT_BMASK(oldattr)))
	      vx->back_match = 0;
	  }
#ifdef VT_THRESHHOLD
	  if (commonrun) {
	    run += commonrun;
	    commonrun=0;
	  }
#endif
	  run++;
	} else {
	  /* render 'run so far' */
	  vx->draw_text(vx->vt.user_data, bl,
			line, runstart, run, attr);
	  vx->back_match = always?0:
	    ((VT_BMASK(newattr) == VT_BMASK(oldattr))
	     && VT_BLANK(oldchar&0xffff)
	     && (newattr&VTATTR_REVERSE)==0);
	  run = 1;
	  runstart = i;
	  attr = newattr;
	  commonrun = 0;
	}
      } else {
	vx->back_match = always?0:
	  ((VT_BMASK(newattr) == VT_BMASK(oldattr))
	   && VT_BLANK(oldchar&0xffff)
	   && (newattr&VTATTR_REVERSE)==0);
	runstart = i;
	attr = newattr;
	run=1;
	commonrun = 0;
      }
    } else {
      if (run) {
#ifdef VT_THRESHHOLD
	/* check for runs of common characters, if they are short, then
	   use them */
	if (commonrun>VT_THRESHHOLD || (newattr!=attr)) {
	  vx->draw_text(vx->vt.user_data, bl,
			line, runstart, run, attr);
	  run=0;
	  commonrun=0;
	} else {
	  commonrun++;
	}
#else
	vx->draw_text(vx->vt.user_data, bl,
		      line, runstart, run, attr);
	run=0;
#endif
      }
    }
    lastchar = newchar & VTATTR_MASK;
  }

  if (run) {
    vx->draw_text(vx->vt.user_data, bl,
		  line, runstart, run, attr);
  }

  l->modcount = 0;
  bl->line = line;
  l->line = line;
}


int vt_get_attr_at (struct _vtx *vx, int col, int row)
{
  struct vt_line *line;
  
  line = (struct vt_line *) vt_list_index (&vx->vt.lines_back, row);
  return line->data [col];
}

/*
  scroll/update a section of lines
  from firstline (fn), scroll count lines 'offset' lines
*/
static int vt_scroll_update(struct _vtx *vx, struct vt_line *fn, int firstline, int count, int offset, int scrolled)
{
  struct vt_line *tn, *bn, *dn, *nn, *bl;	/* top/bottom of scroll area */
  int i, fill;
  int force;
  int top,bottom;
  int index;
  int end;

  /* if we are not scrolling, then the background doesn't need rendering! */
  if (vx->scroll_type == VT_SCROLL_SOMETIMES)
    force=1;
  else
    force=0;

  d(printf("scrolling %d lines from %d, by %d\n", count, firstline, offset));

  d({
    struct vt_line *wn;
    printf("before:\n");
    wn = &vx->vt.lines_back.head;
    while(wn) {
      printf("node %p:  n: %p  p:%p -> %d\n", wn, wn->next, wn->prev, wn->line);
      wn = wn->next;
    }
  });

  /* if we can optimise scrolling - do so */
  if (vx->scroll_type == VT_SCROLL_ALWAYS
      || (vx->scroll_type == VT_SCROLL_SOMETIMES
	  && count>(vx->vt.height/2))) {

    /* find start/end of scroll area */
    if (offset > 0) {
      /* grab n lines at bottom of scroll area, and move them to above it */
      tn = (struct vt_line *)vt_list_index(&vx->vt.lines_back, firstline);
      nn = (struct vt_line *)vt_list_index(&vx->vt.lines, firstline);
      bn = (struct vt_line *)vt_list_index(&vx->vt.lines_back, firstline+offset-1);
      dn = (struct vt_line *)vt_list_index(&vx->vt.lines_back, firstline+count+offset);
    } else {
      /* grab n lines at top of scroll area, and move them to below it */
      tn = (struct vt_line *)vt_list_index(&vx->vt.lines_back, firstline+count+offset);
      nn = (struct vt_line *)vt_list_index(&vx->vt.lines, firstline+count+offset);
      bn = (struct vt_line *)vt_list_index(&vx->vt.lines_back, firstline+count-1);
      dn = (struct vt_line *)vt_list_index(&vx->vt.lines_back, firstline+offset);
    }
    
    if (!tn || !nn || !bn || !dn) {
      g_error("vt_scroll_update tn=%p nn=%p bn=%p dn=%p\n", tn, nn, bn, dn);
    }

    /*    
	  0->dn->4->tn->1->2->bn->5
	  0->dn->4->5
	  0->tn->1->2->bn->dn->4->5
    */
    
    /* remove the scroll segment */
    tn->prev->next = bn->next;
    bn->next->prev = tn->prev;
    
    /* insert it again at the destination */
    tn->prev = dn->prev;
    dn->prev->next = tn;
    bn->next = dn;
    dn->prev = bn;
    
    d({
      struct vt_line *wn;
      int line;
      printf("After\n");
      wn = &vx->vt.lines_back.head;
      line=0;
      while(wn && line<vx->vt.height) {
	printf("node %p:  n: %p  p:%p -> %d\n", wn, wn->next, wn->prev, wn->line);
	wn = wn->next;
	line++;
      }
    });

    /* this 'clears' the rendered data, to properly reflect what just happened.
       SPEEDUP: I suppose we could just leave it as is if we dont have a pixmap ... */
    fill = nn->data[0] & VTATTR_MASK;
    do {
      d(printf("clearning line %d\n", tn->line));
      for (i = 0; i < tn->width; i++) {
	tn->data[i] = fill;
      }
    } while ((tn!=bn) && (tn=tn->next));
    
    /* find out what colour the new lines is - make it match (use
     * first character as a guess), and perform the visual scroll 
     */
    d(printf("firstline = %d, count = %d, offset = %d\n", firstline, count, offset));
    
    /* idea:
       if scrolling 'most' of the screen, always scroll.
       then fix up the other areas by redrawing them, so the background repairs.
    */
    
    d(printf("scrolling ...\n"));
    fill = (nn->data[0] & VTATTR_BACKCOLOURM) >> VTATTR_BACKCOLOURB;
    vx->scroll_area(vx->vt.user_data, firstline, count, offset, fill);
    /* force update of every other line */

    /* get the top line */
    index = vx->vt.scrollbackoffset;
    if (index<0) {
      nn = (struct vt_line *)vt_list_index(&vx->vt.scrollback, index);
      if (!nn) {
	/* check for error condition */
	printf("LINE UNDERFLOW!\n");
	nn = (struct vt_line *)vx->vt.scrollback.head;
      }
    } else {
      nn = (struct vt_line *)vx->vt.lines.head;
    }
    bl = (struct vt_line *)vx->vt.lines_back.head;

    top=firstline;bottom=firstline+count-1;
    if (vx->scroll_type == VT_SCROLL_SOMETIMES)
	    end = vx->vt.height;
    else
	    end = (firstline+count+offset);

    d(printf("fixing up [force=%d] from %d-%d\n", force, top, bottom));
    /*for (i=0;nn->next && i<vx->vt.height;i++) {*/
    for (i=0;nn->next && i<end;i++) {
      d(printf("looking at line %d [%p] ", i, nn));
      if (i<top || i>bottom) {
	d(printf("forcing update of %d [force=%d]\n", i, force));
	vt_line_update(vx, nn, bl, i, force, 0, bl->width);
      } else {
	d(printf(" leaving\n"));
	/* umm, force this line not updated */
	nn->line = i;
      }
      nn->line = i;
      d(printf("%p: line %d, was %d\n", nn, i, nn->line));

      if (nn==(struct vt_line *)vx->vt.scrollback.tailpred) {
	d(printf("++ skipping to real lines at line %d\n", nn->line));
	nn = (struct vt_line *)vx->vt.lines.head;
      } else {
	d(printf("++ going to next line ...\n"));
	nn = nn->next;
      }
      bl = bl->next;
    }
    scrolled=1;
  } else {
    nn = fn;
    /* otherwise, scroll the area by redrawing what we have ... */

    /* we only have to force if we've scrolled this time */
    if (!scrolled && force)
      force=0;

    d(printf("fake scrolling ... [force=%d]\n", force));
    if (offset>0) {
      bl = (struct vt_line *)vt_list_index(&vx->vt.lines_back, firstline);

      for (i=firstline;nn->next && i<(firstline+count+offset);i++) {
	d(printf("updating line %d\n", i));
	vt_line_update(vx, nn, bl, i, force, 0, bl->width);

	if (nn==(struct vt_line *)vx->vt.scrollback.tailpred) {
	  d(printf("++ skipping to real lines at line %d\n", nn->line));
	  nn = (struct vt_line *)vx->vt.lines.head;
	} else {
	  d(printf("++ going to next line ...\n"));
	  nn = nn->next;
	}
	bl = bl->next;
      }
    } else {
      index = vx->vt.scrollbackoffset + offset + firstline;

      if (index<0) {
	nn = (struct vt_line *)vt_list_index(&vx->vt.scrollback, index);
	if (!nn) {
	  /* check for error condition */
	  printf("LINE UNDERFLOW!\n");
	  nn = (struct vt_line *)vx->vt.scrollback.head;
	}
      } else {
	nn = (struct vt_line *)vt_list_index(&vx->vt.lines, index);
      }
      bl = (struct vt_line *)vt_list_index(&vx->vt.lines_back, offset+firstline);

      d(printf("updating %d to %d\n", firstline+offset, firstline+count));
      
      d(printf("negative offset - ooops\n"));
      for (i=firstline+offset;nn->next && i<firstline+count;i++) {
	d(printf("updating line %d\n", i));
	vt_line_update(vx, nn, bl, i, force, 0, bl->width);
	if (nn==(struct vt_line *)vx->vt.scrollback.tailpred) {
	  d(printf("++ skipping to real lines at line %d\n", nn->line));
	  nn = (struct vt_line *)vx->vt.lines.head;
	} else {
	  d(printf("++ going to next line ...\n"));
	  nn = nn->next;
	}
	bl = bl->next;
      }
    }
  }
  return scrolled;
}

/*
  do an optimised update of the screen
  performed in 3 passes -
    first a scroll update pass, for down scrolling
    then a scroll update pass, for up scrolling
  finally a character update pass

  if state==0, perform optimised update
  otherwise perform refresh update (update whole screen)
*/
void vt_update(struct _vtx *vx, int update_state)
{
  int line=0;
  int offset;
  int oldoffset=0;
  struct vt_line *wn, *nn, *fn, *bl;
  int firstline;
  int old_state;
  int update_start=-1;	/* where to start/stop update */
  int update_end=-1;
  int scrolled=0, force;

  d(printf("updating screen\n"));

  wn = NULL;
  nn = NULL;
  fn = NULL;

  old_state = vx->cursor_state(vx->vt.user_data, 0);
  /* turn off any highlighted matches */
  vt_match_highlight(vx, 0);

  /* find first line of visible screen, take into account scrollback */
  offset = vx->vt.scrollbackoffset;
  if (offset<0) {
    wn = (struct vt_line *)vt_list_index(&vx->vt.scrollback, offset);
    if (!wn) {
      /* check for error condition */
      printf("LINE UNDERFLOW!\n");
      wn = (struct vt_line *)vx->vt.scrollback.head;
    }
  } else {
    wn = (struct vt_line *)vx->vt.lines.head;
  }
  
  d({
    struct vt_line *dn;
    printf("INPUT before:\n");
    dn = &vx->vt.lines;
    while(dn) {
      printf("  node %p:  n: %p  p:%p -> %d\n", dn, dn->next, dn->prev, dn->line);
      dn = dn->next;
    }

    dn = wn;
    while(dn) {
      printf("  node %p:  n: %p  p:%p -> %d\n", dn, dn->next, dn->prev, dn->line);
      dn = dn->next;
    }

  });

  /* updated scrollback if we can ... otherwise dont */
  if (!(update_state&UPDATE_REFRESH)) {

    /* if scrollback happening, then force update of newly exposed areas - calculate here */
    offset = vx->vt.scrollbackoffset-vx->vt.scrollbackold;
    if (offset>0) {
      d(printf("scrolling up, by %d\n", offset));
      update_start = vx->vt.height-offset-1;
      update_end = vx->vt.height;
    } else {
      d(printf("scrolling down, by %d\n", -offset));
      update_start = -1;
      update_end = -offset;
    }
    d(printf("forced updated from %d - %d\n", update_start, update_end));
    
    nn = wn->next;
    firstline = 0;		/* this isn't really necessary (quietens compiler) */
    fn = wn;
    while (nn && line<vx->vt.height) {
      int oldline;

      /* enforce full update for 'scrollback' areas */
      if (line>update_start && line<update_end) {
	d(printf("forcing scroll update refresh (line %d)\n", line));
	oldline = -1;
      } else {
	oldline = wn->line;
      }

      d(printf("%p: scanning line %d, offset=%d : line = %d\n ", wn, line, (wn->line-line), wn->line));
      d(printf("(%d - %d)\n", line, wn->line));
      if ((oldline!=-1) && ((offset = oldline-line) >0)) {
	wn->line = line;
	if (offset == oldoffset) {
	  /* accumulate "update" lines */
	} else {
	  if (oldoffset != 0) {
	    d(printf("scrolling (1.1)\n"));
	    scrolled = vt_scroll_update(vx, fn, firstline, line-firstline, oldoffset, scrolled); /* scroll/update a line */
	  }
	  d(printf("found a scrolled line\n"));
	  firstline = line;	/* first line to scroll */
	  fn = wn;
	}
      } else {
	if (oldoffset != 0) {
	  d(printf("scrolling (1.2)\n"));
	  scrolled = vt_scroll_update(vx, fn, firstline, line-firstline, oldoffset, scrolled); /* scroll/update a line */
	}
	offset=0;
      }

      /* goto next logical line */
      if (wn==(struct vt_line *)vx->vt.scrollback.tailpred) {
	d(printf("skipping to real lines at line %d\n", line));
	wn = (struct vt_line *)vx->vt.lines.head;
      } else {
	d(printf("going to next line ...\n"));
	wn = nn;
      }

      oldoffset = offset;
      line ++;
      nn = wn->next;
    }
    if (oldoffset != 0) {
      d(printf("scrolling (1.3)\n"));
      d(printf("oldoffset = %d, must scroll\n", oldoffset));
      scrolled = vt_scroll_update(vx, fn, firstline, line-firstline, oldoffset, scrolled); /* scroll/update a line */
    }

    /* now scan backwards ! */
    d(printf("scanning backwards now\n"));
    
      /* does this need checking for overflow? */
    if (wn == (struct vt_line *)vx->vt.lines.head) {
      wn = (struct vt_line *)vx->vt.scrollback.tailpred;
      nn = wn->prev;
    } else {
      wn = wn->prev;
      nn = wn->prev;
    }

    line = vx->vt.height;
    oldoffset = 0;
    while (nn && line) {
      int oldline;

      line--;

      /* enforce full update for 'scrollback' areas */
      if (line>update_start && line<update_end) {
	d(printf("forcing scroll update refresh (line %d)\n", line));
	oldline = -1;
      } else {
	oldline = wn->line;
      }

      d(printf("%p: scanning line %d, offset=%d : line = %d, oldoffset=%d\n ", wn, line, (wn->line-line), wn->line, oldoffset));
      if ((oldline !=-1) && ((offset = oldline-line) < 0)) {
	wn->line = line;
	if (offset == oldoffset) {
	  /* accumulate "update" lines */
	} else {
	  if (oldoffset != 0) {
	    d(printf("scrolling (2.1)\n"));
	    scrolled = vt_scroll_update(vx, fn, line, firstline-line+1, oldoffset, scrolled); /* scroll/update a line */
	  }
	  d(printf("found a scrolled line\n"));
	  firstline = line;	/* first line to scroll */
	  fn = wn;
	}
      } else {
	if (oldoffset != 0) {
	  d(printf("scrolling (2.2)\n"));
	  scrolled = vt_scroll_update(vx, fn, line+1, firstline-line, oldoffset, scrolled); /* scroll/update a line */
	}
	offset=0;
      }

      /* goto previous logical line */
      if (wn==(struct vt_line *)vx->vt.lines.head) {
	wn = (struct vt_line *)vx->vt.scrollback.tailpred;
	d(printf("going to scrollback buffer line ...\n"));
      } else {
	d(printf("going to prev line ...\n"));
	wn = nn;
      }

      nn = wn->prev;
      oldoffset = offset;
      d(printf("nn = %p\n", nn));
    }
    if (oldoffset != 0) {
      d(printf("scrolling (2.3)\n"));
      d(printf("oldoffset = %d, must scroll 2\n", oldoffset));
      scrolled = vt_scroll_update(vx, fn, 0, firstline-line+1, oldoffset, scrolled); /* scroll/update a line */
    }

    /* have to align the pointer properly for the last pass */
    if (wn==(struct vt_line *)vx->vt.scrollback.tailpred) {
      d(printf("++ skipping to real lines at line %d\n", line));
      wn = (struct vt_line *)vx->vt.lines.head;
    } else {
      d(printf("++ going to next line ...\n"));
      wn = wn->next;
    }
  }

  /* now, re-scan, since all lines should be at the right position now,
     and update as required */
  d(printf("scanning from top again\n"));

  /* see if we have to force an update */
  if (scrolled && vx->scroll_type == VT_SCROLL_SOMETIMES)
    force = 1;
  else
    force = 0;

  nn = wn->next;
  firstline = 0;		/* this isn't really necessary */
  fn = wn;
  line=0;
  bl = (struct vt_line *)vx->vt.lines_back.head;
  offset = vx->vt.scrollbackoffset;
  while (nn && line<vx->vt.height) {
    d(printf("%p: scanning line %d, was %d\n", wn, line, wn->line));
    if (wn->line==-1) {
      vt_line_update(vx, wn, bl, line, 0, 0, bl->width);
      d(printf("manual: updating line %d\n", line));
    } else if (wn->modcount || update_state) {
      vt_line_update(vx, wn, bl, line, force, 0, bl->width);
      d(printf("manual, forced: updating line %d\n", line));
    }
    wn->line = line;		/* make sure line is reset */
    line++;

    /* goto next logical line */
    if (wn==(struct vt_line *)vx->vt.scrollback.tailpred) {
      d(printf("skipping to real lines at line %d\n", line));
      wn = (struct vt_line *)vx->vt.lines.head;
    } else {
      wn = nn;
    }
    
    nn = wn->next;
    bl = bl->next;
    d(printf("  -- wn = %p, wn->next = %p\n", wn, wn->next));
  }

  vx->vt.scrollbackold = vx->vt.scrollbackoffset;

  /* some debug */
#if 0
  {
    struct vt_line *wb, *nb;
    int i;

    wb = (struct vt_line *)vx->vt.lines_back.head;
    nb = wb->next;
    printf("on-screen buffer contains:\n");
    while (nb) {
      printf("%d: ", wb->line);
      for (i=0;i<wb->width;i++) {
	(printf("%c", (wb->data[i]&VTATTR_DATAMASK))); /*>=32?(wb->data[i]&0xffff):' '));*/
      }
      (printf("\n"));
      wb=nb;
      nb=nb->next;
    }
  }
#endif

  vx->cursor_state(vx->vt.user_data, old_state);
}

/*
  updates only a rectangle of the screen

  pass in rectangle of screen to draw (in pixel coordinates)
   - assumes screen order is up-to-date.
   fill is the 'colour' the rectangle is currently.  use a fill colour of -1
   to indicate that the contents of the screen is unknown.
*/
void vt_update_rect(struct _vtx *vx, int fill, int csx, int csy, int cex, int cey)
{
  struct vt_line *wn, *nn;
  int old_state;
  int i;
  struct vt_line *bl;
  uint32 fillin;

  old_state = vx->cursor_state(vx->vt.user_data, 0);	/* ensure cursor is really off */

  d(printf("updating (%d,%d) - (%d,%d)\n", csx, csy, cex, cey));

  /* bounds check */
  if (cex>vx->vt.width)
    cex = vx->vt.width;
  if (csx>vx->vt.width)
    csx = vx->vt.width;

  if (cey>=vx->vt.height)
    cey = vx->vt.height-1;
  if (csy>=vx->vt.height)
    csy = vx->vt.height-1;

  /* check scrollback for current line */
  if ((vx->vt.scrollbackoffset+csy)<0) {
    wn = (struct vt_line *)vt_list_index(&vx->vt.scrollback, vx->vt.scrollbackoffset+csy);
  } else {
    wn = (struct vt_line *)vt_list_index(&vx->vt.lines, vx->vt.scrollbackoffset+csy);
  }

  bl = (struct vt_line *)vt_list_index(&vx->vt.lines_back, csy);
  fillin = (fill<<VTATTR_BACKCOLOURB)&VTATTR_BACKCOLOURM;

  if (wn) {
    nn = wn->next;
    while ((csy<=cey) && nn) {
      d(printf("updating line %d\n", csy));

      /* make the back buffer match the screen state */
      for (i=csx;i<cex && i<bl->width;i++) {
	bl->data[i] = fillin;
      }

      vt_line_update(vx, wn, bl, csy, 0, csx, cex);
      csy++;

      /* skip out of scrollback buffer if need be */
      if (wn==(struct vt_line *)vx->vt.scrollback.tailpred) {
	wn = (struct vt_line *)vx->vt.lines.head;
      } else {
	wn = nn;
      }

      nn = wn->next;
      bl = bl->next;
    }
  }

  vx->cursor_state(vx->vt.user_data, old_state);
}

/*
  returns true if 'c' is in a 'wordclass' (for selections)
*/
static int vt_in_wordclass(struct _vtx *vx, uint32 c)
{
  int ch;

  c &= VTATTR_DATAMASK;
  if (c>=256)
    return 1;
  ch = c&0xff;
  return (vx->wordclass[ch>>3]&(1<<(ch&7)))!=0;
}

/*
  set the match wordclass.  is a string of characters which constitute word
  characters which are selected when match by word is active.

  The syntax is similar to character classes in regec, but without the
  []'s.  Ranges ban be set using A-Z, or 0-9.  Use a leading - to include a
  hyphen.
*/
void vt_set_wordclass(struct _vtx *vx, unsigned char *s)
{
  int start, end, i;

  memset(vx->wordclass, 0, sizeof(vx->wordclass));
  if (s) {
    while ( (start = *s) ) {
      if (s[1]=='-' && s[2]) {
	end = s[2];
	s+=3;
      } else {
	end = start;
	s++;
      }
      for (i=start;i<=end;i++) {
	vx->wordclass[i>>3] |= 1<<(i&7);
      }
    }
  } else {
    for(i=0;i<256;i++)
      if (isalnum(i) || i=='_') {
	vx->wordclass[i>>3] |= 1<<(i&7);
      }
  }
  d( printf("wordclass: "); for(i=0;i<sizeof(vx->wordclass);i++) printf("%2.2x", vx->wordclass[i]); printf("\n") );
}


/*
  fixes up the selection boundaries made by the mouse, so they
  match the screen data (tabs, newlines, etc)

  should this also fix the up/down ordering?
*/
void vt_fix_selection(struct _vtx *vx)
{
  struct vt_line *s, *e;

  int sx, sy, ex, ey;

  /* range check vertical limits */
  if (vx->selendy >= vx->vt.height)
    vx->selendy = vx->vt.height-1;
  if (vx->selstarty >= vx->vt.height)
    vx->selstarty = vx->vt.height-1;

  if (vx->selendy < (-vx->vt.scrollbacklines))
    vx->selendy = -vx->vt.scrollbacklines;
  if (vx->selstarty < (-vx->vt.scrollbacklines))
    vx->selstarty = -vx->vt.scrollbacklines;

  /* range check horizontal */
  if (vx->selstartx<0)
    vx->selstartx=0;
  if (vx->selendx<0)
    vx->selendx=0;

  /* make sure 'start' is at the top/left */
  if ( ((vx->selstarty == vx->selendy) && (vx->selstartx > vx->selendx)) ||
       (vx->selstarty > vx->selendy) ) {
    ex = vx->selstartx;		/* swap start/end */
    ey = vx->selstarty;
    sx = vx->selendx;
    sy = vx->selendy;
  } else {			/* keep start/end same */
    sx = vx->selstartx;      
    sy = vx->selstarty;
    ex = vx->selendx;
    ey = vx->selendy;
  }

  d(printf("fixing selection, starting at (%d,%d) (%d,%d)\n", sx, sy, ex, ey));

  /* check if it is 'on screen' or in the scroll back memory */
  s = (struct vt_line *)vt_list_index(sy<0?(&vx->vt.scrollback):(&vx->vt.lines), sy);
  e = (struct vt_line *)vt_list_index(ey<0?(&vx->vt.scrollback):(&vx->vt.lines), ey);

  /* if we didn't find it ... umm? FIXME: do something? */
  switch(vx->selectiontype & VT_SELTYPE_MASK) {
  case VT_SELTYPE_LINE:
    d(printf("selecting by line\n"));
    sx=0;
    ex=e->width;
    break;
  case VT_SELTYPE_WORD:
    d(printf("selecting by word\n"));
    /* scan back over word chars */
    d(printf("startx = %d %p-> \n", sx, s->data));

    if (ex==sx && ex<e->width && sy==ey)
      ex++;

    if ((s->data[sx]&VTATTR_DATAMASK)==0 || (s->data[sx]&VTATTR_DATAMASK)==9) {
      while ((sx>0) && ((s->data[sx]&VTATTR_DATAMASK) == 0))
	sx--;
      if (sx &&
	  ((s->data[sx])&VTATTR_DATAMASK)!=0x09) /* 'compress' tabs */
	sx++;
    } else {
      while ((sx>0) &&
	     (( (vt_in_wordclass(vx, s->data[sx])))))
	sx--;
      if (!vt_in_wordclass(vx, s->data[sx]))
	sx++;
    }
    d(printf("%d\n", sx));

    /* scan forward over word chars */
    /* special cases for tabs and 'blank' character select */
    if ( !((ex >0) && ((e->data[ex-1]&VTATTR_DATAMASK) != 0)) )
      while ((ex<e->width) && ((e->data[ex]&VTATTR_DATAMASK) == 0))
  	ex++;
    if ( !((ex >0) && (!vt_in_wordclass(vx, e->data[ex-1]))) )
        while ((ex<e->width) && 
	     ( (vt_in_wordclass(vx, e->data[ex]))))
  	ex++;

    break;
  case VT_SELTYPE_CHAR:
  default:
    d(printf("selecting by char\n"));

    if (ex==sx && ex<e->width && sy==ey)
      ex++;

    if ((s->data[sx]&VTATTR_DATAMASK)==0) {
      while ((sx>0) && ((s->data[sx]&VTATTR_DATAMASK) == 0))
	sx--;
      if (sx &&
	  (( ((s->data[sx])&VTATTR_DATAMASK)!=0x09))) /* 'compress' tabs */
	sx++;
    }

    /* special cases for tabs and 'blank' character select */
    if ( !((ex >0) && ((e->data[ex-1]&VTATTR_DATAMASK) != 0)) )
      while ((ex<e->width) && ((e->data[ex]&VTATTR_DATAMASK) == 0))
	ex++;
  }

  if ( ((vx->selstarty == vx->selendy) && (vx->selstartx > vx->selendx)) ||
       (vx->selstarty > vx->selendy) ) {
    vx->selstartx = ex;		/* swap end/start values */
    vx->selendx = sx;
  } else {
    vx->selstartx = sx;		/* swap end/start values */
    vx->selendx = ex;
  }

  d(printf("fixed selection, now    at (%d,%d) (%d,%d)\n", sx, sy, ex, ey));
}

/* convert columns start to column end of line l, into
   chars of size 'size'.
*/
static char *vt_expand_line(struct vt_line *l, int size, int start, int end, char *out)
{
  int i;
  unsigned int c=0;
  int state=0;
  int dataend=0;
  int lf=0;

  /* scan from the end of the line to the start, looking for the
     actual end of screen data on that line */
  for(dataend=l->width;dataend>-1;) {
    dataend--;
    if (l->data[dataend] & VTATTR_DATAMASK) {
      dataend++;
      break;
    }
  }

  if (end>dataend) {
    lf = 1;			/* we selected past the end of the line */
    end = dataend;
  }

  if (start<0)
    start=0;
 
  /* YUCK!  oh well, this seems easiest way to do this! */
  switch(size) {
  case 2: {
    unsigned short *o = (unsigned short *)out;
    for (i=start;i<end;i++) {
      c = l->data[i] & VTATTR_DATAMASK;
      if (state==0) {
	if (c==0x09)
	  state=1;
	else if (c<32)
	  c=' ';
	else if (c>0xffff)
	  c='?';
	*o++ = c;
      } else {
	if (c) {			/* in tab, keep skipping null bytes */
	  if (c!=0x09)
	    state=0;
	  *o++ = c;
	}
      }
      d(printf("%02x", c));
    }
    if (lf)
      *o++='\n';
    out = (char *)o;
  }
  break;
  case 4: {
    unsigned int *o = (unsigned int *)out;
    for (i=start;i<end;i++) {
      c = l->data[i] & VTATTR_DATAMASK;
      if (state==0) {
	if (c==0x09)
	  state=1;
	else if (c<32)
	  c=' ';
	*o++ = c;
      } else {
	if (c) {			/* in tab, keep skipping null bytes */
	  if (c!=0x09)
	    state=0;
	  *o++ = c;
	}
      }
      d(printf("%02x", c));
    }
    if (lf)
      *o++='\n';
    out = (char *)o;
  }
  break;
  case 1:
  default: {
    unsigned char *o = (unsigned char *)out;
    for (i=start;i<end;i++) {
      c = l->data[i] & VTATTR_DATAMASK;
      if (state==0) {
	if (c==0x09)
	  state=1;
	else if (c<32)
	  c=' ';
	else if (c>0xff)
	  c='?';
	*o++ = c;
      } else {
	if (c) {			/* in tab, keep skipping null bytes */
	  if (c!=0x09)
	    state=0;
	  *o++ = c;
	}
      }
      d(printf("%02x", c));
    }
    if (lf)
      *o++='\n';
    out = (char*)o;
  }
  break;
  }
  return out;
}

/*
  select block (sx,sy)-(ex,ey) from the buffer.

  Line '0' is the top of the screen, negative lines are part of the
  scrollback, positive lines are part of the visible screen.
  size is the size of each character.
  size = 1 -> char
  size = 2 -> 16 bit utf
  size = 4 -> 32 bit utf
*/
static char *
vt_select_block(struct _vtx *vx, int size, int sx, int sy, int ex, int ey, int *len)
{
  struct vt_line *wn, *nn;
  int line;
  char *out, *data;
  int tmp;

  if ( ((sy == ey) && (sx > ex)) ||
       (sy > ey) ) {
    /* swap start/end values */
    tmp = sx; sx=ex; ex=tmp;
    tmp = sy; sy=ey; ey=tmp;
  }

  /* makes a rough assumption about the buffer size needed */
  if ( (data = g_malloc(size*(ey-sy+1)*(vx->vt.width+20)+1)) == 0 ) {
    *len = 0;
    printf("ERROR: Cannot g_malloc selection buffer\n");
    return 0;
  }

  out = data;

  line = sy;
  wn = (struct vt_line *)vt_list_index((line<0)?(&vx->vt.scrollback):(&vx->vt.lines), line);

  if (wn)
    nn = wn->next;
  else {
    *len = 0;
    strcpy(data, "");
    return data;
  }

  /* FIXME: check 'wn' exists ... */

  /* only a single line selected? */
  if (sy == ey) {
    out = vt_expand_line(wn, size, sx, ex, out);
  } else {
    /* scan lines */
    while (nn && (line<ey)) {
      d(printf("adding selection from line %d\n", line));
      if (line == sy) {
	out = vt_expand_line(wn, size, sx, wn->width, out); /* first line */
      } else {
	out = vt_expand_line(wn, size, 0, wn->width, out);
      }
      line++;
      if (line==0) {		/* wrapped into 'on screen' area? */
	wn = (struct vt_line *)vt_list_index(&vx->vt.lines, 0);
	nn = wn->next;
      } else {
	wn = nn;
	nn = nn->next;
      }
    }

    /* last line (if it exists - shouldn't happen?) */
    if (nn)
      out = vt_expand_line(wn, size, 0, ex, out);
  }

  *len = (out-data)/size;
  *out = 0;

  d(printf("selected text = \n");
    fwrite(data, out-data, 1, stdout);
    printf("\n"));

  return data;
}

/*
  return currently selected text
*/
char *vt_get_selection(struct _vtx *vx, int size, int *len)
{
  if (vx->selection_data)
    g_free(vx->selection_data);

  vx->selection_data =
    (uint32 *)vt_select_block(vx, size, vx->selstartx, vx->selstarty,
			      vx->selendx, vx->selendy, &vx->selection_size);
  if (len)
    *len = vx->selection_size;

  return (char *)vx->selection_data;
}

/* clear the selection */
void vt_clear_selection(struct _vtx *vx)
{
  if (vx->selection_data) {
    g_free(vx->selection_data);
    vx->selection_data = 0;
    vx->selection_size = 0;
  }
}

/*
  Draw part of the selection

  Called by vt_draw_selection
*/
static void vt_draw_selection_part(struct _vtx *vx, int sx, int sy, int ex, int ey)
{
  int tmp;
  struct vt_line *l, *bl;
  int line;

  /* always draw top->bottom */
  if (sy>ey) {
    tmp = sy;sy=ey;ey=tmp;
    tmp = sx;sx=ex;ex=tmp;
  }

  d(printf("selecting from (%d,%d) to (%d,%d)\n", sx, sy, ex, ey));

  line = sy;
  if (line<0) {
    l = (struct vt_line *)vt_list_index(&vx->vt.scrollback, line);
  } else {
    l = (struct vt_line *)vt_list_index(&vx->vt.lines, line);
  }

  if ((line-vx->vt.scrollbackoffset)>=0)
    bl = (struct vt_line *)vt_list_index(&vx->vt.lines_back, line-vx->vt.scrollbackoffset);
  else
    bl = (struct vt_line *)vx->vt.lines_back.head;

  d(printf("selecting from (%d,%d) to (%d,%d)\n", sx, sy-vx->vt.scrollbackoffset, ex, ey-vx->vt.scrollbackoffset));
  while ((line<=ey) && (l->next) && ((line-vx->vt.scrollbackoffset)<vx->vt.height)) {
    d(printf("line %d = %p ->next = %p\n", line, l, l->next));
    if ((line-vx->vt.scrollbackoffset)>=0) {
      vt_line_update(vx, l, bl, line-vx->vt.scrollbackoffset, 0, 0, bl->width);
      bl=bl->next;
      if (bl->next==0)
	return;
    }
    line++;
    if (line==0) {
      l = (struct vt_line *)vt_list_index(&vx->vt.lines, 0);
    } else {
      l=l->next;
    }
  }
}

/*
  draws selection from vt->selstartx,vt->selstarty to vt->selendx,vt->selendy
*/
void vt_draw_selection(struct _vtx *vx)
{
  /* draw in 2 parts
     difference between old start and new start
     difference between old end and new end
  */

  /* dont draw nothin if we dont have to! */
  if (vx->selendxold == vx->selstartxold
      && vx->selendx == vx->selstartx
      && vx->selendyold == vx->selstartyold
      &&  vx->selendy == vx->selstarty)
    return;

  d(printf("start ... \n"));
  vt_draw_selection_part(vx,
			 vx->selstartx, vx->selstarty,
			 vx->selstartxold, vx->selstartyold);

  d(printf("end ..\n"));
  vt_draw_selection_part(vx,
			 vx->selendx, vx->selendy,
			 vx->selendxold, vx->selendyold);

  vx->selendxold = vx->selendx;
  vx->selendyold = vx->selendy;
  vx->selstartxold = vx->selstartx;
  vx->selstartyold = vx->selstarty;
  vx->selection_changed (vx->vt.user_data);
}

/*
  draw/undraw the cursor
*/
void vt_draw_cursor(struct _vtx *vx, int state)
{
  uint32 attr;

  if (vx->vt.scrollbackold == 0 && vx->vt.cursorx<vx->vt.width) {
    attr = vx->vt.this_line->data[vx->vt.cursorx];
    if (state && (vx->vt.mode & VTMODE_BLANK_CURSOR)==0) {			/* must swap fore/background colour */
      attr = (((attr & VTATTR_FORECOLOURM) >> VTATTR_FORECOLOURB) << VTATTR_BACKCOLOURB)
	| (((attr & VTATTR_BACKCOLOURM) >> VTATTR_BACKCOLOURB) << VTATTR_FORECOLOURB)
      | ( attr & ~(VTATTR_FORECOLOURM|VTATTR_BACKCOLOURM));
    }
    vx->back_match=0;		/* forces re-draw? */
    vx->draw_text(vx->vt.user_data,
		  vx->vt.this_line,
		  vx->vt.cursory, vx->vt.cursorx, 1, attr);
  }
}

/* dummy rendering callbacks */
static void
dummy_draw_text(void *user_data, struct vt_line *line, int row, int col, int len, int attr)
{
}
static void 
dummy_scroll_area(void *user_data, int firstrow, int count, int offset, int fill)
{
}
static int
dummy_cursor_state(void *user_data, int state)
{
  return 0;
}
static void
dummy_selection_changed (void *user_data)
{
}

static struct vt_line *copy_line(struct vt_line *l)
{
  struct vt_line *n = g_malloc(VT_LINE_SIZE(l->width));
  memcpy(n, l, VT_LINE_SIZE(l->width));
  return n;
}

static void vt_highlight(struct _vtx *vx, struct vt_match *m)
{
  struct vt_line *l;
  struct vt_match_block *b;
  int i;
  uint32 mask;

  mask = m->match->highlight_mask;
  b = m->blocks;
  while (b) {
    l = b->line;
    d(printf("updating %d; %d-%d: ", b->lineno, b->start, b->end));
    if ((mask & (VTATTR_FORECOLOURM|VTATTR_BACKCOLOURM)) == 0) {
      for (i=b->start; i<b->end; i++) {
	l->data[i] ^= mask;
      }
    } else {
      b->saveline = copy_line(l);
      for (i=b->start; i<b->end; i++) {
	l->data[i] = (l->data[i] & VTATTR_DATAMASK) | mask;
      }
    }
    d(printf("\n"));
    vt_update_rect(vx, -1, b->start, b->lineno, b->end, b->lineno);
    b = b->next;
  }

}

static void vt_unhighlight(struct _vtx *vx, struct vt_match *m)
{
  struct vt_line *l;
  struct vt_match_block *b;
  int i;
  uint32 mask;

  mask = m->match->highlight_mask;
  b = m->blocks;
  while (b) {
    l = b->line;
    d(printf("updating %d; %d-%d: ", b->lineno, b->start, b->end));

    if (b->saveline==0) {
      for (i=b->start; i<b->end; i++) {
	l->data[i] ^= mask;
      }
    } else {
      memcpy(l->data, b->saveline->data, l->width * sizeof(l->data[0]));
      g_free(b->saveline);
      b->saveline = 0;
    }
    d(printf("\n"));
    vt_update_rect(vx, -1, b->start, b->lineno, b->end, b->lineno);
    b = b->next;
  }

}


/*
 * if m is null, clear the highlight
 */
void vt_match_highlight(struct _vtx *vx, struct vt_match *m)
{
  if (m!=vx->match_shown) {
    if (vx->match_shown) {
      d(printf("unhilighting match '%s'\n", vx->match_shown->matchstr));
      vt_unhighlight(vx, vx->match_shown);
    }
    vx->match_shown = m;
    if (m) {
      d(printf("hilighting match '%s'\n", vx->match_shown->matchstr));
      vt_highlight(vx, vx->match_shown);
    }
  }
}

void vt_free_match_blocks(struct _vtx *vx)
{
  struct vt_match_block *b, *n;
  struct vt_match *m, *mn;

  /* make sure we have nothing highlighted either */
  vt_match_highlight(vx, 0);

  /* blow away the old matches */
  m = vx->matches;
  while (m) {
    b = m->blocks;
    while (b) {
      n = b->next;
      if (b->saveline)
	g_free(b->saveline);
      g_free(b);
      b=n;
    }
    mn = m->next;
    g_free(m->matchstr);
    m = mn;
  }
  vx->matches = 0;
  vx->magic_matched = 0;
}

/*
  regex matching for the current screen
*/
void vt_getmatches(struct _vtx *vx)
{
  char *line, *out, *outend;
  uint32 *in, *inend;
  struct vt_line *wn, *nn, *sol, *ssol;
  int lineno=0, lineskip=0, solineno;
  int c;
  int matchoffset;		/* for lines which span multiple lines */
  struct vt_magic_match *mw, *mn;

  /* blow away the current 'magic blocks' */
  vt_free_match_blocks(vx);

  /* needs to account for changed line widths *sigh* */
  line = g_malloc((vx->vt.width+1) * vx->vt.height);
  out = line;

  /* start from the top */
  if (vx->vt.scrollbackoffset<0) {
    wn = (struct vt_line *)vt_list_index(&vx->vt.scrollback, vx->vt.scrollbackoffset);
    if (!wn) {
      /* check for error condition */
      printf("LINE UNDERFLOW!\n");
      wn = (struct vt_line *)vx->vt.scrollback.head;
    }
  } else {
    wn = (struct vt_line *)vx->vt.lines.head;
  }

  nn = wn->next;
  ssol = wn;
  matchoffset = 0;
  while (nn && (lineno+lineskip)<vx->vt.height) {

    if (ssol==0)
      ssol = wn;

    in = wn->data;
    inend = wn->data + wn->width;
    /* scan backwards for end of line */
    while (inend > in
	   && (inend[0] & VTATTR_DATAMASK)==0)
      inend--;
    /* scan forwards, converting data to char string, tabs to spaces, etc */
    while (in <= inend) {
      c = *in++ & VTATTR_DATAMASK;
      if (c<32)
	c=' ';
      else if (c>0xff)
	c='.';
      *out++ = c;
    }
    /* check for end of line(s) */
    if (inend != wn->data + wn->width - 1 || lineno+lineskip==vx->vt.height-1) {
      *out = 0;
      outend = out;

      /* we can now do the regex scanning for this line */
      mw = (struct vt_magic_match *)vx->magic_list.head;
      mn = mw->next;
      while (mn) {
	regmatch_t pmatch[2];

	d(printf("checking regex '%s' against '%.*s'\n", mw->regex, outend-line, line));

	/* get the current positions of everything */
	solineno = lineno;
	matchoffset = 0;
	sol = ssol;

	/* check this regex, for multiple matches ... */
	out = line;
	while ((out<outend) && regexec(&mw->preg, out, 2, pmatch, 0)==0) {
	  int start = pmatch[0].rm_so + (out-line);
	  int end = pmatch[0].rm_eo + (out-line);
	  struct vt_match_block *b;
	  struct vt_match *m;

	  /* if the user supplied a dumb regex, and it matches the empty string, then
	     we have to scan every character - slow and yuck, but it gives them what
	     they want. */
	  if (pmatch[0].rm_eo == 0) {
	    out++;
	    continue;
	  }

	  /* find the line on which this block starts */
	  while ((start-matchoffset)>sol->width) {
	    matchoffset += sol->width;

	    if (sol==(struct vt_line *)vx->vt.scrollback.tailpred)
	      sol = (struct vt_line *)vx->vt.lines.head;
	    else
	      sol = sol->next;

	    solineno++;
	  }

	  /* add a match, and the first block */
	  m = g_malloc(sizeof(*m));
	  m->next = vx->matches;
	  vx->matches = m;
	  m->match = mw;
	  m->matchstr = g_malloc(end-start+1);
	  sprintf(m->matchstr, "%.*s", end-start, line+start);

	  /* add a block */
	  b = g_malloc(sizeof(*b));
	  b->line = sol;
	  b->saveline = 0;
	  b->lineno = solineno;
	  b->start = start-matchoffset;
	  b->end = (end-matchoffset)>sol->width?sol->width:end-matchoffset;
	  m->blocks = b;
	  b->next = 0;

	  d(printf("block on line %d from %d to %d\n",
		   b->lineno, b->start, b->end));

	  /* for all following line(segments) this match spans ... add them too */
	  while ((end-matchoffset) > sol->width) {
	    matchoffset += sol->width;

	    if (sol==(struct vt_line *)vx->vt.scrollback.tailpred)
	      sol = (struct vt_line *)vx->vt.lines.head;
	    else
		/* to fix the crash */
	      sol = sol->next?sol->next:NULL;;

	    if(!sol) return;

	    solineno++;

	    b = g_malloc(sizeof(*b));
	    b->line = sol;
	    b->saveline = 0;
	    b->lineno = solineno;
	    b->start = 0;
	    b->end = (end-matchoffset)>sol->width?sol->width:end-matchoffset;
	    b->next = m->blocks;
	    m->blocks = b;

	    d(printf("and block on line %d from %d to %d\n",
		     b->lineno, b->start, b->end));
	  }

	  d(printf("found match from %d to %d\n", out-line+pmatch[0].rm_so,
		   out-line+pmatch[0].rm_eo));
	  d(printf(" is '%.*s'\n", pmatch[0].rm_eo - pmatch[0].rm_so,
		   out + pmatch[0].rm_so));

	  /* check to see if this match goes across line boundaries ... */
	  out += pmatch[0].rm_eo; /* skip to next sub-string */
	}

	mw = mn;
	mn = mn->next;
      }
     
      /* go back to the start, and get the next start-of-line */
      out = line;
      lineno += lineskip + 1;
      lineskip = 0;
      ssol = 0;
    } else {
      lineskip++;
    }

    /* next logical line */
    if (wn==(struct vt_line *)vx->vt.scrollback.tailpred)
      wn = (struct vt_line *)vx->vt.lines.head;
    else
      wn = nn;
    nn = wn->next;
  }

  g_free(line);
  vx->magic_matched = 1;
}

void
vt_match_clear(struct _vtx *vx, char *regex)
{
  struct vt_magic_match *m, *n;

  /* make sure there are no dangling references to this match type */
  vt_free_match_blocks(vx);

  m = (struct vt_magic_match *)vx->magic_list.head;
  n = m->next;
  while (n) {
    if (regex==0 || strcmp(m->regex, regex)==0) {
      vt_list_remove((struct vt_listnode *)m);
      g_free(m->regex);
      regfree(&m->preg);
      g_free(m);
    }
    m = n;
    n = n->next;
  }
}

/* return the match descriptor if this cursor position matches
   one */
struct vt_match *vt_match_check(struct _vtx *vx, int x, int y)
{
  struct vt_match_block *b;
  struct vt_match *m;

  m = vx->matches;
  while (m) {
    b = m->blocks;
    while (b) {
      if (b->lineno == y && x>=b->start && x<b->end)
	return m;
      b = b->next;
    }
    m = m->next;
  }
  return m;
}


struct _vtx *vtx_new(int width, int height, void *user_data)
{
  struct _vtx *vx;
  int i;

  vx = g_malloc0(sizeof(struct _vtx));

  /* initial settings */
  vt_init(&vx->vt, width, height);

  vx->selection_data = 0;
  vx->selection_size = 0;
  vx->selected = 0;
  vx->selectiontype = VT_SELTYPE_NONE;

  vx->scroll_type = VT_SCROLL_ALWAYS;

  vx->scroll_area = dummy_scroll_area;
  vx->draw_text = dummy_draw_text;
  vx->cursor_state = dummy_cursor_state;
  vx->selection_changed = dummy_selection_changed;

  /* other parameters initialised to 0 by calloc */
  vx->vt.user_data = user_data;

  /* set '1' bits for all characters considered a 'word' */
  for(i=0;i<256;i++)
    if (isalnum(i) || i=='_')
      vx->wordclass[i>>3] |= 1<<(i&7);

  vt_list_new(&vx->magic_list);
  vx->magic_matched = 0;
  vx->matches = 0;
  vx->match_shown = 0;

  return vx;
}

void vtx_destroy(struct _vtx *vx)
{
  if (vx) {
    vt_destroy(&vx->vt);
    if (vx->selection_data)
      g_free(vx->selection_data);
    vt_free_match_blocks(vx);
    vt_match_clear(vx, 0);
    g_free(vx);
  }
}
