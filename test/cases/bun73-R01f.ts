// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type U = string | number | boolean | null | undefined;
function missed(o: { x: U }) { if (o["x"]) { if (o["x"] === 0) { o["x"].toFixed(); } } }
function element(o: { x: U }) { if (o["x"]) { if (o["x"] === 0) { const p: never = o["x"]; } } }
function property(o: { x: U }) { if (o.x) { if (o.x === 0) { const p: never = o.x; } } }
function mixed(o: { x: U }) { if (o["x"]) { if (o.x === 0) { const p: never = o["x"]; const q: never = o.x; } } }
function text(o: { x: U }) { if (o["x"]) { if (o["x"] === "") { const p: never = o["x"]; } } }
function falsy(o: { x: U }) { if (!o["x"]) { if (o["x"] === 1) { const p: never = o["x"]; } } }
function plainNumber(o: { x: number }) { if (o["x"]) { if (o["x"] === 0) { const p: never = o["x"]; } } }
function arrayIndex(o: U[]) { if (o[0]) { if (o[0] === 0) { const p: never = o[0]; } } }
const sentinel: number = "";
