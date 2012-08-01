include "Option.td"

def clang : Tool;

class ClangOption< list<string> prefixes
                  , string name
                  , dag strparse
                  , string render
                  , dag rendermatch>
  : Option<prefixes, name, clang, strparse, render, rendermatch>;

class ClangFlag<string name>
  : ClangOption<["-"], name, ?, "-"#name, ?>;

class ClangSingleLetterOption<string name>
  : ClangOption< ["-"], name, (or (joined (str ""), (str:$v0)),
                                  (separate (str:$v0)))
                , "-"#name#"$v0", (str:$v0)> {
  int Priority = 1;
}

def clang_f_strict_enums : ClangFlag<"fstrict-enums">;
def clang_f_no_strict_enums : ClangFlag<"fno-strict-enums">;
def clang_f_fast_math : ClangFlag<"ffast-math">;
def clang_o : ClangSingleLetterOption<"o">, MetaVars<["<file>"]>;

// And now for a simi-strange one. -ftemplate-depth.
def clang_f_template_depth
  : ClangOption< ["-"], "ftemplate-depth"
                , (or (joined (str "="), (str:$v0)),
                      (joined (str "-"), (str:$v0)))
                , "-ftemplate-depth=$v0", (str:$v0)>;
// Note that we don't need to also have a clang_f_template_depth_EQ.

// One with a limited set of values.
class ClangSeparateValues<string name, list<string> values>
  : ClangOption< ["-"], name
                , (joined (str "="), (str:$v0 values))
                , "-"#name#"=$v0", (str:$v0)>;

// This won't match unless the value is one of the ones in the list. We can
// generate a very good error message with the information we have that
// includes the list of valid values.
def clang_f_fp_contract : ClangSeparateValues<"ffp-contract", ["fast", "on", "off"]>;
