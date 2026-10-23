// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
declare export default (() => { for (;;) { break; } })();
declare const c = () => { { 1; } };
declare enum E { A = (() => { if (1) { 1; } return 1 })() }
const sentinel: number = "";
