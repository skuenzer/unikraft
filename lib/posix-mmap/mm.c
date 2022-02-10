/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Authors: Stefan Teodorescu <stefanl.teodorescu@gmail.com>
 *
 * Copyright (c) 2021, University Politehnica of Bucharest. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <uk/bitmap.h>
#include <uk/print.h>
#include <uk/plat/paging.h>
#include <uk/syscall.h>

#include <sys/mman.h>

#define MMAP_AREA_START		(1UL << 39) /* 512 GB */
#define MMAP_AREA_END		(1UL << 40) /* 1 TB */
#define MMAP_AREA_SIZE		(MMAP_AREA_END - MMAP_AREA_START)

/*
 * XXX
 * This does for now a linear search, starting from |start|, looking for a
 * memory area with |length| bytes (aligned to page size)
 */
static unsigned long get_free_virtual_area(unsigned long start, size_t length)
{
	unsigned long page;
	struct uk_pagetable *pt = ukplat_pt_get_active();
	__pte_t pte;

	if (!PAGE_ALIGNED(length))
		return -1;

	while (start <= __VADDR_MAX - length) {
		for (page = start; page < start + length; page += PAGE_SIZE) {
			ukplat_pt_walk(pt, PAGE_ALIGN_DOWN(page), NULL, NULL,
					&pte);
			if (PT_Lx_PTE_PRESENT(pte, PAGE_LEVEL))
				break;
		}

		if (page == start + length)
			return start;

		start = page + PAGE_SIZE;
	}

	return -1;
}

static __maybe_unused void dump_vmas()
{
	unsigned long vaddr, vaddr2;
	struct uk_pagetable *pt = ukplat_pt_get_active();
	unsigned long vma_s = 0, vma_max = 0;

	/*vaddr = MMAP_AREA_START;
	while (vaddr < MMAP_AREA_END) {
		__pte_t pte = ukplat_virt_to_pte(pt, vaddr);
		if (!PAGE_PRESENT(pte)) {
			if (vma_s != 0) {
				uk_pr_debug("VMA: %lx - %lx (%lx bytes)\n",
					    vma_s, vaddr, vaddr - vma_s);
				vma_s = 0;
				vma_max = vaddr;
			}
		} else {
			//if (vaddr > 0x10c000000000)
			//	uk_pr_debug("vaddr: %lx -> %lx\n", vaddr,
			//		pte_to_pfn(pte));

			if (vma_s == 0) {
				vma_s = vaddr;
			}
		}

		vaddr += PAGE_SIZE;
	}*/


	vma_max = 0x100028afc000;

	uk_pr_debug("VMA_MAX: %lx\n", vma_max);

	vaddr = 0x100000000000;
	while (vaddr <= vma_max) {
		__pte_t pte;
		ukplat_pt_walk(pt, vaddr, NULL, NULL, &pte);
		if (PT_Lx_PTE_PRESENT(pte, PAGE_LEVEL)) {
			unsigned long paddr = PT_Lx_PTE_PADDR(pte, lvl);
			int count = 0;

			vaddr2 = 0x100000000000;
			while (vaddr2 <= vma_max) {
				__pte_t pte2;
				ukplat_pt_walk(pt, vaddr2, NULL, NULL, &pte2);
				if (PT_Lx_PTE_PRESENT(pte2, PAGE_LEVEL)) {
					unsigned long paddr2 = PT_Lx_PTE_PADDR(pte2, lvl);
					if (paddr2 == paddr) {
						count++;
						if ((count > 1) || (vaddr % 0x100000 == 0)) {
						uk_pr_debug("vaddr: %lx -> %lx @ %lx (%d)\n",
							vaddr, paddr, vaddr2, count);
						}
					}
				}

				vaddr2 += PAGE_SIZE;
			}

			UK_ASSERT(count >= 1);

			//if (count > 1) {
				/*uk_pr_debug("vaddr: %lx -> %lx (%d)\n",
					vaddr, pfn, count);*/
			//}
		}

		vaddr += PAGE_SIZE;
	}
}

static __maybe_unused int libc_to_internal_prot(int prot)
{
	int page_prot = PAGE_ATTR_PROT_NONE;

	if (prot & PROT_READ)
		page_prot |= PAGE_ATTR_PROT_READ;
	if (prot & PROT_WRITE)
		page_prot |= PAGE_ATTR_PROT_WRITE;
	if (prot & PROT_EXEC)
		page_prot |= PAGE_ATTR_PROT_EXEC;

	return page_prot;
}

UK_SYSCALL_DEFINE(void*, mmap, void*, addr, size_t, len, int, prot, int, flags,
		  int, fd, off_t, offset)
{
	struct uk_pagetable *pt = ukplat_pt_get_active();
	unsigned long page_addr = (unsigned long) addr;
	unsigned long page_prot;
	__vaddr_t vaddr;
	struct stat st;
	int file = 0;
	int rc;

	if (flags & MAP_ANONYMOUS) {
		if (fd != -1 || offset) {
			errno = EINVAL;
			return MAP_FAILED;
		}
	} else {
		/* hack to alloc a file */
		flags |= MAP_ANONYMOUS;
		file = 1;
	}

	/* At least one of MAP_SHARED or MAP_PRIVATE has to be specified */
	if (!(flags & MAP_SHARED) && !(flags & MAP_PRIVATE)) {
		errno = EINVAL;
		return MAP_FAILED;
	}

	if (!len) {
		errno = EINVAL;
		return MAP_FAILED;
	}

	len = PAGE_ALIGN_UP(len);
	if (!len) {
		errno = ENOMEM;
		return MAP_FAILED;
	}

	if (flags & MAP_FIXED) {
		/* Discard any overlapping mappings */
		if (munmap(addr, len)) {
			errno = EINVAL;
			return MAP_FAILED;
		}

		page_addr = PAGE_ALIGN_UP(page_addr);
	} else {
		if ((void *) page_addr == NULL) /* || page_addr < MMAP_AREA_START) */
			page_addr = MMAP_AREA_START;
		else
			page_addr = PAGE_ALIGN_UP(page_addr);
	}

	vaddr = get_free_virtual_area(page_addr, len);
	if (vaddr == (unsigned long) -1) {
		errno = ENOMEM;
		return MAP_FAILED;
	}

	uk_pr_debug("mmap: %lx %lx -> %lx\n", (__vaddr_t)addr, len, vaddr);

	if (prot == PROT_NONE) {
		/* TODO: We map pages with no access to an invalid pfn.
		 * Depending on the hardware, this should return a zero page,
		 * be undefined, or cause a SIGBUS exception on access iff we
		 * could access it */
		page_prot = PAGE_ATTR_PROT_NONE;
	} else {
		/*
		 * Map the page RW temporarily to zero it.
		 * TODO: This is ugly and should be done by having a zeroed
		 * frame pool and not having to temporarily map pages with the
		 * wrong protection
		 */
		page_prot = PAGE_ATTR_PROT_READ |
			PAGE_ATTR_PROT_WRITE | PAGE_ATTR_PROT_EXEC;
	}

	rc = ukplat_page_map(pt, vaddr, __PADDR_ANY, len >> PAGE_SHIFT,
			     page_prot, 0);
	if (rc) {
		munmap((void *) vaddr, len);
		errno = -rc;
		return MAP_FAILED;
	}

	if (flags & MAP_ANONYMOUS) {
		if (page_prot != PAGE_ATTR_PROT_NONE) {
			/* MAP_ANONYMOUS pages are zeroed out */
			/*
			 * XXX: there is a bug when building with performance
			 * optimizations flag that make this memset loop
			 * infintely. Using for loop for now.
			 */
			//memset((void *) vaddr, 0, len);
			for (size_t i = 0; i < len / sizeof(unsigned long); i++)
				*((unsigned long *) vaddr + i) = 0;
		}
	}

	if (file) {
		uk_pr_debug(">>>>>>>>>>>>> MAPPING FILE %d\n", fd);

		/* setting the file cursor at the beggining of the file
			* so we don't have problems when reading from it
			*/
		lseek(fd, 0, SEEK_SET);

		int ret = fstat(fd, &st);
		if (ret < 0) {
			errno = ENOMEM;
			return MAP_FAILED;
		}


		size_t file_len = st.st_size;

		if (offset % PAGE_SIZE) {
			errno = ENOMEM;
			return MAP_FAILED;
		}

		if (offset < file_len) {
			size_t from_file = file_len - (size_t) offset;
			size_t to_read = len > from_file ? from_file : len;
			size_t current_read = 0;
			size_t total_read = 0;

			if (offset != 0) {
				lseek(fd, offset, SEEK_SET);
			}

			while (total_read < to_read) {
				current_read = read(fd, (void *)(vaddr + total_read), to_read - total_read);
				if ((long)current_read < 0) {
					uk_pr_err("Error reading from file\n");
				}
				total_read += current_read;
			}
		}
	}

	/* Fix protection */
	/*page_prot = libc_to_internal_prot(prot);
	if (page_prot != eprot) {
		for (page = vaddr; page < vaddr + len; page += PAGE_SIZE)
			ukplat_page_set_prot(pt, page, page_prot, 0);
	}*/

	/*uk_pr_debug("  mmap: %lx %lu (virt: %lu phys: %lu pt: %lu)\n",
		addr, len >> PAGE_SHIFT,
		pt->nr_mapped_pages,
		(pt->fa->total_memory - pt->fa->free_memory) >> PAGE_SHIFT,
		pt->nr_pt_pages);*/

	return (void *) vaddr;
}

UK_SYSCALL_R_DEFINE(int, munmap, void*, addr, size_t, len)
{
	unsigned long pages = PAGE_ALIGN_UP(len) >> PAGE_SHIFT;

	uk_pr_debug("munmap: %lx %lx\n", (__vaddr_t)addr, len);

	if ((__vaddr_t)addr == 0x8000eca000) {
		pages = pages;
	}

	return ukplat_page_unmap(ukplat_pt_get_active(), (__vaddr_t)addr,
				 pages, 0);
}

UK_SYSCALL_R_DEFINE(int, mprotect, void*, addr, size_t, len, int, prot)
{
	unsigned long pages = PAGE_ALIGN_UP(len) >> PAGE_SHIFT;
	unsigned long page_prot;

	uk_pr_debug("mprotect: %lx %lx\n", (__vaddr_t)addr, len);

	page_prot = PAGE_ATTR_PROT_NONE;
	if (prot & PROT_READ)
		page_prot |= PAGE_ATTR_PROT_READ;
	if (prot & PROT_WRITE)
		page_prot |= PAGE_ATTR_PROT_WRITE;
	if (prot & PROT_EXEC)
		page_prot |= PAGE_ATTR_PROT_EXEC;

	return ukplat_page_set_attr(ukplat_pt_get_active(), (__vaddr_t)addr,
				    pages, page_prot, 0);
}

UK_SYSCALL_R_DEFINE(int, msync, void*, addr, size_t, len, int, prot)
{
	return -ENOTSUP;
}
