include "Opt.td"

def lld : Tool;

class LLDOption< list<string> prefixes
               , string name
               , dag strparse
               , string render
               , dag rendermatch>
  : Option<prefixes, name, lld, strparse, render, rendermatch>;

class LLDSingleLetterOption<string name>
  : LLDOption<["-"], name, (joined "", (str:$v1)), "-"#name#"$v1", (str:$v1)> {
  int Priority = 1;
}

class LLDMultipleLetterOption<string name>
  : LLDOption<["-", "--"], name, (or (joined (str "="), (str:$v1),
                                     (separate (str:$v1)))),
              "--"#name#"=$v1", (str:$v1)>;

def lld_entry : LLDMultipleLetterOption<"entry">, MetaVars<["entry"]>;
def lld_entry_single : LLDSingleLetterOption<"e">, MetaVars<["entry"]>,
                                                   Alias<lld_entry>;
