// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"allowJs": true, "checkJs": true, "strict": true, "noEmit": true, "module": "ESNext", "target": "ES2022", "moduleResolution": "bundler", "types": [], "lib": ["es2022"]}, "files": ["c1_computed_string.js", "c1_name_existing.js", "c1_existing_string.js", "c8_computed_none.js", "c8_computed_string.js", "c8_computed_define.js", "c8_computed-declared_none.js", "c8_computed-declared_string.js", "c8_computed-declared_define.js", "c8_name_none.js", "c8_name_string.js", "c8_name_define.js", "c8_string_none.js", "c8_string_string.js", "c8_string_define.js", "c8_symbol_none.js", "c8_symbol_string.js", "c8_symbol_define.js", "c8_existing_none.js", "c8_existing_string.js", "c8_existing_define.js", "c8_read_none.js", "c8_read_string.js", "c8_read_define.js", "c9_computed_none.js", "c9_computed_dot.js", "c9_computed_string.js", "c9_computed_computed.js", "c9_computed_define.js", "c9_computed_existing.js", "c9_computed-declared_none.js", "c9_computed-declared_dot.js", "c9_computed-declared_string.js", "c9_computed-declared_computed.js", "c9_computed-declared_define.js", "c9_computed-declared_existing.js", "c9_name_none.js", "c9_name_dot.js", "c9_name_string.js", "c9_name_computed.js", "c9_name_define.js", "c9_name_existing.js", "c9_string_none.js", "c9_string_dot.js", "c9_string_string.js", "c9_string_computed.js", "c9_string_define.js", "c9_string_existing.js", "c9_symbol_none.js", "c9_symbol_dot.js", "c9_symbol_string.js", "c9_symbol_computed.js", "c9_symbol_define.js", "c9_symbol_existing.js", "c9_existing_none.js", "c9_existing_dot.js", "c9_existing_string.js", "c9_existing_computed.js", "c9_existing_define.js", "c9_existing_existing.js", "c9_read_none.js", "c9_read_dot.js", "c9_read_string.js", "c9_read_computed.js", "c9_read_define.js", "c9_read_existing.js", "c17_computed_define.js", "c17_computed-declared_computed.js", "c17_name_string.js", "c17_string_dot.js", "c17_symbol_none.js", "c17_symbol_existing.js", "c17_existing_define.js", "c17_read_computed.js", "c40_computed-declared_string.js", "c40_string_existing.js", "c40_read_string.js"]}
// @filename: c17_computed-declared_computed.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() {  },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() { this[key] = 1; },
  m9() {  },
  m10() {  },
  m11() {  },
  m12() {  },
  m13() {  },
  m14() {  },
  m15() {  },
  m16() {  },
};
o[key] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c17_computed_define.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() {  },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() { this[k] = 1; },
  m9() {  },
  m10() {  },
  m11() {  },
  m12() {  },
  m13() {  },
  m14() {  },
  m15() {  },
  m16() {  },
};
Object.defineProperty(o, "later", { value: 1 });
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c17_existing_define.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() {  },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() { this.m0 = 1; },
  m9() {  },
  m10() {  },
  m11() {  },
  m12() {  },
  m13() {  },
  m14() {  },
  m15() {  },
  m16() {  },
};
Object.defineProperty(o, "later", { value: 1 });
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c17_name_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() {  },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() { this.extra = 1; },
  m9() {  },
  m10() {  },
  m11() {  },
  m12() {  },
  m13() {  },
  m14() {  },
  m15() {  },
  m16() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c17_read_computed.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() {  },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() { this.extra; },
  m9() {  },
  m10() {  },
  m11() {  },
  m12() {  },
  m13() {  },
  m14() {  },
  m15() {  },
  m16() {  },
};
o[key] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c17_string_dot.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() {  },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() { this["extra"] = 1; },
  m9() {  },
  m10() {  },
  m11() {  },
  m12() {  },
  m13() {  },
  m14() {  },
  m15() {  },
  m16() {  },
};
o.later = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c17_symbol_existing.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() {  },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() { this[sym] = 1; },
  m9() {  },
  m10() {  },
  m11() {  },
  m12() {  },
  m13() {  },
  m14() {  },
  m15() {  },
  m16() {  },
};
o.m1 = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c17_symbol_none.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() {  },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() { this[sym] = 1; },
  m9() {  },
  m10() {  },
  m11() {  },
  m12() {  },
  m13() {  },
  m14() {  },
  m15() {  },
  m16() {  },
};

/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c1_computed_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() { this[k] = 1; },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c1_existing_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() { this.m0 = 1; },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c1_name_existing.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() { this.extra = 1; },
};
o.m1 = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c40_computed-declared_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() {  },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
  m9() {  },
  m10() {  },
  m11() {  },
  m12() {  },
  m13() {  },
  m14() {  },
  m15() {  },
  m16() {  },
  m17() {  },
  m18() {  },
  m19() {  },
  m20() { this[key] = 1; },
  m21() {  },
  m22() {  },
  m23() {  },
  m24() {  },
  m25() {  },
  m26() {  },
  m27() {  },
  m28() {  },
  m29() {  },
  m30() {  },
  m31() {  },
  m32() {  },
  m33() {  },
  m34() {  },
  m35() {  },
  m36() {  },
  m37() {  },
  m38() {  },
  m39() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c40_read_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() {  },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
  m9() {  },
  m10() {  },
  m11() {  },
  m12() {  },
  m13() {  },
  m14() {  },
  m15() {  },
  m16() {  },
  m17() {  },
  m18() {  },
  m19() {  },
  m20() { this.extra; },
  m21() {  },
  m22() {  },
  m23() {  },
  m24() {  },
  m25() {  },
  m26() {  },
  m27() {  },
  m28() {  },
  m29() {  },
  m30() {  },
  m31() {  },
  m32() {  },
  m33() {  },
  m34() {  },
  m35() {  },
  m36() {  },
  m37() {  },
  m38() {  },
  m39() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c40_string_existing.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() {  },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
  m9() {  },
  m10() {  },
  m11() {  },
  m12() {  },
  m13() {  },
  m14() {  },
  m15() {  },
  m16() {  },
  m17() {  },
  m18() {  },
  m19() {  },
  m20() { this["extra"] = 1; },
  m21() {  },
  m22() {  },
  m23() {  },
  m24() {  },
  m25() {  },
  m26() {  },
  m27() {  },
  m28() {  },
  m29() {  },
  m30() {  },
  m31() {  },
  m32() {  },
  m33() {  },
  m34() {  },
  m35() {  },
  m36() {  },
  m37() {  },
  m38() {  },
  m39() {  },
};
o.m1 = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_computed-declared_define.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[key] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};
Object.defineProperty(o, "later", { value: 1 });
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_computed-declared_none.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[key] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};

/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_computed-declared_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[key] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_computed_define.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[k] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};
Object.defineProperty(o, "later", { value: 1 });
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_computed_none.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[k] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};

/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_computed_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[k] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_existing_define.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.m0 = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};
Object.defineProperty(o, "later", { value: 1 });
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_existing_none.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.m0 = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};

/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_existing_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.m0 = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_name_define.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};
Object.defineProperty(o, "later", { value: 1 });
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_name_none.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};

/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_name_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_read_define.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra; },
  m5() {  },
  m6() {  },
  m7() {  },
};
Object.defineProperty(o, "later", { value: 1 });
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_read_none.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra; },
  m5() {  },
  m6() {  },
  m7() {  },
};

/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_read_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra; },
  m5() {  },
  m6() {  },
  m7() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_string_define.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this["extra"] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};
Object.defineProperty(o, "later", { value: 1 });
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_string_none.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this["extra"] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};

/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_string_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this["extra"] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_symbol_define.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[sym] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};
Object.defineProperty(o, "later", { value: 1 });
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_symbol_none.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[sym] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};

/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c8_symbol_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[sym] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_computed-declared_computed.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[key] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o[key] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_computed-declared_define.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[key] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
Object.defineProperty(o, "later", { value: 1 });
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_computed-declared_dot.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[key] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o.later = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_computed-declared_existing.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[key] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o.m1 = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_computed-declared_none.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[key] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};

/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_computed-declared_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[key] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_computed_computed.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[k] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o[key] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_computed_define.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[k] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
Object.defineProperty(o, "later", { value: 1 });
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_computed_dot.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[k] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o.later = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_computed_existing.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[k] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o.m1 = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_computed_none.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[k] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};

/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_computed_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[k] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_existing_computed.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.m0 = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o[key] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_existing_define.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.m0 = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
Object.defineProperty(o, "later", { value: 1 });
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_existing_dot.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.m0 = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o.later = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_existing_existing.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.m0 = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o.m1 = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_existing_none.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.m0 = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};

/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_existing_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.m0 = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_name_computed.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o[key] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_name_define.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
Object.defineProperty(o, "later", { value: 1 });
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_name_dot.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o.later = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_name_existing.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o.m1 = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_name_none.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};

/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_name_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_read_computed.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o[key] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_read_define.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
Object.defineProperty(o, "later", { value: 1 });
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_read_dot.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o.later = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_read_existing.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o.m1 = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_read_none.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};

/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_read_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this.extra; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_string_computed.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this["extra"] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o[key] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_string_define.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this["extra"] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
Object.defineProperty(o, "later", { value: 1 });
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_string_dot.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this["extra"] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o.later = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_string_existing.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this["extra"] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o.m1 = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_string_none.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this["extra"] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};

/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_string_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this["extra"] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_symbol_computed.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[sym] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o[key] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_symbol_define.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[sym] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
Object.defineProperty(o, "later", { value: 1 });
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_symbol_dot.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[sym] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o.later = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_symbol_existing.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[sym] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o.m1 = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_symbol_none.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[sym] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};

/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
// @filename: c9_symbol_string.js
const key = "dynamic"; const sym = Symbol();
const o = {
  m0() {  },
  m1() {  },
  m2() {  },
  m3() {  },
  m4() { this[sym] = 1; },
  m5() {  },
  m6() {  },
  m7() {  },
  m8() {  },
};
o["later"] = 1;
/** @type {never} */ const whole = o;
o.extra; o.later; o.dynamic; o.m0; o.nope;
export {};
