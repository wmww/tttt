// @strict: true
// An object literal target nested in a destructuring assignment (an array literal's element, an object literal's
// property value) is a pattern type like the outermost one (tsc's checkObjectLiteral sets `pattern` on every target),
// so the literal assigned to it gets the excess property check TS2353. Seed 20261114, computedPropertiesInDestructuring1_ES6
let foo = undefined;
let bar4: number;
let b: number;
let rest: {} = {};
[{[foo]: bar4}] = [{bar: "bar"}];
[{bar: b}] = [{bar: 1, baz: 2}];
[[{bar: b}]] = [[{bar: 1, baz: 2}]];
({ x: [{bar: b}] } = { x: [{bar: 1, qux: 3}] });
({ y: {bar: b} } = { y: {bar: 1, qux: 3} });
// A spread leaves it open; a default's literal is checked against the target, the assigned one isn't
[{bar: b, ...rest}] = [{bar: 1, zz: 2}];
[{bar: b} = {bar: 1, w: 2}] = [{bar: 1, w2: 2}];
