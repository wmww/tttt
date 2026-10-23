// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type X={x:1};type Y={y:1};type B={b:1};type C={c:1};type D={d:1};
type BCD = B & C & D;
declare let v1: BCD & (X|Y);
declare let v2: B & C & D & (X|Y);
const e2: number = v2;
const sentinel: number = "";
