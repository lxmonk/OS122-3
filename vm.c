#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"



#define MAX_PSYC_MEM (MAX_PSYC_PAGES * PGSIZE)

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()
struct segdesc gdt[NSEGS];

/* A&T replacement algorithm */
#define A_FIFO 0
#define A_NFU 1
#define A_NONE 2

static int init_done = 0;	/* set to 1 once init is done */


// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpunum()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);

  // Map cpu, and curproc
  c->gdt[SEG_KCPU] = SEG(STA_W, &c->cpu, 8, 0);

  lgdt(c->gdt, sizeof(c->gdt));
  loadgs(SEG_KCPU << 3);

  // Initialize cpu-local storage.
  cpu = c;
  proc = 0;
}
/* A&T forward decl */
int swap_in(pde_t *pgtab, uint pd_idx, pde_t *pde);
uint  page_to_swap(pde_t *pgdir);
int swap_to_file(pde_t *pgdir);

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;
  /* uint page; */

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)p2v(PTE_ADDR(*pde));
  } else {
      if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
          return 0;
      // Make sure all those PTE_P bits are zero.
      memset(pgtab, 0, PGSIZE);
      // The permissions here are overly generous, but they can
      // be further restricted by the permissions in the page table
      // entries, if necessary.
      *pde = v2p(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
        panic("remap");

    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
  { (void*) KERNBASE, 0,             EXTMEM,    PTE_W},  // I/O space
  { (void*) KERNLINK, V2P(KERNLINK), V2P(data), 0}, // kernel text+rodata
  { (void*) data,     V2P(data),     PHYSTOP,   PTE_W},  // kernel data, memory
  { (void*) DEVSPACE, DEVSPACE,      0,         PTE_W},  // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm()
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (p2v(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0)
      return 0;
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(v2p(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  pushcli();
  cpu->gdt[SEG_TSS] = SEG16(STS_T32A, &cpu->ts, sizeof(cpu->ts)-1, 0);
  cpu->gdt[SEG_TSS].s = 0;
  cpu->ts.ss0 = SEG_KDATA << 3;
  cpu->ts.esp0 = (uint)proc->kstack + KSTACKSIZE;
  ltr(SEG_TSS << 3);
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");
  lcr3(v2p(p->pgdir));  // switch to new address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, v2p(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, p2v(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;
  /* A&T */
  int replacement_alg;

  replacement_alg = A_NFU;

#ifdef FIFO
  replacement_alg = A_FIFO;
#endif

#ifdef NONE
  replacement_alg = A_NONE;
#endif

  K_DEBUG_PRINT(2, "replacement_alg=%d", replacement_alg);
  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE) {
      K_DEBUG_PRINT(3,"a = %x, a/PGSIZE = %x",a,a/PGSIZE);
      // A&T max pages in psyc memory
      //      if ((a >= MAX_PSYC_MEM))
      if ((replacement_alg != A_NONE) &&
          (get_mapped_pages_number() >= MAX_PSYC_PAGES))
          swap_to_file(pgdir);
      mem = kalloc();
      if(mem == 0){
          cprintf("allocuvm out of memory\n");
          deallocuvm(pgdir, newsz, oldsz);
          return 0;
      }
      memset(mem, 0, PGSIZE);
      mappages(pgdir, (char*)a, PGSIZE, v2p(mem), PTE_W|PTE_U);
      /* A&T  */
      if (replacement_alg != A_NONE) {
          add_page_va(PGROUNDDOWN(a));
          inc_mapped_pages_number();
      }
      /* A&T  end */

  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a += (NPTENTRIES - 1) * PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = p2v(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = p2v(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
        panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P) && !(*pte & PTE_PG)) /* A&T added PTE_PG check */
        panic("copyuvm: page not present");
    /* A&T */
    if (*pte & PTE_PG) {
        if (map_swap_pages(d, (void*)i, PGSIZE, PTE_W|PTE_U) < 0)
            goto bad;
    } else {
        pa = PTE_ADDR(*pte);
        if((mem = kalloc()) == 0)
            goto bad;
        memmove(mem, (char*)p2v(pa), PGSIZE);
        if(mappages(d, (void*)i, PGSIZE, v2p(mem), PTE_W|PTE_U) < 0)
            goto bad;
    }
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)p2v(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

/* A&T read to pgtab from file, and update the new
 * physical address (PPN) in the pgdir. Set
 * the PTE_P bit, and clear the PTE_PG bit.
 * don't forget to clear the corresponding place in
 * pagefile_addr array */
int swap_from_file(uint va) {
    uint *pfile_va_arr; //array of page va of the process
    struct file *f;
    char *mem;
    pte_t *pte;
    int i;

    /* panic("swap from file. don't panic.\n"); */

    K_DEBUG_PRINT(3, "inside. va=%x", va);
    va = PGROUNDDOWN((uint)va);
    K_DEBUG_PRINT(3, "round down  va=%x", va);
    pfile_va_arr = get_pagefile_addr();
    for (i = 0; i < MAX_SWAP_PAGES; i++) {
        if (pfile_va_arr[i] == va) {
            pfile_va_arr[i] = UNUSED_VA;
            break;
        }
    }
    //not found in file
    if (i == MAX_SWAP_PAGES)
        panic("swap_from_file : page not in swap");

    f = get_pagefile();
    set_f_offset(f, ((uint)i * PGSIZE));

    //allocate memort for page
    mem = kalloc();
    if(mem == 0){
        panic(" can't swap from file'\n");
        return 0;
    }
    memset(mem, 0, PGSIZE);
    if ((pte = walkpgdir(get_pgdir(),(char*)va,0)) == 0)
        panic("swap_from_file : page table not found");
    /* read from file to pgtab */
    fileread(f, mem , PGSIZE);
    *pte &= 0xFFF; /* A&T zero the leftmost 20 bits */
    *pte |= v2p(mem);  /* update pgtab addr */
    *pte &= (~PTE_PG);		/* clear PTE_PG bit */
    *pte |= PTE_P;		/* set PTE_P bit */
    dec_swapped_pages_number();

    add_page_va(va);
    return 0;
}

/* A&T */

uint page_to_swap(pde_t *pgdir) {
    int replacement_alg;

    replacement_alg = A_NFU;

#ifdef FIFO
    replacement_alg = A_FIFO;
#endif

#ifdef NONE
    replacement_alg = A_NONE;
#endif

    K_DEBUG_PRINT(3, "inside. replacement_alg = %d", replacement_alg);

    if (replacement_alg == A_NONE)
        return UNUSED_VA;

    if (replacement_alg == A_FIFO)
        return get_fifo_va();

    return get_nfu_va();

    /* va=0; */

    /* for (pd_idx = 0; pd_idx < NPDENTRIES; pd_idx++) { */
    /*     pde = &pgdir[pd_idx]; */
    /*     if(*pde & PTE_P) { */
    /*         pgtab = (pte_t*)p2v(PTE_ADDR(*pde)); */
    /*         for (ptable_idx = 0; ptable_idx < NPDENTRIES; ptable_idx++) { */
    /*             if(pgtab[ptable_idx] & PTE_P) { */
    /*                 /\* va = (*pde << PDXSHIFT); *\/ */
    /*                 /\* va |= (*pgtab << PTXSHIFT); *\/ */
    /*                 va = PGADDR(pd_idx, ptable_idx, 0); */
    /*                 K_DEBUG_PRINT(3, "pde_idx= %d,ptable_idx = %d, va=%x .",pd_idx,ptable_idx,va); */
    /*                 K_DEBUG_PRINT(5, "pde = %x,pgtab = %x",*pde,*pgtab); */
    /*                 return va; */
    /*             } */
    /*         } */
        /* } */
    /* } */

    /* return UNUSED_VA; */
}

/* A&T
 * write the page table to the pagefile, clear PTE_P
 * and set PTE_PG */
int swap_to_file(pde_t *pgdir) {
    uint *pagefile_addr; // A&T array of page va  address in swap
    uint va_page; //page to swap to file
    int i;
    struct file *f;
    char* v;
    pde_t *pte;

    // check if not init or shell procces
    if (!((init_done) && (not_shell_init())))
        return -1;
    if (get_swapped_pages_number() >= MAX_SWAP_PAGES)
        return -1;
    if ((va_page = page_to_swap(pgdir)) == UNUSED_VA)
        return -1;



    K_DEBUG_PRINT(3, "inside. va_page = %x", va_page);

    pagefile_addr = get_pagefile_addr();
    for (i = 0; i < MAX_SWAP_PAGES; i++) {
        if (pagefile_addr[i] == UNUSED_VA) {
            //saves va address of swap page
            pagefile_addr[i] = va_page;
            break;
        }
    }
    // f = swap file
    f = get_pagefile();
    set_f_offset(f, ((uint)i * PGSIZE));
    //writing page to swap file
    filewrite(f,(char *) va_page, PGSIZE);
    inc_swapped_pages_number();
    if ((pte = walkpgdir(pgdir,(char *)va_page,0)) == 0)
        panic("A&T page to swap problem ");
    // set the flags - not present + in swap
    *pte &= ~PTE_P;
    *pte |= PTE_PG;
    v = p2v(PTE_ADDR(*pte));
    K_DEBUG_PRINT(3,"v= %x\n", v);
    kfree(v);			/* free the page */

    return 0;
}

void set_init_done(void) {
    init_done = 1;
}

int bring_from_swap(uint va) {
    pde_t* pde;

    pde = get_pgdir();
    swap_to_file(pde);
    swap_from_file(va);
    return 0;
}
// A&T update the nfu array
int update_nfu_arr(pde_t* pgdir,uint* addr_arry,uint* nfu_arr)
{
    int i;
    pte_t *pgtab;
    //TODO: loop all process (except shell, init)
    for(i=0; i < MAX_PSYC_PAGES;i++) {
        nfu_arr[i] >>= 1; //right shift by 1
        if ((addr_arry[i] != UNUSED_VA) &&
            ((pgtab = walkpgdir(pgdir,(char*)addr_arry[i],0)) != 0) &&
            (*pgtab & PTE_A)) {

            K_DEBUG_PRINT(2,"pte %x is used",(int)pgtab);
            *pgtab &= ~(PTE_A); //clear the bit
            nfu_arr[i] |= (1 << 31); //0x80000000 (add 1 to the MSB)
        }
    }

    K_DEBUG_PRINT(2,"inside",999);
    return 0;
}

int
map_swap_pages(pde_t *pgdir, void *va, uint size, int perm)
{
    char *a, *last;
    pte_t *pte;

    a = (char*)PGROUNDDOWN((uint)va);
    last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
    for(;;){
        if((pte = walkpgdir(pgdir, a, 1)) == 0)
            return -1;
        if((*pte & PTE_P) || (*pte & PTE_PG))
            panic("remap");

        *pte = perm | PTE_PG; /* A&T PTE_PG instead of PTE_P */
        if(a == last)
            break;
        a += PGSIZE;
    }
    return 0;
}
