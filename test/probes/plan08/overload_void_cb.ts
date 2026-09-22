// @strict: true
type Path = string & { __pathBrand: any };
declare function each<T>(directory: Path, callback: (directory: Path) => T | undefined): T | undefined;
declare function each<T>(directory: string, callback: (directory: string) => T | undefined): T | undefined;
declare function norm(p: string): string;
let roots: string[] | undefined;
each(norm("x"), directory => {
    (roots ??= []).push(directory);
});
function g<T, P extends string>(d: P, cb: (d: P) => T | undefined) {
    return each(d, a => {
        const r = cb(a as P);
        if (r !== undefined) return r;
    });
}
