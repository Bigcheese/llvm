class Tool {
  // The list of all possible prefixes. Not every option in the tool has all
  // prefixes. Any string that does not begin with one of these prefixes and is
  // not an argument to a previous option is considered an input. A string that
  // does begin with a prefix but is not a known option is eligeble for
  // typo-correction.
}

def value;
def flag;
def joined;
def separate;
def or;
def str;

class Option<list<string> prefixes, string name, Tool tool, dag strparse, string render, dag rendermatch> {
  // The tool this option belongs to.
  Tool Tool_ = tool;
  // How to parse the option from a string.
  dag StringParse = strparse;
  // How to render the option to a string.
  string Render = render;
  dag RenderMatch = rendermatch;
  list<string> ValueMetavars;

  list<string> Prefixes = prefixes;
  string Name = name;
  bit IsCaseInsensitive = 0;
  int Priority = 0;
  Option Alias = ?;
}

class Alias<Option alias> {
  Option Alias = alias;
}

class MetaVars<list<string> mv> {
  list<string> ValueMetavars = mv;
}

class CaseInsensitive {
  bit IsCaseInsensitive = 1;
}

class Transform<dag match, dag produce> {
  dag M = match;
  dag P = produce;
}

//def T : Tool;

//def blah : Option<T,
//                  (flag (str "-", "--"), // Prefixes
//                        (str "blah", "bh"), // Flag names
//                       (or (joined (str "="), value:$v1), // Value parse
//                            (separate value:$v1))),
//                  "--blah $v1">; // String render.

//def foo : Option<T,
//                 (flag (str "/"),
//                       (str "foo"),
//                       (joined (str ":"), value:$v1)),
//                 "/foo:$v1">;

//def : Transform<(blah value:$v1), (foo value:$v1)>; // Marks blah as used if it matches.

// clang-driver.td

// def clang_driver : Tool;

//class ClangDriverOptSingleLetter<string name> : Option<name, clang_driver, (value)> {
//  list<string> Prefixes = ["-"];
//}

// cd = clang driver

//def cd_l : ClangDriverOptSingleLetter<"l">;

// ld.td

//def ld : Tool;

//class LDOptSingleLetter<string name> : Option<name, ld, (value)> {
//  list<string> Prefixes = ["-"];
//}

//def ld_l : LDOptSingleLetter<"l">;

//def : Transform<(ld_l cd_l:$a1)>;
