// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let y: any;
declare let s: string;
(s = 1) = y;
[(s = 1)] = y;
({ a: (s = 1) } = y);
for ((s = 1) of y) {}
({ a: ({ b: s } = { b: 1 }) } = y);
({ a: ({ k() {} } = y) } = y);
[s = 1] = y;
const sentinel: number = "";
