#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "anim.h"
#include "cairo_util.h"
#include "log.h"

static bool seeded = false;

static inline int randrange(int min, int max) {
	if (!seeded)
	{
		unsigned int data = 0x13832184;
		for (int i=0; i<16; i++)
		{
			struct timespec ts;
			clock_gettime(CLOCK_MONOTONIC, &ts);
			data ^= ts.tv_nsec;

			data = (data << 10) ^ (data >> 10) ^ (data >> 22) ^ (data << 22);
		}
		srand(data);
		seeded = true;
	}

	const int range = max - min;
	const int mr = RAND_MAX - (RAND_MAX % range);

	int r = rand();
	while (r >= mr)
	  r = rand();

	return min + r % range;
}

static inline int veclen(int x, int y) {
	return x*x + y*y;
}

struct anim_context {
	int cur_x, cur_y;	// Where the BIRD is now
	int nxt_x, nxt_y;	// Where the BIRD is heading
	enum { BIRD_LEFT, BIRD_RIGHT } nxt_foot;
	int nxt_pos;		// Next trace position in the list
	struct anim_config {
		int total_traces;
		int min_velocity;
		int max_velocity;
		int min_accel;
		int max_accel;
		int line_width;
		int trace_len;
		int decay_limit;
	} cf;
	struct trace {
		int x, y;		// Trace root position
		float angle;		// Trace rotation
	} traces[0];
};

static bool check_velocity(const struct anim_context *actx,
	const int dx, const int dy,
	const int width, const int height)
{
	/* Check maximum velocity */
	if (veclen(dx, dy) > actx->cf.max_velocity * actx->cf.max_velocity)
		return false;

	/* Check minimum velocity */
	if (veclen(dx, dy) < actx->cf.min_velocity * actx->cf.min_velocity)
		return false;

	/* Check breaking distance */
	int brkx = (abs(dx) * (abs(dx) - actx->cf.min_accel)) / (- actx->cf.min_accel * 2);
	int brky = (abs(dy) * (abs(dy) - actx->cf.min_accel)) / (- actx->cf.min_accel * 2);

//	printf("Check velocity %d %d -> brk %d %d\n", dx, dy, brkx, brky);

	/* Would run away */
	if (brkx * (dx / abs(dx)) + actx->cur_x > width)
		return false;

	if (brkx * (dx / abs(dx)) + actx->cur_x < 0)
		return false;

	if (brky * (dy / abs(dy)) + actx->cur_y > height)
		return false;

	if (brky * (dy / abs(dy)) + actx->cur_y < 0)
		return false;

	return true;
}

struct anim_context *render_anim(cairo_t *cr, struct anim_context *actx, int width, int height)
{
//	printf("Render ... ");
	const struct anim_config acfgl = {
		.total_traces = 16,
		.max_velocity = 100,
		.min_velocity = 5,
		.max_accel = 20,
		.min_accel = -20,
		.line_width = 2,
		.trace_len = 40,
		.decay_limit = 4,
	}, *acfg = &acfgl;

	if (!actx)
	{
		/* Allocate context */
		int sz = sizeof *actx + acfg->total_traces * sizeof actx->traces[0];
		actx = malloc(sz);

		/* Generate initial position */
		*actx = (struct anim_context) {
			.cur_x = randrange(width/4, 3*width/4),
			.cur_y = randrange(height/4, 3*height/4),
			.nxt_foot = BIRD_LEFT,
			.cf = *acfg,
		};

		/* Generate initial velocity */
		int dx, dy, check = 0;
		do {
			if (check++ > 128)
			{
				free(actx);
				return render_anim(cr, NULL, width, height);
			}
			dx = randrange(-acfg->max_velocity, acfg->max_velocity + 1);
			dy = randrange(-acfg->max_velocity, acfg->max_velocity + 1);
		} while (!check_velocity(actx, dx, dy, width, height));

		/* Write the next position */
		actx->nxt_x = actx->cur_x + dx;
		actx->nxt_y = actx->cur_y + dy;
	}

	/* Switch legs */
	float foot = 0;
	switch (actx->nxt_foot) {
		case BIRD_LEFT:
			actx->nxt_foot = BIRD_RIGHT;
			foot = 20 * 3.14 / 180;
			break;
		case BIRD_RIGHT:
			actx->nxt_foot = BIRD_LEFT;
			foot = -20 * 3.14 / 180;
			break;
	}

	/* Write next trace */
	actx->traces[actx->nxt_pos % actx->cf.total_traces] = (struct trace) {
		.x = actx->cur_x,
		.y = actx->cur_y,
		.angle = atan2f(actx->nxt_y - actx->cur_y, actx->nxt_x - actx->cur_x) + foot,
	};

	/* Next next trace array position */
	actx->nxt_pos++;

	/* Current velocity */
	int cx = actx->nxt_x - actx->cur_x;
	int cy = actx->nxt_y - actx->cur_y;

	/* Move */
	actx->cur_x = actx->nxt_x;
	actx->cur_y = actx->nxt_y;

	/* Update velocity */
	int dx, dy, check = 0;
	do {
		if (check++ > 128)
		{
			free(actx);
			return render_anim(cr, NULL, width, height);
		}
		dx = cx + randrange(
			acfg->min_accel / (3*(actx->cur_x < width / 4) + 1),
			(acfg->max_accel+1) / (3*(actx->cur_x > 3*width / 4) + 1)
			);
		dy = cy + randrange(
			acfg->min_accel / (3*(actx->cur_y < height / 4) + 1),
			(acfg->max_accel+1) / (3*(actx->cur_y > 3*height / 4) + 1));
	} while (!check_velocity(actx, dx, dy, width, height));

	/* Write the next position */
	actx->nxt_x = actx->cur_x + dx;
	actx->nxt_y = actx->cur_y + dy;

	/* Draw the background */
	cairo_set_source_rgb(cr, 0.2000, 0.1500, 0);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_fill(cr);

	/* Draw the traces */
//	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_set_source_rgb(cr, 0.8477, 0.7031, 0.1289);
	cairo_set_line_width(cr, acfg->line_width);

	int mp = actx->nxt_pos - actx->cf.total_traces;
	if (mp < 0) mp = 0;
	for (int ii = mp; ii < actx->nxt_pos; ii++)
	{
		double alpha = 1;
		if (ii - mp < actx->cf.decay_limit)
			alpha = 1.0 / (1 << (actx->cf.decay_limit - (ii - mp)));

		cairo_set_source_rgba(cr, 0.8477, 0.7031, 0.1289, alpha);
//		cairo_set_source_rgba(cr, 0, 0, 0, alpha);
		int i = ii % actx->cf.total_traces;

		/*
		printf("%d at %d,%d (%.1lf) ... ",
		    i, actx->traces[i].x, actx->traces[i].y,
		    actx->traces[i].angle * 180 / 3.14);
		    */

		cairo_save(cr);
		cairo_translate(cr, actx->traces[i].x, actx->traces[i].y);
		cairo_rotate(cr, actx->traces[i].angle);
		cairo_move_to(cr, 0, 0);
		int tl = acfg->trace_len;
		cairo_line_to(cr, tl, 0);
		cairo_move_to(cr, (tl * 3) / 5, 0);
		cairo_line_to(cr, (tl * 23) / 25, (tl * 6) / 25);
		cairo_move_to(cr, (tl * 3) / 5, 0);
		cairo_line_to(cr, (tl * 23) / 25, -(tl * 6) / 25);
		cairo_stroke(cr);
		cairo_restore(cr);
	}

//	printf("\n");
	return actx;
}

void anim_done(struct anim_context *actx)
{
	free(actx);
}
