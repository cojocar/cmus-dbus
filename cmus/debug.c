/* 
 * Copyright 2004-2005 Timo Hirvonen
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <debug.h>

#include <stdio.h>
#include <stdarg.h>

#if DEBUG > 0
extern char *program_name;
static FILE *debug_stream = NULL;
#endif

void debug_init(FILE *stream)
{
#if DEBUG > 0
	debug_stream = stream;
#endif
}

void __debug_bug(const char *function, const char *fmt, ...)
{
#if DEBUG > 0
	const char *format = "\n%s: %s: BUG: ";
	va_list ap;

	fprintf(debug_stream, format, program_name, function);
	va_start(ap, fmt);
	vfprintf(debug_stream, fmt, ap);
	va_end(ap);
	fflush(debug_stream);
	if (debug_stream != stdout && debug_stream != stderr) {
		fprintf(stderr, format, program_name, function);
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
		fflush(stderr);
	}
	exit(127);
#endif
}

void __debug_print(const char *function, const char *fmt, ...)
{
#if DEBUG > 1
	va_list ap;

	fprintf(debug_stream, "%s: ", function);
	va_start(ap, fmt);
	vfprintf(debug_stream, fmt, ap);
	va_end(ap);
	fflush(debug_stream);
#endif
}
