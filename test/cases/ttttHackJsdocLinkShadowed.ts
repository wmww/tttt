// @strict: true
// @noUnusedLocals: true
// `{@link Y}` resolves from where the comment is: a local `Y` there leaves the import unused (TS6133); hacks.md
// `unused_mark_jsdoc_links()`: tttt looked the name up from the file's scope
// @filename: x.ts
export const X = 1;
export const Y = 2;
// @filename: a.ts
import { X, Y } from "./x";
/** {@link X} */
function f() {}
function g() {
    const Y = 1;
    /** {@link Y} */
    function h() {}
    h();
    return Y;
}
f; g;
