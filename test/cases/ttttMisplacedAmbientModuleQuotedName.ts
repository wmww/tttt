// @strict: true
// A misplaced `module "y" {}` is TS1234 alone: checkGrammarModuleElementContext ends tsc's checkModuleDeclaration before
// its TS1035 (bun 51-L02); in a namespace it isn't misplaced
namespace N { module "x" {} }
function f() { module "y" {} }
export { N, f };
