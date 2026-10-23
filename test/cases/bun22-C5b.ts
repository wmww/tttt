// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function mk<X>() { return class<T extends X> { y!: T } }
new (mk<string>())<"a">();
const sentinel: number = "";
