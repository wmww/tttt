// @declaration: true
// A file's overloaded function is `typeof f` also as a class expression's member instantiates it (by its `this`)
async function rimraf(path: string, mode: 1): Promise<void>;
async function rimraf(path: string, mode?: 2): Promise<void>;
async function rimraf(path: string, mode?: number): Promise<void> {}
function single(path: string): void {}
export const Promises = new class {
  get one() { return single; }
  get rm() { return rimraf; }
};
export const o = { get rm() { return rimraf; }, m() { return rimraf; }, p: rimraf };
