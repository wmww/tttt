// @strict: true
declare class Msg { m: number }
declare class Res<R extends Msg = Msg> { r: R }
type Listener<Q extends typeof Msg, S extends typeof Res<InstanceType<Q>>> = (req: InstanceType<Q>, res: InstanceType<S>) => void;
declare function f<Q extends typeof Msg>(q: Q): Res<InstanceType<Q>>;
