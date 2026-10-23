// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "esnext", "module": "esnext" } }
// @filename: a.ts
export {};
interface BadP { then(cb: (v: BadP) => void): void }
interface NoCb { then(): void }
declare const bp: BadP, nc: NoCb, ai: AsyncIterable<NoCb>;
async function* g1() { yield bp; }
async function* g2() { yield nc; }
async function* g3() { yield* ai; }
const sentinel: number = "";
