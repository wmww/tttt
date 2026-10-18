//// [test/cases/ttttDeclarationScriptImportAlias.ts] ////

//// [ttttDeclarationScriptImportAlias.ts]
// A script's import aliases are global declarations, written like its others (tsc's isDeclarationVisible: an import
// alias whose parent is a global source file); one in a namespace isn't, nor one with no module reference (recovery)
declare namespace b { const c: number; }
namespace N { import a = b; export const z = 1; }
declare namespace M { import a = b; }
/** doc */
import d = b.c; // trailing
import e = b;
import from 'foo'


//// [ttttDeclarationScriptImportAlias.js]
"use strict";
var N;
(function (N) {
    N.z = 1;
})(N || (N = {}));
/** doc */
var d = b.c; // trailing
var e = b;
'foo';


//// [ttttDeclarationScriptImportAlias.d.ts]
declare namespace b {
    const c: number;
}
declare namespace N {
    const z = 1;
}
declare namespace M { }
/** doc */
import d = b.c;
import e = b;
import from = ;
//# sourceMappingURL=ttttDeclarationScriptImportAlias.d.ts.map