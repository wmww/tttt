#!/usr/bin/env python3
# Calls that pass two or more type-making calls as arguments: C leaves their evaluation order open (GCC goes right to
# left), and which type is made first decides union order in printed types. Hoist the first into a local.
# Run from the repo root; PURE lists calls that make nothing.
import re,sys,glob,os
PURE=set("ty_intrinsic ty_reference_args ty_regular checker_atom ty_members prop_type ty_param_constraint global_type ty_empty_object_type ty_global_function_type ty_global_object_type ty_is_related ty_mapped_template".split())
def split_args(s):
    depth=0;cur='';out=[]
    for ch in s:
        if ch in '([{': depth+=1
        if ch in ')]}': depth-=1
        if ch==',' and depth==0: out.append(cur);cur=''
        else: cur+=ch
    out.append(cur);return out
for f in sorted(glob.glob(os.path.dirname(os.path.abspath(__file__))+'/src/ty/*.c')+glob.glob(os.path.dirname(os.path.abspath(__file__))+'/src/*.c')):
    src=open(f).read()
    for m in re.finditer(r'\b([a-z_][a-z0-9_]*)\((me|checker|current_checker),',src):
        # find balanced end
        i=m.end();depth=1;j=i
        while j<len(src) and depth:
            if src[j]=='(':depth+=1
            elif src[j]==')':depth-=1
            j+=1
        args=split_args(src[i:j-1])
        calls=[]
        for a in args:
            cs=[c for c in re.findall(r'\b([a-z_][a-z0-9_]*)\((?:me|checker|current_checker)\b',a) if c not in PURE]
            if cs: calls.append(cs[0])
        if len(calls)>=2:
            line=src.count('\n',0,m.start())+1
            print(f"{f.split('/')[-1]}:{line}: {m.group(1)}({', '.join(calls)})")
