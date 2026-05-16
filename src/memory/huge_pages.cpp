#include "jellybean/memory/huge_pages.hpp"
#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace jellybean::memory {

void* allocate_huge_pages(size_t size_bytes) {
#if defined(_WIN32)
    // On Windows, large pages require SE_LOCK_MEMORY_NAME privilege.
    // We'll try it, but fall back to normal pages if it fails.
    void* ptr = VirtualAlloc(NULL, size_bytes, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
    if (!ptr) {
        ptr = VirtualAlloc(NULL, size_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
    return ptr;
#else
    // Align to 2MB for huge pages
    size_t huge_page_size = 2 * 1024 * 1024;
    size_t aligned_size = (size_bytes + huge_page_size - 1) & ~(huge_page_size - 1);

    // Try allocating with MAP_HUGETLB
    void* ptr = mmap(NULL, aligned_size, PROT_READ | PROT_WRITE, 
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    
    if (ptr == MAP_FAILED) {
        // Fallback to normal anonymous mapping
        ptr = mmap(NULL, aligned_size, PROT_READ | PROT_WRITE, 
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    
    if (ptr == MAP_FAILED) return nullptr;
    return ptr;
#endif
}

void free_huge_pages(void* ptr, size_t size_bytes) {
    if (!ptr) return;
#if defined(_WIN32)
    (void)size_bytes;
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    size_t huge_page_size = 2 * 1024 * 1024;
    size_t aligned_size = (size_bytes + huge_page_size - 1) & ~(huge_page_size - 1);
    munmap(ptr, aligned_size);
#endif
}

} // namespace jellybean::memory
