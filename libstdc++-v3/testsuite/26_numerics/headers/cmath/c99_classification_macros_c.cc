// 2001-04-06 gdr

// Copyright (C) 2001-2014 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

// { dg-do compile }
// { dg-add-options no_pch }

// { dg-xfail-if "" { { *-*-linux* *-*-gnu* *-*-darwin* *-*-solaris2.1[0-9]* hppa*-*-hpux* *-*-mingw* } || { uclibc || newlib } } { "*" } { "" } }
// { dg-excess-errors "" { target { { *-*-linux* *-*-gnu* *-*-darwin* *-*-solaris2.1[0-9]* hppa*-*-hpux* *-*-mingw* } || { uclibc || newlib } } } }

#include <math.h>

void fpclassify() { }

void isfinite() { }

void isinf() { }

void isnan() { }

void isnormal() { }

void signbit() { }

void isgreater() { }

void isgreaterequal() { }

void isless() { }

void islessequal() { }

void islessgreater() { }

void isunordered() { }

