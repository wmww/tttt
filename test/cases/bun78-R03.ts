// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function inject<U>(): U;
function g1<Q>(q: Q) { return inject() satisfies <T>(y: Q) => T[]; }
g1(1)(2);
const ok1: (y: number) => unknown = g1(1);
class Box<Q> { handlers = { on: inject() } satisfies { on<T>(y: Q): T | undefined }; }
new Box<string>().handlers.on("s");
g1(1).zzz;
// a call signature is not instantiated in tsc either
function g2<Q>(q: Q) { return inject() satisfies { <T>(y: Q): T[] }; }
g2(1).zzz;
const sentinel: number = "";
