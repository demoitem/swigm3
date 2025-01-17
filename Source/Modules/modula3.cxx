/* -----------------------------------------------------------------------------
 * This file is part of SWIG, which is licensed as a whole under version 3
 * (or any later version) of the GNU General Public License. Some additional
 * terms also apply to certain portions of SWIG. The full details of the SWIG
 * license and copyrights can be found in the LICENSE and COPYRIGHT files
 * included with the SWIG source code as distributed by the SWIG developers
 * and at http://www.swig.org/legal.html.
 *
 * modula3.cxx
 *
 * Modula3 language module for SWIG.
 * ----------------------------------------------------------------------------- */

char cvsroot_modula3_cxx[] = "$Id: modula3.cxx 12225 2010-09-21 06:07:06Z wsfulton $";

/*
  Text formatted with
    indent -sob -br -ce -nut -npsl
*/

/*
  Report:
   - It's not a good concept to use member variables or global variables
     for passing parameters to functions.
     It's not a good concept to use functions of superclasses for specific services.
     E.g. For SWIG this means: Generating accessor functions for member variables
     is the most common but no general task to be processed in membervariableHandler.
     Better provide a service function which generates accessor function code
     and equip this service function with all parameters needed for input (parse node)
     and output (generated code).
   - How can I make globalvariableHandler not to generate
     interface functions to two accessor functions
     (that don't exist) ?
   - How can I generate a typemap that turns every C reference argument into
     its Modula 3 counterpart, that is
       void test(Complex &z);
       PROCEDURE test(VAR z:Complex);
   - neither $*n_mangle nor $*n_type nor $*n_ltype return the type without
     pointer converted to Modula3 equivalent,
     $*n_mangle is the variant closest to what I expect
   - using a typemap like
         typemap(m3wrapintype) int * %{VAR $1_name: INTEGER%}
     has the advantages:
       - one C parameter can be turned into multiple M3 parameters
       - the argument can be renamed
   - using typemaps like
         typemap(m3wrapinmode) int * "VAR"
         typemap(m3wrapintype) int * "INTEGER"
     has the advantages:
       - multiple parameters with same type and default value can be bundled
       - more conform to the other language modules
   - Where takes the reduction of multi-typemaps place?
     How can I preserve all parameters for functions of the intermediary class?
     The answer is Getattrs(n,"tmap:m3rawintype:next")
   - Char() can be used to transform a String to (char *)
     which can be used for output with printf
   - What is the while (checkAttribute()) loop in functionWrapper good for?
     Appearently for skipping (numinputs=0) typemaps.
   - SWIGTYPE const * - typemap is ignored, whereas
     SWIGTYPE *       - typemap is invoked, why?
     Had it been (const SWIGTYPE *) instead?
   - enumeration items should definitely be equipped
     with its plain numerical value
     One could add tag 'numvalue' in CParse/parser.y,
     but it is still possible that someone declares an
     enumeration using a symbolic constant.
     I have quickly hacked
     that the successive number is assigned
     if "enumvalue" has suffix "+1".
     The ultimate solution would be to generate a C program
     which includes the header and outputs all constants.
     This program might be compiled and run
     by 'make' or by SWIG and the resulting output is fed back to SWIG.
   - It's a bad idea to interpret feature value ""
     'disable feature' because the value ""
     might be sensible in case of feature:modula3:oldprefix.
   - What's the difference between "sym:name" and "name" ?
     "name" is the original name and
     "sym:name" is probably modified by the user using %rename
   - Is it possible for 'configure' to find out if m3pp is installed
     and to invoke it for generated Modula3 files?
   - It would be better to separate an arguments purpose and its name,
     because an output variable with name "OUTPUT" is not very descriptive.
     In case of PLPlot this could be solved by typedefs
     that assign special purposes to the array types.
   - Can one interpret $n_basetype as the identifier matched with SWIGTYPE ?

  SWIG's odds:
   - arguments of type (Node *) for SWIG functions
     should be most often better (const Node *):
     Swig_symbol_qualified, Getattr, nodeType, parentNode
   - unique identifier style instead of
     NewString, Getattr, firstChild
   - 'class'.name is qualified,
     'enum'.name and 'enumitem'.name is not
   - Swig_symbol_qualified() returns NIL for enumeration nodes

   - Is there a function that creates a C representation of a SWIG type string?

  ToDo:
   - create WeakRefs only for resources returned by function marked with %newobject
      -> part of output conversion
   - clean typemap conception
      - should a multi-typemap for m3wrapouttype skip the corresponding input parameters?
        when yes - How to handle inout-arguments? In this case like in-argument.
   - C++ classes
   - C++ exceptions
   - allow for moving RECORD and OBJECT definitions
     to separate files, with the main type called T
   - call-back functions
   - special option: fast access to class members by pointer arithmetic,
       member offsets can be determined by a C++ program that print them.
   - emit enumeration definitions when its first item is declared,
       currently enumerations are emitted at the beginning of the file

  Done:
   - addThrow should convert the typemap by itself
      - not possible because routine for attaching mapped types to parameter nodes
        won't work for the function node
   - turning error codes into exceptions
      -> part of output value checking
   - create WeakRefs for resources allocated by the library
      -> part of output conversion
   - TRY..FINALLY..END; can be omitted
      - if there is no m3wrapfreearg
      - no exception can be raised in the body (empty RAISES) list
*/

#include "swigmod.h"

#include <limits.h>		// for INT_MAX
#include <ctype.h>

//for doxygen support
#include <cparse.h>
#include "javadoc.h"

#define USAGE_ARG_DIR "m3wrapargdir typemap expect values: in, out, inout\n"

class MODULA3:public Language {
public:
  enum block_type { no_block, constant, variable, blocktype, revelation };

private:
  struct M3File {
    String *f;
    Hash *import;
    block_type bt;
    /* VC++ 6 doesn't allow the access to 'no_block'
       if it is a private member of MODULA3 class */
    M3File():f(NewString("")), import(NewHash()), bt(no_block) {
    }
    ~M3File() {
      Delete(f);
      Delete(import);
    }

    /* -----------------------------------------------------------------------------
     * enterBlock()
     *
     * Make sure that a given declaration is written to the right declaration block,
     * that is constants are written after "CONST" and so on ...
     * ----------------------------------------------------------------------------- */
    void enterBlock(block_type newbt) {
      static const char *ident[] = { "", "\nCONST\n", "\nVAR\n", "\nTYPE\n", "\nREVEAL\n" };
#ifdef DEBUG
      if ((bt < 0) || (4 < bt)) {
	printf("bt %d out of range\n", bt);
      }
#endif
      if (newbt != bt) {
	Append(f, ident[newbt]);
	bt = newbt;
      }
    }

  };

  static const char *usage;
  const String *empty_string;
  static const int MaxSet = 256;

  Hash *swig_types_hash;
  File *f_begin;
  File *f_runtime;
  File *f_header;
  File *f_wrappers;
  File *f_init;

  bool m3make_flag;             //Flag to generate an m3makefile
  bool genEnumChecks_flag;      //Flag to generate runtime enum checks
  bool dynLink_flag;            //Flag to generate dynamic types for linking
  bool proxy_flag;		// Flag for generating proxy classes
  bool ignores_flag;            //Flag to generate all raw names to be ignored
  bool have_default_constructor_flag;
  bool native_function_flag;	// Flag for when wrapping a native function
  bool enum_constant_flag;	// Flag for when wrapping an enum or constant
  bool static_flag;		// Flag for when wrapping a static functions or member variables
  bool variable_wrapper_flag;	// Flag for when wrapping a nonstatic member variable
  bool wrapping_member_flag;	// Flag for when wrapping a member variable/enum/const
  bool global_variable_flag;	// Flag for when wrapping a global variable
  bool unsafe_raw;
  bool unsafe_module;
  bool doxygen;

  String *m3raw_name;		// raw interface name
  M3File m3raw_intf;		// raw interface
  M3File m3raw_impl;		// raw implementation (usually empty)
  String *m3wrap_name;		// wrapper module
  M3File m3wrap_intf;
  M3File m3wrap_impl;
  String *m3makefile;
  String *targetlibrary;
  String *proxy_class_def;
  String *proxy_class_code;
  String *proxy_class_name;
  String *variable_name;	//Name of a variable being wrapped
  String *variable_type;	//Type of this variable
  String *enumeration_name;	//Name of the current enumeration type
  Hash *enumeration_items;	//and its members
  int enumeration_max;
  Hash *enumeration_coll;	//Collection of all enumerations.
  /* The items are nodes with members:
     "items"  - hash of with key 'itemname' and content 'itemvalue'
     "max"    - maximum value in item list
   */

  Hash *enum_coll;	//Collection of all enumerations.
  int enumAnon;
  Hash *var_coll;       //Collection of all global vars.

  String *constant_values;
  String *constantfilename;
  String *renamefilename;
  String *typemapfilename;
  String *m3raw_imports;	//intermediary class imports from %pragma
  String *module_imports;	//module imports from %pragma
  String *m3raw_baseclass;	//inheritance for intermediary class class from %pragma
  String *module_baseclass;	//inheritance for module class from %pragma
  String *m3raw_interfaces;	//interfaces for intermediary class class from %pragma
  String *module_interfaces;	//interfaces for module class from %pragma
  String *m3raw_class_modifiers;	//class modifiers for intermediary class overriden by %pragma
  String *m3wrap_modifiers;	//class modifiers for module class overriden by %pragma
  String *upcasts_code;		//C++ casts for inheritance hierarchies C++ code
  String *m3raw_cppcasts_code;	//C++ casts up inheritance hierarchies intermediary class code
  String *destructor_call;	//C++ destructor call if any
  String *dynProcs;
  String *dynInit;
  String *outfile;

  enum type_additions { none, pointer, reference };

public:

  /* -----------------------------------------------------------------------------
   * MODULA3()
   * ----------------------------------------------------------------------------- */

MODULA3():
  empty_string(NewString("")),
      swig_types_hash(NULL),
      f_begin(NULL),
      f_runtime(NULL),
      f_header(NULL),
      f_wrappers(NULL),
      f_init(NULL),
      m3make_flag(false),
      genEnumChecks_flag(false),
      dynLink_flag(false),
      proxy_flag(true),
      ignores_flag(false),
      have_default_constructor_flag(false),
      native_function_flag(false),
      enum_constant_flag(false),
      static_flag(false),
      variable_wrapper_flag(false),
      wrapping_member_flag(false),
      global_variable_flag(false),
      unsafe_raw(false),
      unsafe_module(false),
      doxygen(false),
      m3raw_name(NULL),
      m3raw_intf(),
      m3raw_impl(),
      m3wrap_name(NULL),
      m3wrap_intf(),
      m3wrap_impl(),
      m3makefile(NULL),
      targetlibrary(NULL),
      proxy_class_def(NULL),
      proxy_class_code(NULL),
      proxy_class_name(NULL),
      variable_name(NULL),
      variable_type(NULL),
      enumeration_name(NULL),
      enumeration_items(NULL),
      enumeration_max(0),
      enumeration_coll(NULL),
      enumAnon(1),
      var_coll(NULL),
      constant_values(NULL),
      constantfilename(NULL),
      renamefilename(NULL),
      typemapfilename(NULL),
      m3raw_imports(NULL),
      module_imports(NULL),
      m3raw_baseclass(NULL),
      module_baseclass(NULL),
      m3raw_interfaces(NULL),
      module_interfaces(NULL),
      m3raw_class_modifiers(NULL),
      m3wrap_modifiers(NULL),
      upcasts_code(NULL),
      m3raw_cppcasts_code(NULL),
      destructor_call(NULL),
      dynProcs(NULL),
      dynInit(NULL),
      outfile(NULL) {
  }

  /************** some utility functions ***************/

  /* -----------------------------------------------------------------------------
   * getMappedType()
   *
   * Return the type of 'p' mapped by 'map'.
   * Print a standard warning if 'p' can't be mapped.
   * ----------------------------------------------------------------------------- */

  String *getMappedType(Node *p, const char *map) {
    String *mapattr = NewString("tmap:");
    Append(mapattr, map);

    String *tm = Getattr(p, mapattr);
    if (tm == NIL) {
      Swig_warning(WARN_MODULA3_TYPEMAP_TYPE_UNDEF, input_file, line_number,
		   "No '%s' typemap defined for type '%s'\n", map, SwigType_str(Getattr(p, "type"), 0));
    }
    Delete(mapattr);
    return tm;
  }

  /* -----------------------------------------------------------------------------
   * getMappedTypeNew()
   *
   * Similar to getMappedType but uses Swig_type_lookup_new.
   * ----------------------------------------------------------------------------- */

  String *getMappedTypeNew(Node *n, const char *map, const char *lname = "", bool warn = true) {
    String *tm = Swig_typemap_lookup(map, n, lname, 0);
    if ((tm == NIL) && warn) {
      Swig_warning(WARN_MODULA3_TYPEMAP_TYPE_UNDEF, input_file, line_number,
		   "No '%s' typemap defined for type '%s'\n", map, SwigType_str(Getattr(n, "type"), 0));
    }
    return tm;
  }

  /* -----------------------------------------------------------------------------
   * attachMappedType()
   *
   * Obtain the type mapped by 'map' and attach it to the node
   * ----------------------------------------------------------------------------- */

  void attachMappedType(Node *n, const char *map, const char *lname = "") {
    String *tm = Swig_typemap_lookup(map, n, lname, 0);
    if (tm != NIL) {
      String *attr = NewStringf("tmap:%s", map);
      Setattr(n, attr, tm);
      Delete(attr);
    }
  }

  /* -----------------------------------------------------------------------------
   * skipIgnored()
   *
   * Skip all parameters that have 'numinputs=0'
   * with respect to a given typemap.
   * ----------------------------------------------------------------------------- */

  Node *skipIgnored(Node *p, const char *map) {
    String *niattr = NewStringf("tmap:%s:numinputs", map);
    String *nextattr = NewStringf("tmap:%s:next", map);

    while ((p != NIL) && checkAttribute(p, niattr, "0")) {
      p = Getattr(p, nextattr);
    }

    Delete(nextattr);
    Delete(niattr);
    return p;
  }

  /* -----------------------------------------------------------------------------
   * isInParam()
   * isOutParam()
   *
   * Check if the parameter is intended for input or for output.
   * ----------------------------------------------------------------------------- */

  bool isInParam(Node *p) {
    String *dir = Getattr(p, "tmap:m3wrapargdir");
    if ((dir == NIL) || (Strcmp(dir, "in") == 0)
	|| (Strcmp(dir, "inout") == 0)) {
      return true;
    } else if (Strcmp(dir, "out") == 0) {
      return false;
    } else {
      printf("%s", USAGE_ARG_DIR);
      return false;
    }
  }

  bool isOutParam(Node *p) {
    String *dir = Getattr(p, "tmap:m3wrapargdir");
    if ((dir == NIL) || (Strcmp(dir, "in") == 0)) {
      return false;
    } else if ((Strcmp(dir, "out") == 0) || (Strcmp(dir, "inout") == 0)) {
      return true;
    } else {
      printf("%s", USAGE_ARG_DIR);
      return false;
    }
  }

  /* -----------------------------------------------------------------------------
   * printAttrs()
   *
   * For debugging: Show all attributes of a node and their values.
   * ----------------------------------------------------------------------------- */
  void printAttrs(Node *n) {
    Iterator it;
    for (it = First(n); it.key != NIL; it = Next(it)) {
      printf("%s = %s\n", Char(it.key), Char(Getattr(n, it.key)));
    }
  }

  /* -----------------------------------------------------------------------------
   * hasPrefix()
   *
   * Check if a string have a given prefix.
   * ----------------------------------------------------------------------------- */
  bool hasPrefix(const String *str, const String *prefix) {
    int len_prefix = Len(prefix);
    return (Len(str) > len_prefix)
	&& (Strncmp(str, prefix, len_prefix) == 0);
  }

  /* -----------------------------------------------------------------------------
   * getQualifiedName()
   *
   * Return fully qualified identifier of n.
   * ----------------------------------------------------------------------------- */
#if 0
  // Swig_symbol_qualified returns NIL for enumeration nodes
  String *getQualifiedName(Node *n) {
    String *qual = Swig_symbol_qualified(n);
    String *name = Getattr(n, "name");
    if (hasContent(qual)) {
      return NewStringf("%s::%s", qual, name);
    } else {
      return name;
    }
  }
#else
  String *getQualifiedName(Node *n) {
    String *name = Copy(Getattr(n, "name"));
    n = parentNode(n);
    while (n != NIL) {
      const String *type = nodeType(n);
      if ((Strcmp(type, "class") == 0) || (Strcmp(type, "struct") == 0) || (Strcmp(type, "namespace") == 0)) {
	String *newname = NewStringf("%s::%s", Getattr(n, "name"), name);
	Delete(name);
	//name = newname;
	// Hmpf, the class name is already qualified.
	return newname;
      }
      n = parentNode(n);
    }
    return name;
  }
#endif

  /* -----------------------------------------------------------------------------
   * nameToModula3()
   *
   * Turn usual C identifiers like "this_is_an_identifier"
   * into usual Modula 3 identifier like "thisIsAnIdentifier"
   * ----------------------------------------------------------------------------- */
  String *nameToModula3(const String *sym, bool leadingCap) {
    int len_sym = Len(sym);
    char *csym = Char(sym);
    char *m3sym = new char[len_sym + 1];
    int i, j;
    bool cap = leadingCap;
    for (i = 0, j = 0; j < len_sym; j++) {
      char c = csym[j];
      if ((c == '_') || (c == ':')) {
	cap = true;
      } else {
	if (isdigit(c)) {
	  m3sym[i] = c;
	  cap = true;
	} else {
	  if (cap) {
	    m3sym[i] = (char)toupper(c);
	  } else {
	    m3sym[i] = (char)tolower(c);
	  }
	  cap = false;
	}
	i++;
      }
    }
    m3sym[i] = 0;
    String *result = NewString(m3sym);
    delete[]m3sym;
    return result;
  }

  /* -----------------------------------------------------------------------------
   * capitalizeFirst()
   *
   * Make the first character upper case.
   * ----------------------------------------------------------------------------- */
  String *capitalizeFirst(const String *str) {
    return NewStringf("%c%s", toupper(*Char(str)), Char(str) + 1);
  }

  /* -----------------------------------------------------------------------------
   * prefixedNameToModula3()
   *
   * If feature modula3:oldprefix and modula3:newprefix is present
   * and the C identifier has leading 'oldprefix'
   * then it is replaced by the 'newprefix'.
   * The rest is converted to Modula style.
   * ----------------------------------------------------------------------------- */
  String *prefixedNameToModula3(Node *n, const String *sym, bool leadingCap) {
    String *oldPrefix = Getattr(n, "feature:modula3:oldprefix");
    String *newPrefix = Getattr(n, "feature:modula3:newprefix");
    String *result = NewString("");
    char *short_sym = Char(sym);
    // if at least one prefix feature is present
    // the replacement takes place
    if ((oldPrefix != NIL) || (newPrefix != NIL)) {
      if ((oldPrefix == NIL) || hasPrefix(sym, oldPrefix)) {
	short_sym += Len(oldPrefix);
	if (newPrefix != NIL) {
	  Append(result, newPrefix);
	}
      }
    }
    String *suffix = nameToModula3(short_sym, leadingCap || hasContent(newPrefix));
    Append(result, suffix);
    Delete(suffix);
    return result;
  }

  /* -----------------------------------------------------------------------------
   * hasContent()
   *
   * Check if the string exists and contains something.
   * ----------------------------------------------------------------------------- */
  bool hasContent(const String *str) {
    return (str != NIL) && (Strcmp(str, "") != 0);
  }

  /* -----------------------------------------------------------------------------
   * openWriteFile()
   *
   * Caution: The file must be freshly allocated and will be destroyed
   *          by this routine.
   * ----------------------------------------------------------------------------- */

  File *openWriteFile(String *name) {
    File *file = NewFile(name, "w", SWIG_output_files());
    if (!file) {
      FileErrorDisplay(name);
      Exit(EXIT_FAILURE);
    }
    Delete(name);
    return file;
  }

  /* -----------------------------------------------------------------------------
   * aToL()
   *
   * like atol but with additional user warning
   * ----------------------------------------------------------------------------- */

  long aToL(const String *value) {
    char *endptr;
    long numvalue = strtol(Char(value), &endptr, 0);
    if (*endptr != 0) {
      Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number, "The string <%s> does not denote a numeric value.\n", value);
    }
    return numvalue;
  }

  /* -----------------------------------------------------------------------------
   * strToL()
   *
   * like strtol but returns if the conversion was successful
   * ----------------------------------------------------------------------------- */

  bool strToL(const String *value, long &numvalue) {
    char *endptr;
    numvalue = strtol(Char(value), &endptr, 0);
    return (*endptr == 0);
  }

  /* -----------------------------------------------------------------------------
   * evalExpr()
   *
   * Evaluate simple expression as they may occur in "enumvalue" attributes.
   * ----------------------------------------------------------------------------- */

  bool evalExpr(String *value, long &numvalue) {
    // Split changes file status of String and thus cannot receive 'const' strings
    List *summands = Split(value, '+', INT_MAX);
    Iterator sm = First(summands);
    numvalue = 0;
    for (; sm.item != NIL; sm = Next(sm)) {
      String *smvalue = Getattr(constant_values, sm.item);
      long smnumvalue;
      if (smvalue != NIL) {
	if (!strToL(smvalue, smnumvalue)) {
	  return false;
	}
      } else {
	if (!strToL(sm.item, smnumvalue)) {
	  return false;
	}
      }
      numvalue += smnumvalue;
    }
    return true;
  }

  /* -----------------------------------------------------------------------------
   * log2()
   *
   * Determine the position of the single bit of a power of two.
   * Returns true if the given number is a power of two.
   * ----------------------------------------------------------------------------- */

  bool log2(long n, long &exp) {
    exp = 0;
    while (n > 0) {
      if ((n & 1) != 0) {
	return n == 1;
      }
      exp++;
      n >>= 1;
    }
    return false;
  }

  /* -----------------------------------------------------------------------------
   * writeArg
   *
   * Write a function argument or RECORD entry definition.
   * Bundles arguments of same type and default value.
   * 'name.next==NIL' denotes the end of the entry or argument list.
   * ----------------------------------------------------------------------------- */

  bool equalNilStr(const String *str0, const String *str1) {
    if (str0 == NIL) {
      return (str1 == NIL);
    } else {
      return (str1 != NIL) && (Cmp(str0, str1) == 0);
    }
  }

  struct writeArgState {
    String *mode, *name, *type, *value;
    bool hold;
     writeArgState():mode(NIL), name(NIL), type(NIL), value(NIL), hold(false) {
    }
  };

  void writeArg(File *f, writeArgState & state, String *mode, String *name, String *type, String *value) {
    /* skip the first argument,
       only store the information for the next call in this case */
    if (state.name != NIL) {
      if ((!state.hold) && (state.mode != NIL)) {
	Printf(f, "%s ", state.mode);
      }
      if ((name != NIL) && equalNilStr(state.mode, mode) && equalNilStr(state.type, type) && (state.value == NIL) && (value == NIL)
	  /* the same expression may have different values
	     due to side effects of the called function */
	  /*equalNilStr(state.value,value) */
	  ) {
	Printf(f, "%s, ", state.name);
	state.hold = true;
      } else {
	Append(f, state.name);
	if (state.type != NIL) {
	  Printf(f, ": %s", state.type);
	}
	if (state.value != NIL) {
	  Printf(f, ":= %s", state.value);
	}
	Append(f, ";\n");
	state.hold = false;
      }
    }
    /* at the next call the current argument will be the previous one */
    state.mode = mode;
    state.name = name;
    state.type = type;
    state.value = value;
  }

  /* -----------------------------------------------------------------------------
   * getProxyName()
   *
   * Test to see if a type corresponds to something wrapped with a proxy class
   * Return NULL if not otherwise the proxy class name
   * ----------------------------------------------------------------------------- */

  String *getProxyName(SwigType *t) {
    if (proxy_flag) {
      Node *n = classLookup(t);
      if (n) {
	return Getattr(n, "sym:name");
      }
    }
    return NULL;
  }

  /*************** language processing ********************/

  /* ------------------------------------------------------------
   * main()
   * ------------------------------------------------------------ */

  virtual void main(int argc, char *argv[]) {

    int doxygen_translator_flags = 0;

    SWIG_library_directory("modula3");

    // Look for certain command line options
    for (int i = 1; i < argc; i++) {
      if (argv[i]) {
	if (strcmp(argv[i], "-generateconst") == 0) {
	  if (argv[i + 1]) {
	    constantfilename = NewString(argv[i + 1]);
	    Swig_mark_arg(i);
	    Swig_mark_arg(i + 1);
	    i++;
	  } else {
	    Swig_arg_error();
	  }
	} else if (strcmp(argv[i], "-generaterename") == 0) {
	  if (argv[i + 1]) {
	    renamefilename = NewString(argv[i + 1]);
	    Swig_mark_arg(i);
	    Swig_mark_arg(i + 1);
	    i++;
	  } else {
	    Swig_arg_error();
	  }
	} else if (strcmp(argv[i], "-generatetypemap") == 0) {
	  if (argv[i + 1]) {
	    typemapfilename = NewString(argv[i + 1]);
	    Swig_mark_arg(i);
	    Swig_mark_arg(i + 1);
	    i++;
	  } else {
	    Swig_arg_error();
	  }
	} else if (strcmp(argv[i], "-noproxy") == 0) {
	  Swig_mark_arg(i);
	  proxy_flag = false;
	} else if (strcmp(argv[i], "-generateignores") == 0) {
	  Swig_mark_arg(i);
	  ignores_flag = true;
	} else if (strcmp(argv[i], "-generatem3make") == 0) {
	  Swig_mark_arg(i);
	  m3make_flag = true;
	} else if (strcmp(argv[i], "-generateenumchecks") == 0) {
	  Swig_mark_arg(i);
	  genEnumChecks_flag = true;
        } else if ((strcmp(argv[i], "-doxygen") == 0)) {
          Swig_mark_arg(i);
          doxygen = true;
          scan_doxygen_comments = 1;
        } else if (strcmp(argv[i], "-dynlink") == 0) {
          Swig_mark_arg(i);
          dynLink_flag = true;
	} else if (strcmp(argv[i], "-help") == 0) {
	  Printf(stdout, "%s\n", usage);
	}
      }
    }

    //Use the Java doxygen class and substitute the M3 style comment
    //tags when needed.
    if (doxygen)
      doxygenTranslator = new JavaDocConverter(doxygen_translator_flags);

    // Add a symbol to the parser for conditional compilation
    Preprocessor_define("SWIGMODULA3 1", 0);

    // Add typemap definitions
    SWIG_typemap_lang("modula3");
    SWIG_config_file("modula3.swg");

    allow_overloading();
  }

  /* ---------------------------------------------------------------------
   * top()
   * --------------------------------------------------------------------- */

  virtual int top(Node *n) {
    if (hasContent(constantfilename) || hasContent(renamefilename) || hasContent(typemapfilename)) {
      int result = SWIG_OK;
      if (hasContent(constantfilename)) {
	result = generateConstantTop(n) && result;
      }
      if (hasContent(renamefilename)) {
	result = generateRenameTop(n) && result;
      }
      if (hasContent(typemapfilename)) {
	result = generateTypemapTop(n) && result;
      }
      return result;
    } else {
      return generateM3Top(n);
    }
  }

  void scanConstant(File *file, Node *n) {
    Node *child = firstChild(n);
    while (child != NIL) {
      String *constname = NIL;
      String *type = nodeType(child);
      if ((Strcmp(type, "enumitem") == 0)
	  || (Strcmp(type, "constant") == 0)) {
#if 1
	constname = getQualifiedName(child);
#else
	constname = Getattr(child, "value");
	if ((!hasContent(constname))
	    || (('0' <= *Char(constname)) && (*Char(constname) <= '9'))) {
	  constname = Getattr(child, "name");
	}
#endif
      }
      if (constname != NIL) {
//printAttrs(child);
        
        /*
         * 
         * 
peter test

if (is_int(s)) {
  i = strtolint(s)
  if (i > Min_Int && i < Max_Int) {
     format as int
  else
     format as long
if (is_float(s)) {
  d = strtod(s)
  if (d > Min_Flt && d < Max_Flt) {
    format as real
  else
    format as longreal
}
         * 
         * 
         */ 
        
/*        
String *value = Getattr(child, "value");
String *fmtStr;
printf("------%s\n",Char(value));
if (Strstr(value,".")) {
//  fmtStr = NewString("long double");
	Printf(file, "  printf(\"%%%%constnumeric(%%Lg) %s;\\n\", (long double)%s);\n", constname, constname);
} else {
//  fmtStr = NewString("long int");  
	Printf(file, "  printf(\"%%%%constnumeric(%%ld) %s;\\n\", (long int)#%s);\n", constname, constname);
}
*/
      }
      scanConstant(file, child);
      child = nextSibling(child);
    }
  }

  int generateConstantTop(Node *n) {
    File *file = openWriteFile(NewStringf("%s.c", constantfilename));
    if (CPlusPlus) {
      Printf(file, "#include <cstdio>\n");
    } else {
      Printf(file, "#include <stdio.h>\n");
    }
    Printf(file, "#include \"%s\"\n", input_file);
    Printf(file, "\n");
    Printf(file, "int main (int argc, char *argv[]) {\n");
    Printf(file, "\
/*This progam must work for floating point numbers and integers.\n\
  Thus all numbers are converted to double precision floating point format.*/\n");
    scanConstant(file, n);
    Printf(file, "  return 0;\n");
    Printf(file, "}\n");
    Delete(file);
    return SWIG_OK;
  }

  void scanRename(File *file, Node *n) {
    Node *child = firstChild(n);
    while (child != NIL) {
      String *type = nodeType(child);
      if (Strcmp(type, "cdecl") == 0) {
	ParmList *p = Getattr(child, "parms");
	if (p != NIL) {
	  String *name = getQualifiedName(child);
	  String *m3name = nameToModula3(name, true);
	  /*don't know how to get the original C type identifiers */
	  Printf(file, "%%rename(\"%s\") %s;\n", m3name, name);
	  Delete(name);
	  Delete(m3name);
	}
      }
      scanRename(file, child);
      child = nextSibling(child);
    }
  }

  int generateRenameTop(Node *n) {
    File *file = openWriteFile(NewStringf("%s.i", renamefilename));
    Printf(file, "\
/* This file was generated from %s\n\
   by SWIG with option -generaterename. */\n\
\n", input_file);
    scanRename(file, n);
    Delete(file);
    return SWIG_OK;
  }

  void scanTypemap(File *file, Node *n) {
    Node *child = firstChild(n);
    while (child != NIL) {
      String *type = nodeType(child);
      String *storage = Getattr(child, "storage");
      if ((Strcmp(type, "class") == 0) || ((Strcmp(type, "cdecl") == 0) && (storage != NIL)
					   && (Strcmp(storage, "typedef") == 0))) {
	String *name = getQualifiedName(child);
	String *m3name = nameToModula3(name, true);
	Printf(file, "%%typemap(\"m3wrapintype\") %s %%{%s%%}\n", name, m3name);
	Printf(file, "%%typemap(\"m3rawintype\") %s %%{%s%%}\n", name, m3name);
	Printf(file, "\n");
      }
      scanTypemap(file, child);
      child = nextSibling(child);
    }
  }

  int generateTypemapTop(Node *n) {
    File *file = openWriteFile(NewStringf("%s.i", typemapfilename));
    Printf(file, "\
/* This file was generated from %s\n\
   by SWIG with option -generatetypemap. */\n\
\n", input_file);
    scanTypemap(file, n);
    Delete(file);
    return SWIG_OK;
  }

  //Generate Doxygen comments if they exist
  String* do_oxy(Node *n, const char *msg) {
    String *doxygen_comments = NIL;

    if (msg == NIL) {}; //remove compiler warning
#ifdef DEBUG
    printf("IN %s\n",msg);
#endif

    if (doxygen && doxygenTranslator->hasDocumentation(n)) {
      doxygen_comments = doxygenTranslator->getDocumentation(n, 0);
      Replaceall(doxygen_comments, "/*", "\n(*");
      Replaceall(doxygen_comments, "*/", "*)\n");
    }
    return doxygen_comments;
  }

  int generateM3Top(Node *n) {
    /* Initialize all of the output files */
    outfile = Getattr(n, "outfile");

    f_begin = NewFile(outfile, "w", SWIG_output_files());
    if (!f_begin) {
      FileErrorDisplay(outfile);
      Exit(EXIT_FAILURE);
    }
    f_runtime = NewString("");
    f_init = NewString("");
    f_header = NewString("");
    f_wrappers = NewString("");

    m3makefile = NewString("");

    /* Register file targets with the SWIG file handler */
    Swig_register_filebyname("header", f_header);
    Swig_register_filebyname("wrapper", f_wrappers);
    Swig_register_filebyname("begin", f_begin);
    Swig_register_filebyname("runtime", f_runtime);
    Swig_register_filebyname("init", f_init);

    Swig_register_filebyname("m3rawintf", m3raw_intf.f);
    Swig_register_filebyname("m3rawimpl", m3raw_impl.f);
    Swig_register_filebyname("m3wrapintf", m3wrap_intf.f);
    Swig_register_filebyname("m3wrapimpl", m3wrap_impl.f);
    Swig_register_filebyname("m3makefile", m3makefile);

    swig_types_hash = NewHash();

    String *name = Getattr(n, "name");
    // Make the intermediary class and module class names. The intermediary class name can be set in the module directive.
    Node *optionsnode = Getattr(Getattr(n, "module"), "options");
    if (optionsnode != NIL) {
      String *m3raw_name_tmp = Getattr(optionsnode, "m3rawname");
      if (m3raw_name_tmp != NIL) {
	m3raw_name = Copy(m3raw_name_tmp);
      }
    }
    if (m3raw_name == NIL) {
      m3raw_name = NewStringf("%sRaw", name);
    }
    //To import the raw interface
    addImportsSub(m3wrap_impl.import,m3raw_name);

    m3wrap_name = Copy(name);

    proxy_class_def = NewString("");
    proxy_class_code = NewString("");
    m3raw_baseclass = NewString("");
    m3raw_interfaces = NewString("");
    m3raw_class_modifiers = NewString("");	// package access only to the intermediary class by default
    m3raw_imports = NewString("");
    m3raw_cppcasts_code = NewString("");
    m3wrap_modifiers = NewString("public");
    module_baseclass = NewString("");
    module_interfaces = NewString("");
    module_imports = NewString("");
    upcasts_code = NewString("");

    Swig_banner(f_begin);

    Printf(f_runtime, "\n");
    Printf(f_runtime, "#define SWIGMODULA3\n");
    Printf(f_runtime, "\n");

    Swig_name_register("wrapper", "Modula3_%f");

    Printf(f_wrappers, "\n#ifdef __cplusplus\n");
    Printf(f_wrappers, "extern \"C\" {\n");
    Printf(f_wrappers, "#endif\n\n");

    constant_values = NewHash();
    scanForConstPragmas(n);
    enumeration_coll = NewHash();
    collectEnumerations(enumeration_coll, n);

    enum_coll = NewHash();

    var_coll = NewHash();

    /* Emit code */
    Language::top(n);

    // Generate m3makefile
    // This is unnecessary if SWIG is invoked from Quake.
    // The default is not to generate one and only do so if the command line specifies it
    if (m3make_flag) {

      File *file = openWriteFile(NewStringf("%sm3makefile", SWIG_output_directory()));

      Printf(file, "%% automatically generated quake file for %s\n\n", name);

      /* Write the fragments written by '%insert'
         collected while 'top' processed the parse tree */
      Printv(file, m3makefile, NIL);

      Printf(file, "import(\"libm3\")\n");
      //Printf(file, "import_lib(\"%s\",\"/usr/lib\")\n", name);
      Printf(file, "module(\"%s\")\n", m3raw_name);
      Printf(file, "module(\"%s\")\n\n", m3wrap_name);

      if (targetlibrary != NIL) {
	Printf(file, "library(\"%s\")\n", targetlibrary);
      } else {
	Printf(file, "library(\"m3%s\")\n", name);
      }
      Delete(file);
    }

    // Generate the raw interface
    {
      File *file = openWriteFile(NewStringf("%s%s.i3", SWIG_output_directory(), m3raw_name));

      emitBanner(file);

      if (unsafe_raw) {
	Printf(file, "UNSAFE ");
      }
      Printf(file, "INTERFACE %s;\n\n", m3raw_name);

      emitImportStatements(m3raw_intf.import, file);
      Printf(file, "\n");

      // Write the interface generated within 'top'
      Printv(file, m3raw_intf.f, NIL);

      Printf(file, "\nEND %s.\n", m3raw_name);
      Delete(file);
    }

    // Generate the raw module
    {
      File *file = openWriteFile(NewStringf("%s%s.m3", SWIG_output_directory(), m3raw_name));

      emitBanner(file);

      if (unsafe_raw) {
	Printf(file, "UNSAFE ");
      }
      Printf(file, "MODULE %s;\n\n", m3raw_name);

      emitImportStatements(m3raw_impl.import, file);
      Printf(file, "\n");

      // will be empty usually
      Printv(file, m3raw_impl.f, NIL);

      Printf(file, "BEGIN\nEND %s.\n", m3raw_name);
      Delete(file);
    }

    // Generate the interface for the comfort wrappers
    {
      File *file = openWriteFile(NewStringf("%s%s.i3", SWIG_output_directory(), m3wrap_name));

      emitBanner(file);

      Printf(file, "INTERFACE %s;\n", m3wrap_name);

      emitImportStatements(m3wrap_intf.import, file);
      Printf(file, "\n");

      {
	Iterator it = First(enumeration_coll);
	if (it.key != NIL) {
	  Printf(file, "TYPE\n");
	}
	for (; it.key != NIL; it = Next(it)) {
	  Printf(file, "\n");
	  emitEnumeration(file, it.key, it.item);
	}
      }

      // Add the wrapper methods
      Printv(file, m3wrap_intf.f, NIL);

      if (dynLink_flag) {
        Setattr(var_coll, "dynProcArr", "DynArr");
   
        Printf(file, "TYPE\n");
        Printf(file,"DynEnum = {%s};\n",dynProcs);
        Printf(file, "DynProcPublic = OBJECT\n  adr : ADDRESS;\n  METHODS\n  get() : TEXT;\n  END;\n");
        Printf(file,"DynProc <: DynProcPublic;\n");
        Printf(file,"DynArr = ARRAY DynEnum OF DynProc;\n");
      }

      //vars declaration
      {
        Iterator it = First(var_coll);
        if (it.key != NIL) {
          Printf(file, "VAR\n");
        }
        for (; it.key != NIL; it = Next(it)) {
          Printf(file, "\n");
          emitVar(file, it.key, it.item);
        }
      }

      // Finish off the class
      Printf(file, "\nEND %s.\n", m3wrap_name);
      Delete(file);
    }

    // Generate the wrapper routines implemented in Modula 3
    {
      File *file = openWriteFile(NewStringf("%s%s.m3", SWIG_output_directory(), m3wrap_name));

      emitBanner(file);

      if (unsafe_module) {
	Printf(file, "UNSAFE ");
      }
      Printf(file, "MODULE %s;\n\n", m3wrap_name);

      emitImportStatements(m3wrap_impl.import, file);
      Printf(file, "\n");

      // Add the wrapper methods
      Printv(file, m3wrap_impl.f, NIL);

      //include a proc to check the enum values when they are too large for a set
      if (genEnumChecks_flag) {
        Printf(file, "\nPROCEDURE CheckOption(opt : INTEGER; READONLY optArr : ARRAY OF INTEGER) : BOOLEAN =\nVAR found := FALSE;\nBEGIN\nFOR i := FIRST(optArr) TO LAST(optArr) DO\n    IF optArr[i] = opt THEN\nfound := TRUE;\nEND;\nEND;\nRETURN found;\nEND CheckOption;\n");
      }

      if (dynLink_flag) {
        Printf(file,"\nREVEAL\n  DynProc = DynProcPublic BRANDED OBJECT\n name : TEXT;\n  OVERRIDES\n  get := Get;\n  END;\n");
        Printf(file,"\nPROCEDURE Get(self : DynProc) : TEXT =\nBEGIN\n  RETURN self.name;\nEND Get;\n");
      }

      Printf(file, "\nBEGIN\n");
      //if initialisation needed we do it here
      if (dynLink_flag) {
        Printf(file, "dynProcArr := DynArr{%s}\n",dynInit);
      }
      Printf(file, "END %s.\n", m3wrap_name);
      Delete(file);
    }

    if (upcasts_code)
      Printv(f_wrappers, upcasts_code, NIL);

    Printf(f_wrappers, "#ifdef __cplusplus\n");
    Printf(f_wrappers, "}\n");
    Printf(f_wrappers, "#endif\n");

    // Output a Modula 3 type wrapper class for each SWIG type
    for (Iterator swig_type = First(swig_types_hash); swig_type.item != NIL; swig_type = Next(swig_type)) {
      emitTypeWrapperClass(swig_type.key, swig_type.item);
    }

    Delete(swig_types_hash);
    swig_types_hash = NULL;
    Delete(constant_values);
    constant_values = NULL;
    Delete(enumeration_coll);
    enumeration_coll = NULL;
    Delete(var_coll);
    var_coll = NULL;
    Delete(dynProcs);
    dynProcs = NULL;
    Delete(dynInit);
    dynInit = NULL;
    Delete(m3raw_name);
    m3raw_name = NULL;
    Delete(m3raw_baseclass);
    m3raw_baseclass = NULL;
    Delete(m3raw_interfaces);
    m3raw_interfaces = NULL;
    Delete(m3raw_class_modifiers);
    m3raw_class_modifiers = NULL;
    Delete(m3raw_imports);
    m3raw_imports = NULL;
    Delete(m3raw_cppcasts_code);
    m3raw_cppcasts_code = NULL;
    Delete(proxy_class_def);
    proxy_class_def = NULL;
    Delete(proxy_class_code);
    proxy_class_code = NULL;
    Delete(m3wrap_name);
    m3wrap_name = NULL;
    Delete(m3wrap_modifiers);
    m3wrap_modifiers = NULL;
    Delete(targetlibrary);
    targetlibrary = NULL;
    Delete(module_baseclass);
    module_baseclass = NULL;
    Delete(module_interfaces);
    module_interfaces = NULL;
    Delete(module_imports);
    module_imports = NULL;
    Delete(upcasts_code);
    upcasts_code = NULL;
    Delete(constantfilename);
    constantfilename = NULL;
    Delete(renamefilename);
    renamefilename = NULL;
    Delete(typemapfilename);
    typemapfilename = NULL;

    /* Close all of the files */
    Dump(f_runtime, f_begin);
    Dump(f_header, f_begin);
    Dump(f_wrappers, f_begin);
    Wrapper_pretty_print(f_init, f_begin);
    Delete(f_header);
    Delete(f_wrappers);
    Delete(f_init);
    Delete(f_runtime);
    Delete(f_begin);
    return SWIG_OK;
  }

  /* -----------------------------------------------------------------------------
   * emitBanner()
   * ----------------------------------------------------------------------------- */

  void emitBanner(File *f) {
    Printf(f, "(*******************************************************************************\n");
    Swig_banner_target_lang(f, " *");
    Printf(f, "*******************************************************************************)\n\n");
  }

  /* ----------------------------------------------------------------------
   * nativeWrapper()
   * ---------------------------------------------------------------------- */

  virtual int nativeWrapper(Node *n) {
    String *wrapname = Getattr(n, "wrap:name");

    if (!addSymbol(wrapname, n))
      return SWIG_ERROR;

    if (Getattr(n, "type")) {
      Swig_save("nativeWrapper", n, "name", NIL);
      Setattr(n, "name", wrapname);
      native_function_flag = true;
      functionWrapper(n);
      Swig_restore(n);
      native_function_flag = false;
    } else {
      Swig_error(input_file, line_number, "No return type for %%native method %s.\n", Getattr(n, "wrap:name"));
    }

    return SWIG_OK;
  }

  /* ----------------------------------------------------------------------
   * functionWrapper()
   * ---------------------------------------------------------------------- */

  virtual int functionWrapper(Node *n) {
    String *type = nodeType(n);
    String *funcType = Getattr(n, "modula3:functype");
    String *rawname = Getattr(n, "name");
    String *symname = Getattr(n, "sym:name");
    String *capname = capitalizeFirst(symname);
    String *staticMember = Getattr(n,"staticmemberfunctionHandler:storage");
    //String *wname = Swig_name_wrapper(symname)

    //static members lets lose the class prefix
    String *staticName = Getattr(n,"staticmemberfunctionHandler:sym:name");
    if (staticName != NIL) {
      capname = capitalizeFirst(staticName);
    }

    if (Strcmp(type, "cdecl") == 0) {

      //dump each function to be ignored useful for initial testing of a class
      if (ignores_flag && (rawname != NIL))
        printf("%%ignore %s;\n",Char(rawname));

      if (staticMember != NIL) {

	Setattr(n, "modula3:funcname", capname);
	emitCWrapper(n, capname);
      }

      if (funcType == NIL) {
	// no wrapper needed for plain functions

        //further changes for the static name change above
	if (staticName == NIL) {

	  emitM3RawPrototype(n, rawname, symname);
	  emitM3Wrapper(n, symname);

        } else {
	  emitM3RawPrototype(n, rawname, capname);
	  emitM3Wrapper(n, capname);
	}

      } else if (Strcmp(funcType, "method") == 0) {
	Setattr(n, "modula3:funcname", capname);
	emitCWrapper(n, capname);
	emitM3RawPrototype(n, capname, capname);
	emitM3Wrapper(n, capname);
      } else if (Strcmp(funcType, "accessor") == 0) {
	/*
	 * Generate the proxy class properties for public member variables.
	 * Not for enums and constants.
	 */
	if (proxy_flag && wrapping_member_flag && !enum_constant_flag) {
	  // Capitalize the first letter in the function name
	  Setattr(n, "proxyfuncname", capname);
	  Setattr(n, "imfuncname", symname);
	  if (hasPrefix(capname, "Set")) {
	    Setattr(n, "modula3:setname", capname);
	  } else {
	    Setattr(n, "modula3:getname", capname);
	  }

          emitCWrapper(n, capname);
          emitM3RawPrototype(n, capname, capname);
          emitM3Wrapper(n, capname);
	  //proxyClassFunctionHandler(n); 
	}
#ifdef DEBUG
      } else {
	Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number, "Function type <%s> unknown.\n", Char(funcType));
#endif
      }
    } else if (Strcmp(type, "constructor") == 0) {
      String *oname = Getattr(n, "sym:overname");
      String *capname_e;
      String *constr_name = Getattr(n,"constructorDeclaration:sym:name");

      if (Strcmp(constr_name,rawname) == 0) {
        //constructor has not been renamed
        capname_e = NewStringf("%s%s",capname,oname);
      } else {
        //constructor has been renamed
        capname_e = NewStringf("%s",capname);
      }
      emitCWrapper(n, capname_e);
      emitM3RawPrototype(n, capname_e, capname_e);
      emitM3Wrapper(n, capname_e);
    } else if (Strcmp(type, "destructor") == 0) {
      emitCWrapper(n, capname);
      emitM3RawPrototype(n, capname, capname);
      emitM3Wrapper(n, capname);
    }

    Delete(capname);

    return SWIG_OK;
  }

  /* ----------------------------------------------------------------------
   * emitCWrapper()
   *
   * Generate the wrapper in C which calls C++ methods.
   * ---------------------------------------------------------------------- */

  virtual int emitCWrapper(Node *n, const String *wname) {
    String *rawname = Getattr(n, "name");
    String *c_return_type = NewString("");
    String *cleanup = NewString("");
    String *outarg = NewString("");
    String *body = NewString("");
    Hash *throws_hash = NewHash();
    ParmList *l = Getattr(n, "parms");
    SwigType *t = Getattr(n, "type");
    String *symname = Getattr(n, "sym:name");
    bool is_ref = 0;

    String *abstract = Getattr(n, "abstract");
    //if abstract method dont emit
    if (abstract) return 0;

     //overrides here and in fname below
    String *type = nodeType(n);
    String *oname = Getattr(n, "sym:overname");
    Replaceall(oname, "__SWIG_", "");
    if ((Strcmp(oname,"0") != 0) && (Strcmp(type, "constructor") != 0)) {
      wname = NewStringf("%s%s",wname,oname);
    }

    if (!Getattr(n, "sym:overloaded")) {
      if (!addSymbol(wname, n)) {
	return SWIG_ERROR;
      }
    }
    // A new wrapper function object
    Wrapper *f = NewWrapper();

    /* Attach the non-standard typemaps to the parameter list. */
    Swig_typemap_attach_parms("ctype", l, f);

    /* Get return types */
    {
      String *tm = getMappedTypeNew(n, "ctype", "");
      if (tm != NIL) {
	Printf(c_return_type, "%s", tm);
      }
    }

    bool is_void_return = (Cmp(c_return_type, "void") == 0);

    Printv(f->def, " SWIGEXPORT ", c_return_type, " ", wname, "(", NIL);

    // Emit all of the local variables for holding arguments.
    emit_parameter_variables(l, f);

    /* Attach the standard typemaps */
    emit_attach_parmmaps(l, f);
    Setattr(n, "wrap:parms", l);

    // Generate signature and argument conversion for C wrapper
    {
      Parm *p;
      attachParameterNames(n, "tmap:name", "c:wrapname", "m3arg%d");
      bool gencomma = false;
      for (p = skipIgnored(l, "in"); p; p = skipIgnored(p, "in")) {

	String *arg = Getattr(p, "c:wrapname");
	{
	  /* Get the ctype types of the parameter */
	  String *c_param_type = getMappedType(p, "ctype");
	  // Add parameter to C function
	  Printv(f->def, gencomma ? ", " : "", c_param_type, " ", arg, NIL);
	  Delete(c_param_type);
	  gencomma = true;
	}

	// Get typemap for this argument
	String *tm = getMappedType(p, "in");
	if (tm != NIL) {
	  addThrows(throws_hash, "in", p);
	  Replaceall(tm, "$input", arg);
	  Setattr(p, "emit:input", arg);	/*??? */
	  Printf(f->code, "%s\n", tm);
	  p = Getattr(p, "tmap:in:next");
	} else {
	  p = nextSibling(p);
	}
      }
    }

    /* Insert constraint checking code */
    {
      Parm *p;
      for (p = l; p;) {
	String *tm = Getattr(p, "tmap:check");
	if (tm != NIL) {
	  addThrows(throws_hash, "check", p);
	  Replaceall(tm, "$target", Getattr(p, "lname"));	/* deprecated */
	  Replaceall(tm, "$arg", Getattr(p, "emit:input"));	/* deprecated? */
	  Replaceall(tm, "$input", Getattr(p, "emit:input"));
	  Printv(f->code, tm, "\n", NIL);
	  p = Getattr(p, "tmap:check:next");
	} else {
	  p = nextSibling(p);
	}
      }
    }

    /* Insert cleanup code */
    {
      Parm *p;
      for (p = l; p;) {
	String *tm = Getattr(p, "tmap:freearg");
	if (tm != NIL) {
	  addThrows(throws_hash, "freearg", p);
	  Replaceall(tm, "$source", Getattr(p, "emit:input"));	/* deprecated */
	  Replaceall(tm, "$arg", Getattr(p, "emit:input"));	/* deprecated? */
	  Replaceall(tm, "$input", Getattr(p, "emit:input"));
	  Printv(cleanup, tm, "\n", NIL);
	  p = Getattr(p, "tmap:freearg:next");
	} else {
	  p = nextSibling(p);
	}
      }
    }

    /* Insert argument output code */
    {
      Parm *p;
      for (p = l; p;) {
	String *tm = Getattr(p, "tmap:argout");
	if (tm != NIL) {
	  addThrows(throws_hash, "argout", p);
	  Replaceall(tm, "$source", Getattr(p, "emit:input"));	/* deprecated */
	  Replaceall(tm, "$target", Getattr(p, "lname"));	/* deprecated */
	  Replaceall(tm, "$arg", Getattr(p, "emit:input"));	/* deprecated? */
	  Replaceall(tm, "$result", "cresult");
	  Replaceall(tm, "$input", Getattr(p, "emit:input"));
	  Printv(outarg, tm, "\n", NIL);
	  p = Getattr(p, "tmap:argout:next");
	} else {
	  p = nextSibling(p);
	}
      }
    }

    // Get any Modula 3 exception classes in the throws typemap
    ParmList *throw_parm_list = NULL;
    if ((throw_parm_list = Getattr(n, "catchlist"))) {
      Swig_typemap_attach_parms("throws", throw_parm_list, f);
      Parm *p;
      for (p = throw_parm_list; p; p = nextSibling(p)) {
	addThrows(throws_hash, "throws", p);
      }
    }

    if (Cmp(nodeType(n), "constant") == 0) {
      // Wrapping a constant hack
      Swig_save("functionWrapper", n, "wrap:action", NIL);

      // below based on Swig_VargetToFunction()
      SwigType *ty = Swig_wrapped_var_type(Getattr(n, "type"), use_naturalvar_mode(n));
      Setattr(n, "wrap:action", NewStringf("%s = (%s)(%s);", Swig_cresult_name(), SwigType_lstr(ty, 0), Getattr(n, "value")));
    }

    Setattr(n, "wrap:name", wname);

    // Now write code to make the function call
    if (!native_function_flag) {
      String *actioncode = emit_action(n);

      if (Cmp(nodeType(n), "constant") == 0) {
        Swig_restore(n);
      }

      /* Return value if necessary  */
      String *tm;
      if ((tm = Swig_typemap_lookup_out("out", n, Swig_cresult_name(), f, actioncode))) {
	addThrows(throws_hash, "out", n);
	Replaceall(tm, "$source", Swig_cresult_name());	/* deprecated */
	Replaceall(tm, "$target", "cresult");	/* deprecated */
	Replaceall(tm, "$result", "cresult");
	Printf(f->code, "%s", tm);
	if (hasContent(tm))
	  Printf(f->code, "\n");
      } else {
	Swig_warning(WARN_TYPEMAP_OUT_UNDEF, input_file, line_number, "Unable to use return type %s in function %s.\n", SwigType_str(t, 0), rawname);
      }
      emit_return_variable(n, t, f);

      if (!is_void_return) {
        //Add the cresult local properly initialised
        if (Strstr(c_return_type, "&")) {
          //reference type
          is_ref = 1;
          Replaceall(c_return_type, "&", "*"); //make pointer return
          Wrapper_add_localv(f, "cresult", c_return_type, "cresult", NIL);
        } else {
          Wrapper_add_localv(f, "cresult", c_return_type, "cresult", NIL);
        }
      }
    }

    /* Output argument output code */
    Printv(f->code, outarg, NIL);

    /* Output cleanup code */
    Printv(f->code, cleanup, NIL);

    /* Look to see if there is any newfree cleanup code */
    if (GetFlag(n, "feature:new")) {
      String *tm = Swig_typemap_lookup("newfree", n, Swig_cresult_name(), 0);
      if (tm != NIL) {
	addThrows(throws_hash, "newfree", n);
	Replaceall(tm, "$source", Swig_cresult_name());	/* deprecated */
	Printf(f->code, "%s\n", tm);
      }
    }

    /* See if there is any return cleanup code */
    if (!native_function_flag) {
      String *tm = Swig_typemap_lookup("ret", n, Swig_cresult_name(), 0);
      if (tm != NIL) {
	Replaceall(tm, "$source", Swig_cresult_name());	/* deprecated */
	Printf(f->code, "%s\n", tm);
      }
    }

    /* Finish C wrapper */
    Printf(f->def, ") {");

    if (!is_void_return) {
      if (is_ref) {
        Printv(f->code, "    return *cresult;\n", NIL);
      } else {
        //orig
        Printv(f->code, "    return cresult;\n", NIL);
      }
    }
    Printf(f->code, "}\n");

    /* Substitute the cleanup code */
    Replaceall(f->code, "$cleanup", cleanup);

    /* Substitute the function name */
    Replaceall(f->code, "$symname", symname);

    if (!is_void_return) {
      Replaceall(f->code, "$null", "0");
    } else {
      Replaceall(f->code, "$null", "");
    }

    /* Dump the function out */
    if (!native_function_flag) {
      Wrapper_print(f, f_wrappers);
    }

    Delete(c_return_type);
    Delete(cleanup);
    Delete(outarg);
    Delete(body);
    Delete(throws_hash);
    DelWrapper(f);
    return SWIG_OK;
  }

  /* ----------------------------------------------------------------------
   * emitM3RawPrototype()
   *
   * Generate an EXTERNAL procedure declaration in Modula 3
   * which is the interface to an existing C routine or a C wrapper.
   * ---------------------------------------------------------------------- */

  virtual int emitM3RawPrototype(Node *n, const String *cname, const String *m3name) {
    String *abstract = Getattr(n, "abstract");
    //if abstract method dont emit
    if (abstract) return 0;

    String *im_return_type = NewString("");
    //String   *symname = Getattr(n,"sym:name");
    String *staticMember = Getattr(n,"staticmemberfunctionHandler:storage");

    ParmList *l = Getattr(n, "parms");

    /* Attach the non-standard typemaps to the parameter list. */
    Swig_typemap_attach_parms("m3rawinmode", l, NULL);
    Swig_typemap_attach_parms("m3rawintype", l, NULL);

    String *type = nodeType(n);
    String *funcType = Getattr(n, "modula3:functype");
    String *oname = Getattr(n, "sym:overname");

    //renames checking
    Replaceall(oname, "__SWIG_", "");
    if ((Strcmp(oname,"0") != 0) && (Strcmp(type, "constructor") != 0)) {
      m3name = NewStringf("%s%s",m3name,oname);
    }

    /* Get return types */
    bool has_return;
    {
      String *tm = getMappedTypeNew(n, "m3rawrettype", "");
      if (tm != NIL) {
	Printf(im_return_type, "%s", tm);
	//add raw ret type imports
        addImports(m3raw_intf.import, "m3rawrettype", n);
      }
      has_return = hasContent(tm);
    }

    /* cname is the original name if 'n' denotes a C function
       and it is the relabeled name (sym:name) if 'n' denotes a C++ method or similar */
    m3raw_intf.enterBlock(no_block);

      if ((funcType == NIL)  && (Strcmp(type, "cdecl") == 0) && (staticMember == NIL) ) {
        if (dynLink_flag == false) {
          Printf(m3raw_intf.f, "\n<* EXTERNAL %s *>\nPROCEDURE %s (", cname, m3name);
        } else {
          Printf(m3raw_intf.f, "\nTYPE\n %s = PROCEDURE(", cname);
        }
      } else {
        Printf(m3raw_intf.f, "\n<* EXTERNAL %s *>\nPROCEDURE %s (", m3name, m3name);
      }

    // Generate signature for raw interface
    {
      Parm *p;
      writeArgState state;
      attachParameterNames(n, "tmap:rawinname", "modula3:rawname", "arg%d");
      for (p = skipIgnored(l, "m3rawintype"); p; p = skipIgnored(p, "m3rawintype")) {

	/* Get argument passing mode, should be one of VALUE, VAR, READONLY */
	String *mode = Getattr(p, "tmap:m3rawinmode");
	String *argname = Getattr(p, "modula3:rawname");
	String *im_param_type = getMappedType(p, "m3rawintype");
	addImports(m3raw_intf.import, "m3rawintype", p);

	writeArg(m3raw_intf.f, state, mode, argname, im_param_type, NIL);
	if (im_param_type != NIL) {
	  p = Getattr(p, "tmap:m3rawintype:next");
	} else {
	  p = nextSibling(p);
	}
      }
      writeArg(m3raw_intf.f, state, NIL, NIL, NIL, NIL);
    }

    /* Finish M3 raw prototype */
    Printf(m3raw_intf.f, ")");
    // neither a C wrapper nor a plain C function may throw an exception
    //generateThrowsClause(throws_hash, m3raw_intf.f);
    if (has_return) {
      Printf(m3raw_intf.f, ": %s", im_return_type);
    }
    Printf(m3raw_intf.f, ";\n");

    Delete(im_return_type);
    return SWIG_OK;
  }

  /* -----------------------------------------------------------------------
   * variableWrapper()
   * ----------------------------------------------------------------------- */

  virtual int variableWrapper(Node *n) {
    Language::variableWrapper(n);
    return SWIG_OK;
  }

  /* -----------------------------------------------------------------------
   * globalvariableHandler()
   * ----------------------------------------------------------------------- */

  virtual int globalvariableHandler(Node *n) {
    SwigType *t = Getattr(n, "type");
    String *tm;

    // Get the variable type
    if ((tm = getMappedTypeNew(n, "m3wraptype", ""))) {
      substituteClassname(t, tm);
    }

    variable_name = Getattr(n, "sym:name");
    variable_type = Copy(tm);

    // Get the variable type expressed in terms of Modula 3 equivalents of C types
    if ((tm = getMappedTypeNew(n, "m3rawtype", ""))) {
      m3raw_intf.enterBlock(no_block);
      Printf(m3raw_intf.f, "\n<* EXTERNAL *> VAR %s: %s;\n", variable_name, tm);
    }

    Printf(m3wrap_impl.f, "\n\n");

    return 1;
  }

  long getConstNumeric(Node *n) {
    String *constnumeric = Getfeature(n, "constnumeric");
    String *name = Getattr(n, "name");
    long numvalue;
    if (constnumeric == NIL) {
      Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number, "Feature 'constnumeric' is necessary to obtain value of %s.\n", name);
      return 0;
    } else if (!strToL(constnumeric, numvalue)) {
      Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number,
		   "The feature 'constnumeric' of %s specifies value <%s> which is not an integer constant.\n", name, constnumeric);
      return 0;
    } else {
      return numvalue;
    }
  }

  /* ------------------------------------------------------------------------
   * generateIntConstant()
   *
   * Considers node as an integer constant definition
   * and generate a Modula 3 constant definition.
   * ------------------------------------------------------------------------ */
  void generateIntConstant(Node *n, String *name) {
    String *value = Getattr(n, "value");
    String *type = Getfeature(n, "modula3:constint:type");
    String *conv = Getfeature(n, "modula3:constint:conv");

    if (name == NIL) {
      name = Getattr(n, "sym:name");
    }

    long numvalue;
    bool isSimpleNum = strToL(value, numvalue);
    if (!isSimpleNum) {
      numvalue = getConstNumeric(n);
    }

    String *m3value;
    if ((conv == NIL) || ((Strcmp(conv, "set:int") != 0) && (Strcmp(conv, "int:set") != 0))) {
      /* The original value of the constant has precedence over
         'constnumeric' feature since we like to keep
         the style (that is the base) of simple numeric constants */
      if (isSimpleNum) {
        m3value = BaseConv(value);
	/* If we cannot easily obtain the value of a numeric constant,
	   we use the results given by a C compiler. */
      } else {
	m3value = Copy(Getfeature(n, "constnumeric"));        
      }
    } else {
      // if the value can't be converted, it is ignored
      if (convertInt(numvalue, numvalue, conv)) {
	m3value = NewStringf("%d", numvalue);
      } else {
	m3value = NIL;
      }
    }

    if (m3value != NIL) {
      m3wrap_intf.enterBlock(constant);
      Printf(m3wrap_intf.f, "%s", name);
      if (hasContent(type)) {
	Printf(m3wrap_intf.f, ": %s", type);
      }
      Printf(m3wrap_intf.f, " = %s;\n", m3value);
      Delete(m3value);
    }
  }

  /* -----------------------------------------------------------------------
   * generateSetConstant()
   *
   * Considers node as a set constant definition
   * and generate a Modula 3 constant definition.
   * ------------------------------------------------------------------------ */
  void generateSetConstant(Node *n, String *name) {
    String *value = Getattr(n, "value");
    String *type = Getfeature(n, "modula3:constset:type");
    String *setname = Getfeature(n, "modula3:constset:set");
    String *basename = Getfeature(n, "modula3:constset:base");
    String *conv = Getfeature(n, "modula3:constset:conv");

    m3wrap_intf.enterBlock(constant);

    Printf(m3wrap_intf.f, "%s", name);
    if (type != NIL) {
      Printf(m3wrap_intf.f, ":%s ", type);
    }
    Printf(m3wrap_intf.f, " = %s{", setname);

    long numvalue = 0;
    if (!strToL(value, numvalue)) {
      numvalue = getConstNumeric(n);
    }
    convertInt(numvalue, numvalue, conv);

    bool isIntType = Strcmp(basename, "CARDINAL") == 0;
    Hash *items = NIL;
    if (!isIntType) {
      Hash *enumeration = Getattr(enumeration_coll, basename);
      if (enumeration == NIL) {
	Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number, "There is no enumeration <%s> as needed for the set.\n", setname);
	isIntType = true;
      } else {
	items = Getattr(enumeration, "items");
      }
    }

    bool gencomma = false;
    int bitpos = 0;
    while (numvalue > 0) {
      if ((numvalue & 1) != 0) {
	if (isIntType) {
	  if (gencomma) {
	    Printv(m3wrap_intf.f, ",", NIL);
	  }
	  gencomma = true;
	  Printf(m3wrap_intf.f, "%d", bitpos);
	} else {
	  char bitval[15];
	  sprintf(bitval, "%d", bitpos);
	  String *bitname = Getattr(items, bitval);
	  if (bitname == NIL) {
	    Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number, "Enumeration <%s> has no value <%s>.\n", setname, bitval);
	  } else {
	    if (gencomma) {
	      Printv(m3wrap_intf.f, ",", NIL);
	    }
	    gencomma = true;
	    Printf(m3wrap_intf.f, "%s.%s", basename, bitname);
	  }
	}
      }
      numvalue >>= 1;
      bitpos++;
    }
    Printf(m3wrap_intf.f, "};\n");
  }

  String*  ToUpper(String *s) {
    int i=0;
    char *str = Char(s);
    while (str[i]) {
      str[i] = toupper(str[i]);
      i++;
    }
    return s;
  }

  int ContainsAlpha(String *s) {
    int i=0;
    char *str = Char(s);
    while (str[i]) {
      if (isalpha(str[i])) return 1;
      i++;
    }
    return 0;
  }
  
  String*  BaseConv(String *value) {
    String *m3value;
    if (hasPrefix(value, "0x")) {
      m3value = NewStringf("16_%s", Char(value) + 2);
    } else if ((Len(value) > 1) && (*Char(value) == '0')) {
      m3value = NewStringf("8_%s", Char(value) + 1);
    } else {
      m3value = Copy(value);
    }
    return m3value;
  }
  
/* peter
 * was working on a couple of things here. 
 * first was constants and the problem that constnumeric tried
 * to solve which was #define x + y || z 
 * in the h file. If its not simple and not a macro then
 * swig cant parse the expression so it writes a small
 * program to print out constnumeric values which a c compiler
 * can parse and print out. but it treats all integer
 * types as floating point which is rubbish really. We should
 * be able to get the proper type if its a word integer float
 * whatever.
 * 
 */
  void generateConstant(Node *n) {
    // any of the special interpretation disables the default behaviour
    String *enumitem = Getfeature(n, "modula3:enumitem:name");
    String *constset = Getfeature(n, "modula3:constset:name");
    String *constint = Getfeature(n, "modula3:constint:name");
    String *value = Getattr(n, "value");
    String *name = Getattr(n, "sym:name");
      
    if (hasContent(enumitem) || hasContent(constset) || hasContent(constint)) {
      if (hasContent(constset)) {  
	generateSetConstant(n, constset);
      }
      
      if (hasContent(constint)) {       
	generateIntConstant(n, constint);
      }
    } else {
      if (name == NIL) {
	name = Getattr(n, "name");
      }
      m3wrap_intf.enterBlock(constant);
      String *m3value;
            
      //add base
      m3value = BaseConv(value);
//peter test constnumeric
//      m3value = Copy(Getfeature(n, "constnumeric"));

//int parseErr;
//int parseRes = Preprocessor_expr(m3value, &parseErr);
//printf("parseres %d\n",parseRes);

      //should strip L or LL or U or UL or ULL
      //strip any ul UL suffixes from the constant
      String *tmpStr = ToUpper(NewString(m3value));

      char *ul = strchr(Char(tmpStr), 'U');
      if (ul) {
        int size = ul - Char(tmpStr);
        m3value = NewStringWithSize(m3value,size);
      }
      Printf(m3wrap_intf.f, "%s = %s;\n", name, m3value);
    }
  }

  void emitEnumeration(File *file, String *name, Node *n) {
    Printf(file, "%s = {", name);
    int i;
    bool gencomma = false;
    int max = aToL(Getattr(n, "max"));
    Hash *items = Getattr(n, "items");
    for (i = 0; i <= max; i++) {
      if (gencomma) {
	Printf(file, ",");
      }
      Printf(file, "\n");
      gencomma = true;
      char numstr[15];
      sprintf(numstr, "%d", i);
      String *name = Getattr(items, numstr);
      if (name != NIL) {
	Printv(file, name, NIL);
      } else {
	Printf(file, "Dummy%d", i);
      }
    }
    Printf(file, "\n};\n");
  }

  void emitVar(File *file, String *name, String *type) {
    Printv(file, name, ":", type, ";", NIL);
  }

  /* -----------------------------------------------------------------------
   * constantWrapper()
   *
   * Handles constants and enumeration items.
   * ------------------------------------------------------------------------ */

  virtual int constantWrapper(Node *n) {
    generateConstant(n);
    return SWIG_OK;
  }

#if 1
// enumerations are handled like constant definitions
  /* -----------------------------------------------------------------------------
   * enumDeclaration()
   * ----------------------------------------------------------------------------- */

  virtual int enumDeclaration(Node *n) {

    int parseErr;
    bool dupl;
    
    Preprocessor_expr_init();

    String *ntype = nodeType(n);
    //String *name = Getattr(n, "name");
    String *symName = Getattr(n, "sym:name");
    //if an anonymous enum give it a name
    if (symName == NIL) {
      symName = NewStringf("Enum%d",enumAnon);
      enumAnon++;
    }

    //if use symname here instead of name then can rename enums
    String *enumName = NewString(symName);
    String *access = Getattr(n, "access");

    Node *m = parentNode(n);
    String *pntype = nodeType(m);
    String *pname = NIL;
    if (Strcmp(pntype, "class") == 0) {
      pname = Getattr(m, "name");
    }

    if (access != NIL) {
      //protected and private enums not processed
      if (Strcmp(access,"public") != 0 ) return 0;
    }

    if (Strstr(enumName, "::")) {
      //ensure pname is defined for the replace below. Namespace is like class scope
      //This fixes imported enums with class extension to look like local enums
      Replaceall(enumName, "::", "_");
    }

    if (Strcmp(ntype, "enum") == 0) {

      Hash *enumHash = NewHash();
      Hash *thisEnumHash = NewHash(); //the hash for this new enum
      List *enumList = NewList();     //so we can get the order back
      //the hash with the value as key so we can check for duplicates and form an enum from
      //an improper one
      Hash *valHash = NewHash(); 
      
      //save in global hash for runtime checks
      Setattr(enum_coll,enumName,enumHash);

      Setattr(enumHash,"enums",thisEnumHash);
      Setattr(enumHash,"order",enumList);

      Node *child = firstChild(n);

      long max = INT_MIN;
      long min = INT_MAX;
      int enumCount;
      int proper = 1;
      char ival[15];
      dupl = false;
      
      while (child != NIL) {

        String *oxy = do_oxy(child,"EnumDeclaration");
        Replaceall(oxy, "\n", "");

        //String *ename = Getattr(child, "name"); //cannot handle renames of an enumitem
        String *ename = Getattr(child, "sym:name");
        String *enumValue = Getattr(child, "enumvalue");
        String *enumValueEx = Getattr(child, "enumvalueex");

        if (enumValue != NIL) {

          //Not a proper enum check if we have seen it before
          proper = 0;
    
          //scan for syms and replace previous enum values
          ReplaceEnumVals(thisEnumHash,enumValue,pname);

          //this check returns true for both legit hex numbers and syms not defined
          int parseRes = Preprocessor_expr(enumValue, &parseErr);

          if (parseErr == 0) {
            char numstr[15];
            sprintf(numstr, "%d", parseRes);
            Setattr(thisEnumHash,ename,numstr);
            String *ssmm = Getattr(valHash,numstr);
            if (ssmm != NIL) {
              dupl = true;
            }
            Setattr(valHash,numstr,ename);
            
          } else {
            Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number, "Parse error: Bad enumeration <%s><%s><%d>.\n", Char(ename),Char(enumValue),parseErr);
          }
        }

        if (enumValueEx != NIL) {

          /*
            enumValueEx is usually of form previous "sym + 1" so extract up to space
            lookup syme and replace with its value. Could extract the sym by
            searching for a sym something that starts with an alpha and ends with
            non alphanumeric
          */
          String *enumValExCopy = NewString(enumValueEx);
          if (Strstr(enumValExCopy, " + 1")) {

            Replaceall(enumValExCopy, " + 1", "");
            String *symVal = Getattr(thisEnumHash,enumValExCopy);

            if (symVal != NIL) {

              long theVal = aToL(symVal);
              theVal++;
              String *numstr = NewStringf("%ld",theVal);
              Setattr(thisEnumHash, ename, numstr);
              Setattr(valHash,numstr,ename);
            } else {
              Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number, "Could not find symbol in enumeration <%s>.\n", Char(enumValExCopy));
            }

          } else {
            //so it must be a value prob 0 so then set its value
            //fixme should parse the value anyway
            Setattr(thisEnumHash, ename, "0");
            Setattr(valHash,enumValueEx,ename);
          }
        } //enumValueEx not NIL

        Hash *oxyHash = NewHash();
        Setattr(oxyHash,"name",ename);
        Setattr(oxyHash,"oxy",oxy);
        Append(enumList,oxyHash);

        child = nextSibling(child);
      } //while

      //process the list to get min and max and count

      enumCount = 0;
      for (Iterator el = First(thisEnumHash); el.item != NIL; el = Next(el)) {
        String *value = NewString(el.item);

        long theVal = aToL(value);
        if (theVal < min) min = theVal;
        if (theVal > max) max = theVal;
        enumCount++;
      }

      //which assert we use needs to be conveyed
      String *useSet;
      if ((abs(max) < MaxSet) && (abs(min) < MaxSet)) {
        useSet = NewString("1");
      } else {
        useSet = NewString("0");
      }
      Setattr(enumHash,"useSet",useSet);

      String *properStr = NewStringf("%d",proper);
      Setattr(enumHash,"proper",properStr);

      //this deletes the class prefix from the enum before we print it
      if (pname != NIL) {
        //this removes the class prefix from the enum

        String *classPrefix = NewStringf("%s_",pname);
        Replaceall(enumName, classPrefix, "");
      }

      String *errConst = NewStringf("%s_ErrSet",Char(enumName));

      //set this attribute so that later we can generate an assert for the parameter
      Setattr(enumHash,"errconst",errConst);

//-------------------------------------------------

      //Dont process enums in an imported module
      if (ImportMode) return 0;

      //Got the enums now print them

      m3wrap_intf.enterBlock(no_block);

      //the proper enums translate directly to M3 ones
      if (proper == 1) {
        Printf(m3wrap_intf.f, "\nTYPE %s = {",Char(enumName));

        //loop through in order of declaration
        for (Iterator el = First(enumList); el.item != NIL; el = Next(el)) {

          String *name = Getattr(el.item,"name");
          String *oxy = Getattr(el.item,"oxy");

          Printf(m3wrap_intf.f, " %s", Char(name));
          if (Next(el).item != NIL) {
            Printf(m3wrap_intf.f, ",");
          }
          Printf(m3wrap_intf.f, " %s", oxy);
          Printf(m3wrap_intf.f, "\n");
        }

        Printf(m3wrap_intf.f, "};\n\n");

      } else {
        //improper enums
        //the names become constants and the type becomes a subrange
        //from min to max and we set a tmap for a runtime check
        
        //under certain conditions map improper ones to 
        //proper ones ie no duplicates, number less than
        //arbitrary limit 256 and min is positive
        //missing values have substitutes of form XXX_value

        if ((min >= 0) && ((max - min) < 256) && (dupl == false)) {
          bool gencomma = false;
        
          Printf(m3wrap_intf.f, "\nTYPE %s = {",Char(enumName));
        
          for(int i = 0; i <= max; i++) {
            sprintf(ival, "%d", i);  
            String *nn = NewString(ival); 
            String *eVal = Getattr(valHash,nn);
            String *oxy = NewString("");

            if (gencomma) {
              Printf(m3wrap_intf.f, ", ");
            }
            Printf(m3wrap_intf.f, "\n");
            gencomma = true;
          
            if (eVal == NIL) {
              Printf(m3wrap_intf.f, " XXXX_%s", Char(nn));  
            } else {
              //check for any comments
              for (Iterator el = First(enumList); el.item != NIL; el = Next(el)) {
                String *name = Getattr(el.item,"name");
                if (Strcmp(eVal,name) == 0) {
                  oxy = Getattr(el.item,"oxy");
                  break;
                }
              }
              Printf(m3wrap_intf.f, " %s %s", Char(eVal), Char(oxy));
            }
          }
          Printf(m3wrap_intf.f, "};\n\n");

        } else {

          Printf(m3wrap_intf.f, "\nCONST (* Enum %s *)\n",Char(enumName));

          for (Iterator el = First(enumList); el.item != NIL; el = Next(el)) {
            String *name = Getattr(el.item,"name");
            String *oxy = Getattr(el.item,"oxy");
            String *value = Getattr(thisEnumHash,name);

            Printf(m3wrap_intf.f, "  %s = %s; %s\n", Char(name),Char(value),Char(oxy));
          }

          // if the range of values is too large will have to use a Set generic
          //   and not the native set type

          Printf(m3wrap_intf.f, "\nTYPE (* Enum %s *)\n",Char(enumName));
          Printf(m3wrap_intf.f, "  %s = [%d..%d];\n\n",Char(enumName),min,max);

          //Can these be subject to command line option as with the check below??
          if (genEnumChecks_flag) {
            if ((abs(max) < MaxSet) && (abs(min) < MaxSet)) {

              Printf(m3wrap_intf.f, "  %s_SET = SET OF %s;\n",Char(enumName),Char(enumName));

              //Declare a const with proper values in the interface so
              //clients can check their parameters are IN the const
              //the client will have to have an import tmap with the special
              //name Class_ErrSet

              Printf(m3wrap_intf.f, "\nCONST (* Enum %s *)\n  %s_ErrSet = %s_SET{\n",Char(enumName),Char(enumName),Char(enumName));
            } else {
              Printf(m3wrap_intf.f, "  VAR %s_ErrSet := ARRAY[0..%d] OF INTEGER{\n",Char(enumName),enumCount-1);
            }

            bool gencomma = false;
            for (Iterator el = First(enumList); el.item != NIL; el = Next(el)) {
              String *name = Getattr(el.item,"name");
              String *oxy = Getattr(el.item,"oxy");
	      if (gencomma) {
	        Printf(m3wrap_intf.f, ", ");
	      }
	      gencomma = true;
              Printf(m3wrap_intf.f, "  %s %s", Char(name), Char(oxy));
            }
	    Printf(m3wrap_intf.f, "};\n\n");
          }
        
        }
      }
//--------------------------------------------

    } //if nodetype enum

   return SWIG_OK;

  }
#endif

  /* -----------------------------------------------------------------------------
   * enumvalueDeclaration() never called!!
   * ----------------------------------------------------------------------------- */

  virtual int enumvalueDeclaration(Node *n) {

    String *acc_str = Getattr(n, "access");

printf("EnumValueDecl\n");

    if (Strcmp(acc_str, "public") == 0) {
      generateConstant(n);
    }

    /*
       This call would continue processing in the constantWrapper
       which cannot handle values like "RED+1".
       return Language::enumvalueDeclaration(n);
     */
    return SWIG_OK;
  }

  /* -----------------------------------------------------------------------------
   * pragmaDirective()
   *
   * Valid Pragmas:
   * imclassbase            - base (extends) for the intermediary class
   * imclassclassmodifiers  - class modifiers for the intermediary class
   * imclasscode            - text (Modula 3 code) is copied verbatim to the intermediary class
   * imclassimports         - import statements for the intermediary class
   * imclassinterfaces      - interface (implements) for the intermediary class
   *
   * modulebase              - base (extends) for the module class
   * moduleclassmodifiers    - class modifiers for the module class
   * modulecode              - text (Modula 3 code) is copied verbatim to the module class
   * moduleimports           - import statements for the module class
   * moduleinterfaces        - interface (implements) for the module class
   *
   * ----------------------------------------------------------------------------- */

  virtual int pragmaDirective(Node *n) {
    if (!ImportMode) {
      String *lang = Getattr(n, "lang");
      String *code = Getattr(n, "name");
      String *value = Getattr(n, "value");

      if (Strcmp(lang, "modula3") == 0) {

	String *strvalue = NewString(value);
	Replaceall(strvalue, "\\\"", "\"");
	if (Strcmp(code, "imclassbase") == 0) {
	  Delete(m3raw_baseclass);
	  m3raw_baseclass = Copy(strvalue);
	} else if (Strcmp(code, "imclassclassmodifiers") == 0) {
	  Delete(m3raw_class_modifiers);
	  m3raw_class_modifiers = Copy(strvalue);
	} else if (Strcmp(code, "imclasscode") == 0) {
	  Printf(m3raw_intf.f, "%s\n", strvalue);
	} else if (Strcmp(code, "imclassimports") == 0) {
	  Delete(m3raw_imports);
	  m3raw_imports = Copy(strvalue);
	} else if (Strcmp(code, "imclassinterfaces") == 0) {
	  Delete(m3raw_interfaces);
	  m3raw_interfaces = Copy(strvalue);
	} else if (Strcmp(code, "modulebase") == 0) {
	  Delete(module_baseclass);
	  module_baseclass = Copy(strvalue);
	} else if (Strcmp(code, "moduleclassmodifiers") == 0) {
	  Delete(m3wrap_modifiers);
	  m3wrap_modifiers = Copy(strvalue);
	} else if (Strcmp(code, "modulecode") == 0) {
	  Printf(m3wrap_impl.f, "%s\n", strvalue);
	} else if (Strcmp(code, "moduleimports") == 0) {
	  Delete(module_imports);
	  module_imports = Copy(strvalue);
	} else if (Strcmp(code, "moduleinterfaces") == 0) {
	  Delete(module_interfaces);
	  module_interfaces = Copy(strvalue);
	} else if (Strcmp(code, "unsafe_raw") == 0) {
	  unsafe_raw = true;
	} else if (Strcmp(code, "unsafe") == 0) {
	  unsafe_module = true;
	} else if (Strcmp(code, "library") == 0) {
	  if (targetlibrary) {
	    Delete(targetlibrary);
	  }
	  targetlibrary = Copy(strvalue);
	} else if (Strcmp(code, "enumitem") == 0) {
	} else if (Strcmp(code, "constset") == 0) {
	} else if (Strcmp(code, "constint") == 0) {
	} else if (Strcmp(code, "makesetofenum") == 0) {
	  m3wrap_intf.enterBlock(blocktype);
	  Printf(m3wrap_intf.f, "%sSet = SET OF %s;\n", value, value);
	} else {
	  Swig_warning(WARN_MODULA3_UNKNOWN_PRAGMA, input_file, line_number, "Unrecognized pragma <%s>.\n", code);
	}
	Delete(strvalue);
      }
    }
    return Language::pragmaDirective(n);
  }

  void Setfeature(Node *n, const char *feature, const String *value, bool warn = false) {
    String *attr = NewStringf("feature:%s", feature);
    if ((Setattr(n, attr, value) != 0) && warn) {
      Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number, "Feature <%s> of %s did already exist.\n", feature, Getattr(n, "name"));
    }
    Delete(attr);
  }

  String *Getfeature(Node *n, const char *feature) {
    String *attr = NewStringf("feature:%s", feature);
    String *result = Getattr(n, attr);
    Delete(attr);
    return result;
  }

  bool convertInt(long in, long &out, const String *mode) {
    if ((mode == NIL) || (Strcmp(mode, "int:int") == 0) || (Strcmp(mode, "set:set") == 0)) {
      out = in;
      return true;
    } else if (Strcmp(mode, "set:int") == 0) {
      return log2(in, out);
    } else if (Strcmp(mode, "int:set") == 0) {
      out = 1L << in;
      return unsigned (in) < (sizeof(out) * 8);
    } else {
      Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number, "Unknown integer conversion method <%s>.\n", mode);
      return false;
    }
  }

  /*
  Created a list of the enum labels and their values from the
  enumvalue and enumvaluex and at the end it is a proper enum if the
  first elt is value 0 and the last is the value of the count of enums
  and we construct a type with the list, otherwise it is an improper
  enum and we construct consts for all the enum labs with their values.
  and then a var initialisation in a proc and a runtime check before the
  low call. and construct a typemap to be acted upon later to
  change the type of the parm.
  */
  
  void ReplaceEnumVals(Hash *hash, String *s, String *className) {

    //Replace all values in s if they exist
    for (Iterator el = First(hash); el.item != NIL; el = Next(el)) {
      String *name = NewString(el.key);
      String *value = NewStringf("(%s)",el.item);
      Replaceid(s, name, value);
    }
    
    //Check if string contains any names with class prefixes in which case do
    //the full enum search from the global hash table
    if (! Strstr(s, "::")) {
      return;
    }

    //Still enum values from another eum defined in s. So continue searching
    //This only works if from the same class.
    //Since we chop off the classname prefix and search through the global
    //enum hash enum_coll - which assumes collectEnumerations has been run
    //This needs rework since if we had regular expressions we could just chop off
    //all classname prefixes not just the one in which the enum is defined.
    //Also we could probably get away with not calling collectEnumerations and
    //collect all the enums in enumDeclaration

    if (className != NIL) {
      String *cname = NewStringf("%s::",className);
      Replaceall(s, cname , "");
    }

    for (Iterator it2 = First(enum_coll); it2.key != NIL; it2 = Next(it2)) {

      Hash *h = it2.item;

      String *proper = Getattr(h,"proper");
      List *enumList = Getattr(h,"order");
      Hash *theEnums = Getattr(h,"enums");

      //the proper enums translate directly to M3 ones
      if (proper) {

        //random order
        for (Iterator el = First(theEnums); el.item != NIL; el = Next(el)) {
          String *name = NewString(el.key);
          String *value = NewString(el.item);

          //dont need to replace proper ones
          Replaceall(s, name, value);
       }
      } else {
        //improper enums
        for (Iterator el = First(enumList); el.item != NIL; el = Next(el)) {
          String *name = NewString(el.item);
          String *value = Getattr(theEnums,name);

          Replaceall(s, name, value);
        }
      }
    }
  }


  void collectEnumerations(Hash *enums, Node *n) {
    Node *child = firstChild(n);
    while (child != NIL) {
      String *name = Getattr(child, "name");
      const bool isConstant = Strcmp(nodeType(child), "constant") == 0;
      const bool isEnumItem = Strcmp(nodeType(child), "enumitem") == 0;
      if (isConstant || isEnumItem) {
	{
	  String *m3name = Getfeature(child, "modula3:enumitem:name");
	  String *m3enum = Getfeature(child, "modula3:enumitem:enum");
	  String *conv = Getfeature(child, "modula3:enumitem:conv");

	  if (m3enum != NIL) {
	    if (m3name == NIL) {
	      m3name = name;
	    }

	    long max = -1;
	    Hash *items;
	    Hash *enumnode = Getattr(enums, m3enum);
	    if (enumnode == NIL) {
	      enumnode = NewHash();
	      items = NewHash();
	      Setattr(enumnode, "items", items);
	      Setattr(enums, m3enum, enumnode);
	    } else {
	      String *maxstr = Getattr(enumnode, "max");
	      if (maxstr != NIL) {
		max = aToL(maxstr);
	      }
	      items = Getattr(enumnode, "items");
	    }
	    long numvalue;
	    String *value = Getattr(child, "value");
	    if ((value == NIL) || (!strToL(value, numvalue))) {
	      value = Getattr(child, "enumvalue");
	      if ((value == NIL) || (!evalExpr(value, numvalue))) {
		numvalue = getConstNumeric(child);
	      }
	    }
	    Setattr(constant_values, name, NewStringf("%d", numvalue));
	    if (convertInt(numvalue, numvalue, conv)) {
	      String *newvalue = NewStringf("%d", numvalue);
	      String *oldname = Getattr(items, newvalue);
	      if (oldname != NIL) {
		Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number, "The value <%s> is already assigned to <%s>.\n", value, oldname);
	      }
	      Setattr(items, newvalue, m3name);
	      if (max < numvalue) {
		max = numvalue;
	      }
	      Setattr(enumnode, "max", NewStringf("%d", max));
	    }
	  }
	}
      }

      collectEnumerations(enums, child);
      child = nextSibling(child);
    }
  }

  enum const_pragma_type { cpt_none, cpt_constint, cpt_constset, cpt_enumitem };

  struct const_id_pattern {
    String *prefix, *parentEnum;
  };

  void tagConstants(Node *first, String *parentEnum, const const_id_pattern & pat, const String *pragma, List *convdesc) {
    Node *n = first;
    while (n != NIL) {
      String *name = getQualifiedName(n);
      bool isConstant = Strcmp(nodeType(n), "constant") == 0;
      bool isEnumItem = Strcmp(nodeType(n), "enumitem") == 0;
      bool patHasPrefix = hasPrefix(name, pat.prefix);
      bool isParent;
      if (parentEnum == NIL || pat.parentEnum == NIL) isParent = false;
      else isParent = Strcmp(pat.parentEnum, parentEnum) == 0;

      if (
          (isConstant || isEnumItem) &&
          ((pat.prefix == NIL) || (patHasPrefix)) &&
          ( (pat.parentEnum == NIL) ||
            ((parentEnum != NIL)  &&  (isParent)))
         ) {
	String *srctype = Getitem(convdesc, 1);
	String *relationstr = Getitem(convdesc, 3);
	List *relationdesc = Split(relationstr, ',', 2);

	// transform name from C to Modula3 style
	String *srcstyle = NIL;
	String *newprefix = NIL;
	{
	  List *namedesc = Split(Getitem(convdesc, 2), ',', INT_MAX);
	  Iterator nameit = First(namedesc);
	  for (; nameit.item != NIL; nameit = Next(nameit)) {
	    List *nameassign = Split(nameit.item, '=', 2);
	    String *tag = Getitem(nameassign, 0);
	    String *data = Getitem(nameassign, 1);
	    if (Strcmp(tag, "srcstyle") == 0) {
	      srcstyle = Copy(data);
	    } else if (Strcmp(tag, "prefix") == 0) {
	      newprefix = Copy(data);
	    } else {
	      Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number, "Unknown name conversion tag <%s> with value <%s>.\n", tag, data);
	    }
	    Delete(nameassign);
	  }
	  Delete(namedesc);
	}
	const char *stem = Char(name);
	if (pat.prefix != NIL) {
	  stem += Len(pat.prefix);
	}
	String *newname;
	if (Strcmp(srcstyle, "underscore") == 0) {
	  if (newprefix != NIL) {
	    String *newstem = nameToModula3(stem, true);
	    newname = NewStringf("%s%s", newprefix, newstem);
	    Delete(newstem);
	  } else {
	    newname = nameToModula3(stem, true);
	  }
	} else if (Strcmp(srcstyle, "orig") == 0) {
	  //leave the name alone its ok as is
          newname = NewStringf("%s",stem);
	} else {
	  if (srcstyle != NIL) {
	    Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number, "Unknown C identifier style <%s>.\n", srcstyle);
	  }
	  newname = Copy(name);
	}

	if (Strcmp(pragma, "enumitem") == 0) {
	  if (Len(relationdesc) != 1) {
	    Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number, "Expected <enumeration>, got <%s>.\n", relationstr);
	  }
	  Setfeature(n, "modula3:enumitem:name", newname, true);
	  Setfeature(n, "modula3:enumitem:enum", relationstr, true);
	  Setfeature(n, "modula3:enumitem:conv", NewStringf("%s:int", srctype), true);
	} else if (Strcmp(pragma, "constint") == 0) {
	  if (Len(relationdesc) != 1) {
	    Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number, "Expected <ordinal type>, got <%s>.\n", relationstr);
	  }
	  Setfeature(n, "modula3:constint:name", newname, true);
	  Setfeature(n, "modula3:constint:type", Getitem(relationdesc, 0), true);
	  Setfeature(n, "modula3:constint:conv", NewStringf("%s:int", srctype), true);
	} else if (Strcmp(pragma, "constset") == 0) {
	  if (Len(relationdesc) != 2) {
	    Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number, "Expected <set type,base type>, got <%s>.\n", relationstr);
	  }
	  String *settype = Getitem(relationdesc, 0);
	  Setfeature(n, "modula3:constset:name", newname, true);
	  //Setfeature(n,"modula3:constset:type",settype,true);
	  Setfeature(n, "modula3:constset:set", settype, true);
	  Setfeature(n, "modula3:constset:base", Getitem(relationdesc, 1), true);
	  Setfeature(n, "modula3:constset:conv", NewStringf("%s:set", srctype), true);
	}

	Delete(newname);
	Delete(relationdesc);
      }

      if (Strcmp(nodeType(n), "enum") == 0) {
	tagConstants(firstChild(n), name, pat, pragma, convdesc);
      } else {
	tagConstants(firstChild(n), NIL, pat, pragma, convdesc);
      }
      n = nextSibling(n);
    }
  }

  void scanForConstPragmas(Node *n) {
    Node *child = firstChild(n);
    while (child != NIL) {
      const String *type = nodeType(child);
      if (Strcmp(type, "pragma") == 0) {
	const String *lang = Getattr(child, "lang");
	const String *code = Getattr(child, "name");
	String *value = Getattr(child, "value");

	if (Strcmp(lang, "modula3") == 0) {
	  const_pragma_type cpt = cpt_none;
	  if (Strcmp(code, "constint") == 0) {
	    cpt = cpt_constint;
	  } else if (Strcmp(code, "constset") == 0) {
	    cpt = cpt_constset;
	  } else if (Strcmp(code, "enumitem") == 0) {
	    cpt = cpt_enumitem;
	  }
	  if (cpt != cpt_none) {
	    const_id_pattern pat = { NIL, NIL };

	    List *convdesc = Split(value, ';', 4);
	    List *patterndesc = Split(Getitem(convdesc, 0), ',', INT_MAX);
	    Iterator patternit;
	    for (patternit = First(patterndesc); patternit.item != NIL; patternit = Next(patternit)) {
	      List *patternassign = Split(patternit.item, '=', 2);
	      String *tag = Getitem(patternassign, 0);
	      String *data = Getitem(patternassign, 1);
	      if (Strcmp(tag, "prefix") == 0) {
		pat.prefix = Copy(data);
	      } else if (Strcmp(tag, "enum") == 0) {
		pat.parentEnum = Copy(data);
	      } else {
		Swig_warning(WARN_MODULA3_BAD_ENUMERATION, input_file, line_number, "Unknown identification tag <%s> with value <%s>.\n", tag, data);
	      }
	      Delete(patternassign);
	    }
	    tagConstants(child, NIL, pat, code, convdesc);

	    Delete(patterndesc);
	  }
	}
      }
      scanForConstPragmas(child);
      child = nextSibling(child);
    }
  }

  /* -----------------------------------------------------------------------------
   * emitProxyClassDefAndCPPCasts()
   * ----------------------------------------------------------------------------- */

  void emitProxyClassDefAndCPPCasts(Node *n) {
    String *c_classname = SwigType_namestr(Getattr(n, "name"));
    String *c_baseclass = NULL;
    String *baseclass = NULL;
    String *c_baseclassname = NULL;
    String *classDeclarationName = Getattr(n, "name");

    /* Deal with inheritance */
    List *baselist = Getattr(n, "bases");
    if (baselist != NIL) {
      Iterator base = First(baselist);
      c_baseclassname = Getattr(base.item, "name");
      baseclass = Copy(getProxyName(c_baseclassname));
      if (baseclass) {
	c_baseclass = SwigType_namestr(Getattr(base.item, "name"));
      }
      base = Next(base);
      if (base.item != NIL) {
	Swig_warning(WARN_MODULA3_MULTIPLE_INHERITANCE, Getfile(n), Getline(n),
		     "Warning for %s proxy: Base %s ignored. Multiple inheritance is not supported in Modula 3.\n",
		     classDeclarationName, Getattr(base.item, "name"));
      }
    }

    bool derived = baseclass && getProxyName(c_baseclassname);
    if (!baseclass)
      baseclass = NewString("");

    // Inheritance from pure Modula 3 classes
    const String *pure_baseclass = typemapLookup(n, "m3base", classDeclarationName, WARN_NONE);
    if (hasContent(pure_baseclass) && hasContent(baseclass)) {
      Swig_warning(WARN_MODULA3_MULTIPLE_INHERITANCE, Getfile(n), Getline(n),
		   "Warning for %s proxy: Base %s ignored. Multiple inheritance is not supported in Modula 3.\n", classDeclarationName, pure_baseclass);
    }
    // Pure Modula 3 interfaces
    const String *pure_interfaces = typemapLookup(n, derived ? "m3interfaces_derived" : "m3interfaces",
						  classDeclarationName, WARN_NONE);

    // Start writing the proxy class
    Printv(proxy_class_def, typemapLookup(n, "m3imports", classDeclarationName, WARN_NONE),	// Import statements
	   "\n", typemapLookup(n, "m3classmodifiers", classDeclarationName, WARN_MODULA3_TYPEMAP_CLASSMOD_UNDEF),	// Class modifiers
	   " class $m3classname",	// Class name and bases
	   (derived || *Char(pure_baseclass) || *Char(pure_interfaces)) ? " : " : "", baseclass, pure_baseclass, ((derived || *Char(pure_baseclass)) && *Char(pure_interfaces)) ?	// Interfaces
	   ", " : "", pure_interfaces, " {\n", "  private IntPtr swigCPtr;\n",	// Member variables for memory handling
	   derived ? "" : "  protected bool swigCMemOwn;\n", "\n", "  ", typemapLookup(n, "m3ptrconstructormodifiers", classDeclarationName, WARN_MODULA3_TYPEMAP_PTRCONSTMOD_UNDEF),	// pointer constructor modifiers
	   " $m3classname(IntPtr cPtr, bool cMemoryOwn) ",	// Constructor used for wrapping pointers
	   derived ?
	   ": base($imclassname.$m3classnameTo$baseclass(cPtr), cMemoryOwn) {\n"
	   : "{\n    swigCMemOwn = cMemoryOwn;\n", "    swigCPtr = cPtr;\n", "  }\n", NIL);

    if (!have_default_constructor_flag) {	// All proxy classes need a constructor
      Printv(proxy_class_def, "\n", "  protected $m3classname() : this(IntPtr.Zero, false) {\n", "  }\n", NIL);
    }
    // C++ destructor is wrapped by the Dispose method
    // Note that the method name is specified in a typemap attribute called methodname
    String *destruct = NewString("");
    const String *tm = NULL;
    Node *attributes = NewHash();
    String *destruct_methodname = NULL;
    if (derived) {
      tm = typemapLookup(n, "m3destruct_derived", classDeclarationName, WARN_NONE, attributes);
      destruct_methodname = Getattr(attributes, "tmap:m3destruct_derived:methodname");
    } else {
      tm = typemapLookup(n, "m3destruct", classDeclarationName, WARN_NONE, attributes);
      destruct_methodname = Getattr(attributes, "tmap:m3destruct:methodname");
    }
    if (!destruct_methodname) {
      Swig_error(Getfile(n), Getline(n), "No methodname attribute defined in m3destruct%s typemap for %s\n", (derived ? "_derived" : ""), proxy_class_name);
    }
    // Emit the Finalize and Dispose methods
    if (tm) {
      // Finalize method
      if (*Char(destructor_call)) {
	Printv(proxy_class_def, typemapLookup(n, "m3finalize", classDeclarationName, WARN_NONE), NIL);
      }
      // Dispose method
      Printv(destruct, tm, NIL);
      if (*Char(destructor_call))
	Replaceall(destruct, "$imcall", destructor_call);
      else
	Replaceall(destruct, "$imcall", "throw new MethodAccessException(\"C++ destructor does not have public access\")");
      if (*Char(destruct))
	Printv(proxy_class_def, "\n  public ", derived ? "override" : "virtual", " void ", destruct_methodname, "() ", destruct, "\n", NIL);
    }
    Delete(attributes);
    Delete(destruct);

    // Emit various other methods
    Printv(proxy_class_def, typemapLookup(n, "m3getcptr", classDeclarationName, WARN_MODULA3_TYPEMAP_GETCPTR_UNDEF),	// getCPtr method
	   typemapLookup(n, "m3code", classDeclarationName, WARN_NONE),	// extra Modula 3 code
	   "\n", NIL);

    // Substitute various strings into the above template
    Replaceall(proxy_class_def, "$m3classname", proxy_class_name);
    Replaceall(proxy_class_code, "$m3classname", proxy_class_name);

    Replaceall(proxy_class_def, "$baseclass", baseclass);
    Replaceall(proxy_class_code, "$baseclass", baseclass);

    Replaceall(proxy_class_def, "$imclassname", m3raw_name);
    Replaceall(proxy_class_code, "$imclassname", m3raw_name);

    // Add code to do C++ casting to base class (only for classes in an inheritance hierarchy)
    if (derived) {
      Printv(m3raw_cppcasts_code, "\n  [DllImport(\"", m3wrap_name, "\", EntryPoint=\"Modula3_", proxy_class_name, "To", baseclass, "\")]\n", NIL);
      Printv(m3raw_cppcasts_code, "  public static extern IntPtr ", "$m3classnameTo$baseclass(IntPtr objectRef);\n", NIL);

      Replaceall(m3raw_cppcasts_code, "$m3classname", proxy_class_name);
      Replaceall(m3raw_cppcasts_code, "$baseclass", baseclass);

      Printv(upcasts_code,
	     "SWIGEXPORT long Modula3_$imclazznameTo$imbaseclass",
	     "(long objectRef) {\n",
	     "    long baseptr = 0;\n" "    *($cbaseclass **)&baseptr = *($cclass **)&objectRef;\n" "    return baseptr;\n" "}\n", "\n", NIL);

      Replaceall(upcasts_code, "$imbaseclass", baseclass);
      Replaceall(upcasts_code, "$cbaseclass", c_baseclass);
      Replaceall(upcasts_code, "$imclazzname", proxy_class_name);
      Replaceall(upcasts_code, "$cclass", c_classname);
    }
    Delete(baseclass);
  }

  /* ----------------------------------------------------------------------
   * getAttrString()
   *
   * If necessary create and return the string
   * associated with a certain attribute of 'n'.
   * ---------------------------------------------------------------------- */

  String *getAttrString(Node *n, const char *attr) {
    String *str = Getattr(n, attr);
    if (str == NIL) {
      str = NewString("");
      Setattr(n, attr, str);
    }
    return str;
  }

  /* ----------------------------------------------------------------------
   * getMethodDeclarations()
   *
   * If necessary create and return the handle
   * where the methods of the current access can be written to.
   * 'n' must be a member of a struct or a class.
   * ---------------------------------------------------------------------- */

  String *getMethodDeclarations(Node *n) {
    String *acc_str = Getattr(n, "access");
    String *methodattr;
    if (acc_str == NIL) {
      methodattr = NewString("modula3:method:public");
    } else {
      methodattr = NewStringf("modula3:method:%s", acc_str);
    }
    String *methods = getAttrString(parentNode(n), Char(methodattr));
    Delete(methodattr);
    return methods;
  }

  /* ----------------------------------------------------------------------
   * classHandler()
   * ---------------------------------------------------------------------- */

  virtual int classHandler(Node *n) {

    File *f_proxy = NULL;
    proxy_class_name = Copy(Getattr(n, "sym:name"));

    if (proxy_flag) {
      if (!addSymbol(proxy_class_name, n))
	return SWIG_ERROR;

      if (Cmp(proxy_class_name, m3raw_name) == 0) {
	Printf(stderr, "Class name cannot be equal to intermediary class name: %s\n", proxy_class_name);
	Exit(EXIT_FAILURE);
      }

      if (Cmp(proxy_class_name, m3wrap_name) == 0) {
	Printf(stderr, "Class name cannot be equal to module class name: %s\n", proxy_class_name);
	Exit(EXIT_FAILURE);
      }

      String *filen = NewStringf("%s%s.m3", SWIG_output_directory(), proxy_class_name);
      f_proxy = NewFile(filen, "w", SWIG_output_files());
      if (!f_proxy) {
	FileErrorDisplay(filen);
	Exit(EXIT_FAILURE);
      }
      Delete(filen);
      filen = NULL;

      emitBanner(f_proxy);

      Clear(proxy_class_def);
      Clear(proxy_class_code);

      have_default_constructor_flag = false;
      destructor_call = NewString("");
    }

    /* This will invoke memberfunctionHandler, membervariableHandler ...
       and finally it may invoke functionWrapper
       for wrappers and member variable accessors.
       It will invoke Language:constructorDeclaration
       which decides whether to call MODULA3::constructorHandler */
    Language::classHandler(n);

    {
      String *kind = Getattr(n, "kind");
      if (Cmp(kind, "struct") == 0) {
        
        String *rawRecField = Getattr(n, "modula3:rawrecfield");
        String *recField = Getattr(n, "modula3:recfield");
     
	m3raw_intf.enterBlock(blocktype);
	Printf(m3raw_intf.f, "\n%s =\nRECORD\n%s\nEND;\n", proxy_class_name, rawRecField);
        
	m3wrap_intf.enterBlock(blocktype);
	Printf(m3wrap_intf.f, "\n%s =\nRECORD\n%s\nEND;\n", proxy_class_name, recField);

      } else if (Cmp(kind, "class") == 0) {
	enum access_privilege { acc_public, acc_protected, acc_private };
	int max_acc = acc_public;

	const char *acc_name[3] = { "public", "protected", "private" };
	String *methods[3];
	int acc;
	for (acc = acc_public; acc <= acc_private; acc++) {
	  String *methodattr = NewStringf("modula3:method:%s", acc_name[acc]);
	  methods[acc] = Getattr(n, methodattr);
	  Delete(methodattr);
	  max_acc = max_acc > acc ? max_acc : acc;
	}

	/* Determine the name of the base class */
	String *baseclassname = NewString("");
	{
	  List *baselist = Getattr(n, "bases");
	  if (baselist) {
	    /* Look for the first (principal?) base class -
	       Modula 3 does not support multiple inheritance */
	    Iterator base = First(baselist);
	    Append(baseclassname, Getattr(base.item, "sym:name"));
	    base = Next(base);
	    if (base.item != NIL) {
	      Swig_warning(WARN_MODULA3_MULTIPLE_INHERITANCE, Getfile(n), Getline(n),
			   "Warning for %s proxy: Base %s ignored. Multiple inheritance is not supported in Modula 3.\n",
			   proxy_class_name, Getattr(base.item, "name"));
	    }
	  }
	}

	/* the private class of the base class and only this
	   need a pointer to the C++ object */
	bool need_private = !hasContent(baseclassname);
	max_acc = need_private ? acc_private : max_acc;

	/* Declare C++ object as abstract pointer in Modula 3 */
	/* The revelation system does not allow us
	   to imitate the whole class hierarchy of the C++ library,
	   but at least we can distinguish between classes of different roots. */
	if (hasContent(baseclassname)) {
	  m3raw_intf.enterBlock(blocktype);
	  Printf(m3raw_intf.f, "%s = ADDRESS;\n", proxy_class_name);
	} else {
	  m3raw_intf.enterBlock(blocktype);
	  Printf(m3raw_intf.f, "%s <: ADDRESS;\n", proxy_class_name);
	  m3raw_impl.enterBlock(revelation);
	  Printf(m3raw_impl.f, "%s = UNTRACED BRANDED REF RECORD (*Dummy*) END;\n", proxy_class_name);
	}

	String *superclass;
	m3wrap_intf.enterBlock(blocktype);

	//fix class which has no methods
	superclass = NewStringf("%sPublic", proxy_class_name);
        String *oxy = do_oxy(n,"classhandler");
        Printf(m3wrap_intf.f, "%s", oxy);
	Printf(m3wrap_intf.f, "%s <: %s;\n", proxy_class_name, superclass);
	Delete(superclass);

	{
	  static const char *acc_m3suffix[] = { "Public", "Protected", "Private" };
	  int acc;
	  for (acc = acc_public; acc <= acc_private; acc++) {

	    bool process_private = (acc == acc_private);

	    if ((acc == acc_public) || process_private) {

	      String *subclass = NewStringf("%s%s", proxy_class_name, acc_m3suffix[acc]);

	      if (acc == max_acc) {
	      	Printf(m3wrap_impl.f, "PROCEDURE Cleanup_%s(<*UNUSED*>READONLY self: WeakRef.T; ref: REFANY) =\nVAR obj : %s := ref;\nBEGIN\n  Delete_%s(obj);\n END Cleanup_%s;\n", proxy_class_name,proxy_class_name, proxy_class_name, proxy_class_name);

	      	//Need a way to destry the cxxObj from the interface in cases where a cxx object is not created
                //in our init/constructor eg a return class in a method
                Printf(m3wrap_impl.f,"\nPROCEDURE Destroy_%s(self : %s) =\nBEGIN\n  EVAL WeakRef.FromRef(self,Cleanup_%s);\nEND Destroy_%s;\n",proxy_class_name,proxy_class_name,proxy_class_name,proxy_class_name);

		m3wrap_impl.enterBlock(revelation);
		Printf(m3wrap_impl.f, "%s =\n", proxy_class_name);
	        Printf(m3wrap_impl.f, "%s BRANDED OBJECT\n",baseclassname);

	      } else {
		m3wrap_intf.enterBlock(blocktype);
		Printf(m3wrap_intf.f, "%s =\n", subclass);
		Printf(m3wrap_intf.f, "%s BRANDED OBJECT\n", baseclassname);
                if  ((need_private)) {
                  Printf(m3wrap_intf.f, "cxxObj:ADDRESS;\n");
                } 
	      }

	      if (acc == acc_public) {
                String *consOverrides = Getattr(n,"m3consargs");
                Printf(m3wrap_intf.f, "METHODS\n%s%s",consOverrides,methods[acc]);
                Printf(m3wrap_intf.f, "destroyCxx();\n");
                Printf(m3wrap_intf.f, "END;\n\n");
	      }
	      if (acc == max_acc) {
		String *overrides = Getattr(n, "modula3:override");
               // if  ((need_private)) {
               //   Printf(m3wrap_impl.f, "cxxObj:ADDRESS;\n");
               // }                
		Printf(m3wrap_impl.f, "OVERRIDES\n");
		Printf(m3wrap_impl.f, "%s",overrides);
		Printf(m3wrap_impl.f, "destroyCxx := Destroy_%s;\n",proxy_class_name);
		Printf(m3wrap_impl.f, "END;\n\n");
	      }
	      Delete(baseclassname);
	      baseclassname = subclass;
	    }
	  }
	}

	Delete(methods[acc_public]);
	Delete(methods[acc_protected]);
	Delete(methods[acc_private]);

      } else {
	Swig_warning(WARN_MODULA3_TYPECONSTRUCTOR_UNKNOWN, input_file, line_number, "Unknown type constructor %s\n", kind);
      }
    }

    if (proxy_flag) {

      emitProxyClassDefAndCPPCasts(n);

      Printv(f_proxy, proxy_class_def, proxy_class_code, NIL);

      Printf(f_proxy, "}\n");
      Delete(f_proxy);
      f_proxy = NULL;

      Delete(proxy_class_name);
      proxy_class_name = NULL;
      Delete(destructor_call);
      destructor_call = NULL;
    }
    return SWIG_OK;
  }

  /* ----------------------------------------------------------------------
   * memberfunctionHandler()
   * ---------------------------------------------------------------------- */

  virtual int memberfunctionHandler(Node *n) {
    Setattr(n, "modula3:functype", "method");
    Language::memberfunctionHandler(n);

    {
      /* Language::memberfunctionHandler will remove the mapped types
         that emitM3Wrapper may attach */
      ParmList *pl = Getattr(n, "parms");
      Parm *p;
      int i;
      Swig_typemap_attach_parms("m3wrapinmode", pl, NULL);
      Swig_typemap_attach_parms("m3wrapinname", pl, NULL);
      Swig_typemap_attach_parms("m3wrapintype", pl, NULL);
      Swig_typemap_attach_parms("m3wrapindefault", pl, NULL);

      attachParameterNames(n, "tmap:m3wrapinname", "autoname", "arg%d");
      String *rettype = getMappedTypeNew(n, "m3wrapouttype", "");

      String *methodname = Getattr(n, "sym:name");

      //overrides
      String *oname = Getattr(n, "sym:overname");
      Replaceall(oname, "__SWIG_", "");
      if (Strcmp(oname,"0") != 0) {
        methodname = NewStringf("%s%s",methodname,oname);
      }
      String *arguments = createM3Signature(n);
      String *storage = Getattr(n, "storage");
      String *abstract = Getattr(n, "abstract");
      String *comment = NewString("");
      Printf(comment,"%s%s",abstract ? "abstract" : "",storage ? " virtual" : "");
      if (Len(comment) > 0) comment = NewStringf("  (* %s *)",comment);

      //dont implement abstract methods since cannot call them
      //they must be overriden in subclasses
      if (!abstract) {
	{
          int num_exceptions = 0;
          Hash *throws_hash = NewHash();

          for (i = 0, p = pl; p; i++) {
            num_exceptions += addThrows(throws_hash, "m3wrapincheck", p);
            num_exceptions += addThrows(throws_hash, "m3wrapretvar", p);
            num_exceptions += addThrows(throws_hash, "m3wrapargvar", p);
            num_exceptions += addThrows(throws_hash, "m3wrapinconv", p);
            num_exceptions += addThrows(throws_hash, "m3wrapargraw", p);
            num_exceptions += addThrows(throws_hash, "m3wrapretcheck", p);
            num_exceptions += addThrows(throws_hash, "m3wrapoutcheck", p);
            num_exceptions += addThrows(throws_hash, "m3wrapoutconv", p);
            num_exceptions += addThrows(throws_hash, "m3wrapfreearg", p);
            num_exceptions += addThrows(throws_hash, "m3wrapretconv", p);
            p = nextSibling(p);
          }

          String *throws = NewString("");
          generateThrowsClause(throws_hash, throws);
          String *oxy = do_oxy(n,"memberfunctionhandler");
	  String *thisMethod = NewString("");
	  Printf(thisMethod, "%s%s(%s)%s%s%s;%s\n",
                 oxy,
		 methodname,
		 arguments,
		 hasContent(rettype) ? ": " : "",
		 hasContent(rettype) ? (const String *) rettype : "",
		 throws,
		 comment);


	  String *methods = getMethodDeclarations(n);
	  //add this method to the global list
	  Printf(methods,"%s",thisMethod);

	}
	{
	  /* this was attached by functionWrapper
	     invoked by Language::memberfunctionHandler */
	  String *fname = Getattr(n, "modula3:funcname");

	  //overrides
	  if (Strcmp(oname,"0") != 0) {
            fname = NewStringf("%s%s",fname,oname);
          }

	  String *overrides = getAttrString(parentNode(n), "modula3:override");
	  Printf(overrides, "%s := %s;\n", methodname, fname);
	}
      }
    }

    if (proxy_flag) {
      String *overloaded_name = getOverloadedName(n);
      String *intermediary_function_name = Swig_name_member(NSPACE_TODO, proxy_class_name, overloaded_name);
      Setattr(n, "proxyfuncname", Getattr(n, "sym:name"));
      Setattr(n, "imfuncname", intermediary_function_name);
      proxyClassFunctionHandler(n);
      Delete(overloaded_name);
    }
    return SWIG_OK;
  }

  /* ----------------------------------------------------------------------
   * staticmemberfunctionHandler()
   * ---------------------------------------------------------------------- */

  virtual int staticmemberfunctionHandler(Node *n) {

    static_flag = true;
    Language::staticmemberfunctionHandler(n);

    if (proxy_flag) {
      String *overloaded_name = getOverloadedName(n);
      String *intermediary_function_name = Swig_name_member(NSPACE_TODO, proxy_class_name, overloaded_name);
      Setattr(n, "proxyfuncname", Getattr(n, "sym:name"));
      Setattr(n, "imfuncname", intermediary_function_name);
      proxyClassFunctionHandler(n);
      Delete(overloaded_name);
    }
    static_flag = false;

    return SWIG_OK;
  }

  /* -----------------------------------------------------------------------------
   * proxyClassFunctionHandler()
   *
   * Function called for creating a Modula 3 wrapper function around a c++ function in the
   * proxy class. Used for both static and non-static C++ class functions.
   * C++ class static functions map to Modula 3 static functions.
   * Two extra attributes in the Node must be available. These are "proxyfuncname" -
   * the name of the Modula 3 class proxy function, which in turn will call "imfuncname" -
   * the intermediary (PInvoke) function name in the intermediary class.
   * ----------------------------------------------------------------------------- */

  void proxyClassFunctionHandler(Node *n) {
    SwigType *t = Getattr(n, "type");
    ParmList *l = Getattr(n, "parms");
    Hash *throws_hash = NewHash();
    String *intermediary_function_name = Getattr(n, "imfuncname");
    String *proxy_function_name = Getattr(n, "proxyfuncname");
    String *tm;
    Parm *p;
    int i;
    String *imcall = NewString("");
    String *return_type = NewString("");
    String *function_code = NewString("");
    bool setter_flag = false;

    if (!proxy_flag)
      return;

    if (l) {
      if (SwigType_type(Getattr(l, "type")) == T_VOID) {
	l = nextSibling(l);
      }
    }

    /* Attach the non-standard typemaps to the parameter list */
    Swig_typemap_attach_parms("in", l, NULL);
    Swig_typemap_attach_parms("m3wraptype", l, NULL);
    Swig_typemap_attach_parms("m3in", l, NULL);

    /* Get return types */
    if ((tm = getMappedTypeNew(n, "m3wraptype", ""))) {
      substituteClassname(t, tm);
      Printf(return_type, "%s", tm);
    }

    if (proxy_flag && wrapping_member_flag && !enum_constant_flag) {
      // Properties
      setter_flag = (Cmp(Getattr(n, "sym:name"), Swig_name_set(NSPACE_TODO, Swig_name_member(NSPACE_TODO, proxy_class_name, variable_name)))
		     == 0);
    }

    /* Start generating the proxy function */
    Printf(function_code, "  %s ", Getattr(n, "feature:modula3:methodmodifiers"));
    if (static_flag)
      Printf(function_code, "static ");
    if (Getattr(n, "override"))
      Printf(function_code, "override ");
    else if (checkAttribute(n, "storage", "virtual"))
      Printf(function_code, "virtual ");

    Printf(function_code, "%s %s(", return_type, proxy_function_name);

    Printv(imcall, m3raw_name, ".", intermediary_function_name, "(", NIL);
    if (!static_flag)
      Printv(imcall, "swigCPtr", NIL);

    emit_mark_varargs(l);

    int gencomma = !static_flag;

    /* Output each parameter */
    for (i = 0, p = l; p; i++) {

      /* Ignored varargs */
      if (checkAttribute(p, "varargs:ignore", "1")) {
	p = nextSibling(p);
	continue;
      }

      /* Ignored parameters */
      if (checkAttribute(p, "tmap:in:numinputs", "0")) {
	p = Getattr(p, "tmap:in:next");
	continue;
      }

      /* Ignore the 'this' argument for variable wrappers */
      if (!(variable_wrapper_flag && i == 0)) {
	SwigType *pt = Getattr(p, "type");
	String *param_type = NewString("");

	/* Get the Modula 3 parameter type */
	if ((tm = getMappedType(p, "m3wraptype"))) {
	  substituteClassname(pt, tm);
	  Printf(param_type, "%s", tm);
	}

	if (gencomma)
	  Printf(imcall, ", ");

	String *arg = variable_wrapper_flag ? NewString("value") : makeParameterName(n,
										     p,
										     i);

	// Use typemaps to transform type used in Modula 3 wrapper function (in proxy class) to type used in PInvoke function (in intermediary class)
	if ((tm = getMappedType(p, "in"))) {
	  addThrows(throws_hash, "in", p);
	  substituteClassname(pt, tm);
	  Replaceall(tm, "$input", arg);
	  Printv(imcall, tm, NIL);
	}

	/* Add parameter to proxy function */
	if (gencomma >= 2)
	  Printf(function_code, ", ");
	gencomma = 2;
	Printf(function_code, "%s %s", param_type, arg);

	Delete(arg);
	Delete(param_type);
      }
      p = Getattr(p, "tmap:in:next");
    }

    Printf(imcall, ")");
    Printf(function_code, ")");

    // Transform return type used in PInvoke function (in intermediary class) to type used in Modula 3 wrapper function (in proxy class)
    if ((tm = getMappedTypeNew(n, "m3out", ""))) {
      addThrows(throws_hash, "m3out", n);
      if (GetFlag(n, "feature:new"))
	Replaceall(tm, "$owner", "true");
      else
	Replaceall(tm, "$owner", "false");
      substituteClassname(t, tm);
      Replaceall(tm, "$imcall", imcall);
    }

    generateThrowsClause(throws_hash, function_code);
    Printf(function_code, " %s\n\n", tm ? (const String *) tm : empty_string);

    if (proxy_flag && wrapping_member_flag && !enum_constant_flag) {
      // Properties
      if (setter_flag) {
	// Setter method
	if ((tm = getMappedTypeNew(n, "m3varin", ""))) {
	  if (GetFlag(n, "feature:new"))
	    Replaceall(tm, "$owner", "true");
	  else
	    Replaceall(tm, "$owner", "false");
	  substituteClassname(t, tm);
	  Replaceall(tm, "$imcall", imcall);
	  Printf(proxy_class_code, "%s", tm);
	}
      } else {
	// Getter method
	if ((tm = getMappedTypeNew(n, "m3varout", ""))) {
	  if (GetFlag(n, "feature:new"))
	    Replaceall(tm, "$owner", "true");
	  else
	    Replaceall(tm, "$owner", "false");
	  substituteClassname(t, tm);
	  Replaceall(tm, "$imcall", imcall);
	  Printf(proxy_class_code, "%s", tm);
	}
      }
    } else {
      // Normal function call
      Printv(proxy_class_code, function_code, NIL);
    }

    Delete(function_code);
    Delete(return_type);
    Delete(imcall);
    Delete(throws_hash);
  }

  /* ----------------------------------------------------------------------
   * constructorHandler()
   * ---------------------------------------------------------------------- */

  virtual int constructorHandler(Node *n) {

    Hash *throws_hash = NewHash();
    ParmList *l = Getattr(n, "parms");
    String *className = Getattr(n, "name"); 

    //fix to renames and templates
    String *consSymName = Getattr(n,"constructorDeclaration:sym:name");

    String *sname = Getattr(n, "sym:name");
    String *oname = Getattr(n, "sym:overname");
    Replaceall(oname, "__SWIG_", "");

    bool isTemplate = false;

    if (Strstr(className,"<") && Strstr(className,">") ) {
      isTemplate = true;
    }
    /*if name has something like *<(*)> in it then we probably know
    its a template and is not going to be renamed. Is that even possible
    renaming an overloaded template constructor??*/

    //lets compare sname to consSymName not classname
    bool constrRenamed = (Strcmp(sname,className) != 0);
        

    if (constrRenamed) {
      String *overrides = getAttrString(parentNode(n), "modula3:override");
      Printf(overrides, "%s := New_%s;\n", sname,sname);
    } else {
      //default case create an init  name
      String *overrides = getAttrString(parentNode(n), "modula3:override");
      Printf(overrides, "init_%s := New_%s%s;\n", oname,sname,oname);
    }
    
    // this invokes functionWrapper
    Language::constructorHandler(n);

    //Remove the first "self" parameter of the arg list terminated by ;
    String *consArgs = getAttrString(n, "m3consargs");

    char *semi = strchr(Char(consArgs), ';');
    String *tmpArgs = NewString(semi + 1);

    int num_exceptions = 0;
    Parm *p = l;
    int i;
    for (i = 0; p; i++) {

      num_exceptions += addThrows(throws_hash, "m3wrapincheck", p);
      num_exceptions += addThrows(throws_hash, "m3wrapretvar", p);
      num_exceptions += addThrows(throws_hash, "m3wrapargvar", p);
      num_exceptions += addThrows(throws_hash, "m3wrapinconv", p);
      num_exceptions += addThrows(throws_hash, "m3wrapargraw", p);
      num_exceptions += addThrows(throws_hash, "m3wrapretcheck", p);
      num_exceptions += addThrows(throws_hash, "m3wrapoutcheck", p);
      num_exceptions += addThrows(throws_hash, "m3wrapoutconv", p);
      num_exceptions += addThrows(throws_hash, "m3wrapfreearg", p);
      num_exceptions += addThrows(throws_hash, "m3wrapretconv", p);
      p = nextSibling(p);
    }

    String *throws = NewString("");
    generateThrowsClause(throws_hash, throws);

    //set the parent node overrides for the class
    String *classOver = getAttrString(parentNode(n), "m3consargs");

    if (isTemplate) {

      Printf(classOver, "%s (%s) : %s%s;\n", sname, tmpArgs, consSymName,throws);

    } else if (constrRenamed) {
      Printf(classOver, "%s (%s) : %s%s;\n", sname, tmpArgs, className,throws);
    } else {
      //default case use init name
      String *oxy = do_oxy(n,"ConstructorHandler");
      Printf(classOver, "%sinit_%s (%s) : %s%s;\n", oxy, oname, tmpArgs, className,throws);
    }

    if (proxy_flag) {

      Clear(throws_hash);

      String *overloaded_name = getOverloadedName(n);
      String *imcall = NewString("");

      Printf(proxy_class_code, "  %s %s(", Getattr(n, "feature:modula3:methodmodifiers"), proxy_class_name);
      Printv(imcall, " : this(", m3raw_name, ".", Swig_name_construct(NSPACE_TODO, overloaded_name), "(", NIL);

      /* Attach the non-standard typemaps to the parameter list */
      Swig_typemap_attach_parms("in", l, NULL);
      Swig_typemap_attach_parms("m3wraptype", l, NULL);
      Swig_typemap_attach_parms("m3in", l, NULL);

      emit_mark_varargs(l);

      int gencomma = 0;

      String *tm;
      Parm *p = l;
      int i;

      /* Output each parameter */
      for (i = 0; p; i++) {

	/* Ignored varargs */
	if (checkAttribute(p, "varargs:ignore", "1")) {
	  p = nextSibling(p);
	  continue;
	}

	/* Ignored parameters */
	if (checkAttribute(p, "tmap:in:numinputs", "0")) {
	  p = Getattr(p, "tmap:in:next");
	  continue;
	}

	SwigType *pt = Getattr(p, "type");
	String *param_type = NewString("");

	/* Get the Modula 3 parameter type */
	if ((tm = getMappedType(p, "m3wraptype"))) {
	  substituteClassname(pt, tm);
	  Printf(param_type, "%s", tm);
	}

	if (gencomma)
	  Printf(imcall, ", ");

	String *arg = makeParameterName(n, p, i);

	// Use typemaps to transform type used in Modula 3 wrapper function (in proxy class) to type used in PInvoke function (in intermediary class)
	if ((tm = getMappedType(p, "in"))) {
	  addThrows(throws_hash, "in", p);
	  substituteClassname(pt, tm);
	  Replaceall(tm, "$input", arg);
	  Printv(imcall, tm, NIL);
	}

	/* Add parameter to proxy function */
	if (gencomma)
	  Printf(proxy_class_code, ", ");
	Printf(proxy_class_code, "%s %s", param_type, arg);
	gencomma = 1;

	Delete(arg);
	Delete(param_type);
	p = Getattr(p, "tmap:in:next");
      }

      Printf(imcall, "), true)");

      Printf(proxy_class_code, ")");
      Printf(proxy_class_code, "%s", imcall);
      generateThrowsClause(throws_hash, proxy_class_code);
      Printf(proxy_class_code, " {\n");
      Printf(proxy_class_code, "  }\n\n");

      if (!gencomma)		// We must have a default constructor
	have_default_constructor_flag = true;

      Delete(overloaded_name);
      Delete(imcall);

    }

    Delete(throws_hash);

    return SWIG_OK;
  }

  /* ----------------------------------------------------------------------
   * destructorHandler()
   * ---------------------------------------------------------------------- */

  virtual int destructorHandler(Node *n) {
    Language::destructorHandler(n);
    String *symname = Getattr(n, "sym:name");

    if (proxy_flag) {
      Printv(destructor_call, m3raw_name, ".", Swig_name_destroy(NSPACE_TODO, symname), "(swigCPtr)", NIL);
    }
    return SWIG_OK;
  }

  /* ----------------------------------------------------------------------
   * membervariableHandler()
   * ---------------------------------------------------------------------- */

  virtual int membervariableHandler(Node *n) {
    SwigType *t = Getattr(n, "type");
    String *tm;
    String *wtm;
    
    variable_name = Getattr(n, "sym:name");

    // Get the variable type
    if ((tm = getMappedTypeNew(n, "m3rawtype", ""))) {
      substituteClassname(t, tm);
    }
    if ((wtm = getMappedTypeNew(n, "m3wraptype", ""))) {
      substituteClassname(t, wtm);
    }
    
    /* this node supplies correct c type for records */
    String *rawRecField = getAttrString(parentNode(n), "modula3:rawrecfield");
    if (hasContent(rawRecField)) Append(rawRecField,";\n");
    Printf(rawRecField, "%s : %s", variable_name, tm);
    
    String *recField = getAttrString(parentNode(n), "modula3:recfield");
    if (hasContent(recField)) Append(recField,";\n");
    Printf(recField, "%s : %s", variable_name, wtm);
    
    // Output the property's field declaration and accessor methods
    Printf(proxy_class_code, "  public %s %s {", tm, variable_name);

    Setattr(n, "modula3:functype", "accessor");
    wrapping_member_flag = true;
    variable_wrapper_flag = true;
    Language::membervariableHandler(n);
    wrapping_member_flag = false;
    variable_wrapper_flag = false;

    Printf(proxy_class_code, "\n  }\n\n");

    {
      String *methods = getMethodDeclarations(n);
      String *overrides = getAttrString(parentNode(n), "modula3:override");
      SwigType *type = Getattr(n, "type");
      String *m3name = capitalizeFirst(variable_name);
      if (!SwigType_isconst(type)) {
	{
	  String *inmode = getMappedTypeNew(n, "m3wrapinmode", "", false);
	  String *intype = getMappedTypeNew(n, "m3wrapintype", "");
	  Printf(methods, "set%s(%s val:%s);\n", m3name, (inmode != NIL) ? (const String *) inmode : "", intype);
	}
	{
	  /* this was attached by functionWrapper
	     invoked by Language::memberfunctionHandler */
	  String *fname = Getattr(n, "modula3:setname");
	  Printf(overrides, "set%s := %s;\n", m3name, fname);
	}
      }
      {
	{
	  String *outtype = getMappedTypeNew(n, "m3wrapouttype", "");
	  Printf(methods, "get%s():%s;\n", m3name, outtype);
	}
	{
	  /* this was attached by functionWrapper
	     invoked by Language::memberfunctionHandler */
	  String *fname = Getattr(n, "modula3:getname");
	  Printf(overrides, "get%s := %s;\n", m3name, fname);
	}
      }
      Delete(m3name);
    }

    return SWIG_OK;
  }

  /* ----------------------------------------------------------------------
   * staticmembervariableHandler()
   * ---------------------------------------------------------------------- */

  virtual int staticmembervariableHandler(Node *n) {

    bool static_const_member_flag = (Getattr(n, "value") == 0);
    if (static_const_member_flag) {
      SwigType *t = Getattr(n, "type");
      String *tm;

      // Get the variable type
      if ((tm = getMappedTypeNew(n, "m3wraptype", ""))) {
	substituteClassname(t, tm);
      }
      // Output the property's field declaration and accessor methods
      Printf(proxy_class_code, "  public static %s %s {", tm, Getattr(n, "sym:name"));
    }

    variable_name = Getattr(n, "sym:name");
    wrapping_member_flag = true;
    static_flag = true;
    Language::staticmembervariableHandler(n);
    wrapping_member_flag = false;
    static_flag = false;

    if (static_const_member_flag)
      Printf(proxy_class_code, "\n  }\n\n");

    return SWIG_OK;
  }
  
  bool RestrictTmap(String *p,String *tmap,const String *func_name) {
    
    String *tm = NewStringf("%s%s",tmap,":restrictfunc");
    String *thisFunc = Getattr(p,tm);
    if ((thisFunc != NIL) && (Strcmp(thisFunc,func_name) == 0)) {
      return true;
    } else {
      return false;
    };
  };
 
  /* ----------------------------------------------------------------------
   * memberconstantHandler()
   * ---------------------------------------------------------------------- */

  virtual int memberconstantHandler(Node *n) {
    variable_name = Getattr(n, "sym:name");
    wrapping_member_flag = true;
    Language::memberconstantHandler(n);
    wrapping_member_flag = false;
    return SWIG_OK;
  }

  /* -----------------------------------------------------------------------------
   * getOverloadedName()
   * ----------------------------------------------------------------------------- */

  String *getOverloadedName(Node *n) {
    String *overloaded_name = Copy(Getattr(n, "sym:name"));

    if (Getattr(n, "sym:overloaded")) {
      Printv(overloaded_name, Getattr(n, "sym:overname"), NIL);
    }

    return overloaded_name;
  }

  /* -----------------------------------------------------------------------------
   * emitM3Wrapper()
   * It is also used for set and get methods of global variables.
   * ----------------------------------------------------------------------------- */

  void emitM3Wrapper(Node *n, const String *func_name) {
    SwigType *t = Getattr(n, "type");
    ParmList *l = Getattr(n, "parms");
    Hash *throws_hash = NewHash();
    int num_exceptions = 0;
    int num_returns = 0;
    String *rawcall = NewString("");
    String *reccall = NewString("");
    String *local_variables = NewString("");
    String *local_constants = NewString("");
    String *incheck = NewString("");
    String *outcheck = NewString("");
    String *setup = NewString("");
    String *cleanup = NewString("");
    String *outarg = NewString("");	/* don't mix up with 'autark' :-] */
    String *storeout = NewString("");
    String *result_name = NewString("");
    String *return_variables = NewString("");
    const char *result_return = "ret";
    String *function_code = NewString("");
    /*several names for the same function */
    String *raw_name = Getattr(n, "name");	/*original C function name */
    bool setter_flag = false;
    int multiretval = GetFlag(n, "feature:modula3:multiretval");


    String *abstract = Getattr(n, "abstract");
    //if abstract method dont emit
    if (abstract) return;

    if (l) {
      if (SwigType_type(Getattr(l, "type")) == T_VOID) {
	l = nextSibling(l);
      }
    }

    /* Attach the non-standard typemaps to the parameter list */
    Swig_typemap_attach_parms("m3wrapargvar", l, NULL);
    Swig_typemap_attach_parms("m3wrapargconst", l, NULL);
    Swig_typemap_attach_parms("m3wrapargraw", l, NULL);
    Swig_typemap_attach_parms("m3wrapargdir", l, NULL);
    Swig_typemap_attach_parms("m3wrapinmode", l, NULL);
    Swig_typemap_attach_parms("m3wrapinname", l, NULL);
    Swig_typemap_attach_parms("m3wrapintype", l, NULL);
    Swig_typemap_attach_parms("m3wrapindefault", l, NULL);
    Swig_typemap_attach_parms("m3wrapinconv", l, NULL);
    Swig_typemap_attach_parms("m3wrapincheck", l, NULL);
    Swig_typemap_attach_parms("m3wrapoutname", l, NULL);
    Swig_typemap_attach_parms("m3wrapouttype", l, NULL);
    Swig_typemap_attach_parms("m3wrapoutconv", l, NULL);
    Swig_typemap_attach_parms("m3wrapoutcheck", l, NULL);

    //add new tmap for enum checking
    Swig_typemap_attach_parms("m3wrapenumcheck", l, NULL);

    attachMappedType(n, "m3wrapretraw");
    attachMappedType(n, "m3wrapretname");
    attachMappedType(n, "m3wraprettype");
    attachMappedType(n, "m3wrapretvar");
    attachMappedType(n, "m3wrapretconv");
    attachMappedType(n, "m3wrapretcheck");

    Swig_typemap_attach_parms("m3wrapfreearg", l, NULL);


    /* Attach argument names to the parameter list */
    /* should be a separate procedure making use of hashes */
    attachParameterNames(n, "tmap:m3wrapinname", "autoname", "arg%d");

    /* Get return types */
    String *result_m3rawtype = Copy(getMappedTypeNew(n, "m3rawrettype", ""));
    String *result_m3wraptype = Copy(getMappedTypeNew(n, "m3wraprettype", ""));
    bool has_return_raw = hasContent(result_m3rawtype);
    bool has_return_m3 = hasContent(result_m3wraptype);
    if (has_return_m3) {
      num_returns++;
    }

    String *arguments = createM3Signature(n);

    String *type = nodeType(n);

    if (Strcmp(type, "constructor") == 0) {
      Setattr(n,"m3consargs",arguments);
      //It makes sense that the type of the self is the same as the type returned
      String *selfArgs = NewStringf("self:%s;",result_m3wraptype);
      Push(arguments,selfArgs);
    }

    /* Create local variables or RECORD fields for return values
       and determine return type that might result from a converted VAR argument. */
    {
      writeArgState state;
      if (multiretval && has_return_m3) {
	writeArg(return_variables, state, NIL, NewString(result_return), result_m3wraptype, NIL);
      }

      Parm *p = skipIgnored(l, "m3wrapouttype");
      while (p != NIL) {

	String *arg = Getattr(p, "tmap:m3wrapoutname");
	if (arg == NIL) {
	  arg = Getattr(p, "name");
	}

	String *tm = Getattr(p, "tmap:m3wrapouttype");
	if (tm != NIL) {
	  if (isOutParam(p)) {
	    if (!multiretval) {
	      if (num_returns == 0) {
		Printv(result_name, arg, NIL);
		Clear(result_m3wraptype);
		Printv(result_m3wraptype, tm, NIL);
	      } else {
		Swig_warning(WARN_MODULA3_TYPEMAP_MULTIPLE_RETURN, input_file, line_number,
			     "Typemap m3wrapargdir set to 'out' for %s implies a RETURN value, but the routine %s has already one.\nUse %%multiretval feature.\n",
			     SwigType_str(Getattr(p, "type"), 0), raw_name);
	      }
	    }
	    num_returns++;
	    addImports(m3wrap_intf.import, "m3wrapouttype", p);
	    writeArg(return_variables, state, NIL, arg, tm, NIL);
	  }
	  p = skipIgnored(Getattr(p, "tmap:m3wrapouttype:next"), "m3wrapouttype");
	} else {
	  p = nextSibling(p);
	}
      }
      writeArg(return_variables, state, NIL, NIL, NIL, NIL);

      if (multiretval) {
	Printv(result_name, Swig_cresult_name(), NIL);
	Printf(result_m3wraptype, "%sResult", func_name);
	m3wrap_intf.enterBlock(blocktype);
	Printf(m3wrap_intf.f, "%s =\nRECORD\n%sEND;\n", result_m3wraptype, return_variables);
	Printf(local_variables, "%s: %s;\n", result_name, result_m3wraptype);
      } else {
	Append(local_variables, return_variables);
      }
    }

   //added m3wrapenumcheck
   if (genEnumChecks_flag) {

      Parm *p = l;
      while (p != NIL) {

	String *arg = Getattr(p, "autoname");

	String *tm = Getattr(p, "tmap:m3wrapenumcheck");
	if (tm != NIL) {
          Hash *enumHash = Getattr(enum_coll,tm);
          if (enumHash != NIL) {

            String *proper = Getattr(enumHash,"proper");
            if (Strcmp(proper,"0") == 0) { //so its not proper

              //the constant we check against
              String *errConst = Getattr(enumHash,"errconst");
              String *useSet = Getattr(enumHash,"useSet");

              String *runCheck;
              if (Strcmp(useSet,"1") == 0) {
                runCheck = NewStringf("<* ASSERT $input IN %s *>",errConst);
              } else {
                runCheck = NewStringf("<* ASSERT CheckOption($input,%s) *>",errConst);
              }
              Replaceall(runCheck, "$input", arg);

              Printv(setup, runCheck, "\n", NIL);

            } //if proper
          } //if found hash

	  p = Getattr(p, "tmap:m3wrapenumcheck:next");
	} else {
	  p = nextSibling(p);
	}

      }
    }


    /* Declare local constants e.g. for storing argument names. */
    {
      Parm *p = l;
      while (p != NIL) {

	String *arg = Getattr(p, "autoname");

	String *tm = Getattr(p, "tmap:m3wrapargconst");
	if (tm != NIL) {
	  addImports(m3wrap_impl.import, "m3wrapargconst", p);
	  Replaceall(tm, "$input", arg);
	  Printv(local_constants, tm, "\n", NIL);
	  p = Getattr(p, "tmap:m3wrapargconst:next");
	} else {
	  p = nextSibling(p);
	}

      }
    }

    /* Declare local variables e.g. for converted input values. */
    {      
      String *tm = getMappedTypeNew(n, "m3wrapretvar", "", false);
      if (tm != NIL) {
	  //in some cases we only want the import in the implementation
	  //in which case add impleonly="1" to the m3wrapintype tmap
	String *implOnly = Getattr(n, "tmap:m3wrapintype:impleonly");
	if (!implOnly) {
      	  addImports(m3wrap_intf.import, "m3wrapretvar", n);
      	}

	addImports(m3wrap_impl.import, "m3wrapretvar", n);
	addThrows(throws_hash, "m3wrapretvar", n);
	Printv(local_variables, tm, "\n", NIL);
      }

      Parm *p = l;
      while (p != NIL) {

	String *arg = Getattr(p, "autoname");

	tm = Getattr(p, "tmap:m3wrapargvar");
	if (tm != NIL) {
	  /* exceptions that may be raised but can't be caught,
	     thus we won't count them in num_exceptions */
	  addImports(m3wrap_impl.import, "m3wrapargvar", p);
	  addThrows(throws_hash, "m3wrapargvar", p);
	  Replaceall(tm, "$input", arg);
	  Printv(local_variables, tm, "\n", NIL);
	  p = Getattr(p, "tmap:m3wrapargvar:next");
	} else {
	  p = nextSibling(p);
	}

      }
    }

    /* Convert input values from Modula 3 to C. */
    {
      Parm *p = l;
      while (p != NIL) {

	String *arg = Getattr(p, "autoname");

	String *tm = Getattr(p, "tmap:m3wrapinconv");
	if (tm != NIL) {
	  addImports(m3wrap_impl.import, "m3wrapinconv", p);
	  num_exceptions += addThrows(throws_hash, "m3wrapinconv", p);
	  Replaceall(tm, "$input", arg);
	  Printv(setup, tm, "\n", NIL);
	  p = Getattr(p, "tmap:m3wrapinconv:next");
	} else {
	  p = nextSibling(p);
	}

      }
    }

    /* Generate checks for input value integrity. */
    {
      Parm *p = l;
      while (p != NIL) {

	String *arg = Getattr(p, "autoname");

	String *tm = Getattr(p, "tmap:m3wrapincheck");
	if (tm != NIL) {
	  addImports(m3wrap_impl.import, "m3wrapincheck", p);
	  num_exceptions += addThrows(throws_hash, "m3wrapincheck", p);
	  Replaceall(tm, "$input", arg);
	  Printv(incheck, tm, "\n", NIL);
	  p = Getattr(p, "tmap:m3wrapincheck:next");
	} else {
	  p = nextSibling(p);
	}

      }
    }


    String *oname = Getattr(n, "sym:overname");
    Replaceall(oname, "__SWIG_", "");

    if (Strcmp(type, "constructor") != 0) {
      if (Strcmp(oname,"0") != 0) {
        func_name = NewStringf("%s%s",func_name,oname);
      }
    } else {
      //its a constructor nothing needed
    }

    if (dynLink_flag == false) {
      Printv(rawcall, m3raw_name, ".", func_name, "(", NIL);
    } else {
      Printv(rawcall, "LOOPHOLE(dynProcArr[DynEnum.",raw_name,"].adr,",m3raw_name, ".", raw_name, ")(", NIL); 
      if (dynProcs == NIL) {
        dynProcs = NewString("");
        dynInit = NewString("");
      } else {
        Append(dynProcs, ",");
        Append(dynInit, ",");
      }
      Append(dynProcs, raw_name);
      {
        String *parm = NewString("");
        Printv(parm,"NEW(DynProc,name := \"",raw_name,"\",adr := NIL)",NIL);
        Append(dynInit, parm);
      }
    }

    /* Arguments to the raw C function */
    {
      bool gencomma = false;
      Parm *p = l;
      while (p != NIL) {
	if (gencomma) {
	  Printf(rawcall, ", ");
	}
	gencomma = true;
	addImports(m3wrap_impl.import, "m3wrapargraw", p);
	num_exceptions += addThrows(throws_hash, "m3wrapargraw", p);

	String *arg = Getattr(p, "autoname");
	String *qualarg = NewString("");
	if (!isInParam(p)) {
	  String *tmparg = Getattr(p, "tmap:m3wrapoutname");
	  if (tmparg != NIL) {
	    arg = tmparg;
	  }
	  if (multiretval /*&& isOutParam(p) - automatically fulfilled */ ) {
	    Printf(qualarg, "%s.", result_name);
	  }
	}
	Append(qualarg, arg);
	Setattr(p, "m3outarg", qualarg);

	String *tm = Getattr(p, "tmap:m3wrapargraw");
	if (tm != NIL) {
	  Replaceall(tm, "$input", arg);
	  Replaceall(tm, "$output", qualarg);
	  Printv(rawcall, tm, NIL);
	  p = Getattr(p, "tmap:m3wrapargraw:next");
	} else {
	  //Printv(rawcall, Getattr(p,"lname"), NIL);
	  Printv(rawcall, qualarg, NIL);
	  p = nextSibling(p);
	}
	Delete(qualarg);
      }
    }
    Printf(rawcall, ")");

    /* Check for error codes and integrity of results */
    {
      String *tm = getMappedTypeNew(n, "m3wrapretcheck", "", false);
      if (tm != NIL) {
	addImports(m3wrap_impl.import, "m3wrapretcheck", n);
	num_exceptions += addThrows(throws_hash, "m3wrapretcheck", n);
	Printv(outcheck, tm, "\n", NIL);
      }

      Parm *p = l;
      while (p != NIL) {
	tm = Getattr(p, "tmap:m3wrapoutcheck");
	if (tm != NIL) {
	  String *arg = Getattr(p, "autoname");
	  String *outarg = Getattr(p, "m3outarg");
	  addImports(m3wrap_impl.import, "m3wrapoutcheck", p);
	  num_exceptions += addThrows(throws_hash, "m3wrapoutcheck", p);
	  //substituteClassname(Getattr(p,"type"), tm);
	  Replaceall(tm, "$input", arg);
	  Replaceall(tm, "$output", outarg);
	  Printv(outcheck, tm, "\n", NIL);
	  p = Getattr(p, "tmap:m3wrapoutcheck:next");
	} else {
	  p = nextSibling(p);
	}
      }
    }

    /* Convert the results to Modula 3 data structures and
       put them in the record prepared for returning */
    {
      /* m3wrapretconv is processed
         when it is clear if there is some output conversion and checking code */
      Parm *p = l;
      while (p != NIL) {
	String *tm = Getattr(p, "tmap:m3wrapoutconv");
	if (tm != NIL) {
	  String *arg = Getattr(p, "autoname");
	  String *outarg = Getattr(p, "m3outarg");
	  //trim spaces and check if anything in the tmap
	  Chop(tm);
          if (hasContent(tm)) {
	    addImports(m3wrap_impl.import, "m3wrapoutconv", n);
	    num_exceptions += addThrows(throws_hash, "m3wrapoutconv", p);
	    //substituteClassname(Getattr(p,"type"), tm);
	    Replaceall(tm, "$input", arg);
	    Replaceall(tm, "$output", outarg);
	    Printf(storeout, "%s := %s;\n", outarg, tm);
	  }
	  p = Getattr(p, "tmap:m3wrapoutconv:next");
	} else {
	  p = nextSibling(p);
	}
      }
    }

    /* Generate cleanup code */
    {
      Parm *p = l;
      while (p != NIL) {
	String *tm = Getattr(p, "tmap:m3wrapfreearg");
	if (tm != NIL) {
	  String *arg = Getattr(p, "autoname");
	  String *outarg = Getattr(p, "m3outarg");
	  addImports(m3wrap_impl.import, "m3wrapfreearg", p);
	  num_exceptions += addThrows(throws_hash, "m3wrapfreearg", p);
	  //substituteClassname(Getattr(p,"type"), tm);
	  Replaceall(tm, "$input", arg);
	  Replaceall(tm, "$output", outarg);
	  Printv(cleanup, tm, "\n", NIL);
	  p = Getattr(p, "tmap:m3wrapfreearg:next");
	} else {
	  p = nextSibling(p);
	}
      }
    }

    {
      /* Currently I don't know how a typemap similar to the original 'out' typemap
         could help returning the return value. */
      /* Receive result from call to raw library function */
      if (!has_return_raw) {

	Printf(reccall, "%s;\n", rawcall);

	if (hasContent(result_name)) {
	  Printf(outarg, "RETURN %s;\n", result_name);
	}
      } else {

	String *return_raw = getMappedTypeNew(n, "m3wrapretraw", "", false);
	String *return_conv = getMappedTypeNew(n, "m3wrapretconv", "", false);

	/* immediate RETURN would skip result checking */
	if ((hasContent(outcheck) || hasContent(storeout)
	     || hasContent(cleanup)) && (!hasContent(result_name))
	    && (return_raw == NIL)) {
	  Printv(result_name, Swig_cresult_name(), NIL);
	  Printf(local_variables, "%s: %s;\n", result_name, result_m3wraptype);
	}

	String *result_lvalue = Copy(result_name);
	if (multiretval) {
	  Printf(result_lvalue, ".%s", result_return);
	}
	if (return_raw != NIL) {
	  Printf(reccall, "%s := %s;\n", return_raw, rawcall);
	} else if (hasContent(result_name)) {
	  Printf(reccall, "%s := %s;\n", result_lvalue, rawcall);
	} else {
	  Printf(outarg, "RETURN %s;\n", rawcall);
	}
	if (return_conv != NIL) {
	  addImports(m3wrap_impl.import, "m3wrapretconv", n);
	  num_exceptions += addThrows(throws_hash, "m3wrapretconv", n);
	  if (hasContent(result_name)) {
	    Printf(reccall, "%s := %s;\n", result_lvalue, return_conv);
	    Printf(outarg, "RETURN %s;\n", result_name);
	  } else {
	    Printf(outarg, "RETURN %s;\n", return_conv);
	  }
	} else {
	  if (hasContent(result_name)) {
	    Printf(outarg, "RETURN %s;\n", result_name);
	  }
	}
      }
    }
    /* Create procedure header */
    {
      String *header = NewStringf("PROCEDURE %s (%s)",
				  func_name, arguments);

      if ((num_returns > 0) || multiretval) {
	Printf(header, ": %s", result_m3wraptype);
      }
      generateThrowsClause(throws_hash, header);

      Append(function_code, header);

      String *type = nodeType(n);
      String *funcType = Getattr(n, "modula3:functype");
      //Here we output the interface procedure
      //functype is null or method we dont output methods theyre done in classes
      //so anything thats cdecl is ok that includes statics but not constructs or destructors
      if ((Cmp(type, "cdecl") == 0) && (!funcType)) {
        m3wrap_intf.enterBlock(no_block);
        String *oxy = do_oxy(n,"emitM3Wrapper");
        Printf(m3wrap_intf.f, "%s%s;\n\n", oxy, header);
      }

    }

    {
      String *body = NewStringf("%s%s%s%s%s",
				incheck,
				setup,
				reccall,
				outcheck,
				storeout);

      String *exc_handler;
      if (hasContent(cleanup) && (num_exceptions > 0)) {
	exc_handler = NewStringf("TRY\n%sFINALLY\n%sEND;\n", body, cleanup);
      } else {
	exc_handler = NewStringf("%s%s", body, cleanup);
      }

      Printf(function_code, " =\n%s%s%s%sBEGIN\n%s%sEND %s;\n\n",
	     hasContent(local_constants) ? "CONST\n" : "", local_constants,
	     hasContent(local_variables) ? "VAR\n" : "", local_variables, exc_handler, outarg, func_name);

      Delete(exc_handler);
      Delete(body);
    }

    m3wrap_impl.enterBlock(no_block);
    if (proxy_flag && global_variable_flag) {
      setter_flag = (Cmp(Getattr(n, "sym:name"), Swig_name_set(NSPACE_TODO, variable_name)) == 0);
      // Properties
      if (setter_flag) {
	// Setter method
	String *tm = getMappedTypeNew(n, "m3varin", "");
	if (tm != NIL) {
	  if (GetFlag(n, "feature:new")) {
	    Replaceall(tm, "$owner", "true");
	  } else {
	    Replaceall(tm, "$owner", "false");
	  }
	  substituteClassname(t, tm);
	  Replaceall(tm, "$rawcall", rawcall);
	  Replaceall(tm, "$vartype", variable_type);	/* $type is already replaced by some super class */
	  Replaceall(tm, "$var", variable_name);
	  Printf(m3wrap_impl.f, "%s", tm);
	}
      } else {
	// Getter method
	String *tm = getMappedTypeNew(n, "m3varout", "");
	if (tm != NIL) {
	  if (GetFlag(n, "feature:new"))
	    Replaceall(tm, "$owner", "true");
	  else
	    Replaceall(tm, "$owner", "false");
	  substituteClassname(t, tm);
	  Replaceall(tm, "$rawcall", rawcall);
	  Replaceall(tm, "$vartype", variable_type);
	  Replaceall(tm, "$var", variable_name);
	  Printf(m3wrap_impl.f, "%s", tm);
	}
      }
    } else {
      // Normal function call
      Printv(m3wrap_impl.f, function_code, NIL);
    }

    Delete(arguments);
    Delete(return_variables);
    Delete(local_variables);
    Delete(local_constants);
    Delete(outarg);
    Delete(incheck);
    Delete(outcheck);
    Delete(setup);
    Delete(cleanup);
    Delete(storeout);
    Delete(function_code);
    Delete(result_name);
    Delete(result_m3wraptype);
    Delete(reccall);
    Delete(rawcall);
    Delete(throws_hash);
  }

  /*----------------------------------------------------------------------
   * replaceSpecialVariables()
   *--------------------------------------------------------------------*/

  virtual void replaceSpecialVariables(String *method, String *tm, Parm *parm) {
    (void)method;
    SwigType *type = Getattr(parm, "type");
    substituteClassname(type, tm);
  }

  /* -----------------------------------------------------------------------------
   * substituteClassname()
   *
   * Substitute the special variable $m3classname with the proxy class name for classes/structs/unions
   * that SWIG knows about.
   * Otherwise use the $descriptor name for the Modula 3 class name. Note that the $&m3classname substitution
   * is the same as a $&descriptor substitution, ie one pointer added to descriptor name.
   * Inputs:
   *   pt - parameter type
   *   tm - typemap contents that might contain the special variable to be replaced
   * Outputs:
   *   tm - typemap contents complete with the special variable substitution
   * Return:
   *   substitution_performed - flag indicating if a substitution was performed
   * ----------------------------------------------------------------------------- */

  bool substituteClassname(SwigType *pt, String *tm) {
    bool substitution_performed = false;
    if (Strstr(tm, "$m3classname") || Strstr(tm, "$&m3classname")) {
      String *classname = getProxyName(pt);
      if (classname) {
	Replaceall(tm, "$&m3classname", classname);	// getProxyName() works for pointers to classes too
	Replaceall(tm, "$m3classname", classname);
      } else {			// use $descriptor if SWIG does not know anything about this type. Note that any typedefs are resolved.
	String *descriptor = NULL;
	SwigType *type = Copy(SwigType_typedef_resolve_all(pt));

	if (Strstr(tm, "$&m3classname")) {
	  SwigType_add_pointer(type);
	  descriptor = NewStringf("SWIGTYPE%s", SwigType_manglestr(type));
	  Replaceall(tm, "$&m3classname", descriptor);
	} else {		// $m3classname
	  descriptor = NewStringf("SWIGTYPE%s", SwigType_manglestr(type));
	  Replaceall(tm, "$m3classname", descriptor);
	}

	// Add to hash table so that the type wrapper classes can be created later
	Setattr(swig_types_hash, descriptor, type);
	Delete(descriptor);
	Delete(type);
      }
      substitution_performed = true;
    }
    return substitution_performed;
  }

  /* -----------------------------------------------------------------------------
   * makeParameterName()
   *
   * Inputs:
   *   n - Node
   *   p - parameter node
   *   arg_num - parameter argument number
   * Return:
   *   arg - a unique parameter name
   * ----------------------------------------------------------------------------- */

  String *makeParameterName(Node *n, Parm *p, int arg_num) {

    // Use C parameter name unless it is a duplicate or an empty parameter name
    String *pn = Getattr(p, "name");
    int count = 0;
    ParmList *plist = Getattr(n, "parms");
    while (plist) {
      if ((Cmp(pn, Getattr(plist, "name")) == 0))
	count++;
      plist = nextSibling(plist);
    }
    String *arg = (!pn || (count > 1)) ? NewStringf("arg%d",
						    arg_num) : Copy(Getattr(p,
									    "name"));

    return arg;
  }

  /* -----------------------------------------------------------------------------
   * attachParameterNames()
   *
   * Inputs:
   *   n      - Node of a function declaration
   *   tmid   - attribute name for overriding C argument names,
   *              e.g. "tmap:m3wrapinname",
   *              don't forget to attach the mapped types before
   *   nameid - attribute for attaching the names,
   *              e.g. "modula3:inname"
   *   fmt    - format for the argument name containing %d
   *              e.g. "arg%d"
   * ----------------------------------------------------------------------------- */

  void attachParameterNames(Node *n, const char *tmid, const char *nameid, const char *fmt) {
    /* Use C parameter name if present and unique,
       otherwise create an 'arg%d' name */
    Hash *hash = NewHash();
    Parm *p = Getattr(n, "parms");
    int count = 1; //need to start from 1 not 0 to handle unnamed args
    while (p != NIL) {
      String *name = Getattr(p, tmid);
      if (name == NIL) {
	name = Getattr(p, "name");
      }
      String *newname;
      if ((!hasContent(name)) || (Getattr(hash, name) != NIL)) {
	newname = NewStringf(fmt, count);
      } else {
	newname = Copy(name);
      }
      if (1 == Setattr(hash, newname, "1")) {
	Swig_warning(WARN_MODULA3_DOUBLE_ID, input_file, line_number, "Argument '%s' twice.\n", newname);
      }
      Setattr(p, nameid, newname);
      p = nextSibling(p);
      count++;
    }
    Delete(hash);
  }

  /* -----------------------------------------------------------------------------
   * createM3Signature()
   *
   * Create signature of M3 wrapper procedure
   * Call attachParameterNames and attach mapped types before!
   *   m3wrapintype, m3wrapinmode, m3wrapindefault
   * ----------------------------------------------------------------------------- */

  String *createM3Signature(Node *n) {
    String *arguments = NewString("");
    Parm *p = skipIgnored(Getattr(n, "parms"), "m3wrapintype");
    writeArgState state;
    while (p != NIL) {

      /* Get the M3 parameter type */
      String *tm = getMappedType(p, "m3wrapintype");
      if (tm != NIL) {
	if (isInParam(p)) {
	  //in some cases we only want the import in the implementation
	  //in which case add impleonly="1" to the m3wrapintype tmap
	  String *implOnly = Getattr(p, "tmap:m3wrapintype:impleonly");
	  if (!implOnly) {
	    addImports(m3wrap_intf.import, "m3wrapintype", p);
	  }
	  addImports(m3wrap_impl.import, "m3wrapintype", p);
	  String *mode = Getattr(p, "tmap:m3wrapinmode");
	  String *deflt = Getattr(p, "tmap:m3wrapindefault");
	  String *arg = Getattr(p, "autoname");
	  SwigType *pt = Getattr(p, "type");
	  substituteClassname(pt, tm);	/* do we need this ? */
	  writeArg(arguments, state, mode, arg, tm, deflt);
	}
	p = skipIgnored(Getattr(p, "tmap:m3wrapintype:next"), "m3wrapintype");
      } else {
	p = nextSibling(p);
      }
    }
    writeArg(arguments, state, NIL, NIL, NIL, NIL);
    return (arguments);
  }

/* not used any longer
    - try SwigType_str if required again */
#if 0
  /* -----------------------------------------------------------------------------
   * createCSignature()
   *
   * Create signature of C function
   * ----------------------------------------------------------------------------- */

  String *createCSignature(Node *n) {
    String *arguments = NewString("");
    bool gencomma = false;
    Node *p;
    for (p = Getattr(n, "parms"); p != NIL; p = nextSibling(p)) {
      if (gencomma) {
	Append(arguments, ",");
      }
      gencomma = true;
      String *type = Getattr(p, "type");
      String *ctype = getMappedTypeNew(type, "ctype");
      Append(arguments, ctype);
    }
    return arguments;
  }
#endif

  /* -----------------------------------------------------------------------------
   * emitTypeWrapperClass()
   * ----------------------------------------------------------------------------- */

  void emitTypeWrapperClass(String *classname, SwigType *type) {
    Node *n = NewHash();
    Setfile(n, input_file);
    Setline(n, line_number);

    String *filen = NewStringf("%s%s.m3", SWIG_output_directory(), classname);
    File *f_swigtype = NewFile(filen, "w", SWIG_output_files());
    if (!f_swigtype) {
      FileErrorDisplay(filen);
      Exit(EXIT_FAILURE);
    }
    String *swigtype = NewString("");

    // Emit banner name
    emitBanner(f_swigtype);

    // Pure Modula 3 baseclass and interfaces
    const String *pure_baseclass = typemapLookup(n, "m3base", type, WARN_NONE);
    const String *pure_interfaces = typemapLookup(n, "m3interfaces", type, WARN_NONE);

    // Emit the class
    Printv(swigtype, typemapLookup(n, "m3imports", type, WARN_NONE),	// Import statements
	   "\n", typemapLookup(n, "m3classmodifiers", type, WARN_MODULA3_TYPEMAP_CLASSMOD_UNDEF),	// Class modifiers
	   " class $m3classname",	// Class name and bases
	   *Char(pure_baseclass) ? " : " : "", pure_baseclass, *Char(pure_interfaces) ?	// Interfaces
	   " : " : "", pure_interfaces, " {\n", "  private IntPtr swigCPtr;\n", "\n", "  ", typemapLookup(n, "m3ptrconstructormodifiers", type, WARN_MODULA3_TYPEMAP_PTRCONSTMOD_UNDEF),	// pointer constructor modifiers
	   " $m3classname(IntPtr cPtr, bool bFutureUse) {\n",	// Constructor used for wrapping pointers
	   "    swigCPtr = cPtr;\n", "  }\n", "\n", "  protected $m3classname() {\n",	// Default constructor
	   "    swigCPtr = IntPtr.Zero;\n", "  }\n", typemapLookup(n, "m3getcptr", type, WARN_MODULA3_TYPEMAP_GETCPTR_UNDEF),	// getCPtr method
	   typemapLookup(n, "m3code", type, WARN_NONE),	// extra Modula 3 code
	   "}\n", "\n", NIL);

    Replaceall(swigtype, "$m3classname", classname);
    Printv(f_swigtype, swigtype, NIL);

    Delete(f_swigtype);
    Delete(filen);
    Delete(swigtype);
  }

  /* -----------------------------------------------------------------------------
   * typemapLookup()
   * n - for input only and must contain info for Getfile(n) and Getline(n) to work
   * tmap_method - typemap method name
   * type - typemap type to lookup
   * warning - warning number to issue if no typemaps found
   * typemap_attributes - the typemap attributes are attached to this node and will
   *   also be used for temporary storage if non null
   * return is never NULL, unlike Swig_typemap_lookup()
   * ----------------------------------------------------------------------------- */

  const String *typemapLookup(Node *n, const_String_or_char_ptr tmap_method, SwigType *type, int warning, Node *typemap_attributes = 0) {
    Node *node = !typemap_attributes ? NewHash() : typemap_attributes;
    Setattr(node, "type", type);
    Setfile(node, Getfile(n));
    Setline(node, Getline(n));
    const String *tm = Swig_typemap_lookup(tmap_method, node, "", 0);
    if (!tm) {
      tm = empty_string;
      if (warning != WARN_NONE)
	Swig_warning(warning, Getfile(n), Getline(n), "No %s typemap defined for %s\n", tmap_method, SwigType_str(type, 0));
    }
    if (!typemap_attributes)
      Delete(node);
    return tm;
  }

  /* -----------------------------------------------------------------------------
   * addThrows()
   *
   * Add all exceptions to a hash that are associated with the 'typemap'.
   * Return number the number of these exceptions.
   * ----------------------------------------------------------------------------- */

  int addThrows(Hash *throws_hash, const String *typemap, Node *parameter) {
    // Get the comma separated throws clause - held in "throws" attribute in the typemap passed in
    int len = 0;
    String *throws_attribute = NewStringf("%s:throws", typemap);

    addImports(m3wrap_intf.import, throws_attribute, parameter);
    addImports(m3wrap_impl.import, throws_attribute, parameter);

    String *throws = getMappedTypeNew(parameter, Char(throws_attribute), "", false);

    if (throws) {
      // Put the exception classes in the throws clause into a temporary List
      List *temp_classes_list = Split(throws, ',', INT_MAX);
      len = Len(temp_classes_list);

      // Add the exception classes to the node throws list, but don't duplicate if already in list
      if (temp_classes_list /*&& hasContent(temp_classes_list) */ ) {
	for (Iterator cls = First(temp_classes_list); cls.item != NIL; cls = Next(cls)) {
	  String *exception_class = NewString(cls.item);
	  Replaceall(exception_class, " ", "");	// remove spaces
	  Replaceall(exception_class, "\t", "");	// remove tabs
	  if (hasContent(exception_class)) {
	    // $m3classname substitution
	    SwigType *pt = Getattr(parameter, "type");
	    substituteClassname(pt, exception_class);
	    // Don't duplicate the exception class in the throws clause
	    Setattr(throws_hash, exception_class, "1");
	  }
	  Delete(exception_class);
	}
      }
      Delete(temp_classes_list);
    }
    Delete(throws_attribute);
    return len;
  }

  /* -----------------------------------------------------------------------------
   * generateThrowsClause()
   * ----------------------------------------------------------------------------- */

  void generateThrowsClause(Hash *throws_hash, String *code) {
    // Add the throws clause into code
    if (Len(throws_hash) > 0) {
      Iterator cls = First(throws_hash);
      Printf(code, " RAISES {%s", cls.key);
      for (cls = Next(cls); cls.key != NIL; cls = Next(cls)) {
	Printf(code, ", %s", cls.key);
      }
      Printf(code, "}");
    }
  }

  /* -----------------------------------------------------------------------------
   * addImports()
   *
   * Add all imports that are needed for contents of 'typemap'.
   * ----------------------------------------------------------------------------- */

  void addImportsSub(Hash *hash,String *imports) {

    List *import_list = Split(imports, ' ', 3);

    if (import_list) {

      int importLen = Len(import_list);
      String *modName = Getitem(import_list, 0);

      Hash *modHash = Getattr(hash,modName);
      if (!modHash) {
        modHash = NewHash();
        Setattr(hash,modName,modHash);
      }

      if (importLen == 1 || importLen == 3) {

        Hash *itemHash = Getattr(modHash,"IMPORT");
        if (!itemHash) {
          itemHash = NewHash();
          Setattr(modHash,"IMPORT",itemHash);
        }

        if (importLen == 1) {
          //this gives import A AS A which we detect in the generate imports later
          Setattr(itemHash,modName,modName);
        } else {
          //should check item 1 is AS
          if (Strcmp(Getitem(import_list, 1), "AS") == 0) {
            String *item3 = Getitem(import_list, 2);
            Setattr(itemHash,modName,item3);
          } else {
            Swig_warning(WARN_MODULA3_BAD_IMPORT, input_file, line_number,
			 "Malformed import '%s' \n", imports, 0);
          }
        }
      } else {

        Hash *itemHash = Getattr(modHash,"FROM");
        if (itemHash == NIL) {
          itemHash = NewHash();
          Setattr(modHash,"FROM",itemHash);
        }
        String *item2 = Getitem(import_list, 1);
        Setattr(itemHash,item2,item2);
      }
    }
  }


  void addImports(Hash *imports_hash, const String *typemap, Node *node) {
    // Get the comma separated imports clause - held in "imports" attribute in the typemap passed in
    String *imports_attribute = NewStringf("%s:import", typemap);
    String *imports = getMappedTypeNew(node, Char(imports_attribute), "", false);

    if (imports) {

      List *import_list = Split(imports, ',', INT_MAX);
      // Add the import classes to the node imports list, but don't duplicate if already in list
      if (import_list) {

	for (Iterator imp = First(import_list); imp.item != NIL; imp = Next(imp)) {

	  Chop(imp.item); //remove trailing space before split
	  addImportsSub(imports_hash,imp.item);
	}
      }
    }
  }

  /* -----------------------------------------------------------------------------
   * emitImportStatements()
   * ----------------------------------------------------------------------------- */

  void emitImportStatements(Hash *imports_hash, String *code) {
    // Add the imports statements into code
    Printf(code, "\n");

    Iterator mainIter = First(imports_hash);
    while (mainIter.key) {

      if (hasContent(mainIter.key)) {

        String *modName = NewString(mainIter.key);

        Hash *modHash = mainIter.item;

        if (modHash) {

          Hash *importHash = Getattr(modHash,"IMPORT");
          if (importHash) {

            Iterator importIter = First(importHash);
            while (importIter.key) {
              if (Strcmp(modName, importIter.item) == 0) {
                Printf(code,"IMPORT %s;\n",Char(modName));
              } else {
                Printf(code,"IMPORT %s AS %s;\n",Char(modName),Char(importIter.item));
              }
              importIter = Next(importIter);
            }

          }

          Hash *fromHash = Getattr(modHash,"FROM");
          if (fromHash) {

            Iterator fromIter = First(fromHash);
            String *theImports = NewString("");

            while (fromIter.key) {

              Printf(theImports, "%s", fromIter.key);
              Replaceall(theImports, "&", ",");  //change any & to , for multiple object import

              fromIter = Next(fromIter);
              if (fromIter.key != NIL) Printf(theImports, ",", fromIter.key);
            }
            Printf(code,"FROM %s IMPORT %s;\n",Char(modName),Char(theImports));

          }

        } else {
            //nothing
        }

      }
      mainIter = Next(mainIter);
    }
  }

};				/* class MODULA3 */

/* -----------------------------------------------------------------------------
 * swig_modula3()    - Instantiate module
 * ----------------------------------------------------------------------------- */

extern "C" Language *swig_modula3(void) {
  return new MODULA3();
}

/* -----------------------------------------------------------------------------
 * Static member variables
 * ----------------------------------------------------------------------------- */

const char *MODULA3::usage = "\
Modula 3 Options (available with -modula3)\n\
     -generateconst <file>   - generate code for computing numeric values of constants\n\
     -generaterename <file>  - generate suggestions for %rename\n\
     -generatetypemap <file> - generate templates for some basic typemaps\n\
     -generatem3make - generate an m3makefile\n\
     -dynlink - generate dynamic link types instead of raw calls\n\
     -generateignores - generate a list of methods to ignore(test wrapping)\n\
\n";

