// @declaration: true
// @declarationMap: true
// A script's import aliases are global declarations, written like its others (tsc's isDeclarationVisible: an import
// alias whose parent is a global source file); one in a namespace isn't, nor one with no module reference (recovery)
declare namespace b { const c: number; }
namespace N { import a = b; export const z = 1; }
declare namespace M { import a = b; }
/** doc */
import d = b.c; // trailing
import e = b;
import from 'foo'
