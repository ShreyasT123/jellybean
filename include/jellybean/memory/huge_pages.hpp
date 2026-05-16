#pragma once
#include <cstddef>
#include <cstdlib>
#include <memory>

namespace jellybean::memory {

/**
 * @brief Allocates memory using huge pages (2MB) if available.
 * 
 * On Linux, uses mmap with MAP_HUGETLB. Falls back to standard mmap if fails.
 * On Windows, uses VirtualAlloc with MEM_LARGE_PAGES (requires privileges).
 */
void* allocate_huge_pages(size_t size_bytes);

/**
 * @brief Frees memory allocated via allocate_huge_pages.
 */
void free_huge_pages(void* ptr, size_t size_bytes);

/**
 * @brief RAII deleter for huge pages.
 */
struct HugePageDeleter {
    size_t size;
    void operator()(void* ptr) const {
        if (ptr) free_huge_pages(ptr, size);
    }
};

template<typename T>
using huge_page_unique_ptr = std::unique_ptr<T[], HugePageDeleter>;

template<typename T>
huge_page_unique_ptr<T> allocate_huge_pages_unique(size_t count) {
    size_t size = count * sizeof(T);
    void* ptr = allocate_huge_pages(size);
    if (!ptr) {
        std::abort();
    }
    return huge_page_unique_ptr<T>(static_cast<T*>(ptr), HugePageDeleter{size});
}

} // namespace jellybean::memory
