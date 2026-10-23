// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
interface Tree { children: Tree[] }
let root: Tree | undefined;
function getRoot() { return root ??= build() as Tree; }
function build() { return { children: [getRoot()] }; }
let x: number;
function g1() { return x = g1() as number; }
function g6() { return [x = g6() as number]; }
function g10() { x = g10() as number; return x; }
function f4() { const r = f4() as number; return r; }
const sentinel: number = "";
