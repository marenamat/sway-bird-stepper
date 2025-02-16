#ifndef _SWAY_BIRD_ANIM_H
#define _SWAY_BIRD_ANIM_H

#include "cairo_util.h"

struct anim_context *render_anim(cairo_t *, struct anim_context *, int, int);
void anim_done(struct anim_context *);

#endif
