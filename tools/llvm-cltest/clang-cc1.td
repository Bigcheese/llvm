include "Option.td"

def clang_cc1 : Tool;

class ClangCC1Option< list<string> prefixes
                    , string name
                    , dag strparse
                    , string render
                    , dag rendermatch>
  : Option<prefixes, name, clang_cc1, strparse, render, rendermatch>;

class ClangCC1Flag<string name>
  : ClangCC1Option<["-"], name, ?, "-"#name, ?>;
class ClangCC1Separate<string name>
  : ClangCC1Option<["-"], name, (separate (str:$v0)), "-"#name#" $v0", (str:$v0)>;
class ClangCC1SeparateValues<string name, list<string> values>
  : ClangCC1Option< ["-"], name
                  , (joined (str "="), (str:$v0 values))
                  , "-"#name#"=$v0", (str:$v0)>;

def clang_cc1_f_strict_enums : ClangCC1Flag<"fstrict-enums">;
def clang_cc1_f_template_depth : ClangCC1Separate<"ftemplate-depth">;
def clang_cc1_f_fp_contract : ClangCC1SeparateValues<"ffp-contract", ["fast", "on", "off"]>;
