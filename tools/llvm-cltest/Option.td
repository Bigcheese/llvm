class Tool {
  // The list of all possible prefixes. Not every option in the tool has all
  // prefixes. Any string that does not begin with one of these prefixes and is
  // not an argument to a previous option is considered an input Argument. A
  // string that does begin with a prefix but is not a known option is eligible
  // for typo-correction.
}

def joined;
def separate;
def or;
def str;

class Option<list<string> prefixes, string name, Tool tool, dag strparse, string render, dag rendermatch> {
  // The tool this Option belongs to.
  Tool Tool_ = tool;

  // How to parse the Option from argc+argv.
  dag StringParse = strparse;

  // How to render the Option to a string. RenderMatch is used to capture
  // values and assign them identifiers. When Render is printed, these values
  // are inserted into it in the marked locations.
  string Render = render;
  dag RenderMatch = rendermatch;

  // The meta-variable name of each value.
  list<string> ValueMetavars;

  // The list of valid prefixes for this Option. The parser will check if
  // Prefixes[i] + Name is a prefix of a potential Option for each prefix in
  // Prefixes.
  list<string> Prefixes = prefixes;

  // The name of this Option without any prefixes or postfixes. This is what
  // typo correction is checked against.
  string Name = name;

  // Is Name case insensitive.
  bit IsCaseInsensitive = 0;

  // Should this Option be hidden from the default help.
  bit IsHidden = 0;

  // Used as a tiebraker when multiple Options share the same prefix. Higher
  // values are picked first.
  int Priority = 0;

  // The single Option that this Option is an alias of.
  Option Alias = ?;

    // The help text for this Option.
  string HelpText = ?;
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

def not;

class Transform<list<dag> match, list<dag> produce> {
  list<dag> M = match;
  list<dag> P = produce;
}

class Forward<Option from, Option to> {
  Option From = from;
  Option To = to;
}
