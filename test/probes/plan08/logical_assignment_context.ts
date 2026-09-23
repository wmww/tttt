// @strict: true
interface Box<T> { v: T[] }
declare function make<T>(): Box<T>;
interface Mod { m: number }
declare let x: Box<Mod> | undefined;
x ||= make();
declare let y: { mods?: Box<Mod> };
y.mods ||= make();
(y.mods ||= make()).v;
let z: Box<string> | undefined;
(z ||= make()).v.push("a");
