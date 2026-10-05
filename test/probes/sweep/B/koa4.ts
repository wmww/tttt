declare class App {}
declare namespace App {
    type DefaultStateExtends = any;
    interface DefaultState extends DefaultStateExtends {}
}
interface E {}
function f<T>(t: T) { const d: App.DefaultState = t; const e: E = t; }
