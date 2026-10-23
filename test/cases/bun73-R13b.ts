// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
// valid programs: tsc reports nothing but the sentinel
function method({ o = { m() {} } } = { o: { m() {} } }) {}
function inArray({ a = [() => {}] } = { a: [() => {}] }) {}
function inObject({ a = { f: () => {} } } = { a: { f: () => {} } }) {}
function sibling({ a = () => {}, b = () => a } = { a: () => {}, b: () => () => {} }) {}
const { s1 = () => s2, s2 = () => s1 } = { s1: () => 1, s2: () => 2 };
const { v = { f: () => {} } } = { v: { f: () => {} } };
const [w = [() => {}]] = [[() => {}]];
const sentinel: number = "";
