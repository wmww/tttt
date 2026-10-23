// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends object>(options: (T & { a?: 1 }) | string) {
  const { url, other } = options;
  url;
}
const sentinel: number = "";
