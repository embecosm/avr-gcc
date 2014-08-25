// { dg-require-atomic-builtins "" }
// { dg-options "-std=gnu++11" }

// Copyright (C) 2014 Free Software Foundation, Inc.
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

#include <atomic>
#include <testsuite_hooks.h>

// libstdc++/60658

struct Foo {
  char buf[1];
};

struct Bar {
  char buf[100];
};

int
main ()
{
  bool test __attribute__((unused)) = true;

  std::atomic<Foo*> a;
  std::atomic<Bar*> b;

  VERIFY (a.is_lock_free() == b.is_lock_free());
}
