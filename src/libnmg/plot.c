/*                      N M G _ P L O T . C
 * BRL-CAD
 *
 * Copyright (c) 1993-2025 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @addtogroup nmg */
/** @{ */
/** @file primitives/nmg/nmg_plot.c
 *
 * This file contains routines that create VLISTs and UNIX-plot files.
 * Some routines are essential to the MGED interface, some are more
 * for diagnostic and visualization purposes.
 *
 * There are several distinct families:
 *
 * nmg_ENTITY_to_vlist - Wireframes & polygons.  For MGED "ev".
 * nmg_pl_ENTITY       - Fancy edgeuse drawing, to plot file.
 * nmg_vlblock_ENTITY  - Fancy edgeuse drawing, into vlblocks.
 * show_broken_ENTITY  - Graphical display of classifier results.
 * ...as well as assorted wrappers for debugging use.
 *
 * In the interest of having only a single way of creating the fancy
 * drawings, the code is migrating to creating everything first as
 * VLBLOCKS, and converting that to UNIX-plot files or other formats
 * as appropriate.
 */
/** @} */

#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "bio.h"

#include "vmath.h"
#include "bu/malloc.h"
#include "bv/plot3.h"
#include "bv/vlist.h"
#include "nmg.h"

#define US_DELAY 10 /* Additional delay between frames */

double nmg_eue_dist = 0.05;

/************************************************************************
 *									*
 *		NMG to VLIST routines, for MGED "ev" command.		*
 * XXX should take a flags array, to ensure each item done only once!   *
 *									*
 ************************************************************************/

/**
 * Plot a single vertexuse
 */
void
nmg_vu_to_vlist(struct bu_list *vhead, const struct vertexuse *vu, struct bu_list *vlfree)
{
    struct vertex *v;
    register struct vertex_g *vg;

    BU_CK_LIST_HEAD(vhead);
    NMG_CK_VERTEXUSE(vu);
    v = vu->v_p;
    NMG_CK_VERTEX(v);
    vg = v->vg_p;
    if (vg) {
	/* Only thing in this shell is a point */
	NMG_CK_VERTEX_G(vg);
	BV_ADD_VLIST(vlfree, vhead, vg->coord, BV_VLIST_LINE_MOVE);
	BV_ADD_VLIST(vlfree, vhead, vg->coord, BV_VLIST_LINE_DRAW);
    }
}


/**
 * Plot a list of edgeuses.  The last edge is joined back to the first.
 */
void
nmg_eu_to_vlist(struct bu_list *vhead, const struct bu_list *eu_hd, struct bu_list *vlfree)
{
    struct edgeuse *eu;
    struct edgeuse *eumate;
    struct vertexuse *vu;
    struct vertexuse *vumate;
    register struct vertex_g *vg;
    register struct vertex_g *vgmate;

    BU_CK_LIST_HEAD(vhead);

    /* Consider all the edges in the wire edge list */
    for (BU_LIST_FOR(eu, edgeuse, eu_hd)) {
	/* This wire edge runs from vertex to mate's vertex */
	NMG_CK_EDGEUSE(eu);
	vu = eu->vu_p;
	NMG_CK_VERTEXUSE(vu);
	NMG_CK_VERTEX(vu->v_p);
	vg = vu->v_p->vg_p;

	eumate = eu->eumate_p;
	NMG_CK_EDGEUSE(eumate);
	vumate = eumate->vu_p;
	NMG_CK_VERTEXUSE(vumate);
	NMG_CK_VERTEX(vumate->v_p);
	vgmate = vumate->v_p->vg_p;

	if (!vg || !vgmate) {
	    bu_log("nmg_eu_to_vlist() no vg or mate?\n");
	    continue;
	}
	NMG_CK_VERTEX_G(vg);
	NMG_CK_VERTEX_G(vgmate);

	BV_ADD_VLIST(vlfree, vhead, vg->coord, BV_VLIST_LINE_MOVE);
	BV_ADD_VLIST(vlfree, vhead, vgmate->coord, BV_VLIST_LINE_DRAW);
    }
}


/**
 * Plot a single loopuse into a bv_vlist chain headed by vhead.
 *
 * Needs to be able to handle both linear edges and cnurb edges.
 */
void
nmg_lu_to_vlist(struct bu_list *vhead, const struct loopuse *lu, int poly_markers, const vectp_t normal, struct bu_list *vlfree)


/* bit vector! */

{
    const struct edgeuse *eu;
    const struct vertexuse *vu;
    const struct vertex *v;
    register const struct vertex_g *vg;
    const struct vertex_g *first_vg;
    const struct vertexuse *first_vu;
    int isfirst;
    point_t centroid;
    int npoints;

    BU_CK_LIST_HEAD(vhead);

    NMG_CK_LOOPUSE(lu);
    if (BU_LIST_FIRST_MAGIC(&lu->down_hd)==NMG_VERTEXUSE_MAGIC) {
	/* Process a loop of a single vertex */
	vu = BU_LIST_FIRST(vertexuse, &lu->down_hd);
	nmg_vu_to_vlist(vhead, vu, vlfree);
	return;
    }

    /* Consider all the edges in the loop */
    isfirst = 1;
    first_vg = (struct vertex_g *)0;
    first_vu = (struct vertexuse *)0;
    npoints = 0;
    VSETALL(centroid, 0);
    for (BU_LIST_FOR(eu, edgeuse, &lu->down_hd)) {

	/* Consider this edge */
	NMG_CK_EDGEUSE(eu);
	vu = eu->vu_p;
	NMG_CK_VERTEXUSE(vu);
	v = vu->v_p;
	NMG_CK_VERTEX(v);
	vg = v->vg_p;
	if (!vg) {
	    continue;
	}
	NMG_CK_VERTEX_G(vg);
	VADD2(centroid, centroid, vg->coord);
	npoints++;
	if (isfirst) {
	    if (poly_markers & NMG_VLIST_STYLE_POLYGON) {
		/* Insert a "start polygon, normal" marker */
		BV_ADD_VLIST(vlfree, vhead, normal, BV_VLIST_POLY_START);
		if (poly_markers & NMG_VLIST_STYLE_USE_VU_NORMALS
		    && vu->a.magic_p) {
		    BV_ADD_VLIST(vlfree, vhead,
				 vu->a.plane_p->N,
				 BV_VLIST_POLY_VERTNORM);
		}
		BV_ADD_VLIST(vlfree, vhead, vg->coord, BV_VLIST_POLY_MOVE);
	    } else {
		/* move */
		BV_ADD_VLIST(vlfree, vhead, vg->coord, BV_VLIST_LINE_MOVE);
	    }
	    isfirst = 0;
	    first_vg = vg;
	    first_vu = vu;
	} else {
	    if (poly_markers & NMG_VLIST_STYLE_POLYGON) {
		if (poly_markers & NMG_VLIST_STYLE_USE_VU_NORMALS
		    && vu->a.magic_p) {
		    BV_ADD_VLIST(vlfree, vhead,
				 vu->a.plane_p->N,
				 BV_VLIST_POLY_VERTNORM);
		}
		BV_ADD_VLIST(vlfree, vhead, vg->coord, BV_VLIST_POLY_DRAW);
	    } else {
		/* Draw */
		BV_ADD_VLIST(vlfree, vhead, vg->coord, BV_VLIST_LINE_DRAW);
	    }
	}

	if (!eu->g.magic_p)
	    continue;

	/* If cnurb edgeuse, draw points interior to the curve here */
	if (*eu->g.magic_p != NMG_EDGE_G_CNURB_MAGIC) continue;

	/* XXX only use poly markers when face is planar, not snurb */
	nmg_cnurb_to_vlist(vhead, eu, 10,
			   (poly_markers & NMG_VLIST_STYLE_POLYGON) ?
			   BV_VLIST_POLY_DRAW : BV_VLIST_LINE_DRAW, vlfree);
    }

    /* Draw back to the first vertex used */
    if (!isfirst && first_vg) {
	if (poly_markers & NMG_VLIST_STYLE_POLYGON) {
	    /* Draw, end polygon */
	    if (poly_markers & NMG_VLIST_STYLE_USE_VU_NORMALS
		&& first_vu->a.magic_p) {
		BV_ADD_VLIST(vlfree, vhead,
			     first_vu->a.plane_p->N,
			     BV_VLIST_POLY_VERTNORM);
	    }
	    BV_ADD_VLIST(vlfree, vhead, first_vg->coord, BV_VLIST_POLY_END);
	} else {
	    /* Draw */
	    BV_ADD_VLIST(vlfree, vhead, first_vg->coord, BV_VLIST_LINE_DRAW);
	}
    }
    if ((poly_markers  & NMG_VLIST_STYLE_VISUALIZE_NORMALS) && npoints > 2) {
	/* Draw surface normal as a little vector */
	double f;
	vect_t tocent;
	point_t tip;
	struct faceuse *fu;
	struct face *fp;

	if (*lu->up.magic_p == NMG_FACEUSE_MAGIC) {
	    fu = lu->up.fu_p;
	    NMG_CK_FACEUSE(fu);

	    fp = fu->f_p;
	} else
	    fp = (struct face *)NULL;

	f = 1.0 / npoints;
	VSCALE(centroid, centroid, f);
	VSUB2(tocent, first_vg->coord, centroid);
	f = MAGNITUDE(tocent) * 0.5;
	if (fp) {
	    if (*fp->g.magic_p != NMG_FACE_G_SNURB_MAGIC) {
		/* snurb normals are plotted in nmg_snurb_fu_to_vlist() */
		BV_ADD_VLIST(vlfree, vhead, centroid, BV_VLIST_LINE_MOVE);
		VJOIN1(tip, centroid, f, normal);
		BV_ADD_VLIST(vlfree, vhead, tip, BV_VLIST_LINE_DRAW);
	    }
	}

	/* For any vertexuse attributes with normals, draw them too */
	for (BU_LIST_FOR(eu, edgeuse, &lu->down_hd)) {
	    struct vertexuse_a_plane *vua;
	    /* Consider this edge */
	    vu = eu->vu_p;
	    if (!vu->a.magic_p || *vu->a.magic_p != NMG_VERTEXUSE_A_PLANE_MAGIC) continue;
	    vua = vu->a.plane_p;
	    v = vu->v_p;
	    vg = v->vg_p;
	    if (!vg) continue;
	    NMG_CK_VERTEX_G(vg);
	    BV_ADD_VLIST(vlfree, vhead, vg->coord, BV_VLIST_LINE_MOVE);
	    VJOIN1(tip, vg->coord, f, vua->N);
	    BV_ADD_VLIST(vlfree, vhead, tip, BV_VLIST_LINE_DRAW);
	}
    }
}


void
nmg_snurb_fu_to_vlist(struct bu_list *vhead, const struct faceuse *fu, int poly_markers, struct bu_list *vlfree)
{
    struct face_g_snurb *fg;

    BU_CK_LIST_HEAD(vhead);

    NMG_CK_FACEUSE(fu);
    NMG_CK_FACE(fu->f_p);
    fg = fu->f_p->g.snurb_p;
    NMG_CK_FACE_G_SNURB(fg);

    /* XXX For now, draw the whole surface, not just the interior */
    nmg_snurb_to_vlist(vhead, fg, 10, vlfree);

    if (poly_markers & NMG_VLIST_STYLE_VISUALIZE_NORMALS) {
	fastf_t f;
	point_t uv_centroid;
	point_t mid_srf;
	point_t corner;
	vect_t fu_norm;
	vect_t tocent;
	point_t tip;

	uv_centroid[0] = (fg->u.knots[fg->u.k_size-1] + fg->u.knots[0])/2.0;
	uv_centroid[1] = (fg->v.knots[fg->v.k_size-1] + fg->v.knots[0])/2.0;
	uv_centroid[2] = 1.0;

	nmg_snurb_fu_get_norm(fu, uv_centroid[0], uv_centroid[1], fu_norm);
	nmg_snurb_fu_eval(fu, uv_centroid[0], uv_centroid[1], mid_srf);

	nmg_snurb_fu_eval(fu, fg->u.knots[0], fg->v.knots[0], corner);
	VSUB2(tocent, corner, mid_srf);
	f = MAGNITUDE(tocent) * 0.5;

	BV_ADD_VLIST(vlfree, vhead, mid_srf, BV_VLIST_LINE_MOVE);
	VJOIN1(tip, mid_srf, f, fu_norm);
	BV_ADD_VLIST(vlfree, vhead, tip, BV_VLIST_LINE_DRAW);
    }
}


/**
 * Plot the entire contents of a shell.
 *
 * poly_markers =
 * 0 for vectors
 * 1 for polygons
 * 2 for polygons and surface normals drawn with vectors
 */
void
nmg_s_to_vlist(struct bu_list *vhead, const struct shell *s, int poly_markers, struct bu_list *vlfree)
{
    struct faceuse *fu;
    struct face_g_plane *fg;
    register struct loopuse *lu;
    vect_t normal;

    BU_CK_LIST_HEAD(vhead);
    NMG_CK_SHELL(s);

    /* faces */
    for (BU_LIST_FOR(fu, faceuse, &s->fu_hd)) {
	vect_t n;

	/* Consider this face */
	NMG_CK_FACEUSE(fu);
	if (fu->orientation != OT_SAME) continue;
	NMG_CK_FACE(fu->f_p);

	if (fu->f_p->g.magic_p && *fu->f_p->g.magic_p == NMG_FACE_G_SNURB_MAGIC) {

	    if (!(poly_markers & NMG_VLIST_STYLE_NO_SURFACES))
		nmg_snurb_fu_to_vlist(vhead, fu, poly_markers, vlfree);

	    VSET(n, 1, 0, 0);	/* sanity */
	    for (BU_LIST_FOR(lu, loopuse, &fu->lu_hd)) {
		nmg_lu_to_vlist(vhead, lu, poly_markers, n, vlfree);
	    }
	    continue;
	}

	/* Handle planar faces directly */
	fg = fu->f_p->g.plane_p;
	NMG_CK_FACE_G_PLANE(fg);
	NMG_GET_FU_NORMAL(n, fu);
	for (BU_LIST_FOR(lu, loopuse, &fu->lu_hd)) {
	    nmg_lu_to_vlist(vhead, lu, poly_markers, n, vlfree);
	}
    }

    /* wire loops.  poly_markers=0 so wires are always drawn as vectors */
    VSETALL(normal, 0);
    for (BU_LIST_FOR(lu, loopuse, &s->lu_hd)) {
	nmg_lu_to_vlist(vhead, lu, 0, normal, vlfree);
    }

    /* wire edges */
    nmg_eu_to_vlist(vhead, &s->eu_hd, vlfree);

    /* single vertices */
    if (s->vu_p) {
	nmg_vu_to_vlist(vhead, s->vu_p, vlfree);
    }
}


void
nmg_r_to_vlist(struct bu_list *vhead, const struct nmgregion *r, int poly_markers, struct bu_list *vlfree)
{
    register struct shell *s;

    BU_CK_LIST_HEAD(vhead);
    NMG_CK_REGION(r);
    for (BU_LIST_FOR(s, shell, &r->s_hd)) {
	nmg_s_to_vlist(vhead, s, poly_markers, vlfree);
    }
}


void
nmg_m_to_vlist(struct bu_list *vhead, struct model *m, int poly_markers, struct bu_list *vlfree)
{
    register struct nmgregion *r;

    BU_CK_LIST_HEAD(vhead);
    NMG_CK_MODEL(m);
    for (BU_LIST_FOR(r, nmgregion, &m->r_hd)) {
	NMG_CK_REGION(r);
	nmg_r_to_vlist(vhead, r, poly_markers, vlfree);
    }
}


/************************************************************************
 *									*
 *		Routines to lay out the fancy edgeuse drawings		*
 *									*
 ************************************************************************/

#define LEE_DIVIDE_TOL (1.0e-5)	/* sloppy tolerance */


/**
 * Given an edgeuse, find an offset for its vertexuse which will place
 * it "above" and "inside" the area of the face.
 *
 * The point will be offset inwards along the edge slightly, to avoid
 * obscuring the vertex, and will be offset off the face (in the
 * direction of the face normal) slightly, to avoid obscuring the edge
 * itself.
 */
void
nmg_offset_eu_vert(point_t base, const struct edgeuse *eu, const vect_t face_normal, int tip)
{
    struct edgeuse *prev_eu;
    const struct edgeuse *this_eu;
    vect_t prev_vec;	/* from cur_pt to prev_pt */
    vect_t eu_vec;		/* from cur_pt to next_pt */
    vect_t prev_left;
    vect_t eu_left;
    vect_t delta_vec;	/* offset vector from vertex */
    struct vertex_g *this_vg, *mate_vg, *prev_vg;

    memset((char *)delta_vec, 0, sizeof(vect_t));
    prev_eu = BU_LIST_PPREV_CIRC(edgeuse, eu);
    this_eu = eu;

    NMG_CK_EDGEUSE(this_eu);
    NMG_CK_VERTEXUSE(this_eu->vu_p);
    NMG_CK_VERTEX(this_eu->vu_p->v_p);
    this_vg = this_eu->vu_p->v_p->vg_p;
    NMG_CK_VERTEX_G(this_vg);

    NMG_CK_EDGEUSE(this_eu->eumate_p);
    NMG_CK_VERTEXUSE(this_eu->eumate_p->vu_p);
    NMG_CK_VERTEX(this_eu->eumate_p->vu_p->v_p);
    mate_vg = this_eu->eumate_p->vu_p->v_p->vg_p;
    NMG_CK_VERTEX_G(mate_vg);

    NMG_CK_EDGEUSE(prev_eu);
    NMG_CK_VERTEXUSE(prev_eu->vu_p);
    NMG_CK_VERTEX(prev_eu->vu_p->v_p);
    prev_vg = prev_eu->vu_p->v_p->vg_p;
    NMG_CK_VERTEX_G(prev_vg);

    /* get "left" vector for edgeuse */
    VSUB2(eu_vec, mate_vg->coord, this_vg->coord);
    VUNITIZE(eu_vec);
    VCROSS(eu_left, face_normal, eu_vec);


    /* get "left" vector for previous edgeuse */
    VSUB2(prev_vec, this_vg->coord, prev_vg->coord);
    VUNITIZE(prev_vec);
    VCROSS(prev_left, face_normal, prev_vec);

    /* get "delta" vector to apply to vertex */
    VADD2(delta_vec, prev_left, eu_left);

    if (MAGSQ(delta_vec) > VDIVIDE_TOL) {
	VUNITIZE(delta_vec);
	VJOIN2(base, this_vg->coord,
	       (nmg_eue_dist*1.3), delta_vec,
	       (nmg_eue_dist*0.8), face_normal);

    } else if (tip) {
	VJOIN2(base, this_vg->coord,
	       (nmg_eue_dist*1.3), prev_left,
	       (nmg_eue_dist*0.8), face_normal);
    } else {
	VJOIN2(base, this_vg->coord,
	       (nmg_eue_dist*1.3), eu_left,
	       (nmg_eue_dist*0.8), face_normal);
    }
}


/**
 * Get the two (offset and shrunken) endpoints that represent an
 * edgeuse.  Return the base point, and a point 60% along the way
 * towards the other end.
 */
static void nmg_eu_coords(const struct edgeuse *eu, fastf_t *base, fastf_t *tip60)
{
    point_t tip = VINIT_ZERO;

    NMG_CK_EDGEUSE(eu);

    if (*eu->up.magic_p == NMG_SHELL_MAGIC ||
	(*eu->up.magic_p == NMG_LOOPUSE_MAGIC &&
	 *eu->up.lu_p->up.magic_p == NMG_SHELL_MAGIC)) {
	/* Wire edge, or edge in wire loop */
	VMOVE(base, eu->vu_p->v_p->vg_p->coord);
	NMG_CK_EDGEUSE(eu->eumate_p);
	VMOVE(tip, eu->eumate_p->vu_p->v_p->vg_p->coord);
    } else if (*eu->up.magic_p == NMG_LOOPUSE_MAGIC &&
	       *eu->up.lu_p->up.magic_p == NMG_FACEUSE_MAGIC) {
	/* Loop in face */
	struct faceuse *fu;
	vect_t face_normal;

	fu = eu->up.lu_p->up.fu_p;
	NMG_GET_FU_NORMAL(face_normal, fu);


	nmg_offset_eu_vert(base, eu, face_normal, 0);
	nmg_offset_eu_vert(tip, BU_LIST_PNEXT_CIRC(edgeuse, eu),
			   face_normal, 1);

    } else
	bu_bomb("nmg_eu_coords: bad edgeuse up. What's going on?\n");

    VBLEND2(tip60, 0.4, base, 0.6, tip);
}


/**
 * Find location for 80% tip on edgeuse's radial edgeuse.
 */
static void nmg_eu_radial(const struct edgeuse *eu, fastf_t *tip)
{
    point_t b2 = VINIT_ZERO;
    point_t t2 = VINIT_ZERO;

    NMG_CK_EDGEUSE(eu->radial_p);
    NMG_CK_VERTEXUSE(eu->radial_p->vu_p);
    NMG_CK_VERTEX(eu->radial_p->vu_p->v_p);
    NMG_CK_VERTEX_G(eu->radial_p->vu_p->v_p->vg_p);

    nmg_eu_coords(eu->radial_p, b2, t2);

    /* find point 80% along other eu where radial pointer should touch */
    VCOMB2(tip, 0.8, t2, 0.2, b2);
}


/**
 * Return the base of the next edgeuse
 */
static void nmg_eu_next_base(const struct edgeuse *eu, fastf_t *next_base)
{
    point_t t2;
    register struct edgeuse *nexteu;

    NMG_CK_EDGEUSE(eu);
    nexteu = BU_LIST_PNEXT_CIRC(edgeuse, eu);
    NMG_CK_EDGEUSE(nexteu);
    NMG_CK_VERTEXUSE(nexteu->vu_p);
    NMG_CK_VERTEX(nexteu->vu_p->v_p);
    NMG_CK_VERTEX_G(nexteu->vu_p->v_p->vg_p);

    nmg_eu_coords(nexteu, next_base, t2);
}


/************************************************************************
 *									*
 *		NMG to UNIX-Plot routines, for visualization		*
 *  XXX These should get replaced with calls to the vlblock routines	*
 *									*
 ************************************************************************/

void
nmg_pl_v(FILE *fp, const struct vertex *v, long *b)
{
    pointp_t p;
    static char label[128];

    NMG_INDEX_RETURN_IF_SET_ELSE_SET(b, v->index);

    NMG_CK_VERTEX(v);
    NMG_CK_VERTEX_G(v->vg_p);
    p = v->vg_p->coord;

    pl_color(fp, 255, 255, 255);
    if (nmg_debug & NMG_DEBUG_LABEL_PTS) {
	(void)sprintf(label, "%g %g %g", p[0], p[1], p[2]);
	pdv_3move(fp, p);
	pl_label(fp, label);
    }
    pdv_3point(fp, p);
}


void
nmg_pl_e(FILE *fp, const struct edge *e, long *b, int red, int green, int blue)
{
    pointp_t p0, p1;
    point_t end0, end1;
    vect_t v;

    NMG_INDEX_RETURN_IF_SET_ELSE_SET(b, e->index);

    NMG_CK_EDGEUSE(e->eu_p);
    NMG_CK_VERTEXUSE(e->eu_p->vu_p);
    NMG_CK_VERTEX(e->eu_p->vu_p->v_p);
    NMG_CK_VERTEX_G(e->eu_p->vu_p->v_p->vg_p);
    p0 = e->eu_p->vu_p->v_p->vg_p->coord;

    NMG_CK_VERTEXUSE(e->eu_p->eumate_p->vu_p);
    NMG_CK_VERTEX(e->eu_p->eumate_p->vu_p->v_p);
    NMG_CK_VERTEX_G(e->eu_p->eumate_p->vu_p->v_p->vg_p);
    p1 = e->eu_p->eumate_p->vu_p->v_p->vg_p->coord;

    /* leave a little room between the edge endpoints and the vertex
     * compute endpoints by forming a vector between verts, scale
     * vector and modify points
     */
    VSUB2SCALE(v, p1, p0, 0.95);
    VADD2(end0, p0, v);
    VSUB2(end1, p1, v);

    pl_color(fp, red, green, blue);
    pdv_3line(fp, end0, end1);

    nmg_pl_v(fp, e->eu_p->vu_p->v_p, b);
    nmg_pl_v(fp, e->eu_p->eumate_p->vu_p->v_p, b);
}


void
nmg_pl_eu(FILE *fp, const struct edgeuse *eu, long *b, int red, int green, int blue)
{
    point_t base, tip;
    point_t radial_tip;
    point_t next_base;

    NMG_CK_EDGEUSE(eu);
    NMG_CK_EDGE(eu->e_p);
    NMG_CK_VERTEXUSE(eu->vu_p);
    NMG_CK_VERTEX(eu->vu_p->v_p);
    NMG_CK_VERTEX_G(eu->vu_p->v_p->vg_p);

    NMG_CK_VERTEXUSE(eu->eumate_p->vu_p);
    NMG_CK_VERTEX(eu->eumate_p->vu_p->v_p);
    NMG_CK_VERTEX_G(eu->eumate_p->vu_p->v_p->vg_p);

    NMG_INDEX_RETURN_IF_SET_ELSE_SET(b, eu->index);

    nmg_pl_e(fp, eu->e_p, b, red, green, blue);

    if (*eu->up.magic_p == NMG_LOOPUSE_MAGIC &&
	*eu->up.lu_p->up.magic_p == NMG_FACEUSE_MAGIC) {

	nmg_eu_coords(eu, base, tip);
	if (eu->up.lu_p->up.fu_p->orientation == OT_SAME)
	    red += 50;
	else if (eu->up.lu_p->up.fu_p->orientation == OT_OPPOSITE)
	    red -= 50;
	else
	    red = green = blue = 255;

	pl_color(fp, red, green, blue);
	pdv_3line(fp, base, tip);

	nmg_eu_radial(eu, radial_tip);
	pl_color(fp, red, green-20, blue);
	pdv_3line(fp, tip, radial_tip);

	pl_color(fp, 0, 100, 0);
	nmg_eu_next_base(eu, next_base);
	pdv_3line(fp, tip, next_base);

/*** presently unused ***
     nmg_eu_last(eu, last_tip);
     pl_color(fp, 0, 200, 0);
     pdv_3line(fp, base, last_tip);
****/
    }
}


void
nmg_pl_lu(FILE *fp, const struct loopuse *lu, long *b, int red, int green, int blue, struct bu_list *vlfree)
{
    struct bv_vlblock *vbp = bv_vlblock_init(vlfree, 32);

    nmg_vlblock_lu(vbp, lu, b, red, green, blue, 0, vlfree);
    bv_plot_vlblock(fp, vbp);
    bv_vlblock_free(vbp);
}


void
nmg_pl_fu(FILE *fp, const struct faceuse *fu, long *b, int red, int green, int blue, struct bu_list *vlfree)
{
    struct loopuse *lu;
    struct bv_vlblock *vbp;

    NMG_CK_FACEUSE(fu);
    NMG_INDEX_RETURN_IF_SET_ELSE_SET(b, fu->index);

    vbp = bv_vlblock_init(vlfree, 32);

    for (BU_LIST_FOR(lu, loopuse, &fu->lu_hd)) {
	nmg_vlblock_lu(vbp, lu, b, red, green, blue, 1, vlfree);
    }

    bv_plot_vlblock(fp, vbp);
    bv_vlblock_free(vbp);
}


/**
 * Note that "b" should probably be defined a level higher, to reduce
 * malloc/free calls when plotting multiple shells.
 */
void
nmg_pl_s(FILE *fp, const struct shell *s, struct bu_list *vlfree)
{
    struct bv_vlblock *vbp = bv_vlblock_init(vlfree, 32);

    nmg_vlblock_s(vbp, s, 0, vlfree);
    bv_plot_vlblock(fp, vbp);
    bv_vlblock_free(vbp);
}


void
nmg_pl_shell(FILE *fp, const struct shell *s, int fancy, struct bu_list *vlfree)
{
    struct bv_vlblock *vbp = bv_vlblock_init(vlfree, 32);

    nmg_vlblock_s(vbp, s, fancy, vlfree);
    bv_plot_vlblock(fp, vbp);
    bv_vlblock_free(vbp);
}


void
nmg_pl_r(FILE *fp, const struct nmgregion *r, struct bu_list *vlfree)
{
    struct bv_vlblock *vbp = bv_vlblock_init(vlfree, 32);

    nmg_vlblock_r(vbp, r, 0, vlfree);
    bv_plot_vlblock(fp, vbp);
    bv_vlblock_free(vbp);
}


void
nmg_pl_m(FILE *fp, const struct model *m, struct bu_list *vlfree)
{
    struct bv_vlblock *vbp = bv_vlblock_init(vlfree, 32);

    nmg_vlblock_m(vbp, m, 0, vlfree);
    bv_plot_vlblock(fp, vbp);
    bv_vlblock_free(vbp);
}


/************************************************************************
 *									*
 *		Visualization of fancy edgeuses into VLBLOCKs		*
 *									*
 *  This is the preferred method of obtaining fancy NMG displays.	*
 *									*
 ************************************************************************/

void
nmg_vlblock_v(struct bv_vlblock *vbp, const struct vertex *v, long *tab, struct bu_list *vlfree)
{
    pointp_t p;
    struct bu_list *vh;

    BV_CK_VLBLOCK(vbp);
    NMG_CK_VERTEX(v);
    NMG_INDEX_RETURN_IF_SET_ELSE_SET(tab, v->index);

    NMG_CK_VERTEX_G(v->vg_p);
    p = v->vg_p->coord;

    vh = bv_vlblock_find(vbp, 255, 255, 255);
    BV_ADD_VLIST(vlfree, vh, p, BV_VLIST_LINE_MOVE);
    BV_ADD_VLIST(vlfree, vh, p, BV_VLIST_LINE_DRAW);
}


void
nmg_vlblock_e(struct bv_vlblock *vbp, const struct edge *e, long *tab, int red, int green, int blue, struct bu_list *vlfree)
{
    pointp_t p0, p1;
    point_t end0, end1;
    vect_t v;
    struct bu_list *vh;

    BV_CK_VLBLOCK(vbp);
    NMG_CK_EDGE(e);
    NMG_INDEX_RETURN_IF_SET_ELSE_SET(tab, e->index);

    NMG_CK_EDGEUSE(e->eu_p);
    NMG_CK_VERTEXUSE(e->eu_p->vu_p);
    NMG_CK_VERTEX(e->eu_p->vu_p->v_p);
    NMG_CK_VERTEX_G(e->eu_p->vu_p->v_p->vg_p);
    p0 = e->eu_p->vu_p->v_p->vg_p->coord;

    NMG_CK_VERTEXUSE(e->eu_p->eumate_p->vu_p);
    NMG_CK_VERTEX(e->eu_p->eumate_p->vu_p->v_p);
    NMG_CK_VERTEX_G(e->eu_p->eumate_p->vu_p->v_p->vg_p);
    p1 = e->eu_p->eumate_p->vu_p->v_p->vg_p->coord;

    /* leave a little room between the edge endpoints and the vertex
     * compute endpoints by forming a vector between verts, scale vector
     * and modify points
     */
    VSUB2SCALE(v, p1, p0, 0.90);
    VADD2(end0, p0, v);
    VSUB2(end1, p1, v);

    vh = bv_vlblock_find(vbp, red, green, blue);
    BV_ADD_VLIST(vlfree, vh, end0, BV_VLIST_LINE_MOVE);
    BV_ADD_VLIST(vlfree, vh, end1, BV_VLIST_LINE_DRAW);

    nmg_vlblock_v(vbp, e->eu_p->vu_p->v_p, tab, vlfree);
    nmg_vlblock_v(vbp, e->eu_p->eumate_p->vu_p->v_p, tab, vlfree);
}


void
nmg_vlblock_eu(struct bv_vlblock *vbp, const struct edgeuse *eu, long *tab, int red, int green, int blue, int fancy, struct bu_list *vlfree)
{
    point_t base = VINIT_ZERO;
    point_t next_base = VINIT_ZERO;
    point_t radial_tip = VINIT_ZERO;
    point_t tip = VINIT_ZERO;
    struct bu_list *vh = NULL;

    BV_CK_VLBLOCK(vbp);
    NMG_CK_EDGEUSE(eu);
    NMG_INDEX_RETURN_IF_SET_ELSE_SET(tab, eu->index);

    NMG_CK_EDGE(eu->e_p);
    NMG_CK_VERTEXUSE(eu->vu_p);
    NMG_CK_VERTEX(eu->vu_p->v_p);
    NMG_CK_VERTEX_G(eu->vu_p->v_p->vg_p);

    NMG_CK_VERTEXUSE(eu->eumate_p->vu_p);
    NMG_CK_VERTEX(eu->eumate_p->vu_p->v_p);
    NMG_CK_VERTEX_G(eu->eumate_p->vu_p->v_p->vg_p);

    nmg_vlblock_e(vbp, eu->e_p, tab, red, green, blue, vlfree);

    if (!fancy) return;

    if (*eu->up.magic_p == NMG_LOOPUSE_MAGIC &&
	*eu->up.lu_p->up.magic_p == NMG_FACEUSE_MAGIC) {

	/* if "fancy" doesn't specify plotting edgeuses of this
	 * particular face orientation, return
	 */
	if ((eu->up.lu_p->up.fu_p->orientation == OT_SAME &&
	     (fancy & 1) == 0) ||
	    (eu->up.lu_p->up.fu_p->orientation == OT_OPPOSITE &&
	     (fancy & 2) == 0))
	    return;

	nmg_eu_coords(eu, base, tip);
	/* draw edgeuses of an OT_SAME faceuse in bright green, and
	 * edgeuses of an OT_OPPOSITE faceuse in cyan.  WIRE/UNSPEC
	 * edgeuses are drawn white.
	 */
	if (eu->up.lu_p->up.fu_p->orientation == OT_SAME) {
	    if (eu->up.lu_p->orientation == OT_SAME) {
		/* green */
		red = 75;
		green = 250;
		blue = 75;
	    } else if (eu->up.lu_p->orientation == OT_OPPOSITE) {
		/* yellow */
		red = 250;
		green = 250;
		blue = 75;
	    } else {
		red = 250;
		green = 50;
		blue = 250;
	    }
	} else if (eu->up.lu_p->up.fu_p->orientation == OT_OPPOSITE) {
	    if (eu->up.lu_p->orientation == OT_SAME) {
		/* blue */
		red = 100;
		green = 100;
		blue = 250;
	    } else if (eu->up.lu_p->orientation == OT_OPPOSITE) {
		/* cyan */
		red = 200;
		green = 100;
		blue = 250;
	    } else {
		/* dark magenta */
		red = 125;
		green = 0;
		blue = 125;
	    }
	} else
	    red = green = blue = 255;

	/* draw the portion from the vertexuse to just beyond the
	 * midway point to represent the edgeuse
	 */
	vh = bv_vlblock_find(vbp, red, green, blue);
	BV_ADD_VLIST(vlfree, vh, base, BV_VLIST_LINE_MOVE);
	BV_ADD_VLIST(vlfree, vh, tip, BV_VLIST_LINE_DRAW);

	/* draw a line from the tip of the edgeuse part to a point
	 * behind the tip of the radial edgeuse.  This provides 2
	 * visual cues.  First it allows us to identify the radial
	 * edgeuse, and second, it makes a "half arrowhead" on the
	 * edgeuse, making it easier to recognize the direction of the
	 * edgeuse
	 */
	nmg_eu_radial(eu, radial_tip);
	vh = bv_vlblock_find(vbp, red, green-20, blue);
	BV_ADD_VLIST(vlfree, vh, tip, BV_VLIST_LINE_MOVE);
	BV_ADD_VLIST(vlfree, vh, radial_tip, BV_VLIST_LINE_DRAW);

	/* we draw a line from the tip of the edgeuse line to the
	 * vertexuse/start of the next edgeuse in the loop.  This
	 * helps us to visually trace the loop from edgeuse to
	 * edgeuse.  The color of this part encodes the loopuse
	 * orientation.
	 */
	nmg_eu_next_base(eu, next_base);
	red *= 0.5;
	green *= 0.5;
	blue *= 0.5;
	vh = bv_vlblock_find(vbp, red, green, blue);
	BV_ADD_VLIST(vlfree, vh, tip, BV_VLIST_LINE_MOVE);
	BV_ADD_VLIST(vlfree, vh, next_base, BV_VLIST_LINE_DRAW);
    }
}


/**
 * Draw the left vector for this edgeuse.
 * At the tip, write the angle around the edgeuse, in degrees.
 *
 * Color is determined by caller.
 */
void
nmg_vlblock_euleft(struct bu_list *vh, const struct edgeuse *eu, const point_t center, const mat_t mat, const vect_t xvec, const vect_t yvec, double len, struct bu_list *vlfree, const struct bn_tol *tol)
{
    vect_t left;
    point_t tip;
    fastf_t fan_len;
    fastf_t char_scale;
    double ang;
    char str[128];

    NMG_CK_EDGEUSE(eu);
    BN_CK_TOL(tol);

    if (nmg_find_eu_leftvec(left, eu) < 0) return;

    /* fan_len is based on length of eu */
    fan_len = len * 0.2;
    VJOIN1(tip, center, fan_len, left);

    BV_ADD_VLIST(vlfree, vh, center, BV_VLIST_LINE_MOVE);
    BV_ADD_VLIST(vlfree, vh, tip, BV_VLIST_LINE_DRAW);

    ang = bg_angle_measure(left, xvec, yvec) * RAD2DEG;
    sprintf(str, "%g", ang);

    /* char_scale is based on length of eu */
    char_scale = len * 0.05;
    bv_vlist_3string(vh, vlfree, str, tip, mat, char_scale);
}


/**
 * Given an edgeuse, plot all the edgeuses around the common edge.  A
 * graphical parallel to nmg_pr_fu_around_eu_vecs().
 *
 * If the "fancy" flag is set, draw an angle fan around the edge
 * midpoint, using the same angular reference as
 * nmg_pr_fu_around_eu_vecs(), so that the printed output can be
 * cross-referenced to this display.
 */
void
nmg_vlblock_around_eu(struct bv_vlblock *vbp, const struct edgeuse *arg_eu, long *tab, int fancy, struct bu_list *vlfree, const struct bn_tol *tol)
{
    const struct edgeuse *orig_eu;
    register const struct edgeuse *eu;
    vect_t xvec, yvec, zvec;
    point_t center = VINIT_ZERO;
    mat_t mat;
    struct bu_list *vh;
    fastf_t len;

    BV_CK_VLBLOCK(vbp);
    NMG_CK_EDGEUSE(arg_eu);
    BN_CK_TOL(tol);

    if (fancy) {
	VSUB2(xvec, arg_eu->eumate_p->vu_p->v_p->vg_p->coord,
	      arg_eu->vu_p->v_p->vg_p->coord);
	len = MAGNITUDE(xvec);

	/* Erect coordinate system around eu */
	nmg_eu_2vecs_perp(xvec, yvec, zvec, arg_eu, tol);

	/* Construct matrix to rotate characters from 2D drawing space
	 * into model coordinates, oriented in plane perpendicular to
	 * eu.
	 */
	MAT_ZERO(mat);
	mat[0] = xvec[X];
	mat[4] = xvec[Y];
	mat[8] = xvec[Z];

	mat[1] = yvec[X];
	mat[5] = yvec[Y];
	mat[9] = yvec[Z];

	mat[2] = zvec[X];
	mat[6] = zvec[Y];
	mat[10] = zvec[Z];
	mat[15] = 1;

	VADD2SCALE(center, arg_eu->vu_p->v_p->vg_p->coord,
		   arg_eu->eumate_p->vu_p->v_p->vg_p->coord, 0.5);

	/* Yellow, for now */
	vh = bv_vlblock_find(vbp, 255, 200, 0);
    } else {
	vh = (struct bu_list *)NULL;
	len = 1;
    }

    orig_eu = arg_eu->eumate_p;

    eu = orig_eu;
    do {
	if (fancy) nmg_vlblock_euleft(vh, eu, center, mat, xvec, yvec, len, vlfree, tol);

	nmg_vlblock_eu(vbp, eu, tab, 80, 100, 170, 3, vlfree);
	eu = eu->eumate_p;

	nmg_vlblock_eu(vbp, eu, tab, 80, 100, 170, 3, vlfree);
	eu = eu->radial_p;
    } while (eu != orig_eu);
}


void
nmg_vlblock_lu(struct bv_vlblock *vbp, const struct loopuse *lu, long *tab, int red, int green, int blue, int fancy, struct bu_list *vlfree)
{
    struct edgeuse *eu;
    uint32_t magic1;
    struct vertexuse *vu;

    BV_CK_VLBLOCK(vbp);
    NMG_CK_LOOPUSE(lu);
    NMG_INDEX_RETURN_IF_SET_ELSE_SET(tab, lu->index);

    magic1 = BU_LIST_FIRST_MAGIC(&lu->down_hd);
    if (magic1 == NMG_VERTEXUSE_MAGIC &&
	lu->orientation != OT_BOOLPLACE) {
	vu = BU_LIST_PNEXT(vertexuse, &lu->down_hd);
	NMG_CK_VERTEXUSE(vu);
	nmg_vlblock_v(vbp, vu->v_p, tab, vlfree);
    } else if (magic1 == NMG_EDGEUSE_MAGIC) {
	for (BU_LIST_FOR(eu, edgeuse, &lu->down_hd)) {
	    nmg_vlblock_eu(vbp, eu, tab, red, green, blue, fancy, vlfree);
	}
    }
}


void
nmg_vlblock_fu(struct bv_vlblock *vbp, const struct faceuse *fu, long *tab, int fancy, struct bu_list *vlfree)
{
    struct loopuse *lu;

    BV_CK_VLBLOCK(vbp);
    NMG_CK_FACEUSE(fu);
    NMG_INDEX_RETURN_IF_SET_ELSE_SET(tab, fu->index);

    for (BU_LIST_FOR(lu, loopuse, &fu->lu_hd)) {
	/* Draw in pale blue / purple */
	if (fancy) {
	    nmg_vlblock_lu(vbp, lu, tab, 80, 100, 170, fancy, vlfree);
	} else {
	    /* Non-fancy */
	    nmg_vlblock_lu(vbp, lu, tab, 80, 100, 170, 0, vlfree);
	}
    }
}


void
nmg_vlblock_s(struct bv_vlblock *vbp, const struct shell *s, int fancy, struct bu_list *vlfree)
{
    struct faceuse *fu;
    struct loopuse *lu;
    struct edgeuse *eu;
    struct model *m;
    long *tab;

    BV_CK_VLBLOCK(vbp);
    NMG_CK_SHELL(s);
    NMG_CK_REGION(s->r_p);
    m = s->r_p->m_p;
    NMG_CK_MODEL(m);

    /* get space for list of items processed */
    tab = (long *)bu_calloc(m->maxindex+1, sizeof(long), "nmg_vlblock_s tab[]");

    for (BU_LIST_FOR(fu, faceuse, &s->fu_hd)) {
	NMG_CK_FACEUSE(fu);
	nmg_vlblock_fu(vbp, fu, tab, fancy, vlfree);
    }

    for (BU_LIST_FOR(lu, loopuse, &s->lu_hd)) {
	NMG_CK_LOOPUSE(lu);
	if (fancy) {
	    nmg_vlblock_lu(vbp, lu, tab, 255, 0, 0, fancy, vlfree);
	} else {
	    /* non-fancy, wire loops in red */
	    nmg_vlblock_lu(vbp, lu, tab, 200, 0, 0, 0, vlfree);
	}
    }

    for (BU_LIST_FOR(eu, edgeuse, &s->eu_hd)) {
	NMG_CK_EDGEUSE(eu);
	NMG_CK_EDGE(eu->e_p);

	if (fancy) {
	    nmg_vlblock_eu(vbp, eu, tab, 200, 200, 0, fancy, vlfree);
	} else {
	    /* non-fancy, wire edges in yellow */
	    nmg_vlblock_eu(vbp, eu, tab, 200, 200, 0, 0, vlfree);
	}
    }
    if (s->vu_p) {
	nmg_vlblock_v(vbp, s->vu_p->v_p, tab, vlfree);
    }

    bu_free((char *)tab, "nmg_vlblock_s tab[]");
}


void
nmg_vlblock_r(struct bv_vlblock *vbp, const struct nmgregion *r, int fancy, struct bu_list *vlfree)
{
    struct shell *s;

    BV_CK_VLBLOCK(vbp);
    NMG_CK_REGION(r);

    for (BU_LIST_FOR(s, shell, &r->s_hd)) {
	nmg_vlblock_s(vbp, s, fancy, vlfree);
    }
}


void
nmg_vlblock_m(struct bv_vlblock *vbp, const struct model *m, int fancy, struct bu_list *vlfree)
{
    struct nmgregion *r;

    BV_CK_VLBLOCK(vbp);
    NMG_CK_MODEL(m);

    for (BU_LIST_FOR(r, nmgregion, &m->r_hd)) {
	nmg_vlblock_r(vbp, r, fancy, vlfree);
    }
}


/************************************************************************
 *									*
 *		Visualization helper routines				*
 *									*
 ************************************************************************/

/**
 * If another use of this edge is in another shell, plot all the uses
 * around this edge.
 */
void
nmg_pl_edges_in_2_shells(struct bv_vlblock *vbp, long *b, const struct edgeuse *eu, int fancy, struct bu_list *vlfree, const struct bn_tol *tol)
{
    const struct edgeuse *eur;
    const struct shell *s;

    BN_CK_TOL(tol);
    eur = eu;
    NMG_CK_EDGEUSE(eu);
    NMG_CK_LOOPUSE(eu->up.lu_p);
    NMG_CK_FACEUSE(eu->up.lu_p->up.fu_p);
    s = eu->up.lu_p->up.fu_p->s_p;
    NMG_CK_SHELL(s);

    do {
	NMG_CK_EDGEUSE(eur);

	if (*eur->up.magic_p == NMG_LOOPUSE_MAGIC &&
	    *eur->up.lu_p->up.magic_p == NMG_FACEUSE_MAGIC &&
	    eur->up.lu_p->up.fu_p->s_p != s) {
	    nmg_vlblock_around_eu(vbp, eu, b, fancy, vlfree, tol);
	    break;
	}

	eur = eur->radial_p->eumate_p;
    } while (eur != eu);
}


/**
 * Called by nmg_bool.c
 */
void
nmg_pl_isect(const char *filename, const struct shell *s, struct bu_list *vlfree, const struct bn_tol *tol)
{
    struct faceuse *fu;
    struct loopuse *lu;
    struct edgeuse *eu;
    long *b;
    FILE *fp;
    uint32_t magic1;
    struct bv_vlblock *vbp;

    NMG_CK_SHELL(s);
    BN_CK_TOL(tol);

    fp=fopen(filename, "wb");
    if (fp == (FILE *)NULL) {
	(void)perror(filename);
	bu_bomb("unable to open file for writing");
    }

    b = (long *)bu_calloc(s->r_p->m_p->maxindex+1, sizeof(long),
			  "nmg_pl_isect flags[]");

    vbp = bv_vlblock_init(vlfree, 32);

    bu_log("overlay %s\n", filename);
    if (s->sa_p) {
	NMG_CK_SHELL_A(s->sa_p);
    }

    for (BU_LIST_FOR(fu, faceuse, &s->fu_hd)) {
	NMG_CK_FACEUSE(fu);
	for (BU_LIST_FOR(lu, loopuse, &fu->lu_hd)) {
	    NMG_CK_LOOPUSE(lu);
	    magic1 = BU_LIST_FIRST_MAGIC(&lu->down_hd);
	    if (magic1 == NMG_EDGEUSE_MAGIC) {
		for (BU_LIST_FOR(eu, edgeuse, &lu->down_hd)) {
		    NMG_CK_EDGEUSE(eu);
		    nmg_pl_edges_in_2_shells(vbp, b, eu, 0, vlfree, tol);
		}
	    } else if (magic1 == NMG_VERTEXUSE_MAGIC) {
		;
	    } else {
		bu_bomb("nmg_pl_isect() bad loopuse down\n");
	    }
	}
    }

    bv_plot_vlblock(fp, vbp);
    bv_vlblock_free(vbp);

    bu_free((char *)b, "nmg_pl_isect flags[]");

    (void)fclose(fp);
}


/**
 * Called from nmg_bool.c/nmg_face_combine()
 */
void
nmg_pl_comb_fu(int num1, int num2, const struct faceuse *fu1, struct bu_list *vlfree)
{
    FILE *fp;
    char name[64];
    int do_plot = 0;
    int do_anim = 0;
    struct model *m;
    long *tab;
    struct bv_vlblock *vbp;

    if (nmg_debug & NMG_DEBUG_PLOTEM &&
	nmg_debug & NMG_DEBUG_FCUT) do_plot = 1;
    if (nmg_debug & NMG_DEBUG_PL_ANIM) do_anim = 1;

    if (!do_plot && !do_anim) return;

    m = nmg_find_model(&fu1->l.magic);
    NMG_CK_MODEL(m);
    /* get space for list of items processed */
    tab = (long *)bu_calloc(m->maxindex+1, sizeof(long),
			    "nmg_pl_comb_fu tab[]");

    vbp = bv_vlblock_init(vlfree, 32);

    nmg_vlblock_fu(vbp, fu1, tab, 3, vlfree);

    if (do_plot) {
	(void)sprintf(name, "comb%d.%d.plot3", num1, num2);
	fp = fopen(name, "wb");
	if (fp == (FILE *)NULL) {
	    (void)perror(name);
	    return;
	}
	bu_log("overlay %s\n", name);

	bv_plot_vlblock(fp, vbp);

	(void)fclose(fp);
    }

    if (do_anim) {
	if (nmg_vlblock_anim_upcall) {
	    /* need to cast nmg_vlblock_anim_upcall pointer for actual use as a function */
	    void (*cfp)(struct bv_vlblock *, int, int);
	    cfp = (void (*)(struct bv_vlblock *, int, int))nmg_vlblock_anim_upcall;
	    cfp(vbp,
		(nmg_debug&NMG_DEBUG_PL_SLOW) ? US_DELAY : 0,
		0);
	} else {
	    bu_log("null nmg_vlblock_anim_upcall, no animation\n");
	}
    }
    bv_vlblock_free(vbp);
    bu_free((char *)tab, "nmg_pl_comb_fu tab[]");
}


/**
 * Note that 'str' is expected to contain a %d to place the frame
 * number.
 *
 * Called from nmg_isect_2faces and other places.
 */
void
nmg_pl_2fu(const char *str, const struct faceuse *fu1, const struct faceuse *fu2, int show_mates, struct bu_list *vlfree)
{
    FILE *fp;
    char name[32];
    struct model *m;
    long *tab;
    static int num = 1;
    struct bv_vlblock *vbp;

    if ((nmg_debug & (NMG_DEBUG_PLOTEM|NMG_DEBUG_PL_ANIM)) == 0) return;

    m = nmg_find_model(&fu1->l.magic);
    NMG_CK_MODEL(m);
    /* get space for list of items processed */
    tab = (long *)bu_calloc(m->maxindex+1, sizeof(long),
			    "nmg_pl_comb_fu tab[]");

    /* Create the vlblock */
    vbp = bv_vlblock_init(vlfree, 32);

    nmg_vlblock_fu(vbp, fu1, tab, 3, vlfree);
    if (show_mates)
	nmg_vlblock_fu(vbp, fu1->fumate_p, tab, 3, vlfree);

    nmg_vlblock_fu(vbp, fu2, tab, 3, vlfree);
    if (show_mates)
	nmg_vlblock_fu(vbp, fu2->fumate_p, tab, 3, vlfree);

    if (nmg_debug & NMG_DEBUG_PLOTEM) {
	snprintf(name, 32, str, num++);
	bu_log("overlay %s\n", name);
	fp=fopen(name, "wb");
	if (fp == (FILE *)NULL) {
	    perror(name);
	    return;
	}
	bv_plot_vlblock(fp, vbp);
	(void)fclose(fp);
    }

    if (nmg_debug & NMG_DEBUG_PL_ANIM) {
	/* Cause animation of boolean operation as it proceeds! */
	if (nmg_vlblock_anim_upcall) {
	    /* need to cast nmg_vlblock_anim_upcall pointer for actual use as a function */
	    void (*cfp)(struct bv_vlblock *, int, int);
	    cfp = (void (*)(struct bv_vlblock *, int, int))nmg_vlblock_anim_upcall;
	    cfp(vbp,
		(nmg_debug&NMG_DEBUG_PL_SLOW) ? US_DELAY : 0,
		0);
	}
    }

    bv_vlblock_free(vbp);
    bu_free((char *)tab, "nmg_pl_2fu tab[]");
}


/************************************************************************
 *									*
 *			Graphical display of classifier results		*
 *									*
 ************************************************************************/

int nmg_class_nothing_broken=1;
static char **global_classlist;
static long *broken_tab;
static int broken_tab_len;
static int broken_color;
static unsigned char broken_colors[][3] = {
    { 100, 100, 255 },	/* NMG_CLASS_AinB (bright blue) */
    { 255,  50,  50 },	/* NMG_CLASS_AonBshared (red) */
    { 255,  50, 255 }, 	/* NMG_CLASS_AonBanti (magenta) */
    {  50, 255,  50 },	/* NMG_CLASS_AoutB (bright green) */
    { 255, 255, 255 },	/* UNKNOWN (white) */
    { 255, 255, 125 }	/* no classification list (cyan) */
};
#define PICK_BROKEN_COLOR(p) { \
	if (global_classlist == (char **)NULL) { \
	    broken_color = 5; \
	} else if (NMG_INDEX_TEST(global_classlist[NMG_CLASS_AinB], (p))) \
	    broken_color = NMG_CLASS_AinB; \
	else if (NMG_INDEX_TEST(global_classlist[NMG_CLASS_AonBshared], (p))) \
	    broken_color = NMG_CLASS_AonBshared; \
	else if (NMG_INDEX_TEST(global_classlist[NMG_CLASS_AonBanti], (p))) \
	    broken_color = NMG_CLASS_AonBanti; \
	else if (NMG_INDEX_TEST(global_classlist[NMG_CLASS_AoutB], (p))) \
	    broken_color = NMG_CLASS_AoutB; \
	else \
	    broken_color = 4;}

static void
show_broken_vu(struct bv_vlblock *vbp, const struct vertexuse *vu, struct bu_list *vlfree)
{
    pointp_t p;
    struct bu_list *vh;
    struct vertex *v;
    point_t pt;

    NMG_CK_VERTEXUSE(vu);
    v = vu->v_p;
    NMG_CK_VERTEX(v);
    NMG_CK_VERTEX_G(v->vg_p);

    NMG_INDEX_RETURN_IF_SET_ELSE_SET(broken_tab, v->index);

    NMG_CK_VERTEX_G(v->vg_p);
    p = v->vg_p->coord;

    PICK_BROKEN_COLOR(vu->v_p);
    if (broken_color == 4) {
	PICK_BROKEN_COLOR(vu);
    }
    vh = bv_vlblock_find(vbp,
			 broken_colors[broken_color][0], broken_colors[broken_color][1], broken_colors[broken_color][2]);

    BV_ADD_VLIST(vlfree, vh, p, BV_VLIST_LINE_MOVE);
    BV_ADD_VLIST(vlfree, vh, p, BV_VLIST_LINE_DRAW);


    VMOVE(pt, p);
    pt[0] += 0.05;
    BV_ADD_VLIST(vlfree, vh, pt, BV_VLIST_LINE_MOVE);
    VMOVE(pt, p);
    pt[0] -= 0.05;
    BV_ADD_VLIST(vlfree, vh, pt, BV_VLIST_LINE_DRAW);

    VMOVE(pt, p);
    pt[1] += 0.05;
    BV_ADD_VLIST(vlfree, vh, pt, BV_VLIST_LINE_MOVE);
    VMOVE(pt, p);
    pt[1] -= 0.05;
    BV_ADD_VLIST(vlfree, vh, pt, BV_VLIST_LINE_DRAW);

    VMOVE(pt, p);
    pt[2] += 0.05;
    BV_ADD_VLIST(vlfree, vh, pt, BV_VLIST_LINE_MOVE);
    VMOVE(pt, p);
    pt[2] -= 0.05;
    BV_ADD_VLIST(vlfree, vh, pt, BV_VLIST_LINE_DRAW);

    BV_ADD_VLIST(vlfree, vh, p, BV_VLIST_LINE_MOVE);
}


static void
show_broken_e(struct bv_vlblock *vbp, const struct edgeuse *eu, struct bu_list *vlfree)
{
    pointp_t p0, p1;
    point_t end0, end1;
    vect_t v;
    struct bu_list *vh;

    NMG_CK_VERTEXUSE(eu->vu_p);
    NMG_CK_VERTEX(eu->vu_p->v_p);
    NMG_CK_VERTEX_G(eu->vu_p->v_p->vg_p);
    NMG_CK_VERTEXUSE(eu->eumate_p->vu_p);
    NMG_CK_VERTEX(eu->eumate_p->vu_p->v_p);
    NMG_CK_VERTEX_G(eu->eumate_p->vu_p->v_p->vg_p);

    NMG_INDEX_RETURN_IF_SET_ELSE_SET(broken_tab, eu->e_p->index);

    p0 = eu->vu_p->v_p->vg_p->coord;
    p1 = eu->eumate_p->vu_p->v_p->vg_p->coord;

    /* leave a little room between the edge endpoints and the vertex
     * compute endpoints by forming a vector between verts, scale
     * vector, and modify points
     */
    VSUB2SCALE(v, p1, p0, 0.90);
    VADD2(end0, p0, v);
    VSUB2(end1, p1, v);


    PICK_BROKEN_COLOR(eu->e_p);
    if (broken_color == 4) {
	PICK_BROKEN_COLOR(eu);
    }

    vh = bv_vlblock_find(vbp,
			 broken_colors[broken_color][0], broken_colors[broken_color][1], broken_colors[broken_color][2]);

    BV_ADD_VLIST(vlfree, vh, end0, BV_VLIST_LINE_MOVE);
    BV_ADD_VLIST(vlfree, vh, end1, BV_VLIST_LINE_DRAW);

    show_broken_vu(vbp, eu->vu_p, vlfree);
    show_broken_vu(vbp, eu->eumate_p->vu_p, vlfree);

}


static void
show_broken_eu(struct bv_vlblock *vbp, const struct edgeuse *eu, int fancy, struct bu_list *vlfree)
{
    struct bu_list *vh;
    int red, green, blue;
    point_t base = VINIT_ZERO;
    point_t tip = VINIT_ZERO;
    point_t radial_tip = VINIT_ZERO;
    point_t next_base = VINIT_ZERO;

    NMG_CK_EDGEUSE(eu);
    NMG_CK_EDGE(eu->e_p);

    show_broken_e(vbp, eu, vlfree);

    if (!fancy) return;

    /* paint the edgeuse lines */
    if (*eu->up.magic_p == NMG_LOOPUSE_MAGIC &&
	*eu->up.lu_p->up.magic_p == NMG_FACEUSE_MAGIC) {

	red = broken_colors[broken_color][0];
	green = broken_colors[broken_color][1];
	blue = broken_colors[broken_color][2];

	nmg_eu_coords(eu, base, tip);
	if (eu->up.lu_p->up.fu_p->orientation == OT_SAME)
	    red += 50;
	else if (eu->up.lu_p->up.fu_p->orientation == OT_OPPOSITE)
	    red -= 50;
	else
	    red = green = blue = 255;

	vh = bv_vlblock_find(vbp, red, green, blue);
	BV_ADD_VLIST(vlfree, vh, base, BV_VLIST_LINE_MOVE);
	BV_ADD_VLIST(vlfree, vh, tip, BV_VLIST_LINE_DRAW);

	nmg_eu_radial(eu, radial_tip);
	vh = bv_vlblock_find(vbp, red, green-20, blue);
	BV_ADD_VLIST(vlfree, vh, tip, BV_VLIST_LINE_MOVE);
	BV_ADD_VLIST(vlfree, vh, radial_tip, BV_VLIST_LINE_DRAW);

	nmg_eu_next_base(eu, next_base);
	vh = bv_vlblock_find(vbp, 0, 100, 0);
	BV_ADD_VLIST(vlfree, vh, tip, BV_VLIST_LINE_MOVE);
	BV_ADD_VLIST(vlfree, vh, next_base, BV_VLIST_LINE_DRAW);
    }

}


static void
show_broken_lu(struct bv_vlblock *vbp, const struct loopuse *lu, int fancy, struct bu_list *vlfree)
{
    register struct edgeuse *eu;
    struct bu_list *vh;
    vect_t n;

    NMG_CK_LOOPUSE(lu);

    if (BU_LIST_FIRST_MAGIC(&lu->down_hd)==NMG_VERTEXUSE_MAGIC) {
	register struct vertexuse *vu;
	vu = BU_LIST_FIRST(vertexuse, &lu->down_hd);
	show_broken_vu(vbp, vu, vlfree);
	return;
    }

    if (nmg_debug & NMG_DEBUG_GRAPHCL) {
	for (BU_LIST_FOR(eu, edgeuse, &lu->down_hd))
	    show_broken_eu(vbp, eu, fancy, vlfree);
    }

    /* Draw colored polygons for the actual face loops */
    /* Faces are not classified, only loops */
    /* This can obscure the edge/vertex info */
    PICK_BROKEN_COLOR(lu->l_p);
    vh = bv_vlblock_find(vbp,
			 broken_colors[broken_color][0], broken_colors[broken_color][1], broken_colors[broken_color][2]);

    if (*lu->up.magic_p == NMG_FACEUSE_MAGIC) {
	NMG_GET_FU_NORMAL(n, lu->up.fu_p);
    } else {
	/* For wire loops, use a constant normal */
	VSET(n, 0, 0, 1);
    }

    if ((nmg_debug & (NMG_DEBUG_GRAPHCL|NMG_DEBUG_PL_LOOP)) == (NMG_DEBUG_PL_LOOP)) {
	/* If only NMG_DEBUG_PL_LOOP set, just draw lu as wires */
	nmg_lu_to_vlist(vh, lu, 0, n, vlfree);
    } else if ((nmg_debug & (NMG_DEBUG_GRAPHCL|NMG_DEBUG_PL_LOOP)) == (NMG_DEBUG_GRAPHCL|NMG_DEBUG_PL_LOOP)) {
	/* Draw as polygons if both set */
	nmg_lu_to_vlist(vh, lu, 1, n, vlfree);
    } else {
	/* If only NMG_DEBUG_GRAPHCL set, don't draw lu's at all */
    }
}


static void
show_broken_fu(struct bv_vlblock *vbp, const struct faceuse *fu, int fancy, struct bu_list *vlfree)
{
    register struct loopuse *lu;

    NMG_CK_FACEUSE(fu);
    for (BU_LIST_FOR(lu, loopuse, &fu->lu_hd)) {
	show_broken_lu(vbp, lu, fancy, vlfree);
    }
}


static void
show_broken_s(struct bv_vlblock *vbp, const struct shell *s, int fancy, struct bu_list *vlfree)
{
    struct faceuse *fu;
    struct loopuse *lu;
    struct edgeuse *eu;

    NMG_CK_SHELL(s);
    for (BU_LIST_FOR(fu, faceuse, &s->fu_hd))
	show_broken_fu(vbp, fu, fancy, vlfree);
    for (BU_LIST_FOR(lu, loopuse, &s->lu_hd))
	show_broken_lu(vbp, lu, fancy, vlfree);
    for (BU_LIST_FOR(eu, edgeuse, &s->eu_hd))
	show_broken_eu(vbp, eu, fancy, vlfree);
    if (s->vu_p)
	show_broken_vu(vbp, s->vu_p, vlfree);
}
static void
show_broken_r(struct bv_vlblock *vbp, const struct nmgregion *r, int fancy, struct bu_list *vlfree)
{
    register struct shell *s;

    NMG_CK_REGION(r);
    for (BU_LIST_FOR(s, shell, & r->s_hd))
	show_broken_s(vbp, s, fancy, vlfree);
}


static void
show_broken_m(struct bv_vlblock *vbp, const struct model *m, int fancy, struct bu_list *vlfree)
{
    register struct nmgregion *r;

    NMG_CK_MODEL(m);
    for (BU_LIST_FOR(r, nmgregion, &m->r_hd))
	show_broken_r(vbp, r, fancy, vlfree);
}


static int stepalong = 0;

void
nmg_plot_sigstepalong(int UNUSED(i))
{
    stepalong=1;
}


/**
 * XXX Needs new name, with nmg_ prefix, and a stronger indication
 * that this is a graphical display of classifier operation.
 */
void
nmg_show_broken_classifier_stuff(uint32_t *p, char **classlist, int all_new, int fancy, const char *a_string, struct bu_list *vlfree)
{
    static struct bv_vlblock *vbp = (struct bv_vlblock *)NULL;
    struct model *m;

/* printf("showing broken stuff\n"); */

    global_classlist = classlist;

    nmg_class_nothing_broken = 0;

    if (!vbp)
	vbp = bv_vlblock_init(vlfree, 32);
    else if (all_new) {
	bv_vlblock_free(vbp);
	vbp = (struct bv_vlblock *)NULL;
	vbp = bv_vlblock_init(vlfree, 32);
    }

    m = nmg_find_model(p);
    /* get space for list of items processed */
    if (!broken_tab) {
	broken_tab = (long *)bu_calloc(m->maxindex+1, sizeof(long),
				       "nmg_vlblock_s tab[]");
	broken_tab_len = m->maxindex+1;
    } else {
	if (broken_tab_len < m->maxindex+1) {
	    bu_log("nmg_show_broken_classifier_stuff() maxindex increased! was %d, now %ld\n",
		   broken_tab_len, m->maxindex+1);
	    broken_tab = (long *)bu_realloc((char *)broken_tab,
					    (m->maxindex+1) * sizeof(long),
					    "nmg_vlblock_s tab[] enlargement");
	    broken_tab_len = m->maxindex+1;
	}
	if (all_new) {
	    memset((char *)broken_tab, 0,  (m->maxindex+1) * sizeof(long));
	}
    }


    switch (*p) {
	case NMG_MODEL_MAGIC:
	    show_broken_m(vbp, (struct model *)p, fancy, vlfree);
	    break;
	case NMG_REGION_MAGIC:
	    show_broken_r(vbp, (struct nmgregion *)p, fancy, vlfree);
	    break;
	case NMG_SHELL_MAGIC:
	    show_broken_s(vbp, (struct shell *)p, fancy, vlfree);
	    break;
	case NMG_FACE_MAGIC:
	    show_broken_fu(vbp, ((struct face *)p)->fu_p, fancy, vlfree);
	    break;
	case NMG_FACEUSE_MAGIC:
	    show_broken_fu(vbp, (struct faceuse *)p, fancy, vlfree);
	    break;
	case NMG_LOOPUSE_MAGIC:
	    show_broken_lu(vbp, (struct loopuse *)p, fancy, vlfree);
	    break;
	case NMG_EDGE_MAGIC:
	    show_broken_eu(vbp, ((struct edge *)p)->eu_p, fancy, vlfree);
	    break;
	case NMG_EDGEUSE_MAGIC:
	    show_broken_eu(vbp, (struct edgeuse *)p, fancy, vlfree);
	    break;
	case NMG_VERTEXUSE_MAGIC:
	    show_broken_vu(vbp, (struct vertexuse *)p, vlfree);
	    break;
	default:
	    bu_log("Unknown magic number %u %x %zu %p\n",
		   (unsigned)*p, (unsigned)*p, (size_t)p, (void *)p);
	    break;
    }

    /*
     * Cause animation of boolean operation as it proceeds!  The
     * "copy" flag on nmg_vlblock_anim_upcall() means that the vlist
     * will remain, undisturbed, for further use.
     */
    if (nmg_vlblock_anim_upcall) {
	void (*cur_sigint)(int);

	/* need to cast nmg_vlblock_anim_upcall pointer for actual use as a function */
	void (*cfp)(struct bv_vlblock *, int, int);
	cfp = (void (*)(struct bv_vlblock *, int, int))nmg_vlblock_anim_upcall;

	if (!a_string) {
	    cfp(vbp,
		(nmg_debug&NMG_DEBUG_PL_SLOW) ? US_DELAY : 0,
		1);
	} else {

	    bu_log("NMG Intermediate display Ctrl-C to continue (%s)\n", a_string);
	    cur_sigint = signal(SIGINT, nmg_plot_sigstepalong);

	    cfp(vbp,
		(nmg_debug&NMG_DEBUG_PL_SLOW) ? US_DELAY : 0,
		1);

	    for (stepalong = 0; !stepalong;) {
		(*nmg_mged_debug_display_hack)();
	    }
	    signal(SIGINT, cur_sigint);
	    bu_log("Continuing\n");
	}
    } else {
	/* Non interactive, drop a plot file */
	char buf[128];
	static int num = 0;
	FILE *fp;

	sprintf(buf, "cbroke%d.plot3", num++);
	fp = fopen(buf, "wb");
	if (fp) {
	    bv_plot_vlblock(fp, vbp);
	    fclose(fp);
	    bu_log("overlay %s for %s\n", buf, a_string);
	}

	bv_vlblock_free(vbp);
	vbp = (struct bv_vlblock *)NULL;
	bu_free((char *)broken_tab, "broken_tab");
	broken_tab = (long *)NULL;
	broken_tab_len = 0;
    }
}


void
nmg_face_plot(const struct faceuse *fu, struct bu_list *vlfree)
{
    FILE *fp;
    char name[32];
    struct model *m;
    struct bv_vlblock *vbp;
    long *tab;
    int fancy;
    static int num = 1;

    if ((nmg_debug & (NMG_DEBUG_PLOTEM|NMG_DEBUG_PL_ANIM)) == 0) return;

    NMG_CK_FACEUSE(fu);

    m = nmg_find_model((uint32_t *)fu);
    NMG_CK_MODEL(m);

    /* get space for list of items processed */
    tab = (long *)bu_calloc(m->maxindex+1, sizeof(long),
			    "nmg_face_plot tab[]");

    vbp = bv_vlblock_init(vlfree, 32);

    fancy = 3;	/* show both types of edgeuses */
    nmg_vlblock_fu(vbp, fu, tab, fancy, vlfree);

    if (nmg_debug & NMG_DEBUG_PLOTEM) {
	(void)sprintf(name, "face%d.plot3", num++);
	bu_log("overlay %s\n", name);
	fp=fopen(name, "wb");
	if (fp == (FILE *)NULL) {
	    perror(name);
	    return;
	}
	bv_plot_vlblock(fp, vbp);
	(void)fclose(fp);
    }

    if (nmg_debug & NMG_DEBUG_PL_ANIM) {
	/* Cause animation of boolean operation as it proceeds! */
	if (nmg_vlblock_anim_upcall) {
	    /* if requested, delay 3/4 second */
	    /* need to cast nmg_vlblock_anim_upcall pointer for actual use as a function */
	    void (*cfp)(struct bv_vlblock *, int, int);
	    cfp = (void (*)(struct bv_vlblock *, int, int))nmg_vlblock_anim_upcall;
	    cfp(vbp,
		(nmg_debug&NMG_DEBUG_PL_SLOW) ? 750000 : 0,
		0);
	} else {
	    bu_log("null nmg_vlblock_anim_upcall, no animation\n");
	}
    }
    bv_vlblock_free(vbp);
    bu_free((char *)tab, "nmg_face_plot tab[]");

}


/**
 * Just like nmg_face_plot, except it draws two faces each iteration.
 */
void
nmg_2face_plot(const struct faceuse *fu1, const struct faceuse *fu2, struct bu_list *vlfree)
{
    struct model *m;
    struct bv_vlblock *vbp;
    long *tab;
    int fancy;

    if (! (nmg_debug & NMG_DEBUG_PL_ANIM)) return;

    NMG_CK_FACEUSE(fu1);
    NMG_CK_FACEUSE(fu2);

    m = nmg_find_model((uint32_t *)fu1);
    NMG_CK_MODEL(m);

    /* get space for list of items processed */
    tab = (long *)bu_calloc(m->maxindex+1, sizeof(long),
			    "nmg_2face_plot tab[]");

    vbp = bv_vlblock_init(vlfree, 32);

    fancy = 3;	/* show both types of edgeuses */
    nmg_vlblock_fu(vbp, fu1, tab, fancy, vlfree);
    nmg_vlblock_fu(vbp, fu2, tab, fancy, vlfree);

    /* Cause animation of boolean operation as it proceeds! */
    if (nmg_vlblock_anim_upcall) {
	/* if requested, delay 3/4 second */
	/* need to cast nmg_vlblock_anim_upcall pointer for actual use as a function */
	void (*cfp)(struct bv_vlblock *, int, int);
	cfp = (void (*)(struct bv_vlblock *, int, int))nmg_vlblock_anim_upcall;
	cfp(vbp,
	    (nmg_debug&NMG_DEBUG_PL_SLOW) ? 750000 : 0,
	    0);
    } else {
	bu_log("null nmg_vlblock_anim_upcall, no animation\n");
    }
    bv_vlblock_free(vbp);
    bu_free((char *)tab, "nmg_2face_plot tab[]");

}


/**
 * Plot the loop, and a ray from vu1 to vu2.
 */
void
nmg_face_lu_plot(const struct loopuse *lu, const struct vertexuse *vu1, const struct vertexuse *vu2, struct bu_list *vlfree)
{
    FILE *fp;
    struct model *m;
    long *b;
    char buf[128];
    static int num = 0;
    vect_t dir;
    point_t p1, p2;

    if (!(nmg_debug&NMG_DEBUG_PLOTEM)) return;

    NMG_CK_LOOPUSE(lu);
    NMG_CK_VERTEXUSE(vu1);
    NMG_CK_VERTEXUSE(vu2);

    m = nmg_find_model((uint32_t *)lu);
    sprintf(buf, "loop%d.plot3", num++);

    fp = fopen(buf, "wb");
    if (fp == NULL) {
	perror(buf);
	return;
    }
    b = (long *)bu_calloc(m->maxindex, sizeof(long), "nmg_face_lu_plot flag[]");
    nmg_pl_lu(fp, lu, b, 255, 0, 0, vlfree);

    /*
     * Two yellow lines for the ray.
     * Overshoot edge by +/-10%, for visibility.
     * Don't draw over top of the actual edge, it might hide verts.
     */
    pl_color(fp, 255, 255, 0);
    VSUB2(dir, vu2->v_p->vg_p->coord, vu1->v_p->vg_p->coord);
    VJOIN1(p1, vu1->v_p->vg_p->coord, -0.1, dir);
    pdv_3line(fp, p1, vu1->v_p->vg_p->coord);
    VJOIN1(p2, vu1->v_p->vg_p->coord,  1.1, dir);
    pdv_3line(fp, vu2->v_p->vg_p->coord, p2);

    fclose(fp);
    bu_log("overlay %s\n", buf);
    bu_free((char *)b, "nmg_face_lu_plot flag[]");
}


/**
 * Plot the loop, a ray from vu1 to vu2, and the left vector.
 */
void
nmg_plot_lu_ray(const struct loopuse *lu, const struct vertexuse *vu1, const struct vertexuse *vu2, const vect_t left, struct bu_list *vlfree)
{
    FILE *fp;
    struct model *m;
    long *b;
    char buf[128];
    static int num = 0;
    vect_t dir;
    point_t p1, p2;
    fastf_t left_mag;

    if (!(nmg_debug&NMG_DEBUG_PLOTEM)) return;

    NMG_CK_LOOPUSE(lu);
    NMG_CK_VERTEXUSE(vu1);
    NMG_CK_VERTEXUSE(vu2);

    m = nmg_find_model((uint32_t *)lu);
    sprintf(buf, "loop%d.plot3", num++);

    fp = fopen(buf, "wb");
    if (fp == NULL) {
	perror(buf);
	return;
    }
    b = (long *)bu_calloc(m->maxindex, sizeof(long), "nmg_plot_lu_ray flag[]");
    nmg_pl_lu(fp, lu, b, 255, 0, 0, vlfree);

    /*
     * Two yellow lines for the ray, and a third for the left vector.
     * Overshoot edge by +/-10%, for visibility.
     * Don't draw over top of the actual edge, it might hide verts.
     */
    pl_color(fp, 255, 255, 0);
    VSUB2(dir, vu2->v_p->vg_p->coord, vu1->v_p->vg_p->coord);
    VJOIN1(p1, vu1->v_p->vg_p->coord, -0.1, dir);
    pdv_3line(fp, p1, vu1->v_p->vg_p->coord);
    VJOIN1(p2, vu1->v_p->vg_p->coord,  1.1, dir);
    pdv_3line(fp, vu2->v_p->vg_p->coord, p2);

    /* The left vector */
    left_mag = 0.1 * MAGNITUDE(dir);
    VJOIN1(p2, p1, left_mag, left);
    pdv_3line(fp, p1, p2);

    fclose(fp);
    bu_log("overlay %s\n", buf);
    bu_free((char *)b, "nmg_plot_lu_ray flag[]");
}


void
nmg_plot_ray_face(const char *fname, point_t pt, const vect_t dir, const struct faceuse *fu, struct bu_list *vlfree)
{
    FILE *fp;
    long *b;
    point_t pp;
    static int i = 0;
    char name[1024] = {0};

    if (! (nmg_debug & NMG_DEBUG_NMGRT))
	return;

    snprintf(name, 1024, "%s%0d.plot3", fname, i++);
    fp = fopen(name, "w");
    if (fp == (FILE *)NULL) {
	perror(name);
	bu_log("plot_ray_face cannot open %s", name);
	bu_bomb("aborting");
    }

    b = (long *)bu_calloc(fu->s_p->r_p->m_p->maxindex, sizeof(long), "bit vec");

    nmg_pl_fu(fp, fu, b, 200, 200, 200, vlfree);

    bu_free((char *)b, "bit vec");

    VSCALE(pp, dir, 1000.0);
    VADD2(pp, pt, pp);
    pdv_3line(fp, pt, pp);
    (void)fclose(fp);
    bu_log("overlay %s\n", name);
}


/**
 * Draw and label all the loopuses gathered around this edgeuse.
 *
 * Called by nmg_radial_join_eu().
 */
void
nmg_plot_lu_around_eu(const char *prefix, const struct edgeuse *eu, struct bu_list *vlfree, const struct bn_tol *tol)
{
    char file[256];
    static int num = 0;
    struct model *m;
    struct bv_vlblock *vbp;
    long *tab;
    const struct edgeuse *eur;
    FILE *fp;

    NMG_CK_EDGEUSE(eu);
    BN_CK_TOL(tol);

    snprintf(file, 256, "%s%0d.plot3", prefix, num++);
    bu_log("overlay %s\n", file);
    fp = fopen(file, "wb");
    if (fp == (FILE *)NULL) {
	bu_log("plot_lu_around_eu() cannot open %s", file);
	return;
    }

    m = nmg_find_model((uint32_t *)eu);
    NMG_CK_MODEL(m);
    tab = (long *)bu_calloc(m->maxindex, sizeof(long), "bit vec");

    vbp = bv_vlblock_init(vlfree, 32);

    /* Draw all the left vectors, and a fancy edgeuse plot */
    nmg_vlblock_around_eu(vbp, eu, tab, 3, vlfree, tol);

    eur = eu;
    do {
	NMG_CK_EDGEUSE(eur);

	if (*eur->up.magic_p == NMG_LOOPUSE_MAGIC) {
	    /* Draw this loop in non-fancy format, for context */
	    nmg_vlblock_lu(vbp, eur->up.lu_p, tab, 80, 100, 170, 0, vlfree);
	}
	eur = eur->radial_p->eumate_p;
    } while (eur != eu);

    bv_plot_vlblock(fp, vbp);
    (void)fclose(fp);
    bv_vlblock_free(vbp);
    bu_free((char *)tab, "bit vec");
}


/**
 * A routine to draw the entire surface of a face_g_snurb.
 * No handling of trimming curves is done.
 */
int
nmg_snurb_to_vlist(struct bu_list *vhead, const struct face_g_snurb *fg, int n_interior, struct bu_list *vlfree)
/* typ. 10 */
{
    register int i;
    register int j;
    register fastf_t * vp;
    struct knot_vector tkv1,
	tkv2,
	tau1,
	tau2;
    struct face_g_snurb *r, *c;
    int coords;

    BU_CK_LIST_HEAD(vhead);
    NMG_CK_FACE_G_SNURB(fg);

    nmg_nurb_kvgen(&tkv1,
		  fg->u.knots[0],
		  fg->u.knots[fg->u.k_size-1], n_interior);

    nmg_nurb_kvgen(&tkv2,
		  fg->v.knots[0],
		  fg->v.knots[fg->v.k_size-1], n_interior);

    nmg_nurb_kvmerge(&tau1, &tkv1, &fg->u);
    nmg_nurb_kvmerge(&tau2, &tkv2, &fg->v);

/** nmg_hack_snurb(&n, fg);	/ XXX */

    r = nmg_nurb_s_refine(fg, RT_NURB_SPLIT_COL, &tau2);
    NMG_CK_SNURB(r);
    c = nmg_nurb_s_refine(r, RT_NURB_SPLIT_ROW, &tau1);
    NMG_CK_SNURB(c);

    coords = RT_NURB_EXTRACT_COORDS(c->pt_type);

    if (RT_NURB_IS_PT_RATIONAL(c->pt_type)) {
	vp = c->ctl_points;
	for (i= 0; i < c->s_size[0] * c->s_size[1]; i++) {
	    fastf_t one_over_vp;
	    vp[0] *= (one_over_vp = 1/vp[3]);
	    vp[1] *= one_over_vp;
	    vp[2] *= one_over_vp;
	    vp[3] *= one_over_vp;
	    vp += coords;
	}
    }

    vp = c->ctl_points;
    for (i = 0; i < c->s_size[0]; i++) {
	BV_ADD_VLIST(vlfree, vhead, vp, BV_VLIST_LINE_MOVE);
	vp += coords;
	for (j = 1; j < c->s_size[1]; j++) {
	    BV_ADD_VLIST(vlfree, vhead, vp, BV_VLIST_LINE_DRAW);
	    vp += coords;
	}
    }

    for (j = 0; j < c->s_size[1]; j++) {
	int stride;

	stride = c->s_size[1] * coords;
	vp = &c->ctl_points[j * coords];
	BV_ADD_VLIST(vlfree, vhead, vp, BV_VLIST_LINE_MOVE);
	vp += stride;
	for (i = 1; i < c->s_size[0]; i++) {
	    BV_ADD_VLIST(vlfree, vhead, vp, BV_VLIST_LINE_DRAW);
	    vp += stride;
	}
    }
    nmg_nurb_free_snurb(c);
    nmg_nurb_free_snurb(r);

    bu_free((char *) tau1.knots, "rt_nurb_plot:tau1.knots");
    bu_free((char *) tau2.knots, "rt_nurb_plot:tau2.knots");
    bu_free((char *) tkv1.knots, "rt_nurb_plot:tkv1>knots");
    bu_free((char *) tkv2.knots, "rt_nurb_plot:tkv2.knots");

    return 0;
}


/**
 * Draw interior points on a cnurb curve.
 *
 * The endpoints are not drawn, as those points are (should) match the
 * vertices at the end of the edgeuse, and are handled by the caller.
 *
 * Special processing is performed for the order <= 0 (linear) cnurbs.
 *
 * If the curve is on a snurb face, it is in parameter space.
 * If the curve is on a planar face, it is in XYZ space.
 */
void
nmg_cnurb_to_vlist(struct bu_list *vhead, const struct edgeuse *eu, int n_interior, int cmd, struct bu_list *vlfree)
/* typ. 10 */
/* BV_VLIST_LINE_DRAW, etc. */
{
    const struct edge_g_cnurb *eg;
    const struct faceuse *fu;
    register int i;
    register fastf_t *vp = (fastf_t *)NULL;
    struct edge_g_cnurb n;
    const struct edge_g_cnurb *c;
    int coords;

    memset(&n, 0, sizeof(struct edge_g_cnurb));

    BU_CK_LIST_HEAD(vhead);
    NMG_CK_EDGEUSE(eu);
    eg = eu->g.cnurb_p;
    NMG_CK_EDGE_G_CNURB(eg);

    fu = nmg_find_fu_of_eu(eu);	/* may return NULL */
    NMG_CK_FACEUSE(fu);
    if (nmg_debug & NMG_DEBUG_BASIC) {
	bu_log("nmg_cnurb_to_vlist() eu=%p, n=%d, order=%d\n",
	       (void *)eu, n_interior, eg->order);
    }

    if (eg->order <= 0) {
	/* linear cnurb on planar face -- no intermediate points to draw */
	if (*fu->f_p->g.magic_p == NMG_FACE_G_PLANE_MAGIC)
	    return;

	/* linear cnurb on snurb face -- cnurb ctl pts are UV */
	n.order = 2;
	n.l.magic = NMG_EDGE_G_CNURB_MAGIC;
	n.c_size = 2;
	nmg_nurb_gen_knot_vector(&n.k, n.order, 0.0, 1.0);
	n.pt_type = RT_NURB_MAKE_PT_TYPE(2, RT_NURB_PT_UV, RT_NURB_PT_NONRAT);
	n.ctl_points = (fastf_t *)bu_malloc(
	    sizeof(fastf_t) * RT_NURB_EXTRACT_COORDS(n.pt_type) *
	    n.c_size, "nmg_cnurb_to_vlist() order0 ctl_points[]");
	/* Set ctl points to parametric values */
	NMG_CK_VERTEXUSE_A_CNURB(eu->vu_p->a.cnurb_p);
	n.ctl_points[0] = eu->vu_p->a.cnurb_p->param[0];
	n.ctl_points[1] = eu->vu_p->a.cnurb_p->param[1];
	n.ctl_points[2] = eu->eumate_p->vu_p->a.cnurb_p->param[0];
	n.ctl_points[3] = eu->eumate_p->vu_p->a.cnurb_p->param[1];
	c = &n;
    } else {
	/* Just use eg */
	c = eg;
    }

    NMG_CK_CNURB(c);

    coords = RT_NURB_EXTRACT_COORDS(c->pt_type);

    if (*fu->f_p->g.magic_p == NMG_FACE_G_PLANE_MAGIC) {
	/* cnurb on planar face -- ctl points are XYZ */

	vp = c->ctl_points;
	/* Omit first and last points */
	for (i = 1; i < c->c_size-1; i++) {
	    BV_ADD_VLIST(vlfree, vhead, vp, cmd);
	    vp += coords;
	}
    } else {
	const struct face_g_snurb *s;
	fastf_t final[4];
	fastf_t inv_homo;
	fastf_t param_delta;
	fastf_t crv_param;

	/* cnurb on spline face -- ctl points are UV or UVW */
	if (coords != 2 && !RT_NURB_IS_PT_RATIONAL(c->pt_type)) bu_log("nmg_cnurb_to_vlist() coords=%d\n", coords);
	s = fu->f_p->g.snurb_p;

	/* This section uses nmg_nurb_c_eval(), but nmg_nurb_c_refine is likely faster.
	 * XXXX Need a way to selectively and recursively refine curve to avoid
	 * feeding nmg_nurb_s_eval() parameters outside domain of surface.
	 */
	param_delta = (c->k.knots[c->k.k_size-1] - c->k.knots[0])/(fastf_t)(n_interior+1);
	crv_param = c->k.knots[0];
	for (i = 0; i < n_interior; i++) {
	    point_t uvw;

	    /* evaluate curve at parameter values */
	    crv_param += param_delta;

	    VSETALL(uvw, 0);

	    nmg_nurb_c_eval(c, crv_param, uvw);

	    if (RT_NURB_IS_PT_RATIONAL(c->pt_type)) {
		uvw[0] = uvw[0]/uvw[2];
		uvw[1] = uvw[1]/uvw[2];
	    }

	    /* convert 'uvw' from UV coord to XYZ coord via surf! */
	    nmg_nurb_s_eval(s, uvw[0], uvw[1], final);

	    if (RT_NURB_IS_PT_RATIONAL(s->pt_type)) {
		/* divide out homogeneous coordinate */
		inv_homo = 1.0/final[3];
		VSCALE(final, final, inv_homo);
	    }

	    BV_ADD_VLIST(vlfree, vhead, final, cmd);
	    vp += coords;
	}
    }

    if (eg->order <= 0) {
	bu_free((char *)n.k.knots, "nmg_cnurb_to_vlist() n.knot.knots");
	bu_free((char *)n.ctl_points, "nmg_cnurb_to_vlist() ctl_points");
    }
}


/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
