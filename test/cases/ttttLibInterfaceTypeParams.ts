// TS2428 goes to every declaration of the merged interface, the lib files' too when they are checked
// @target: es2015
// @skipDefaultLibCheck: false
interface Map { extra: number }
declare const m: Map;
