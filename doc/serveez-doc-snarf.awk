## Process this file with awk to produce a sed input.
#
# doc/serveez-doc-snarf.awk
#
# Extract documentation string from source files and produce a sed
# script for further processing. 
#
# Copyright (C) 2001 Stefan Jahn <stefan@lkcc.org>
#
# This is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
# 
# This software is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this package; see the file COPYING.  If not, write to
# the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.  
#
# $Id: serveez-doc-snarf.awk,v 1.4 2001/11/22 17:17:51 ela Exp $
#

# read lines until end of C comment has been reached
function extract_doc(line)
{
    example = 0
    end = index(substr(line, 3), "*/")
    if (!end) {
      doc = substr (line, 3)
    } else {
      doc = substr (line, 3, end - 1)
    }
    gsub(/ [ ]+/, " ", doc)
    gsub(/\t/, " ", doc)
    if (doc ~ / $/) { doc = substr(doc, 1, length(doc) - 1) }
    if (doc ~ /^ /) { doc = substr(doc, 2) }
    while (end == 0) {
      if (getline <= 0) {
	err = "unexpected EOF or error"
	err = (err ": " ERRNO)
	print err > "/dev/stderr"
	exit
      }
      end = index($0, "*/")
      line = $0
      if (index($0, " * ") == 1) {
	line = substr($0, 4)
      } else if ($0 == " *") {
	line = ""
      }

      # detect beginned end and of example
      if (line ~ /\@example/) { example = 1 }
      else if (line ~ /\@end example/) { example = 0 }

      # do not strip off spaces if there is an @example
      if (example == 0) {
	gsub(/\*\//, "", line)
	gsub(/ [ ]+/, " ", line)
	gsub(/\t/, " ", line)
	if (line ~ / $/) { line = substr(line, 1, length(line) - 1) }
        if (line ~ /^ /) { line = substr(line, 2) }
      }

      # add a line to the documentation string
      if (length(line) > 0) { 
	if (length(doc) > 0) { doc = (doc "\n") }
	doc = (doc line)
      }
    }

    return doc
}

# create location comment with a given line offset
function create_loc(offset)
{
    nr = FNR + offset
    file = FILENAME
    while (index(file, "/")) {
	file = substr(file, index(file, "/") + 1) }
    loc = ("@c doc-snarfer [" file ":" nr "]\\\n")
}

# process variable definitions
function handle_variable(line)
{
    if (line ~ /^ / || line ~ /^\t/) { return }
    if (line ~ /[ ]+[a-zA-Z0-9_\*\"\[\]]+[ ]*[;=]$/) {
	gsub(/ [ ]+/, " ", line)
	gsub(/\t/, " ", line)
	gsub(/\;/, "", line)
	gsub(/[ ]*=$/, "", line)
	gsub(/\[[A-Za-z0-9_]*\]/, "", line)
	if (index(line, "/*")) {
	    docu = extract_doc(substr(line, index(line, "/*")))
	    gsub(/\/\*.+\*\//, "", line)
	}
	split(line, el, " ")
	len = 1
	for (i in el) { 
	    len++
	    if (el[i] == "static") { return }
	}
	def = ""
	if (el[len - 2] == "=") { 
	    def = el[len - 1]
	    len -= 2
	}
	var = el[len - 1]
	while (index(var, "*") == 1) {
	    var = substr(var, 2)
	    el[len - 1] = "*"
	    len++
	}
	typ = ""
	for (i = 1; i < len - 1; i++) {
	    if (typ != "") { typ = (typ " ") }
	    typ = (typ el[i])
	}
	if (typ == "") { return }

        # enclose the variable type into braces if necessary
	if (index(typ, " ")) {
	    typ = ("{" typ "}") }

        # finally the texinfo variable definition
	vardef = (typ " " var)
	gsub(/\*/, "", docu)
	gsub(/\n/, "\\\n", docu)
	doc = docu
	if (length(def)) {
	    doc = ("Initial value: " def "@*\\\n" doc)
	}
	replace = ("@deftypevar " vardef "\\\n" doc "\\\n" "@end deftypevar")
	replace = (loc replace)
	sedexp = ("/^" toupper(var) "_DEFVAR/" " c\\\n" replace "\\\n")
	print sedexp
	docu = ""
    }
}

# handle macro definitions
function handle_macro(line)
{
    if (line ~ /^\#define /) {
      gsub(/\#define /, "", line)
      end = index(line, "(")

      # expression macro
      if (end > index(line, " ")) { 
	end = index(line, " ") 
	mac = substr(line, 1, end)
	macdef = mac
      # statement macro
      } else if (end != -1) {
	mac = substr(line, 1, end - 1)
	line = substr(line, end + 1)
	end = index(line, ")")
	args = substr(line, 1, end - 1)
	split(args, arg, ",")
	n = 0
	for(i in arg) {
	  gsub(/[ \t]/, "", arg[i])
	  n++
	}
	macdef = (mac " (")
	for(a = 1; a <= n; a++) { 
          if (a == 1) {
            macdef = (macdef arg[a]) 
	  } else {
            macdef = (macdef ", " arg[a])
	  }
	} 
	macdef = (macdef ")")
      }
      if (length(mac)) {
	doc = docu
	gsub(/\n/, "\\\n", doc)
        # finally create texinfo doc
	replace = ("@defmac " macdef "\\\n" doc "\\\n" "@end defmac")
	replace = (loc replace)
	sedexp = ("/^" toupper(mac) "_DEFMAC/" " c\\\n" replace "\\\n")
        print sedexp
        docu = ""
      }
    }
}

# variable declarations
/^[a-zA-Z0-9_\*]+[ ]+[a-zA-Z0-9_\*\"\[\]]+[ ]*[;=]$/
{
    handle_variable($0)
}

# find start of C comment
/^\/\*/ \
{ 
  retry = 1
  while(retry) {

    docu = extract_doc($0)

    # parse the C function
    retry = 0
    found = 0
    dist = 0
    guile_func = ""
    loc = "@c [file:line]\\\n"
    getline ret

    while (found == 0) {

        # handle variable definitions
	if (ret ~ /[ ]+[a-zA-Z0-9_\*\"\[\]]+[ ]*[;=]$/) {
	    create_loc(0)
	    handle_variable(ret)
	    next
	}
	# is this a guile function ?
	else if (ret ~ /^\#define FUNC_NAME /) {
	  create_loc(2)
	  gsub(/\#define FUNC_NAME \"/, "", ret)
	  guile_func = substr(ret, 1, index(ret, "\"") - 1)
	}
        # handle macro definitions
	else if (ret ~ /^\#define /) {
	  create_loc(0)
	  handle_macro(ret)
	  next
	}

	getline line
	# while trying to find a valid C function we found a new comment
	if (line ~ /^\/\*/) { 
	    $0 = line
	    retry = 1
	    break
	}
	if (line ~ /.+\(/) {

            # special guile socket callback setter/getter
	    if (line ~ /^MAKE_SOCK_CALLBACK/) {
	      create_loc(0)
	      args = "sock, proc"
	    } 
	    else {
	      create_loc(0)
	      c_func = substr(line, 1, index(line, "(") - 1)
	      gsub(/[ \t]/, "", c_func)
	      line = substr(line, index(line, "(") + 1)

              # cleanup the comments in argument list
	      gsub(/\/\*[^\*]+\*\//, "", line)

	      # find out about the functions arguments
	      args = line
	      while (index(line, ")") == 0) {
		getline line
		gsub(/\/\*[^\*]+\*\//, "", line)
		args = (args line)
	      }
	      args = substr(args, 0, index(args, ")") - 1)
	    }

	    found++
	    break
	}
	dist++
	ret = line
    }

    # canonize return types
    gsub(/ [ ]+/, " ", ret)
    gsub(/\t/, " ", ret)
    gsub(/[^a-zA-Z0-9\*_ ]/, "", ret)
    if (ret ~ /^ /) { ret = substr(ret, 2) }
    if (ret ~ / $/) { ret = substr(ret, 1, length(ret) - 1) }

    # drop invalid functions
    if (retry) { continue }
    if (dist > 2 || c_func ~ /typedef/ || c_func ~ /struct/ ||
	c_func ~ /\#/ || !found || !length(c_func) || c_func ~ /\/\*/ ||
	!length(ret))
      { next }

    # cleanup tabs in argument list
    gsub(/\t/, " ", args)
    # seperate the arguments
    split(args, arg, ",")
    c_args = ""
    guile_args = ""
    a = 0
    # count the arguments and save result
    for (x in arg) { a++ }
    for (x = 1; x <= a; x++) {
	# split the type definition
	split(arg[x], type, " ")
	# find last item
	i = 0
	for (v in type) { 
	    var = type[v]
	    i++
	}

	# check if last item has a '*' or '**' prefix
	if (index(var, "*")) {
	  n = index(var, "*")
	  if (index(var, "**") == n) { 
	    n = 2
	    type[i] = "**"
	  } else { 
	    n = 1
	    type[i] = "*"
	  }
	  type[i+1] = substr(var, index(var, "*") + n)
	  var = type[i+1]
	  i++
	}

	# check if last item has a trailing '[]'
	if (index(var, "[")) {
	    type[i] = substr(var, 1, index(var, "[") - 1)
	    type[i+1] = substr(var, index(var, "["))
	    var = type[i]
	    i++
	}

	# rejoin the arguments
	c_arg = ""
        guile_arg = var
	for (n = 1; n <= i; n++) {
	    if (c_arg != "") { c_arg = (c_arg " ") }
	    if (type[n] != var || var == "void") {
		c_arg = (c_arg type[n])
	    } else {
		c_arg = (c_arg "@var{" var "}")
	    }
	}
	
	# rejoin the argument list
	if (c_args != "") { c_args = (c_args ", ") }
	c_args = (c_args c_arg)
	if (guile_args != "") { guile_args = (guile_args " ") }
	guile_args = (guile_args guile_arg)
    }

    # correct guile argument list
    if (guile_args == "void") { guile_args = "" }

    # enclose the return value into braces if necessary
    if (index(ret, " ")) { ret = ("{" ret "}") }

    # finally the texinfo function definition and documentation
    gsub(/\n/, "\\\n", docu)
    if (length(guile_func) > 0) {
      funcdef = (guile_func " " guile_args)
      replace = ("@defun " funcdef "\\\n" docu "\\\n" "@end defun")
      replace = (loc replace)
      sedexp = ("/^" toupper(guile_func) "_DEFUN/" " c\\\n" replace "\\\n")
    } else {
      funcdef = (ret " " c_func " (" c_args ")")
      replace = ("@deftypefun " funcdef "\\\n" docu "\\\n" "@end deftypefun")
      replace = (loc replace)
      sedexp = ("/^" toupper(c_func) "_DEFUN/" " c\\\n" replace "\\\n")
    }
    print sedexp
    docu = ""
  }
}