/* Minimal <mach-o/swap.h>: byte-swap the Mach-O structures the SETUP tools touch. */
#pragma once
#include <mach-o/arch.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#define SW32(x) ((x) = __builtin_bswap32(x))
#define SW64(x) ((x) = __builtin_bswap64(x))
static inline void swap_mach_header(struct mach_header *h, enum NXByteOrder o)
{ (void)o; SW32(h->magic); SW32(h->cputype); SW32(h->cpusubtype); SW32(h->filetype);
  SW32(h->ncmds); SW32(h->sizeofcmds); SW32(h->flags); }
static inline void swap_mach_header_64(struct mach_header_64 *h, enum NXByteOrder o)
{ swap_mach_header((struct mach_header *)h, o); SW32(h->reserved); }
static inline void swap_segment_command(struct segment_command *s, enum NXByteOrder o)
{ (void)o; SW32(s->cmd); SW32(s->cmdsize); SW32(s->vmaddr); SW32(s->vmsize); SW32(s->fileoff);
  SW32(s->filesize); SW32(s->maxprot); SW32(s->initprot); SW32(s->nsects); SW32(s->flags); }
static inline void swap_segment_command_64(struct segment_command_64 *s, enum NXByteOrder o)
{ (void)o; SW32(s->cmd); SW32(s->cmdsize); SW64(s->vmaddr); SW64(s->vmsize); SW64(s->fileoff);
  SW64(s->filesize); SW32(s->maxprot); SW32(s->initprot); SW32(s->nsects); SW32(s->flags); }
static inline void swap_section(struct section *s, uint32_t n, enum NXByteOrder o)
{ (void)o; for (uint32_t i = 0; i < n; i++) { SW32(s[i].addr); SW32(s[i].size); SW32(s[i].offset);
  SW32(s[i].align); SW32(s[i].reloff); SW32(s[i].nreloc); SW32(s[i].flags); SW32(s[i].reserved1); SW32(s[i].reserved2); } }
static inline void swap_section_64(struct section_64 *s, uint32_t n, enum NXByteOrder o)
{ (void)o; for (uint32_t i = 0; i < n; i++) { SW64(s[i].addr); SW64(s[i].size); SW32(s[i].offset);
  SW32(s[i].align); SW32(s[i].reloff); SW32(s[i].nreloc); SW32(s[i].flags); SW32(s[i].reserved1);
  SW32(s[i].reserved2); SW32(s[i].reserved3); } }
static inline void swap_symtab_command(struct symtab_command *s, enum NXByteOrder o)
{ (void)o; SW32(s->cmd); SW32(s->cmdsize); SW32(s->symoff); SW32(s->nsyms); SW32(s->stroff); SW32(s->strsize); }
static inline void swap_uuid_command(struct uuid_command *u, enum NXByteOrder o)
{ (void)o; SW32(u->cmd); SW32(u->cmdsize); }
static inline void swap_nlist(struct nlist *n, uint32_t count, enum NXByteOrder o)
{ (void)o; for (uint32_t i = 0; i < count; i++) { SW32(n[i].n_un.n_strx); n[i].n_desc = __builtin_bswap16(n[i].n_desc); SW32(n[i].n_value); } }
static inline void swap_nlist_64(struct nlist_64 *n, uint32_t count, enum NXByteOrder o)
{ (void)o; for (uint32_t i = 0; i < count; i++) { SW32(n[i].n_un.n_strx); n[i].n_desc = __builtin_bswap16(n[i].n_desc); SW64(n[i].n_value); } }
