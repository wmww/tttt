// An augmented `export =` is a module to tsc's printer: through `import x = require` its file ("file1.ts" in a message),
// through a namespace import's clone the augmentation's name as written (getSpecifierForModuleSymbol)
// @target: es2015
// @strict: false
// @module: commonjs

// @filename: file1.ts
class foo {}
namespace foo {
    export var v = 1;
}
export = foo;

// @filename: file2.ts
import x = require("./file1"); 
x.b = 1;

// OK - './file1' is a namespace
declare module "./file1" {
    interface A { a }
}

// @filename: file3.ts
import * as x from "./file1";
import "./file2";
let a: x.A;
let b = x.b;