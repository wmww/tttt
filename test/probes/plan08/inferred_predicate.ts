// @strict: true
interface R { fileName: string }
declare const files: readonly (R | undefined)[];
const nonNil = files.filter(f => f !== undefined);
nonNil[0].fileName;
function isString(x: unknown) { return typeof x === "string"; }
declare const u: unknown;
if (isString(u)) u.length;
const notInferred = (x: number | string) => typeof x === "number" && x > 0;
