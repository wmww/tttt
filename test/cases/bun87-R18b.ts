// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "skipLibCheck": true } }
// @filename: a.ts
var use = f2();
const fn = () => { const v: 1 = null! as Has<"$m", D8>; };
function f2() { return null! as D8; }
const sentinel: number = "";
// @filename: d.d.ts
type Has<K, T> = K extends keyof T ? true : false;
type renamed<T> = { [K in keyof T as `$${K & string}`]: T[K] };
interface D8 extends renamed<D8> { m(): void; n: number }
