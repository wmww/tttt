// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type X={x:1};type Y={y:1};type B={b:1};type C={c:1};type D={d:1};
type BCD = B & C & D;
type T1 = BCD & (X|Y);
declare let v3: B & C & D & (X|Y);
const e3: number = v3;
const sentinel: number = "";
