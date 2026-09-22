// @strict: true
interface Base { readonly name: string; readonly scoped: boolean; readonly text: string | ((cb: (n: string) => string) => string); }
interface Scoped extends Base { readonly scoped: true; }
interface Unscoped extends Base { readonly scoped: false; readonly text: string; }
type Helper = Scoped | Unscoped;
declare function helperString(input: TemplateStringsArray, ...args: string[]): (cb: (n: string) => string) => string;
export const h: Helper = {
    name: "x",
    scoped: true,
    text: helperString`a${"b"}c`,
};
export const h2: Helper = {
    name: "x",
    scoped: true,
    text: (cb) => "",
};
