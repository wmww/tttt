// An ambient module's augmented `export =` is named by the augmentation in a message (getSpecifierForModuleSymbol:
// its export= container is no file)
// @target: es2015
// @strict: false
// @module: commonjs
// @filename: file1.d.ts
declare module "file1" {
    function foo(): void;
    namespace foo {
        export var v: number;
    }
    export = foo;
}


// @filename: file2.ts
/// <reference path="file1.d.ts"/>
import x = require("file1"); 
x.b = 1;

// OK - './file1' is a namespace
declare module "file1" {
    interface A { a }
}

// @filename: file3.ts
import * as x from "file1";
import "file2";
let a: x.A;
let b = x.b;