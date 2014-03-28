/*
 * lib_ansi.c - ANSI escape sequences library
 *
 * http://en.wikipedia.org/wiki/ANSI_escape_code
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2014 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "../include/ktap_types.h"
#include "ktap.h"
#include "kp_vm.h"

/**
 * function ansi.clear_screen - Move cursor to top left and clear screen.
 *
 * Description: Sends ansi code for moving cursor to top left and then the
 * ansi code for clearing the screen from the cursor position to the end.
 */

static int kplib_ansi_clear_screen(ktap_state_t *ks)
{
	kp_printf(ks, "\033[1;1H\033[J");
	return 0;
}

/**
 * function ansi.set_color - Set the ansi Select Graphic Rendition mode.
 * @fg: Foreground color to set.
 *
 * Description: Sends ansi code for Select Graphic Rendition mode for the
 * given forground color. Black (30), Blue (34), Green (32), Cyan (36),
 * Red (31), Purple (35), Brown (33), Light Gray (37).
 */

static int kplib_ansi_set_color(ktap_state_t *ks)
{
	int fg = kp_arg_checknumber(ks, 1);

	kp_printf(ks, "\033[%dm", fg);
	return 0;
}

/**
 * function ansi.set_color2 - Set the ansi Select Graphic Rendition mode.
 * @fg: Foreground color to set.
 * @bg: Background color to set.
 *
 * Description: Sends ansi code for Select Graphic Rendition mode for the
 * given forground color, Black (30), Blue (34), Green (32), Cyan (36),
 * Red (31), Purple (35), Brown (33), Light Gray (37) and the given
 * background color, Black (40), Red (41), Green (42), Yellow (43),
 * Blue (44), Magenta (45), Cyan (46), White (47).
 */
static int kplib_ansi_set_color2(ktap_state_t *ks)
{
	int fg = kp_arg_checknumber(ks, 1);
	int bg = kp_arg_checknumber(ks, 2);
	
	kp_printf(ks, "\033[%d;%dm", fg, bg);
	return 0;
}

/**
 * function ansi.set_color3 - Set the ansi Select Graphic Rendition mode.
 * @fg: Foreground color to set.
 * @bg: Background color to set.
 * @attr: Color attribute to set.
 *
 * Description: Sends ansi code for Select Graphic Rendition mode for the
 * given forground color, Black (30), Blue (34), Green (32), Cyan (36),
 * Red (31), Purple (35), Brown (33), Light Gray (37), the given
 * background color, Black (40), Red (41), Green (42), Yellow (43),
 * Blue (44), Magenta (45), Cyan (46), White (47) and the color attribute
 * All attributes off (0), Intensity Bold (1), Underline Single (4),
 * Blink Slow (5), Blink Rapid (6), Image Negative (7).
 */
static int kplib_ansi_set_color3(ktap_state_t *ks)
{
	int fg = kp_arg_checknumber(ks, 1);
	int bg = kp_arg_checknumber(ks, 2);
	int attr = kp_arg_checknumber(ks, 3);

	if (attr)
		kp_printf(ks, "\033[%d;%d;%dm", fg, bg, attr);
	else
		kp_printf(ks, "\033[%d;%dm", fg, bg);
	
	return 0;
}

/**
 * function ansi.reset_color - Resets Select Graphic Rendition mode.
 *
 * Description: Sends ansi code to reset foreground, background and color
 * attribute to default values.
 */
static int kplib_ansi_reset_color(ktap_state_t *ks)
{
	kp_printf(ks, "\033[0;0m");
	return 0;
}

/**
 * function ansi.new_line - Move cursor to new line.
 *
 * Description: Sends ansi code new line.
 */
static int kplib_ansi_new_line (ktap_state_t *ks)
{
	kp_printf(ks, "\12");
	return 0;
}

static const ktap_libfunc_t ansi_lib_funcs[] = {
	{"clear_screen", kplib_ansi_clear_screen},
	{"set_color", kplib_ansi_set_color},
	{"set_color2", kplib_ansi_set_color2},
	{"set_color3", kplib_ansi_set_color3},
	{"reset_color", kplib_ansi_reset_color},
	{"new_line", kplib_ansi_new_line},
	{NULL}
};

int kp_lib_init_ansi(ktap_state_t *ks)
{
	return kp_vm_register_lib(ks, "ansi", ansi_lib_funcs); 
}
