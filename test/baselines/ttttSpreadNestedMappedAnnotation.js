//// [test/cases/ttttSpreadNestedMappedAnnotation.ts] ////

//// [ttttSpreadNestedMappedAnnotation.ts]
// A spread property of a mapped type of a mapped type (`Pick<Readonly<A & B>, K>`) reuses the annotation it comes from,
// through every mapped type that says the same (excalidraw selection.d.ts: `editingGroupId: AppState["editingGroupId"]`)
interface AppState { editingGroupId: string | null; other: number }
type Common = { x: AppState["other"] };
type Canvas = Readonly<Common & { editingGroupId: AppState["editingGroupId"] }>;
type Canvas2 = Common & { editingGroupId: AppState["editingGroupId"] };
declare const p1: Pick<Canvas, "editingGroupId">;
declare const p2: Pick<Canvas2, "editingGroupId">;
declare const p3: Readonly<Canvas2>;
export const s1 = { ...p1 };
export const s2 = { ...p2 };
export const s3 = { ...p3 };


//// [ttttSpreadNestedMappedAnnotation.js]
export const s1 = { ...p1 };
export const s2 = { ...p2 };
export const s3 = { ...p3 };


//// [ttttSpreadNestedMappedAnnotation.d.ts]
interface AppState {
    editingGroupId: string | null;
    other: number;
}
export declare const s1: {
    editingGroupId: AppState["editingGroupId"];
};
export declare const s2: {
    editingGroupId: AppState["editingGroupId"];
};
export declare const s3: {
    x: AppState["other"];
    editingGroupId: AppState["editingGroupId"];
};
export {};
