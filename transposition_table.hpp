#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

struct TranspositionEntry {
    std::uint64_t key = 0;
    int score = 0;
    std::uint8_t depth = 0;
    std::uint8_t bound = 0;
    std::uint32_t best_move = 0;
};

class SharedTranspositionTable {
public:
    static constexpr std::size_t kSize = 1u << 20;
    SharedTranspositionTable();
    ~SharedTranspositionTable();
    SharedTranspositionTable(const SharedTranspositionTable&) = delete;
    SharedTranspositionTable& operator=(const SharedTranspositionTable&) = delete;

    TranspositionEntry probe(std::uint64_t key) const;
    // Search compares remaining depth before its historical uint8_t storage conversion.
    void store(const TranspositionEntry& entry, int replacement_depth = -1);
    std::size_t memory_bytes() const;
    static bool native_lock_free_supported();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
