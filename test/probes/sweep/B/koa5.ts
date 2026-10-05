// @strict: true
export {};
type DefaultStateExtends = any;
interface DefaultState extends DefaultStateExtends {}
function f<T>(t: T) { const d: DefaultState = t; }
