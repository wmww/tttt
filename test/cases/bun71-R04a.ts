// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function use(...x: any[]): void;
function F(this: any) { if (F) { use(new.target); } }
function G(this: any) { return G && new.target; }
const h = function H(this: any) { if (H) { use(new.target); } };
const sentinel: number = "";
