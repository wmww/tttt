// @strict: true
// @target: es2022
// @experimentalDecorators: false,true
// A parameter's TS1206 (decorators not valid there) ends tsc's checkGrammarModifiers: no TS1187 for a parameter
// property's binding pattern after it (bun 28-C27b), no TS1317 for a rest one
declare const dec: any;
class C { constructor(@dec public {x}: any, @dec public ...r: any[]) {} }
const D = class { constructor(@dec public {x}: any) {} };
