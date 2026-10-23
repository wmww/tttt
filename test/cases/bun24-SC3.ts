// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const w2 = <T,>(t: T) => ({ ...{}, w: t });
const d2: string = w2(w2(w2(w2(w2(w2(w2(w2(w2(w2(w2(w2(w2(w2(1))))))))))))));
const sentinel: number = "";
