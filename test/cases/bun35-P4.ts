// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import "#1";
const sentinel: number = "";
// @filename: package.json
{ "imports": { "#1": "#2", "#2": "#3", "#3": "#4", "#4": "#5", "#5": "#6", "#6": "#7", "#7": "#8", "#8": "#9", "#9": "#10", "#10": "./x.d.ts" } }
// @filename: x.d.ts
export {};
