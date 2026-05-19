#include "jellybean/memory/huge_pages.hpp"
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>

namespace jellybean::memory {

auto allocate_huge_pages(size_t size_bytes) -> void* {
    // Align to 2MB for huge pages
    size_t huge_page_size = 2 * 1024 * 1024;
    size_t aligned_size = (size_bytes + huge_page_size - 1) & ~(huge_page_size - 1);

    // Try allocating with MAP_HUGETLB
    void* ptr = mmap(nullptr, aligned_size, PROT_READ | PROT_WRITE, 
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    
    if (ptr == MAP_FAILED) {
        // Fallback to normal anonymous mapping
        ptr = mmap(nullptr, aligned_size, PROT_READ | PROT_WRITE, 
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    
    if (ptr == MAP_FAILED) return nullptr;
    return ptr;
}

void free_huge_pages(void* ptr, size_t size_bytes) {
    if (!ptr) return;
    size_t huge_page_size = 2 * 1024 * 1024;
    size_t aligned_size = (size_bytes + huge_page_size - 1) & ~(huge_page_size - 1);
    munmap(ptr, aligned_size);
}

} // namespace jellybean::memory
