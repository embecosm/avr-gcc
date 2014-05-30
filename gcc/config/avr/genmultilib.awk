# Copyright (C) 2011-2014 Free Software Foundation, Inc.
#
# This file is part of GCC.
#
# GCC is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 3, or (at your option) any later
# version.
#
# GCC is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License
# along with GCC; see the file COPYING3.  If not see
# <http://www.gnu.org/licenses/>.

##################################################################
#  
# Transform Core/Device Information from avr-mcus.def to a
# Representation that is understood by GCC's multilib Machinery.
#
# The Script works as a Filter from STDIN to STDOUT.
# 
# FORMAT = "Makefile": Generate Makefile Snipet that sets some
#                      MULTILIB_* Variables as needed.
#
##################################################################

BEGIN {
    FS ="[(, \t]+"
    option[""] = ""
    tiny_stack[""] = 1
    comment = 1
    n_mcu = 0
    n_cores = 0

    mtiny[0] = ""
    mtiny[1] = "tiny-stack"
    option["tiny-stack"] = "msp8"
}

##################################################################
# Add some Comments to the generated Files and copy-paste
# Copyright Notice from above.
##################################################################

/^#/ {
    if (!comment)
	next
    else if (comment == 1)
    {
	if (FORMAT == "Makefile")
	{
	    print "# Auto-generated Makefile Snip"
	    print "# Generated by    : ./gcc/config/avr/genmultilib.awk"
	    print "# Generated from  : ./gcc/config/avr/avr-mcus.def"
	    print "# Used by         : tmake_file from Makefile and genmultilib"
	    print ""
	}
    }

    comment = 2;

    print
}

/^$/ {
    # The first empty line stops copy-pasting the GPL comments
    # from this file to the generated file.

    comment = 0
}

##################################################################
# Run over all AVR_MCU Lines and gather Information:
# cores[]     : Enumerates the Cores (avr2, avr25, ...)
# mcu[]       : Enumerates the Devices
# tiny_stack[]: Maps Core/Device to 0 (2-byte SP) or 1 (1-byte SP)
# option[]    : Maps Core/Device to the mmcu= option to get it
# toCore[]    : Maps Device to its Core
##################################################################

/^AVR_MCU/ {
    name = $2
    gsub ("\"", "", name)

    if ($4 == "NULL")
    {
	core = name

	# avr1 is supported for Assembler only:  It gets no multilib
	if (core == "avr1")
	    next

	cores[n_cores] = core
	n_cores++
	tiny_stack[core] = 0
	option[core] = "march=" core

	next
    }

    # avr1 is supported for Assembler only:  Its Devices are ignored
    if (core == "avr1")
	next

    tiny_stack[name]  = $5
    mcu[n_mcu] = name
    n_mcu++
    option[name]      = "mmcu=" name
    toCore[name]      = core

    if (tiny_stack[name] == 1)
	tiny_stack[core] = 1
}

##################################################################
# 
# We gathered all the Information, now build/output the following:
#
#    awk Variable         target Variable          FORMAT
#  -----------------------------------------------------------
#    m_options     <->    MULTILIB_OPTIONS         Makefile
#    m_dirnames    <->    MULTILIB_DIRNAMES           "
#    m_exceptions  <->    MULTILIB_EXCEPTIONS         "
#    m_matches     <->    MULTILIB_MATCHES            "
#
##################################################################

END {
    m_options    = "\nMULTILIB_OPTIONS = "
    m_dirnames   = "\nMULTILIB_DIRNAMES ="
    m_exceptions = "\nMULTILIB_EXCEPTIONS ="
    m_matches    = "\nMULTILIB_MATCHES ="

    ##############################################################
    # Compose MULTILIB_OPTIONS.  This represents the Cross-Product
    #    (avr2, avr25, ...) x msp8

    sep = ""
    for (c = 0; c < n_cores; c++)
    {
	m_options = m_options sep option[cores[c]]
	sep = "/"
    }

    # The ... x msp8
    m_options = m_options " " option[mtiny[1]]

    ##############################################################
    # Map Device to its multilib

    for (t = 0; t < n_mcu; t++)
    {
	core = toCore[mcu[t]]
	
	line = option[core] ":" option[mcu[t]]
	gsub ("=", "?", line)
	gsub (":", "=", line)

	m_matches = m_matches " \\\n\t" line
    }

    ####################################################################
    # Compose MULTILIB_DIRNAMES and MULTILIB_EXEPTIONS

    n_mtiny = 2
    for (t = 0; t < n_mtiny; t++)
	for (c = -1; c < n_cores; c++)
	{
	    if (c == -1)
		core = ""
	    else
		core = cores[c]

	    # The Directory Name for this multilib

	    if (core != "" && mtiny[t] != "")
	    {
		mdir = core "/" mtiny[t]
		mopt = option[core] "/" option[mtiny[t]]
	    }
	    else
	    {
		mdir = core mtiny[t]
		mopt = option[core] option[mtiny[t]]
	    }

	    if (core != "" && tiny_stack[core] == 0 && mtiny[t] != "")
	    {
		# There's not a single SP = 8 Devices for this Core:
		# Don't build respective multilib
		m_exceptions = m_exceptions " \\\n\t" mopt
		continue
	    }

	    if (core != "avr2" || mtiny[t] == "")
		m_dirnames = m_dirnames " " mdir
	}

    ############################################################
    # Output that Stuff
    ############################################################

    if (FORMAT == "Makefile")
    {
	# Intended Target: ./gcc/config/avr/t-multilib

	print m_options
	print m_dirnames
	print m_exceptions
	print m_matches
    }
}
