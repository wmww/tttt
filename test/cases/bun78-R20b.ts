// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "strictNullChecks": false, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function o1(a: {}): void; function o1({ x = null }) {}
function o2(a: {}): void; function o2({ x = undefined, y = [], z = { w: null } }) {}
class O3 { m(a: {}): void; m({ x = null }) {} constructor(a: {}); constructor({ x = null }) {} }
export function o4(): void; export function o4({ x = null } = {}) {}
// both report: no overloads
function p1({ x = null }) {}
const sentinel: number = "";
