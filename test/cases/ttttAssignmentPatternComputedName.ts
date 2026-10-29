// @strict: true
// A destructuring assignment's computed name leaves its object literal pattern open (no excess property check on the
// assigned literal) only when its type is a property key type but no literal (tsc's patternWithComputedProperties):
// `[undefined]` (TS2464) and literal or unique symbol names keep it. Seed 20261114, computedPropertiesInDestructuring1_ES6
let foo = undefined;
let bar: any, x: any;
declare const k: string;
declare const s: unique symbol;
({[foo]: bar} = {bar: "bar"});
({["a"]: x} = {a: 1, b: 2});
({[k]: bar} = {bar: "bar", c: 1});
({[s]: bar} = {[s]: 1, d: 2});
({[1]: bar} = {1: 1, e: 2});
