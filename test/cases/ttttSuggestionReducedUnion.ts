// @strict: true
// A spelling suggestion goes by the reduced union: a member that is an intersection of conflicting discriminants
// (`Rect & Arrow`, never) offers no properties and doesn't hide the others' (tsc's getPropertiesOfType through
// getReducedApparentType; bun's 44-C6b, excalidraw's element types narrowed by a generic predicate)
type Base = Readonly<{ id: string; x: number }>;
type Rect = Base & { type: "rectangle" };
type Linear = Base & Readonly<{ type: "line" | "arrow"; points: number[] }>;
type Arrow = Linear & Readonly<{ type: "arrow"; elbowed: boolean }>;
declare const v1: (Rect & Arrow) | Arrow;
v1.elbowedX;
declare const v4: (Rect & Arrow) | (Linear & Arrow);
v4.elbowedX;
type El = Rect | Linear | Arrow;
declare const isArrow: <T extends El>(e?: T | null) => e is T & Arrow;
function f(e: El) { if (isArrow(e)) { e.elbowedX; } }
