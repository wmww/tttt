// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function mk<X>() { return class<T extends X> { y!: T } }
declare function take<R>(c: new () => R): R;
take(mk<string>()).y.length;
const sentinel: number = "";
