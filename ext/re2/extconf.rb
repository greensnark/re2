# re2 (http://github.com/mudge/re2)
# Ruby bindings to re2, an "efficient, principled regular expression library"
#
# Copyright (c) 2010, Paul Mucur (http://mucur.name)
# Released under the BSD Licence, please see LICENSE.txt

require 'mkmf'

incl, lib = dir_config("re2", "/usr/local/include", "/usr/local/lib")

$CFLAGS << " -Wall -Wextra -funroll-loops"

have_library("stdc++")
if have_library("re2")
  create_makefile("re2")
else
  abort "You must have re2 installed and specified with --with-re2-dir, please see http://code.google.com/p/re2/wiki/Install"
end