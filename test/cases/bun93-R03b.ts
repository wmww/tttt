// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type X={x:1};type Y={y:1};type B={b:1};type C={c:1};type D={d:1};
type BCD = B & C & D;
type T2 = B & C & D & (X|Y);
declare let v4: BCD & (X|Y);
const e4: number = v4;
const sentinel: number = "";
