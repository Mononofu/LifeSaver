/* dangerball, Copyright (c) 2001-2008 Jamie Zawinski <jwz@jwz.org>
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
      "*showFPS:      False       \n" \
      "*wireframe:    False       \n" \

# define refresh_ball 0
# define release_ball 0
#undef countof
#define countof(x) (sizeof((x))/sizeof((*x)))

#include "xlockmore.h"
#include "colors.h"
#include "sphere.h"
#include "tube.h"
#include "rotator.h"
#include "gltrackball.h"
#include <ctype.h>

#ifdef USE_GL /* whole file */

#define RANDOM_LIFE 3

/* layout of one cell (32bit int):
 * | 4 bit neighbours | 4 bit state | 8 bit blue | 8 bit green | 8 bit red |
 */

/* state masks */
#define ALIVE_MASK 0x01000000
/* coloring */
#define ALIVE_COLOR 0x00ffffff
#define DEAD 0x0
#define DEATH_THRESH 0x00100000
#define DEATH_SUB 0x000faaaa


/* a new cell will appear if the number of neighbors (Sum)
 * is equal or more than r[0] and equal or less than r[1]
 * a cell will die if the Sum is more than r[2] or less than r[3] */
int R[4] = {3, 3, 3, 2};

typedef struct {
  GLXContext *glx_context;

  unsigned int **world;
  unsigned int **old_world;

  int width, height;

  time_t last_update;
  int frames;
} ball_configuration;

static ball_configuration *bps = NULL;

static GLfloat scale;

static XrmOptionDescRec opts[] = {
  { "-scale",  ".scale",  XrmoptionSepArg, 0 },
};

static argtype vars[] = {
  {&scale,     "scale",  "Scale",  "1.0",  t_Float},
};

ENTRYPOINT ModeSpecOpt ball_opts = {countof(opts), opts, countof(vars), vars, NULL};

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
reshape_ball (ModeInfo *mi, int width, int height)
{
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  gluOrtho2D (0, (GLint) width, 0, (GLint) height);

  glClear(GL_COLOR_BUFFER_BIT);
}

static void
setup_world(ModeInfo *mi)
{
  ball_configuration *bp = &bps[MI_SCREEN(mi)];

  
  bp->glx_context = init_GL(mi);

  bp->width = MI_WIDTH(mi) / scale;
  bp->height = MI_HEIGHT(mi) / scale;

  bp->world = generate_world(bp->width, bp->height);
  bp->old_world = generate_world(bp->width, bp->height);

  reshape_ball (mi, MI_WIDTH(mi), MI_HEIGHT(mi));
}

ENTRYPOINT Bool
ball_handle_event (ModeInfo *mi, XEvent *event)
{
  if (event->xany.type == ButtonPress && event->xbutton.button == Button1)
  {
    setup_world(mi);
    return True;
  }

  return False;
}

static void
set_neighbors(int x, int y, int state, ModeInfo *mi)
{
  ball_configuration *bp = &bps[MI_SCREEN(mi)];
  int a, b;

  if(state)
    for(a = -1; a <= 1; ++a)
      for(b = -1; b <= 1; ++b)
        bp->world[y+b][x+a] += 0x02000000;
  else
    for(a = -1; a <= 1; ++a)
      for(b = -1; b <= 1; ++b)
        bp->world[y+b][x+a] -= 0x02000000;


  if(state) {
    bp->world[y][x] -= 0x02000000;
    bp->world[y][x] |= ALIVE_COLOR;
    bp->world[y][x] |= ALIVE_MASK;
  }
  else {
    bp->world[y][x] += 0x02000000;
    bp->world[y][x] &= 0xfeffffff;
    /* save state and neighbor count and restore after setting color */
    int tmp = bp->world[y][x] & 0xff000000;
    bp->old_world[y][x] &= 0x00ffffff;
    bp->world[y][x] = bp->old_world[y][x] < DEATH_THRESH ? 0 : bp->old_world[y][x] - DEATH_SUB;
    bp->world[y][x] += tmp;
  }

}

static void
randomize_world(ModeInfo *mi)
{
  ball_configuration *bp = &bps[MI_SCREEN(mi)];
  int x, y, new_state;

  /* randomize borders */
  for(x = 1; x < bp->width; ++x)
  {
    new_state = random() % RANDOM_LIFE == 0 ? 1 : 0;
    if(new_state != ((bp->world[1][x] >> 24) & 0x01))
      set_neighbors(x, 1, new_state, mi);

    new_state = random() % RANDOM_LIFE == 0 ? 1 : 0;
    if(new_state != ((bp->world[bp->height][x] >> 24) & 0x01))
      set_neighbors(x, bp->height, new_state, mi);
    /* set padding as well as the normal map */
    /*bp->world[bp->height-1][x] = bp->world[1][x];
    bp->world[0][x] = bp->world[bp->height-2][x];*/
  }

  for(y = 1; y < bp->height; ++y)
  {
    new_state = random() % RANDOM_LIFE == 0 ? 1 : 0;
    if(new_state != ((bp->world[y][1] >> 24) & 0x01))
      set_neighbors(1, y, new_state, mi);

    new_state = random() % RANDOM_LIFE == 0 ? 1 : 0;
    if(new_state != ((bp->world[y][bp->width] >> 24) & 0x01))
      set_neighbors(bp->width, y, new_state, mi);

    /*
    bp->world[y][1] = bp->world[y][bp->width-1] = random() % RANDOM_LIFE == 0 ? ALIVE_COLOR : DEAD;
    bp->world[y][bp->width-2] = bp->world[y][0] = random() % RANDOM_LIFE == 0 ? ALIVE_COLOR : DEAD;*/
  }
}


static void
update_world(ModeInfo *mi)
{
  ball_configuration *bp = &bps[MI_SCREEN(mi)];
  int x, y;


  randomize_world(mi);

 for(y = 0; y < bp->height+1; ++y)
      memcpy(bp->old_world[y], bp->world[y], bp->width * sizeof(int));


  for(x = 1; x < bp->width; ++x)
  {
    for(y = 1; y < bp->height; ++y)
    {
      int state = bp->old_world[y][x] >> 24;
      int alive_count = state >> 1;

      if(state & 0x01) {
        /* cell alive - turn off if not enough neighbors */
        if((alive_count > R[2] || alive_count < R[3]))
          set_neighbors(x, y, 0, mi);
      }
      else {
        /* cell dead - turn on if correct neighbors */
        if(alive_count >= R[0] && alive_count <= R[1])
          set_neighbors(x, y, 1, mi);
      }
    }
  }
}


ENTRYPOINT void 
init_ball (ModeInfo *mi)
{
  if (!bps) {
    bps = (ball_configuration *)
      calloc (MI_NUM_SCREENS(mi), sizeof (ball_configuration));
    if (!bps) {
      fprintf(stderr, "%s: out of memory\n", progname);
      exit(1);
    }
  }

  setup_world(mi);
}


ENTRYPOINT void
draw_ball (ModeInfo *mi)
{
  ball_configuration *bp = &bps[MI_SCREEN(mi)];
  Display *dpy = MI_DISPLAY(mi);
  Window window = MI_WINDOW(mi);

  time_t now = time(NULL);

  if (!bp->glx_context)
    return;

  bp->frames++;

  /* print FPS every 5 seconds */
  if(now > bp->last_update + 5)
  {
    bp->last_update = now;
    fprintf(stderr, "FPS: %f \n", bp->frames / 5.0f);
    bp->frames = 0;
  }


  update_world(mi);

  glXMakeCurrent(MI_DISPLAY(mi), MI_WINDOW(mi), *(bp->glx_context));

  /*glShadeModel(GL_SMOOTH);*/

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_NORMALIZE);
  glEnable(GL_CULL_FACE);

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable (GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

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

XSCREENSAVER_MODULE_2 ("DangerBall", dangerball, ball)

#endif /* USE_GL */