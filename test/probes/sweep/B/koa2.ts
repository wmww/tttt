declare namespace App {
    type DefaultStateExtends = any;
    interface DefaultState extends DefaultStateExtends {}
    type PC<StateT = DefaultState> = { x: number } & { state: StateT };
    interface Context extends PC {}
}
interface MC<StateT> extends App.Context { state: StateT; }
