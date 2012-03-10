/* xscreensaver, Copyright (c) 1992-2008 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty.
 */

#include "screenhack.h"
#include <stdlib.h>

#define ALIVE 255
#define DEAD 0
#define RANDOM_LIFE 3

/* a new cell will appear if the number of neighbors (Sum)
 * is equal or more than r[0] and equal or less than r[1]
 * a cell will die if the Sum is more than r[2] or less than r[3] */
int R[4] = {3, 3, 3, 2};

struct state {
  Display *dpy;
  Window window;

  GC gc;
  int delay;
  unsigned long fg, bg, pixels [256];
  int npixels;
  int xlim, ylim;
  Colormap cmap;

  unsigned char **world;
  unsigned char **old_world;
  int scale, length, height;
};

static void HSVtoRGB( float *r, float *g, float *b, float h, float s, float v )
{
  int i;
  float f, p, q, t;
  if( s == 0 ) {
    *r = *g = *b = v;
    return;
  }
  h /= 60;      
  i = floor( h );
  f = h - i;     
  p = v * ( 1 - s );
  q = v * ( 1 - s * f );
  t = v * ( 1 - s * ( 1 - f ) );
  switch( i ) {
    case 0:
      *r = v;
      *g = t;
      *b = p;
      break;
    case 1:
      *r = q;
      *g = v;
      *b = p;
      break;
    case 2:
      *r = p;
      *g = v;
      *b = t;
      break;
    case 3:
      *r = p;
      *g = q;
      *b = v;
      break;
    case 4:
      *r = t;
      *g = p;
      *b = v;
      break;
    default:    
      *r = v;
      *g = p;
      *b = q;
      break;
  }
}

static void *
generate_world(int length, int height)
{
  /* to allow faster calculation, we have 1 char padding around
   * our world, so we need to add that here */
  int i;
  unsigned char **world = calloc(length + 2, sizeof(char *));
  /* calloc will set all bits to 0 */
  world[0] = calloc((length+2) * (height+2), sizeof(char));

  for(i = 1; i < length+2; i++)
    world[i] = world[0] + i * height;

  return world;
}

static void
setup_world(void *closure)
{
  struct state* st = (struct state *) closure;

  st->length = st->xlim / st->scale;
  st->height = st->ylim / st->scale;

  st->world = generate_world(st->length, st->height);
  st->old_world = generate_world(st->length, st->height);
}

static void *
life_init (Display *dpy, Window window)
{
  struct state *st = (struct state *) calloc (1, sizeof(*st));
  int i;

  XGCValues gcv;
  XWindowAttributes xgwa;
  st->dpy = dpy;
  st->window = window;

  XGetWindowAttributes (st->dpy, st->window, &xgwa);
  st->xlim = xgwa.width;
  st->ylim = xgwa.height;
  st->cmap = xgwa.colormap;
  gcv.foreground= st->fg= get_pixel_resource(st->dpy, st->cmap, "foreground","Foreground");
  gcv.background= st->bg= get_pixel_resource(st->dpy, st->cmap, "background","Background");

  st->delay = get_integer_resource (st->dpy, "delay", "Integer");
  st->scale = get_integer_resource (st->dpy, "scale", "Integer");
  if (st->delay < 0) st->delay = 0;

  st->gc = XCreateGC (st->dpy, st->window, GCForeground, &gcv);


  for(st->npixels = 0; st->npixels < 256; st->npixels++)
  {
    float r, g, b;
    HSVtoRGB(&r, &g, &b, 195.0f, st->npixels/256.0f, st->npixels/256.0f);

    if(st->npixels == 255)
      r = g = b = 1.0f;

    XColor fgc;
    fgc.flags = DoRed|DoGreen|DoBlue;
    fgc.red = 65535*r;
    fgc.green = 65535*g;
    fgc.blue = 65535*b;
    XAllocColor (st->dpy, st->cmap, &fgc);
    st->pixels[st->npixels] = fgc.pixel;
  }

  setup_world(st);
  return st;
}

static void
randomize_world(void *closure)
{
  struct state* st = (struct state *) closure;
  int x, y;

  /* randomize borders */
  for(x = 0; x < st->length+1; ++x)
  {
    /* set padding as well as the normal map */
    st->world[x][1] = st->world[x][st->height-1] = random() % RANDOM_LIFE == 0 ? ALIVE : DEAD;
    st->world[x][st->height-2] = st->world[x][0] = random() % RANDOM_LIFE == 0 ? ALIVE : DEAD;
  }

  for(y = 0; y < st->height+1; ++y)
  {
    st->world[1][y] = st->world[st->length - 1][y] = random() % RANDOM_LIFE == 0 ? ALIVE : DEAD;
    st->world[st->length - 2][y] = st->world[0][y] = random() % RANDOM_LIFE == 0 ? ALIVE : DEAD;
  }
}

static int
max(int a, int b)
{
  return a > b ? a : b;
}

static void
update_world(void *closure)
{
  struct state* st = (struct state *) closure;
  int x, y;

  randomize_world(closure);

  void *tmp = st->world;
  st->world = st->old_world;
  st->old_world = tmp;

  for(x = 1; x < st->length; ++x)
  {
    for(y = 1; y < st->height; ++y)
    {
      int alive_count = 0;
      int a, b;

      for(a = -1; a <= 1; ++a)
        for(b = -1; b <= 1; ++b)
          if(st->old_world[x+a][y+b] == ALIVE)
            ++alive_count;

      if(st->old_world[x][y] == ALIVE)
        --alive_count;

      st->world[x][y] = st->old_world[x][y];

      if(alive_count >= R[0] && alive_count <= R[1])
        st->world[x][y] = ALIVE;
      
      if(alive_count > R[2] || alive_count < R[3])
        st->world[x][y] = max(st->world[x][y] - 10, 0);
    }
  }
}

static void
paint_world(void *closure)
{
  struct state* st = (struct state *) closure;
  int x, y;

  XGCValues gcv;
  Pixmap p = XCreatePixmap(st->dpy, st->window,st->xlim, st->ylim, 24);

  for(x = 1; x < st->length; ++x)
    for(y = 1; y < st->height; ++y) {
      /* select color according to age */
      gcv.foreground = st->pixels[ st->world[x][y] ];
      XChangeGC (st->dpy, st->gc, GCForeground, &gcv);

      XFillRectangle(st->dpy, p, st->gc, st->scale*x, st->scale*y, st->scale, st->scale);
    }

  XCopyArea(st->dpy, p, st->window, st->gc, 0, 0, st->xlim, st->ylim, 0, 0);
  XFreePixmap(st->dpy, p);
}

static unsigned long
life_draw (Display *dpy, Window window, void *closure)
{
  struct state* st = (struct state *) closure;

  update_world(closure);
  paint_world(closure);

  return st->delay;
}

static const char *life_defaults [] = {
  ".background: black",
  ".foreground: white",
  "*delay:  100000",
  "*scale: 4",
  "*grey: false",
  0
};

static XrmOptionDescRec life_options [] = {
  { "-delay",   ".delay", XrmoptionSepArg, 0 },
  { "-scale",   ".scale", XrmoptionSepArg, 0 },
  { "-grey",    ".grey",  XrmoptionNoArg, "True" },
  { 0, 0, 0, 0 }
};

static void
life_reshape (Display *dpy, Window window, void *closure, 
                 unsigned int w, unsigned int h)
{
  struct state *st = (struct state *) closure;
  st->xlim = w;
  st->ylim = h;
  setup_world(st);
}

static Bool
life_event (Display *dpy, Window window, void *closure, XEvent *event)
{
  return False;
}

static void
life_free (Display *dpy, Window window, void *closure)
{
}

XSCREENSAVER_MODULE ("Life", life)