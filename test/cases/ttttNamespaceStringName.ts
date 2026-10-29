// @target: es2015
// @noTypesAndSymbols: true
// `namespace "100" {}` (a string after `namespace`, not `module`): the name is missing (TS1003) and nothing is taken,
// so the string is an expression statement and the block after it wants a ';' (tsc's parseModuleBlock without its
// '{'). Seed 20261114, parseInvalidNames num-to-str
namespace "100" {}
