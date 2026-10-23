// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function h(v: string | number) { if (p(v)) { return v; } return 0; }
function p(x: string | number, ...y: ReturnType<typeof h>[]) { return !!x; }
const sentinel: number = "";
