#pragma once

// The program's atoms, one table every checker shares (plan 86 stage 4c): atoms are compared by pointer, so types two
// checkers share must name properties with the same atoms. Lookups take no lock (a shard's slot array is swapped whole
// when it grows, a slot is written atom first and hash last); a miss locks its shard (64, by hash) and looks again
#include "ty/ty.h"

typedef struct AtomTable AtomTable;

AtomTable* atom_table_new(void);
void atom_table_drop(AtomTable* me); // After everything that holds an atom
Atom const* atom_table_get(AtomTable* me, csview sv); // `sv` as it is (checker_atom() cooks names first)
u32 atom_table_len(AtomTable const* me);
