// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: a.tsx
const g = <T,>x => x;
const h = <T extends object> x, y;
const i = <T,>{ a: 1 };
const j = <T, 2>(x: T) => x;
