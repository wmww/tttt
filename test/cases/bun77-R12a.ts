// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "allowJs": true, "checkJs": true, "jsx": "preserve", "types": [], "lib": ["es2022"] } }
// @filename: a.jsx
/** @param {number} a @param {number} extra */
function h1(a) { return <a:arguments />; }
/** @param {number} a @param {number} extra */
function h4(a) { return <div a:arguments={1} />; }
/** @param {number} a @param {number} extra */
function h7(a) { return <arguments:a />; }
/** @param {number} a @param {number} extra */
function h3(a) { return <div arguments={1} />; }
/** @type {number} */
const sentinel = "";
