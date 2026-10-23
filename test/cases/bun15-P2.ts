// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface MyAsyncIter { next(v: string): Promise<IteratorResult<Promise<number>, void>>; [Symbol.asyncIterator](): MyAsyncIter }
const f: () => MyAsyncIter | AsyncGenerator<number, void, boolean> = async function* () { const x = yield 1; const b: boolean = x; };
const sentinel: number = "";
