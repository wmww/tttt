type A = `a${"b" | "c"}d`;
type B = `x${number}`;
type C = `${string}`;
type D = `${1}-${true}-${null}-${undefined}-${10n}`;
type E<T extends string> = `pre${T}post`;
type F = E<"mid">;
type G = Uppercase<"hello">;
type H = Lowercase<"HeLLo" | "WORLD">;
type I = Capitalize<"foo">;
type J = Uncapitalize<"FooBar">;
type K<T extends string> = Uppercase<T>;
type L = `${Uppercase<"a">}${Capitalize<string>}`;
type M = `a\`b\${c}`;
type N = `${never}x`;
type O = `${any}`;
let a: A = "abd";
let b: B = "x12";
let b2: B = "xy";
let k: K<"q"> = "Q";
function f<T extends string>(x: E<T>, y: K<T>) {
    let s: string = x;
    let t: string = y;
    return [x, y];
}
