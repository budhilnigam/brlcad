/*                         P S . C
 * BRL-CAD
 *
 * Copyright (c) 2008-2025 United States Government as represented by
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
/** @file libged/ps.c
 *
 * The ps command.
 *
 */

#include "common.h"

#include <stdlib.h>
#include <ctype.h>
#include <string.h>


#include "bu/getopt.h"
#include "vmath.h"
#include "bn.h"
#include "bg/clip.h"

#include "../ged_private.h"

static void
ps_draw_header(FILE *fp, char *font, char *title, char *creator, int linewidth, fastf_t scale, int xoffset, int yoffset)
{
    fprintf(fp, "%%!PS-Adobe-1.0\n\
%%begin(plot)\n\
%%%%DocumentFonts:  %s\n",
            font);

    fprintf(fp, "%%%%Title: %s\n", title);

    fprintf(fp, "\
%%%%Creator: %s\n\
%%%%BoundingBox: 0 0 324 324    %% 4.5in square, for TeX\n\
%%%%EndComments\n\
\n",
            creator);

    fprintf(fp, "\
%d setlinewidth\n\
\n", linewidth);

    fprintf(fp, "\
%% Sizes, made functions to avoid scaling if not needed\n\
/FntH /%s findfont 80 scalefont def\n\
/DFntL { /FntL /%s findfont 73.4 scalefont def } def\n\
/DFntM { /FntM /%s findfont 50.2 scalefont def } def\n\
/DFntS { /FntS /%s findfont 44 scalefont def } def\n\
\n", font, font, font, font);

    fprintf(fp, "\
%% line styles\n\
/NV { [] 0 setdash } def        %% normal vectors\n\
/DV { [8] 0 setdash } def       %% dotted vectors\n\
/DDV { [8 8 32 8] 0 setdash } def       %% dot-dash vectors\n\
/SDV { [32 8] 0 setdash } def   %% short-dash vectors\n\
/LDV { [64 8] 0 setdash } def   %% long-dash vectors\n\
\n\
/NEWPG {\n\
        %d %d translate\n\
        %f %f scale     %% 0-4096 to 324 units (4.5 inches)\n\
} def\n\
\n\
FntH setfont\n\
NEWPG\n\
",
            xoffset, yoffset, scale, scale);
}

#define PS_COORD(_x) ((int)((_x)+2048))
#define PS_COLOR(_c) ((_c)*(1.0/255.0))

static void
ps_draw_solid(fastf_t perspective, FILE *fp, struct bv_scene_obj *sp, matp_t psmat)
{
    static vect_t last;
    point_t clipmin = {-1.0, -1.0, -MAX_FASTF};
    point_t clipmax = {1.0, 1.0, MAX_FASTF};
    struct bv_vlist *tvp;
    point_t *pt_prev=NULL;
    fastf_t dist_prev=1.0;
    fastf_t dist;
    struct bv_vlist *vp = (struct bv_vlist *)&sp->s_vlist;
    fastf_t delta;

    fprintf(fp, "%f %f %f setrgbcolor\n",
            PS_COLOR(sp->s_color[0]),
            PS_COLOR(sp->s_color[1]),
            PS_COLOR(sp->s_color[2]));

    /* delta is used in clipping to insure clipped endpoint is slightly
     * in front of eye plane (perspective mode only).
     * This value is a SWAG that seems to work OK.
     */
    delta = psmat[15]*0.0001;
    if (delta < 0.0)
        delta = -delta;
    if (delta < SQRT_SMALL_FASTF)
        delta = SQRT_SMALL_FASTF;

    for (BU_LIST_FOR(tvp, bv_vlist, &vp->l)) {
        size_t i;
        size_t nused = tvp->nused;
        int *cmd = tvp->cmd;
        point_t *pt = tvp->pt;
        for (i = 0; i < nused; i++, cmd++, pt++) {
            static vect_t start, fin;
            switch (*cmd) {
                case BV_VLIST_POLY_START:
                case BV_VLIST_POLY_VERTNORM:
                case BV_VLIST_TRI_START:
                case BV_VLIST_TRI_VERTNORM:
                    continue;
                case BV_VLIST_POLY_MOVE:
                case BV_VLIST_LINE_MOVE:
                case BV_VLIST_TRI_MOVE:
                    /* Move, not draw */
                    if (perspective > 0) {
                        /* cannot apply perspective transformation to
                         * points behind eye plane!!!!
                         */
                        dist = VDOT(*pt, &psmat[12]) + psmat[15];
                        if (dist <= 0.0) {
                            pt_prev = pt;
                            dist_prev = dist;
                            continue;
                        } else {
                            MAT4X3PNT(last, psmat, *pt);
                            dist_prev = dist;
                            pt_prev = pt;
                        }
                    } else
                        MAT4X3PNT(last, psmat, *pt);
                    continue;
                case BV_VLIST_POLY_DRAW:
                case BV_VLIST_POLY_END:
                case BV_VLIST_LINE_DRAW:
                case BV_VLIST_TRI_DRAW:
		case BV_VLIST_TRI_END:
                    /* draw */
                    if (perspective > 0) {
                        /* cannot apply perspective transformation to
                         * points behind eye plane!!!!
                         */
                        dist = VDOT(*pt, &psmat[12]) + psmat[15];
                        if (dist <= 0.0) {
                            if (dist_prev <= 0.0) {
                                /* nothing to plot */
                                dist_prev = dist;
                                pt_prev = pt;
                                continue;
                            } else {
                                if (pt_prev) {
				    fastf_t alpha;
				    vect_t diff;
				    point_t tmp_pt;

				    /* clip this end */
				    VSUB2(diff, *pt, *pt_prev);
				    alpha = (dist_prev - delta) / (dist_prev - dist);
				    VJOIN1(tmp_pt, *pt_prev, alpha, diff);
				    MAT4X3PNT(fin, psmat, tmp_pt);
                                }
                            }
                        } else {
                            if (dist_prev <= 0.0) {
                                if (pt_prev) {
				    fastf_t alpha;
				    vect_t diff;
				    point_t tmp_pt;

				    /* clip other end */
				    VSUB2(diff, *pt, *pt_prev);
				    alpha = (-dist_prev + delta) / (dist - dist_prev);
				    VJOIN1(tmp_pt, *pt_prev, alpha, diff);
				    MAT4X3PNT(last, psmat, tmp_pt);
				    MAT4X3PNT(fin, psmat, *pt);
                                }
                            } else {
                                MAT4X3PNT(fin, psmat, *pt);
                            }
                        }
                    } else
                        MAT4X3PNT(fin, psmat, *pt);
                    VMOVE(start, last);
                    VMOVE(last, fin);
                    break;
            }

            if (bg_ray_vclip(start, fin, clipmin, clipmax) == 0)
                continue;

            fprintf(fp,
                    "newpath %d %d moveto %d %d lineto stroke\n",
                    PS_COORD(start[0] * 2047),
                    PS_COORD(start[1] * 2047),
                    PS_COORD(fin[0] * 2047),
                    PS_COORD(fin[1] * 2047));
        }
    }
}


static void
ps_draw_body(struct bu_list *hdlp, FILE *fp, mat_t model2view, fastf_t perspective, vect_t eye_pos)
{
    struct display_list *gdlp;
    struct display_list *next_gdlp;
    mat_t newmat;
    matp_t mat;
    mat_t perspective_mat;
    struct bv_scene_obj *sp;

    mat = model2view;

    if (0 < perspective) {
        point_t l, h;

        VSET(l, -1.0, -1.0, -1.0);
        VSET(h, 1.0, 1.0, 200.0);

        if (ZERO(eye_pos[Z] - 1.0)) {
            /* This way works, with reasonable Z-clipping */
            persp_mat(perspective_mat, perspective,
		      (fastf_t)1.0f, (fastf_t)0.01f, (fastf_t)1.0e10f, (fastf_t)1.0f);
        } else {
            /* This way does not have reasonable Z-clipping,
             * but includes shear, for GDurf's testing.
             */
            deering_persp_mat(perspective_mat, l, h, eye_pos);
        }

        bn_mat_mul(newmat, perspective_mat, mat);
        mat = newmat;
    }

    gdlp = BU_LIST_NEXT(display_list, hdlp);
    while (BU_LIST_NOT_HEAD(gdlp, hdlp)) {
        next_gdlp = BU_LIST_PNEXT(display_list, gdlp);

        for (BU_LIST_FOR(sp, bv_scene_obj, &gdlp->dl_head_scene_obj)) {
            ps_draw_solid(perspective, fp, sp, mat);
        }

        gdlp = next_gdlp;
    }
}


static void
ps_draw_border(FILE *fp, float red, float green, float blue)
{
    fprintf(fp, "%f %f %f setrgbcolor\n", red, green, blue);
    fprintf(fp, "newpath 0 0 moveto 4096 0 lineto stroke\n");
    fprintf(fp, "newpath 4096 0 moveto 4096 4096 lineto stroke\n");
    fprintf(fp, "newpath 4096 4096 moveto 0 4096 lineto stroke\n");
    fprintf(fp, "newpath 0 4096 moveto 0 0 lineto stroke\n");
}


static void
ps_draw_footer(FILE *fp)
{
    fputs("% showpage   % uncomment to use raw file\n", fp);
    fputs("%end(plot)\n", fp);
}


static void
dl_ps(struct bu_list *hdlp, FILE *fp, int border, char *font, char *title, char *creator, int linewidth, fastf_t scale, int xoffset, int yoffset, mat_t model2view, fastf_t perspective, vect_t eye_pos, float red, float green, float blue)
{
    ps_draw_header(fp, font, title, creator, linewidth, scale, xoffset, yoffset);
    if (border)
	ps_draw_border(fp, red, green, blue);
    ps_draw_body(hdlp, fp, model2view, perspective, eye_pos);
    ps_draw_footer(fp);

}

static fastf_t ps_default_ppi = 72.0;
static fastf_t ps_default_scale = 4.5 * 72.0 / 4096.0;

int
ged_ps_core(struct ged *gedp, int argc, const char *argv[])
{
    FILE *fp;
    struct bu_vls creator = BU_VLS_INIT_ZERO;
    struct bu_vls title = BU_VLS_INIT_ZERO;
    struct bu_vls font = BU_VLS_INIT_ZERO;
    fastf_t scale = ps_default_scale;
    int linewidth = 4;
    int xoffset = 0;
    int yoffset = 0;
    int border = 0;
    int k;
    int r, g, b;

    float border_red = 0.0;
    float border_green = 0.0;
    float border_blue = 0.0;

    static const char *usage = "[-a author] [-b] [-c r/g/b] [-f font] [-s size] [-t title] [-x offset] [-y offset] file";

    GED_CHECK_DATABASE_OPEN(gedp, BRLCAD_ERROR);
    GED_CHECK_VIEW(gedp, BRLCAD_ERROR);
    GED_CHECK_DRAWABLE(gedp, BRLCAD_ERROR);
    GED_CHECK_ARGC_GT_0(gedp, argc, BRLCAD_ERROR);

    /* initialize result */
    bu_vls_trunc(gedp->ged_result_str, 0);

    /* must be wanting help */
    if (argc == 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	return GED_HELP;
    }

    /* Initialize var defaults */
    bu_vls_printf(&font, "Courier");
    bu_vls_printf(&title, "No Title");
    bu_vls_printf(&creator, "LIBGED ps");

    /* Process options */
    bu_optind = 1;
    while ((k = bu_getopt(argc, (char * const *)argv, "a:bc:f:s:t:x:y:")) != -1) {
	double tmp_f;

	switch (k) {
	    case 'a':
		bu_vls_trunc(&creator, 0);
		bu_vls_printf(&creator, "%s", bu_optarg);

		break;
	    case 'b':
		border = 1;
		break;
	    case 'c':
		if (sscanf(bu_optarg, "%d%*c%d%*c%d", &r, &g, &b) != 3) {
		    bu_vls_printf(gedp->ged_result_str, "%s: bad color - %s", argv[0], bu_optarg);
		    return BRLCAD_ERROR;
		}

		/* Clamp color values */
		if (r < 0)
		    r = 0;
		else if (r > 255)
		    r = 255;

		if (g < 0)
		    g = 0;
		else if (g > 255)
		    g = 255;

		if (b < 0)
		    b = 0;
		else if (b > 255)
		    b = 255;

		border_red = PS_COLOR(r);
		border_green = PS_COLOR(g);
		border_blue = PS_COLOR(b);

		break;
	    case 'f':
		bu_vls_trunc(&font, 0);
		bu_vls_printf(&font, "%s", bu_optarg);

		break;
	    case 's':
		if (sscanf(bu_optarg, "%lf", &tmp_f) != 1) {
		    bu_vls_printf(gedp->ged_result_str, "%s: bad size - %s", argv[0], bu_optarg);
		    goto bad;
		}

		if (tmp_f < 0.0 || NEAR_ZERO(tmp_f, 0.1)) {
		    bu_vls_printf(gedp->ged_result_str, "%s: bad size - %s, must be greater than 0.1 inches\n", argv[0], bu_optarg);
		    goto bad;
		}

		scale = tmp_f * ps_default_ppi / 4096.0;

		break;
	    case 't':
		bu_vls_trunc(&title, 0);
		bu_vls_printf(&title, "%s", bu_optarg);

		break;
	    case 'x':
		if (sscanf(bu_optarg, "%lf", &tmp_f) != 1) {
		    bu_vls_printf(gedp->ged_result_str, "%s: bad x offset - %s", argv[0], bu_optarg);
		    goto bad;
		}
		xoffset = (int)(tmp_f * ps_default_ppi);

		break;
	    case 'y':
		if (sscanf(bu_optarg, "%lf", &tmp_f) != 1) {
		    bu_vls_printf(gedp->ged_result_str, "%s: bad y offset - %s", argv[0], bu_optarg);
		    goto bad;
		}
		yoffset = (int)(tmp_f * ps_default_ppi);

		break;
	    default:
		bu_vls_printf(gedp->ged_result_str, "%s: Unrecognized option - %s", argv[0], argv[bu_optind-1]);
		goto bad;
	}
    }

    if ((argc - bu_optind) != 1) {
	bu_vls_printf(gedp->ged_result_str, "Usage: %s %s", argv[0], usage);
	goto bad;
    }

    fp = fopen(argv[bu_optind], "wb");
    if (fp == NULL) {
	bu_vls_printf(gedp->ged_result_str, "%s: Error opening file - %s\n", argv[0], argv[bu_optind]);
	goto bad;
    }

    dl_ps(gedp->ged_gdp->gd_headDisplay, fp, border, bu_vls_addr(&font), bu_vls_addr(&title), bu_vls_addr(&creator), linewidth, scale, xoffset, yoffset, gedp->ged_gvp->gv_model2view, gedp->ged_gvp->gv_perspective, gedp->ged_gvp->gv_eye_pos, border_red, border_green, border_blue);

    fclose(fp);

    bu_vls_free(&font);
    bu_vls_free(&title);
    bu_vls_free(&creator);

    return BRLCAD_OK;

bad:
    bu_vls_free(&font);
    bu_vls_free(&title);
    bu_vls_free(&creator);

    return BRLCAD_ERROR;
}


#ifdef GED_PLUGIN
#include "../include/plugin.h"
struct ged_cmd_impl ps_cmd_impl = {"ps", ged_ps_core, GED_CMD_DEFAULT};
const struct ged_cmd ps_cmd = { &ps_cmd_impl };

struct ged_cmd_impl postscript_cmd_impl = {"postscript", ged_ps_core, GED_CMD_DEFAULT};
const struct ged_cmd postscript_cmd = { &postscript_cmd_impl };

const struct ged_cmd *ps_cmds[] = { &ps_cmd, &postscript_cmd, NULL };

static const struct ged_plugin pinfo = { GED_API,  ps_cmds, 2 };

COMPILER_DLLEXPORT const struct ged_plugin *ged_plugin_info(void)
{
    return &pinfo;
}
#endif /* GED_PLUGIN */

/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
