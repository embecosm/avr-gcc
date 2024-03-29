/* Common VxWorks AE target definitions for GNU compiler.
   Copyright (C) 2004-2014 Free Software Foundation, Inc.
   Contributed by CodeSourcery, LLC.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* This header should be included after including vx-common.h.  */

/* Most of the definitions below this point are versions of the
   vxworks.h definitions, without the -mrtp bits.  */

/* The directory containing the VxWorks AE target headers.  */
#define VXWORKSAE_TARGET_DIR \
  "/home/tornado/vxworks-ae/latest/target"

/* Include target/vThreads/h or target/h (depending on the compilation
   mode), and then target/val/h (in either mode).  The macros defined
   are in the user's namespace, but the VxWorks headers require
   them.  */
#undef VXWORKS_ADDITIONAL_CPP_SPEC
#define VXWORKS_ADDITIONAL_CPP_SPEC "					\
 %{!nostdinc:%{isystem*}}						\
 %{mvthreads:-DVTHREADS=1						\
	 %{!nostdinc:-isystem " VXWORKSAE_TARGET_DIR "/vThreads/h}}	\
 %{!mvthreads:-DAE653_BUILD=1						\
	 %{!nostdinc:-isystem " VXWORKSAE_TARGET_DIR "/h}}		\
 %{!nostdinc:-isystem " VXWORKSAE_TARGET_DIR "/val/h}"

#undef VXWORKS_LIB_SPEC
#define VXWORKS_LIB_SPEC ""

#undef VXWORKS_LINK_SPEC
#define VXWORKS_LINK_SPEC	\
  "-r %{v:-V}"
 
#undef VXWORKS_LIBGCC_SPEC
#define VXWORKS_LIBGCC_SPEC	\
  "-lgcc"

#undef VXWORKS_STARTFILE_SPEC
#define VXWORKS_STARTFILE_SPEC ""

#define VXWORKS_KIND VXWORKS_KIND_AE

/* Both kernels and RTPs have the facilities required by this macro.  */
#define TARGET_POSIX_IO

/* A VxWorks 653 implementation of TARGET_OS_CPP_BUILTINS.  */
#define VXWORKS_OS_CPP_BUILTINS()                                       \
  do                                                                    \
    {                                                                   \
      builtin_define ("__vxworks");                                     \
      builtin_define ("__VXWORKS__");                                   \
    }                                                                   \
  while (0)

/* Do VxWorks-specific parts of TARGET_OPTION_OVERRIDE.  */
#undef VXWORKS_OVERRIDE_OPTIONS
#define VXWORKS_OVERRIDE_OPTIONS vxworks_override_options ()
extern void vxworks_override_options (void);
