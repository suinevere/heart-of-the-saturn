/* Declarations for getopt.
   Copyright 1989, 1990, 1991, 1992, 1993, 1994, 1996, 1997, 1998, 2000,
   2002 Free Software Foundation, Inc.

   NOTE: The canonical source of this file is maintained with the GNU C Library.
   Bugs can be reported to bug-glibc@gnu.org.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
   USA.  */
#ifndef _GETOPT_H
#define _GETOPT_H 1

#ifdef	__cplusplus
extern "C" {
#endif

extern char *optarg;

extern int optind;

extern int opterr;

extern int optopt;

struct option
{
#if defined (__STDC__) && __STDC__
  const char *name;
#else
  char *name;
#endif
  int has_arg;
  int *flag;
  int val;
};

#define	no_argument		0
#define required_argument	1
#define optional_argument	2

#if defined (__STDC__) && __STDC__
#if !HAVE_DECL_GETOPT
#if defined (__GNU_LIBRARY__) || defined (HAVE_DECL_GETOPT)
extern int getopt (int argc, char *const *argv, const char *shortopts);
#else
#ifndef __cplusplus
extern int getopt ();
#endif
#endif
#endif

extern int getopt_long (int argc, char *const *argv, const char *shortopts,
		        const struct option *longopts, int *longind);
extern int getopt_long_only (int argc, char *const *argv,
			     const char *shortopts,
		             const struct option *longopts, int *longind);

extern int _getopt_internal (int argc, char *const *argv,
			     const char *shortopts,
		             const struct option *longopts, int *longind,
			     int long_only);
#else
extern int getopt ();
extern int getopt_long ();
extern int getopt_long_only ();

extern int _getopt_internal ();
#endif

#ifdef	__cplusplus
}
#endif

#endif
