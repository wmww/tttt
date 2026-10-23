// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "jsx": "preserve", "types": [], "lib": ["es2022"] } }
// @filename: a.jsx
/** @param {number} a @param {number} extra */
function s1(a) { switch (a) { case <div arguments />: let arguments = 1; } }
/** @param {number} a @param {number} extra */
function s5(a) { switch (a) { case 1: let arguments = 1; break; case <div arguments />: break; } }
/** @param {number} a @param {number} extra */
function s8(a) { { const x = <div arguments />; let arguments = 1; } }
/** @type {number} */
const sentinel = "";
