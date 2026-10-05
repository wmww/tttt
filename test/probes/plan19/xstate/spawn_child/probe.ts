// xstate types.test.ts 870 (tsc: no errors; tttt -p this directory). The rest argument `{ id: 'ok1' }` of a generic
// `...args: SpawnArguments<..., TActor>` is checked against its contextual type instantiated with the return mapper
// (TActor from the createMachine context), else 'ok1' was widened to string against the conditional's constraint
import { createMachine, spawnChild, MachineContext, AnyEventObject, ProvidedActor, ActionFunction, ParameterizedObject, EventObject } from '../../../../../thirdparty/corpora/xstate/packages/core/src/index.ts';
const child = createMachine({});
type Actors = { src: 'child'; id: 'ok1' | 'ok2'; logic: typeof child; };
declare function cm3<TActor extends ProvidedActor, _ = any>(
  config: { types?: { actors?: TActor } } & { entry?: ActionFunction<MachineContext, AnyEventObject, AnyEventObject, undefined, TActor, ParameterizedObject, ParameterizedObject, string, EventObject> }): void;
cm3({
  types: {} as { actors: Actors },
  entry: spawnChild('child', { id: 'ok1' })
});
