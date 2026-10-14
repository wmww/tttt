// @target: es2015
// An import that resolves to nothing merged with an exported value of the module: tsc's local symbol is Alias|ExportValue,
// so a value reference and an expando host are the export, and the unknown symbol it resolves to only has a value
// meaning (the class name prints bare as a type, `typeof import(...).Baz` as a value)
import Foo from "blah";
import { Bar } from "blah2";
import * as Baz from "blah3";
export function Foo() { }
Foo.bar = () => { };
export const Bar = 1;
const b2 = Bar;
export class Baz { static s = 1 }
const c = Baz.s;
