/*
 * option.h - getopt function interface
 *
 * Copyright (C) 2000 Stefan Jahn <stefan@lkcc.org>
 * Copyright (C) 2000 Raimund Jacob <raimi@lkcc.org>
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this package; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * $Id: option.h,v 1.2 2000/06/11 21:39:17 raimi Exp $
 *
 */

#ifndef __OPTION_H__
#define __OPTION_H__

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if HAVE_GETOPT_H
# include <getopt.h>
#else
# define __EXTENSIONS__
/* FreeBSD and probably all commercial Un*ces define getopt() 
   in this specific file */
# include <unistd.h>
#endif

/*
 * Defining here the struct and #define's for getopt_long() if it
 * is in libiberty.a but could not be found in getopt.h
 */
#if defined(HAVE_GETOPT_LONG) && !defined(DECLARED_GETOPT_LONG)

struct option
{
  const char *name;
  int has_arg;
  int *flag;
  int val;
};

#define no_argument       0
#define required_argument 1
#define optional_argument 2

extern int getopt_long(int argc, 
		       char * const argv[], 
		       const char *optstring,
		       const struct option *longopts, 
		       int *longindex);

#endif /* DECLARED_GETOPT_LONG */

#ifndef HAVE_GETOPT

int getopt(int argc, char * const argv[], const char *optstring);
extern char *optarg;

#endif

#endif /* __OPTION_H__ */
