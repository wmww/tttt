// Known failing (agents/bugs/bun_others.md): in a cycle of interfaces tsc drops the base of the one whose getBaseTypes
// started first and keeps the other's (J1's base J0 is kept: j1.x0 is fine); tttt drops both
interface J0 extends J1 { x0: 1 }
interface J1 extends J0 { x1: 1 }
declare const j0: J0
declare const j1: J1
j0.x1; j1.x0
