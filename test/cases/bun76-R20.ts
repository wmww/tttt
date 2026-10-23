// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare const key: string;
h1({});
function h1({ [key]: a = 1, b = a }) {}
function h2({ [key]: a = 1, b = a }) {}
h2({});
function h3({ [key]: a = 1, b = a }) {}
h4({});
function h4({ b = a, [key]: a = 1 }) {}
h5({});
function h5({ [key]: a = 1, b = () => a }) {}
h6({});
function h6({ x: { [key]: a = 1 }, b = a }) {}
h7([]);
function h7([{ [key]: a = 1 }, b = a]) {}
const h8 = function ({ [key]: a = 1, b = a }) {};
const h9 = ({ [key]: a = 1, b = a }) => {};
const sentinel: number = "";
