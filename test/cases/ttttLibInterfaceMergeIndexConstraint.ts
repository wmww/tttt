// An interface merged into a lib one is checked once, by the first declaration that is checked: with the lib
// unchecked (the harness's skipDefaultLibCheck) that is this file's, and what it finds at this file's declarations is
// reported (TS2411 at `data`), what it finds in the lib isn't
// @target: es2015
class A { foo!: string; }
interface Object {
    data: A;
    [x: string]: Object;
}
