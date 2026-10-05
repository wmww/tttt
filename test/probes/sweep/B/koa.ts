type DefaultStateExtends = any;
interface DefaultState extends DefaultStateExtends {}
interface Context { state: DefaultState; }
interface MC<StateT> extends Context { state: StateT; }
function f<T>(t: T) { const d: DefaultState = t; }
