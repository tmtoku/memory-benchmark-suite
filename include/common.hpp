#pragma once

#include <sys/mman.h>
#include <unistd.h>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>

#ifdef __GNUC__
#define FORCE_INLINE __attribute__((always_inline)) inline
#else
#define FORCE_INLINE inline
#endif

namespace common
{
    constexpr auto KiB = std::size_t{1024};
    constexpr auto MiB = std::size_t{1024} * KiB;
    constexpr auto GiB = std::size_t{1024} * MiB;

    [[nodiscard]] inline std::size_t get_cache_line_bytes()
    {
        const auto cache_line_bytes = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
        if (cache_line_bytes < 1)
        {
            throw std::runtime_error("Failed to get the cache line size.");
        }

        return static_cast<std::size_t>(cache_line_bytes);
    }

    [[nodiscard]] inline std::size_t get_cache_size(const std::int32_t cache_index)
    {
        const auto file_path = "/sys/devices/system/cpu/cpu0/cache/index" + std::to_string(cache_index) + "/size";
        std::ifstream ifs(file_path);
        if (!ifs.is_open())
        {
            throw std::runtime_error("Failed to open a file: " + file_path);
        }

        std::string line;
        std::getline(ifs, line);

        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
        {
            line.pop_back();
        }

        if (line.empty())
        {
            throw std::runtime_error("Empty cache size file: " + file_path);
        }

        auto cache_size = std::stoul(line);
        const auto suffix = line.back();
        if (suffix == 'K')
        {
            cache_size *= KiB;
        }
        else if (suffix == 'M')
        {
            cache_size *= MiB;
        }
        return cache_size;
    }

    [[nodiscard]] inline std::size_t get_page_size()
    {
        const auto page_size = sysconf(_SC_PAGESIZE);
        if (page_size < 1)
        {
            throw std::runtime_error("Failed to get the page size.");
        }
        return static_cast<std::size_t>(page_size);
    }

    [[nodiscard]] inline std::size_t get_hugepage_size()
    {
        static const auto hugepage_size = []() {
            std::ifstream ifs("/proc/meminfo");
            std::string line;
            if (!ifs.is_open())
            {
                throw std::runtime_error("Failed to open /proc/meminfo to get the hugepage size.");
            }
            while (std::getline(ifs, line))
            {
                if (line.find("Hugepagesize:") != std::string::npos)
                {
                    std::stringstream ss(line);
                    std::string label;
                    std::size_t value = 0;
                    std::string unit;

                    ss >> label >> value >> unit;

                    if (unit == "kB")
                    {
                        return value * KiB;
                    }
                    if (unit == "MB")
                    {
                        return value * MiB;
                    }
                    throw std::runtime_error("Unknown unit for Hugepagesize in /proc/meminfo: " + unit);
                }
            }
            throw std::runtime_error("Could not find 'Hugepagesize' entry in /proc/meminfo.");
        }();

        return hugepage_size;
    }

    template <typename T>
    [[nodiscard]] auto allocate_hugepage_buffer(const std::size_t buffer_size_in_bytes)
    {
        const auto hugepage_size = get_hugepage_size();
        const auto size = (buffer_size_in_bytes + hugepage_size - 1) / hugepage_size * hugepage_size;

        void* const buffer =
            mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
        if (buffer == MAP_FAILED)
        {
            throw std::bad_alloc();
        }

        auto deleter = [size](T* p) noexcept { munmap(static_cast<void*>(p), size); };
        return std::unique_ptr<T, decltype(deleter)>(static_cast<T*>(buffer), std::move(deleter));
    }

    template <typename T>
    [[nodiscard]] std::unique_ptr<T, void (*)(void*)> allocate_aligned_buffer(const std::size_t buffer_size_in_bytes,
                                                                              const std::size_t alignment_bytes)
    {
        if (alignment_bytes == 0 || (alignment_bytes & (alignment_bytes - 1)) != 0)
        {
            throw std::invalid_argument("`alignment_bytes` must be a power of 2.");
        }

        if (alignment_bytes % sizeof(void*) != 0)
        {
            throw std::invalid_argument("`alignment_bytes` must be a multiple of " + std::to_string(sizeof(void*)) +
                                        ".");
        }

        const auto alignment_mask = ~(alignment_bytes - 1);
        const auto size = (buffer_size_in_bytes + (alignment_bytes - 1)) & alignment_mask;

        void* const buffer = std::aligned_alloc(alignment_bytes, size);
        if (buffer == nullptr)
        {
            throw std::bad_alloc();
        }

        return {static_cast<T*>(buffer), std::free};
    }
}  // namespace common
