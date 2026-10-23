// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function n(x: never) {}
namespace N { const x = 1; }
const nn = N;
if (typeof nn !== "object") n(nn);
if (!nn) n(nn);
const sentinel: number = "";
