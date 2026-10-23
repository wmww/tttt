// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "target": "es2015", "lib": ["es5", "es2015.symbol", "es2015.symbol.wellknown", "es2015.iterable"] } }
// @filename: a.ts
function* g(): IterableIterator<(x: number) => void> { yield* [x => {}]; }
const k: () => IterableIterator<(x: number) => void> = function* () { yield x => {}; };
const sentinel: number = "";
