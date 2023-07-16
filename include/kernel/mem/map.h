#pragma once

#include <types.h>
#include <elf.h>
#include <paging.h>

void boot_map_region(struct page_table *pml4, void *va, size_t size,
    physaddr_t pa, uint64_t flags);
void *mmio_map_region(physaddr_t pa, size_t size);
void boot_map_kernel(struct page_table *pml4, struct elf *elf_hdr);
uint64_t convert_flags_from_elf_to_pages(struct elf_proghdr *hdr);
int convert_flags_from_pages_to_vma(uint64_t page_flags);
uint64_t convert_flags_from_vma_to_pages(int vma_flags);

