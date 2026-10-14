// Live-heap tracker (LD_PRELOAD), run through ../../memtrack.sh. main.c's phase_done() calls memtrack_dump() and
// memtrack_scan() (weak symbols) when MEMTRACK is set; each writes $MEMTRACK_DIR/memtrack-dump-<phase>.txt (size, usable
// size, 3 caller offsets per live block: an.py FILE f0|f01|size|sizef) and memtrack-scan-<phase>.txt (non-null Spans and
// all-zero words per block: scan.py FILE; gcd.py FILE: sizes per site). A Span is recognised as a word pointing at a
// block of sizeof(SourceFile) bytes (MEMTRACK_SOURCE_FILE_SIZE, see memtrack.sh), then a pointer, then a small length.
// Not thread-safe beyond a spinlock, backtrace() per malloc: ~10x slower than a normal run
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <execinfo.h>
#include <dlfcn.h>
#include <malloc.h>
#include <stdatomic.h>
#include <unistd.h>
extern void* __libc_malloc(size_t); extern void __libc_free(void*); extern void* __libc_calloc(size_t,size_t); extern void* __libc_realloc(void*,size_t);
typedef struct { void* p; uint32_t size; uint32_t pad; void* f[3]; } E;
#define CAP (1u<<26)
static E* tab; static atomic_flag lk = ATOMIC_FLAG_INIT; static __thread int inside;
static void lock(void){ while(atomic_flag_test_and_set(&lk)); } static void unlock(void){ atomic_flag_clear(&lk); }
static size_t h(void*p){ return ((uintptr_t)p>>4)*0x9E3779B97F4A7C15ull>>38; }
static size_t cum_bytes, cum_n, freed_bytes, freed_n, realloc_n, realloc_old_bytes;
static void ins(void*p,size_t s){ if(!p||inside) return; __atomic_fetch_add(&cum_bytes,s,0); __atomic_fetch_add(&cum_n,1,0); inside=1; void* bt[5]; int n=backtrace(bt,5); inside=0;
  lock(); if(!tab) tab=mmap(0,sizeof(E)*CAP,3,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
  size_t i=h(p)&(CAP-1); while(tab[i].p && tab[i].p!=(void*)1) i=(i+1)&(CAP-1);
  tab[i].p=p; tab[i].size=s; for(int k=0;k<3;k++) tab[i].f[k]= n>k+2? bt[k+2]:0; unlock(); }
static void del(void*p){ if(!p||!tab||inside) return; lock(); size_t i=h(p)&(CAP-1); while(tab[i].p){ if(tab[i].p==p){tab[i].p=(void*)1; freed_bytes+=tab[i].size; freed_n++; break;} i=(i+1)&(CAP-1);} unlock(); }
void* malloc(size_t s){ void* p=__libc_malloc(s); ins(p,s); return p; }
void* calloc(size_t a,size_t b){ void* p=__libc_calloc(a,b); ins(p,a*b); return p; }
void free(void*p){ del(p); __libc_free(p); }
void* realloc(void*o,size_t s){ if(o && !inside){ __atomic_fetch_add(&realloc_n,1,0); __atomic_fetch_add(&realloc_old_bytes,malloc_usable_size(o),0); } del(o); void* p=__libc_realloc(o,s); ins(p,s); return p; }

static int cmpp(const void*a,const void*b){ uintptr_t x=*(uintptr_t*)a,y=*(uintptr_t*)b; return x<y?-1:x>y; }
void memtrack_scan(const char* name){
  size_t source_file_size = getenv("MEMTRACK_SOURCE_FILE_SIZE") ? strtoul(getenv("MEMTRACK_SOURCE_FILE_SIZE"), 0, 10) : 440;
  inside=1; lock();
  size_t nc=0; uintptr_t* cand=__libc_malloc(sizeof(uintptr_t)*(1<<20));
  for(size_t i=0;i<CAP;i++) if(tab[i].p && tab[i].p!=(void*)1 && tab[i].size==source_file_size) cand[nc++]=(uintptr_t)tab[i].p;
  qsort(cand,nc,sizeof *cand,cmpp);
  char fn[256]; snprintf(fn,256,"%s/memtrack-scan-%s.txt",getenv("MEMTRACK_DIR")?getenv("MEMTRACK_DIR"):"/tmp",name); FILE* f=fopen(fn,"w");
  for(size_t i=0;i<CAP;i++) if(tab[i].p && tab[i].p!=(void*)1){ E*e=&tab[i]; uintptr_t* w=e->p; size_t n=e->size/8, sp=0, z=0, nullsp=0;
    for(size_t k=0;k<n;k++){ if(!w[k]){ z++; if(k+2<n && !w[k+1] && !w[k+2]) {} continue; }
      if(k+2<n && bsearch(&w[k],cand,nc,sizeof *cand,cmpp) && w[k+1] && w[k+2]<(1u<<24)){ sp++; k+=2; } }
    Dl_info d; unsigned long off=0; if(e->f[0] && dladdr(e->f[0],&d)) off=(unsigned long)((char*)e->f[0]-(char*)d.dli_fbase);
    fprintf(f,"%lx %u %zu %zu\n",off,e->size,sp,z); }
  fclose(f); __libc_free(cand); unlock(); inside=0;
  if(getenv("MEMTRACK_STOP") && !strcmp(getenv("MEMTRACK_STOP"),name)) _exit(0); }
void memtrack_dump(const char* name){
  inside=1; char fn[256]; snprintf(fn,256,"%s/memtrack-dump-%s.txt",getenv("MEMTRACK_DIR")?getenv("MEMTRACK_DIR"):"/tmp",name); FILE* f=fopen(fn,"w"); lock();
  Dl_info di; dladdr((void*)memtrack_dump,&di);
  for(size_t i=0;i<CAP;i++) if(tab[i].p && tab[i].p!=(void*)1){ E*e=&tab[i]; fprintf(f,"%u %zu",e->size,malloc_usable_size(e->p));
     for(int k=0;k<3;k++){ Dl_info d; if(e->f[k] && dladdr(e->f[k],&d)) fprintf(f," %lx",(unsigned long)((char*)e->f[k]-(char*)d.dli_fbase)); else fprintf(f," 0"); } fprintf(f,"\n"); }
  unlock(); struct mallinfo2 mi=mallinfo2(); fprintf(stderr,"%s: cumulative malloc %zu MB in %zu calls, freed %zu MB in %zu (of which realloc %zu MB in %zu)\n",name,cum_bytes>>20,cum_n,freed_bytes>>20,freed_n,realloc_old_bytes>>20,realloc_n); fprintf(stderr,"%s: mallinfo arena %zu MB hblkhd %zu MB uordblks %zu MB fordblks %zu MB\n",name,mi.arena>>20,mi.hblkhd>>20,mi.uordblks>>20,mi.fordblks>>20); fclose(f); inside=0; }
