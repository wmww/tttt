// @strict: true
// @noEmit: true
// A generic function argument whose parameter type has no single signature isn't resolved: its parameters aren't
// typed (tsc's instantiateTypeWithSingleGenericCallSignature reads the declared type parameters, then the contextual
// signature). Typing `value` here resolved Snapshot's base, the model's own type, in flight: TS7022/TS7024/TS2502
// that tsc doesn't report (kebo's mobx-state-tree model, SnapshotIn unresolved there)
// @filename: model.d.ts
export interface ModelActions {
    [key: string]: FunctionWithFlag;
}
export type Instance<T> = T extends { Type: infer I } ? I : T;
export interface IModelType<PROPS, OTHERS> {
    Type: PROPS & OTHERS;
    actions<A extends ModelActions>(fn: (self: Instance<this>) => A): IModelType<PROPS, OTHERS & A>;
}
export declare function model(name: string): IModelType<{}, {}>;
export declare const types: { model: typeof model };
// @filename: transaction.ts
import { types } from "./model";
export const TransactionModel = types
  .model("Transaction")
  .actions((self) => {
    function updateField<K extends keyof TransactionSnapshotIn>(value: TransactionSnapshotIn[K]) {}
    return { updateField };
  });
export interface TransactionSnapshotIn extends SnapshotIn<typeof TransactionModel> {}
