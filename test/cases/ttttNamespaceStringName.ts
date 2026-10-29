// @target: es2015
// `namespace "100" {}` (a string after `namespace`, not `module`): the name is missing (TS1003) and nothing is taken,
// so the string is an expression statement and the block after it wants a ';' (tsc's parseModuleBlock without its
// '{'). The namespace of the missing name is still declared: the symbols baseline's `(Missing)`. Seed 20261114,
// parseInvalidNames num-to-str
namespace "100" {}
export namespace 'x' { var a; }
namespace "y"
