// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const ks: string;
class D3 { [ks] = 1; }
interface D3 { (): void }
declare const d3: D3;
const t: { __t: 1 } = d3[ks];
const sentinel: number = "";
