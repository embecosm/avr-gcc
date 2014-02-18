// { dg-options "-std=gnu++0x" }
// { dg-do compile }

// Copyright (C) 2010-2014 Free Software Foundation, Inc.
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

// 20.7.1 Class template unique_ptr [unique.ptr]

#include <memory>

using std::unique_ptr;

// { dg-prune-output "static assertion failed" }

void
test01()
{
  unique_ptr<long, void(*)(long*)> p1; // { dg-error "here" }

  unique_ptr<short, void(*)(short*)> p2(nullptr); // { dg-error "here" }

  unique_ptr<int, void(*)(int*)> p3(new int); // { dg-error "here" }
}

void
test02()
{
  unique_ptr<long[], void(*)(long*)> p1; // { dg-error "here" }

  unique_ptr<short[], void(*)(short*)> p2(nullptr); // { dg-error "here" }

  unique_ptr<int[], void(*)(int*)> p3(new int[1]); // { dg-error "here" }
}


int
main()
{
  test01();
  test02();
  return 0;
}
