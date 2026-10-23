// @filename: tsconfig.json
{ "compilerOptions": { "target": "es2015", "libReplacement": true, "noEmit": true, "strict": true, "types": [], "skipLibCheck": true }, "files": ["index.ts"] }
// @filename: index.ts
/// <reference lib="dom" />
const a: ABC = { abc: 1 };
const b: string = libValue;
const c: number = libBad;
const d: string = libFn(1, 2);
const e: string = new LibClass().m();
const f: string = new LibClass().p;
window.localStorage;
const sentinel: number = "";
// @filename: node_modules/@typescript/lib-dom/index.ts
interface ABC { abc: string }
var libValue = 1;
var libBad: string = 2;
function libFn(a, b) { return a + b + missingName; }
class LibClass { p = 1; m() { return this.p.nope; } }
async function libAsync() {}
function libRec1() { return [libRec2()]; }
function libRec2() { return [libRec1()]; }
