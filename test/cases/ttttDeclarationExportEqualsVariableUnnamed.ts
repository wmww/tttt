// A variable a module `export =`s has no container to be named through (tsc's getContainersOfSymbol takes declarations
// whose parent is the source file, and a variable declaration's is its list): its function type prints structurally,
// not as `typeof import("./a")`; a function declaration's is named (redux-toolkit's nodenext-cjs `lazy(() =>
// import('./PostsList.js'))`)
// @strict: true
// @declaration: true
// @module: commonjs
// @esModuleInterop: true
// @filename: /a.ts
const PostsList = () => 1;
export = PostsList;
// @filename: /f.ts
function Post() { return 1; }
export = Post;
// @filename: /b.ts
declare function lazy<T>(load: () => Promise<{ default: T }>): { t: T };
export const L = lazy(() => import('./a'));
export const F = lazy(() => import('./f'));
