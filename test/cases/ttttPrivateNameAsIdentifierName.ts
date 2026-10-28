// @jsx: preserve
// @module: esnext
// @target: es2022
// tsc's tokenIsIdentifierOrKeyword says yes to a private name (PrivateIdentifier follows Identifier among the token
// kinds): wherever an identifier name goes, `#a` is one, an Identifier node (TS2304 when looked up, never the binder's
// TS18016 or TS18012): a type reference, a JSX tag or attribute name, an import attribute's key. Where parseIdentifier
// takes it (after `asserts`), TS18016
// @filename: x.ts
export const a = 1;
// @filename: a.ts
var x: #a;
var y: #b.c;
import { a } from "./x" with { #constructor: "a" };
declare function f(x: any): asserts #a;
// @filename: b.tsx
declare namespace JSX { interface IntrinsicElements { div: any } interface Element {} }
const j1 = <div #constructor="x" />;
const j2 = <div a="1" #b />;
const j3 = <#c />;
