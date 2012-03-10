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


#define DEF_SPIN        "True"
#define DEF_WANDER      "True"
#define DEF_SPEED       "0.05"

#define SPIKE_FACES   12  /* how densely to render spikes */
#define SMOOTH_SPIKES True
#define SPHERE_SLICES 32  /* how densely to render spheres */
#define SPHERE_STACKS 16

#define WIDTH  1000
#define HEIGHT 1000
#define RANDOM_LIFE 3
#define ALIVE 255
#define DEAD 0


/* a new cell will appear if the number of neighbors (Sum)
 * is equal or more than r[0] and equal or less than r[1]
 * a cell will die if the Sum is more than r[2] or less than r[3] */
int R[4] = {3, 3, 3, 2};

typedef struct {
  GLXContext *glx_context;

  unsigned char world[2][WIDTH][HEIGHT][3];

  int cur;

  time_t last_update;
  int frames;
} ball_configuration;

static ball_configuration *bps = NULL;

static Bool do_spin;
static GLfloat speed;
static Bool do_wander;

static XrmOptionDescRec opts[] = {
  { "-spin",   ".spin",   XrmoptionNoArg, "True" },
  { "+spin",   ".spin",   XrmoptionNoArg, "False" },
  { "-speed",  ".speed",  XrmoptionSepArg, 0 },
  { "-wander", ".wander", XrmoptionNoArg, "True" },
  { "+wander", ".wander", XrmoptionNoArg, "False" }
};

static argtype vars[] = {
  {&do_spin,   "spin",   "Spin",   DEF_SPIN,   t_Bool},
  {&do_wander, "wander", "Wander", DEF_WANDER, t_Bool},
  {&speed,     "speed",  "Speed",  DEF_SPEED,  t_Float},
};

ENTRYPOINT ModeSpecOpt ball_opts = {countof(opts), opts, countof(vars), vars, NULL};

/* Window management, etc
 */
ENTRYPOINT void
reshape_ball (ModeInfo *mi, int width, int height)
{
  GLfloat h = (GLfloat) height / (GLfloat) width;

  glViewport (0, 0, (GLint) width, (GLint) height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective (30.0, 1/h, 1.0, 100.0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  gluLookAt( 0.0, 0.0, 5.0,
             0.0, 0.0, 0.0,
             0.0, 1.0, 0.0);

  glClear(GL_COLOR_BUFFER_BIT);
}

ENTRYPOINT Bool
ball_handle_event (ModeInfo *mi, XEvent *event)
{
  return False;
}

static void
randomize_world(ModeInfo *mi)
{
  ball_configuration *bp = &bps[MI_SCREEN(mi)];
  int x, y;

  /* randomize borders */
  for(x = 0; x < WIDTH; ++x)
  {
    /* set padding as well as the normal map */
    bp->world[bp->cur][x][1][2] = bp->world[bp->cur][x][WIDTH-1][2] = random() % RANDOM_LIFE == 0 ? ALIVE : DEAD;
    bp->world[bp->cur][x][WIDTH-2][2] = bp->world[bp->cur][x][0][2] = random() % RANDOM_LIFE == 0 ? ALIVE : DEAD;
  }

  for(y = 0; y < HEIGHT; ++y)
  {
    bp->world[bp->cur][1][y][2] = bp->world[bp->cur][HEIGHT-1][y][2] = random() % RANDOM_LIFE == 0 ? ALIVE : DEAD;
    bp->world[bp->cur][HEIGHT-2][y][2] = bp->world[bp->cur][0][y][2] = random() % RANDOM_LIFE == 0 ? ALIVE : DEAD;
  }
}


static int
max(int a, int b)
{
  return a > b ? a : b;
}

static void
update_world(ModeInfo *mi)
{
  ball_configuration *bp = &bps[MI_SCREEN(mi)];
  int x, y;
  int old = bp->cur;

  randomize_world(mi);

  bp->cur = (bp->cur + 1) % 2;

  for(x = 1; x < WIDTH-1; ++x)
  {
    for(y = 1; y < HEIGHT-1; ++y)
    {
      int alive_count = 0;
      int a, b;

      for(a = -1; a <= 1; ++a)
        for(b = -1; b <= 1; ++b)
          if(bp->world[old][x+a][y+b][2] == ALIVE)
            ++alive_count;

      if(bp->world[old][x][y][2] == ALIVE)
        --alive_count;

      if(alive_count >= R[0] && alive_count <= R[1])
        bp->world[bp->cur][x][y][2] = ALIVE;
      else if((alive_count > R[2] || alive_count < R[3]))
        bp->world[bp->cur][x][y][2] = max(bp->world[old][x][y][2] - 25, 0);
      else
        bp->world[bp->cur][x][y][2] = bp->world[old][x][y][2];
    }
  }
}


ENTRYPOINT void 
init_ball (ModeInfo *mi)
{
  ball_configuration *bp;

  if (!bps) {
    bps = (ball_configuration *)
      calloc (MI_NUM_SCREENS(mi), sizeof (ball_configuration));
    if (!bps) {
      fprintf(stderr, "%s: out of memory\n", progname);
      exit(1);
    }
  }

  bp = &bps[MI_SCREEN(mi)];

  bp->glx_context = init_GL(mi);

  reshape_ball (mi, MI_WIDTH(mi), MI_HEIGHT(mi));
}


ENTRYPOINT void
draw_ball (ModeInfo *mi)
{
  ball_configuration *bp = &bps[MI_SCREEN(mi)];
  Display *dpy = MI_DISPLAY(mi);
  Window window = MI_WINDOW(mi);

  if (!bp->glx_context)
    return;

  bp->frames++;

  time_t now = time(NULL);
  /* print FPS every 5 seconds */
  if(now > bp->last_update + 5)
  {
    bp->last_update = now;
    fprintf(stderr, "FPS: %f \n", bp->frames / 5.0f);
    bp->frames = 0;
  }


  update_world(mi);

  glXMakeCurrent(MI_DISPLAY(mi), MI_WINDOW(mi), *(bp->glx_context));

  glShadeModel(GL_SMOOTH);

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
      WIDTH,
      HEIGHT,
      0,
      GL_RGB,
      GL_UNSIGNED_BYTE,
      &(bp->world[bp->cur][0][0][0])
  );

  glBegin(GL_QUADS);
      glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0, -1.0);
      glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0, -1.0);
      glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0,  1.0);
      glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0,  1.0);
  glEnd();

  glFlush();
  glFinish();

  glXSwapBuffers(dpy, window);
}

XSCREENSAVER_MODULE_2 ("DangerBall", dangerball, ball)

#endif /* USE_GL */