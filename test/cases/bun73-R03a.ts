// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function c(): boolean;
function call() { let x = []; for (; true; x.push("")) { if (Array.isArray(x)) { if (x) break; } } }
function callOfName() { let x = []; for (; true; x()) { if (Array.isArray(x)) { if (x) break; } } }
function construct() { let x = []; for (; true; new x()) { if (Array.isArray(x)) { if (x) break; } } }
function chain() { let x = []; for (; true; x.push("").toFixed()) { if (Array.isArray(x)) { if (x) break; } } }
function callback() { let x = []; for (; true; x.map(e => e)) { if (Array.isArray(x)) { if (x) break; } } }
function optional() { let x = []; for (; true; x?.push("")) { if (Array.isArray(x)) { if (x) break; } } }
function element() { let x = []; for (; true; x["push"]("")) { if (Array.isArray(x)) { if (x) break; } } }
const sentinel: number = "";
