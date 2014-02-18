// { dg-options "-std=gnu++11" }
// { dg-do run }

//
// 2014-01-07  Tim Shen <timshen91@gmail.com>
//
// Copyright (C) 2010-2014 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

// 28.3 Requirements [re.req]
// 28.2 (4) Table 127 - Regular expression traits class requirements
// 28.7 Class template regex_traits [re.traits]

#include <regex>
#include <string>
#include <testsuite_hooks.h>

using namespace std;

template<typename CharT>
  class MyRegexTraits
  : public regex_traits<CharT>
  {
  public:
    CharT
    translate(CharT c) const
    {
      return c+1;
    }
  };

void
test01()
{
  bool test __attribute__((unused)) = true;

  basic_regex<wchar_t, MyRegexTraits<wchar_t>> re(L".");
  VERIFY(!regex_match(L"\n", re));
  VERIFY(!regex_match(L"\r", re));
  VERIFY(!regex_match(L"\u2028", re));
  VERIFY(!regex_match(L"\u2029", re));
}

int main()
{
  test01();
  return 0;
}
