/* LifeSaver, Copyright (c) 2001-2008 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty.
 */

#define DEFAULTS  "*delay:  30000       \n" \
      "*count:        30          \n" \

# define refresh_life 0
# define release_life 0
#undef countof
#define countof(x) (sizeof((x))/sizeof((*x)))

#include "xlockmore.h"
#include <ctype.h>

#ifdef USE_GL /* whole file */

#define RANDOM_LIFE 3

/* layout of one cell (32bit int):
 * | 4 bit state | 4 bit neighbours | 8 bit blue | 8 bit green | 8 bit red |
 */

#define DEATH_SUB 0x000faaaa
#define ONE_LIFE 0x10000000
#define ONE_NEIGHBOR 0x01000000
#define ALIVE_GAME 0xf0000000
#define ALIVE_DRAW 0x00ffffff
#define DEAD_GAME 0xe0000000

/* a new cell will appear if the number of neighbors (Sum)
 * is equal or more than r[0] and equal or less than r[1]
 * a cell will die if the Sum is more than r[2] or less than r[3] */
const int R[4] = {3, 3, 3, 2};

const int DEAD_COLOR[15] = {
  0xFFECBA,
  0xFFE299,
  0xFFD877,
  0xFFCF56,
  0xFFC532,
  0xFFBB11,
  0xF4AF00,
  0xE5A400,
  0xD39700,
  0xC48C00,
  0xB27F00,
  0xA07300,
  0x916800,
  0x7F5B00,
  0x000000
};

typedef struct {
  GLXContext *glx_context;

  unsigned int **world;
  unsigned int **old_world;

  int width, height;

  time_t last_update;
  int frames;
} life_configuration;

static life_configuration *bps = NULL;

static GLfloat scale;

static XrmOptionDescRec opts[] = {
  { "-scale",  ".scale",  XrmoptionSepArg, 0 },
};

static argtype vars[] = {
  {&scale,     "scale",  "Scale",  "2.0",  t_Float},
};

ENTRYPOINT ModeSpecOpt life_opts = {countof(opts), opts, countof(vars), vars, NULL};

static int
alive(int cell)
{
  return (cell & ALIVE_GAME) == ALIVE_GAME;
}

static void *
generate_world(int length, int height)
{
  /* to allow faster calculation, we have 1 char padding around
   * our world, so we need to add that here */
  int i;
  unsigned int **world = calloc(height+2, sizeof(int *));
  /* calloc will set all bits to 0 */
  world[0] = calloc((length+2) * (height+2), sizeof(int));

  for(i = 1; i < height+2; i++)
    world[i] = world[0] + i * length;

  return world;
}

/* Window management, etc
 */
ENTRYPOINT void
reshape_life (ModeInfo *mi, int width, int height)
{
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  gluOrtho2D (0, (GLint) width, 0, (GLint) height);

  glClear(GL_COLOR_BUFFER_BIT);
}

static void
setup_world(ModeInfo *mi)
{
  life_configuration *bp = &bps[MI_SCREEN(mi)];

  
  bp->glx_context = init_GL(mi);

  bp->width = MI_WIDTH(mi) / scale;
  bp->height = MI_HEIGHT(mi) / scale;

  bp->world = generate_world(bp->width, bp->height);
  bp->old_world = generate_world(bp->width, bp->height);

  reshape_life (mi, MI_WIDTH(mi), MI_HEIGHT(mi));
}

ENTRYPOINT Bool
life_handle_event (ModeInfo *mi, XEvent *event)
{
  if (event->xany.type == ButtonPress && event->xbutton.button == Button1)
  {
    setup_world(mi);
    return True;
  }

  return False;
}

static void
set_cell(int x, int y, int alive, ModeInfo *mi)
{
  life_configuration *bp = &bps[MI_SCREEN(mi)];
  int a, b;

  if(alive) {
    for(a = -1; a <= 1; ++a)
      for(b = -1; b <= 1; ++b)
        bp->world[y+b][x+a] += ONE_NEIGHBOR;

    /* we are not our own neighbor */
    bp->world[y][x] -= ONE_NEIGHBOR;
    bp->world[y][x] |= ALIVE_DRAW | ALIVE_GAME;
  }
  else {
    bp->world[y][x] += ONE_NEIGHBOR;
    bp->world[y][x] &= 0x0f000000;
    bp->world[y][x] |= DEAD_GAME | DEAD_COLOR[0];

    for(a = -1; a <= 1; ++a)
      for(b = -1; b <= 1; ++b)
        bp->world[y+b][x+a] -= ONE_NEIGHBOR;
  }
}

static void
randomize_world(ModeInfo *mi)
{
  life_configuration *bp = &bps[MI_SCREEN(mi)];
  int x, y, new_state;

  /* randomize borders */
  for(x = 1; x < bp->width; ++x)
  {
    new_state = random() % RANDOM_LIFE == 0 ? 1 : 0;
    if(new_state != alive(bp->world[1][x]))
      set_cell(x, 1, new_state, mi);

    new_state = random() % RANDOM_LIFE == 0 ? 1 : 0;
    if(new_state != alive(bp->world[bp->height][x]))
      set_cell(x, bp->height, new_state, mi);
  }

  for(y = 1; y < bp->height; ++y)
  {
    new_state = random() % RANDOM_LIFE == 0 ? 1 : 0;
    if(new_state != alive(bp->world[y][1]))
      set_cell(1, y, new_state, mi);

    new_state = random() % RANDOM_LIFE == 0 ? 1 : 0;
    if(new_state != alive(bp->world[y][bp->width]))
      set_cell(bp->width, y, new_state, mi);
  }
}


static void
update_world(ModeInfo *mi)
{
  life_configuration *bp = &bps[MI_SCREEN(mi)];
  int x, y;

  randomize_world(mi);

  for(y = 0; y < bp->height+1; ++y)
    memcpy(bp->old_world[y], bp->world[y], bp->width * sizeof(int));


  for(x = 1; x < bp->width; ++x)
  {
    for(y = 1; y < bp->height; ++y)
    {
      int state = bp->old_world[y][x] >> 24;
      int alive_count = state & 0x0f;
      int cell_alive = state >> 4;

      if(alive(bp->old_world[y][x])) {
        /* cell alive - turn off if not enough neighbors */
        if((alive_count > R[2] || alive_count < R[3]))
          set_cell(x, y, 0, mi);
      }
      else {
        /* cell dead - turn on if correct neighbors */
        if(alive_count >= R[0] && alive_count <= R[1])
          set_cell(x, y, 1, mi);
        else if(cell_alive > 1) {
          bp->world[y][x] -= ONE_LIFE;
          bp->world[y][x] &= 0xff000000;
          bp->world[y][x] += DEAD_COLOR[16-cell_alive];
        }
        else if(cell_alive == 1) {
          bp->world[y][x] &= 0x0f000000;
        }

      }
    }
  }
}

ENTRYPOINT void 
init_life (ModeInfo *mi)
{
  if (!bps) {
    bps = (life_configuration *)
      calloc (MI_NUM_SCREENS(mi), sizeof (life_configuration));
    if (!bps) {
      fprintf(stderr, "%s: out of memory\n", progname);
      exit(1);
    }
  }

  setup_world(mi);
}


ENTRYPOINT void
draw_life (ModeInfo *mi)
{
  life_configuration *bp = &bps[MI_SCREEN(mi)];
  Display *dpy = MI_DISPLAY(mi);
  Window window = MI_WINDOW(mi);

  time_t now = time(NULL);

  if (!bp->glx_context)
    return;

#ifdef DEBUG
  bp->frames++;

  /* print FPS every 5 seconds */
  if(now > bp->last_update + 5)
  {
    bp->last_update = now;
    fprintf(stderr, "FPS: %f \n", bp->frames / 5.0f);
    bp->frames = 0;
  }
#endif

  update_world(mi);

  glXMakeCurrent(MI_DISPLAY(mi), MI_WINDOW(mi), *(bp->glx_context));

  /*glShadeModel(GL_SMOOTH);*/

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_NORMALIZE);
  glEnable(GL_CULL_FACE);

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable (GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glTexImage2D (
      GL_TEXTURE_2D,
      0,
      GL_RGB,
      bp->width,
      bp->height,
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      &(bp->world[0][0])
  );

  glBegin(GL_QUADS);
      glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0, 0.0);
      glTexCoord2f(1.0f, 0.0f); glVertex2f(MI_WIDTH(mi), 0.0);
      glTexCoord2f(1.0f, 1.0f); glVertex2f(MI_WIDTH(mi),  MI_HEIGHT(mi));
      glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0,  MI_HEIGHT(mi));
  glEnd();

  glFlush();
  glFinish();

  glXSwapBuffers(dpy, window);
}

XSCREENSAVER_MODULE ("LifeSaver", life)

#endif /* USE_GL */