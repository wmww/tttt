// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function t(p: () => void) { if (p) { const g = (x: any): p is any => true; } }
function u(p: () => void) { if (p) { const g = (x: any): asserts p => {}; } }
const sentinel: number = "";
