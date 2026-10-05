export {};
declare namespace App {
    type DefaultStateExtends = any;
    interface DefaultState extends DefaultStateExtends {}
}
interface MC<StateT> { state: StateT; }
interface Ctx { state: App.DefaultState; }
declare let a: MC<unknown>; declare let b: Ctx;
function f<T>(x: MC<T>) { b = x; }
