// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function m(a?: { b?: { c?: string } }) { if (a?.b!.c) { const s: string = a.b!.c; } }
function n(a?: { g?(): string }) { if (a?.g!()) a.g; }
const sentinel: number = "";
