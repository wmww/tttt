// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Base = Readonly<{ id: string; x: number }>;
type Rect = Base & { type: "rectangle" };
type Text = Base & Readonly<{ type: "text"; text: string }>;
type Linear = Base & Readonly<{ type: "line" | "arrow"; points: number[] }>;
type Arrow = Linear & Readonly<{ type: "arrow"; elbowed: boolean }>;
type El = Rect | Text | Linear | Arrow;
declare const isArrow: <T extends El>(e?: T | null) => e is T & Arrow;
function f(e: El) {
  if (isArrow(e)) {
    e.elbowedX;
  }
}
const sentinel: number = "";
