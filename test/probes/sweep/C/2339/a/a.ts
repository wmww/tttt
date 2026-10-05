class C { x = 1; }
function f(): void {}
namespace f {
    export { C };
    export { C as D };
}
new f.C();
new f.D();
declare namespace N { export { C }; }
new N.C();
