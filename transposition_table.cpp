#include "transposition_table.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>

#if !defined(CHESS_HAVE_NATIVE_CAS128) || !defined(__GNUC__) || !defined(__x86_64__) || \
    !defined(__SIZEOF_INT128__) || !defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16)
#error The transposition table requires GCC x86-64 native 128-bit CAS
#endif
#include <cpuid.h>

namespace {
static_assert(sizeof(int) == sizeof(std::uint32_t) && std::numeric_limits<int>::digits == 31,
              "TT packing requires a signed 32-bit score");

std::uint64_t pack_payload(const TranspositionEntry& entry) {
    std::uint32_t score;
    std::memcpy(&score, &entry.score, sizeof(score));
    return score | (std::uint64_t(entry.depth) << 32) |
        (std::uint64_t(entry.bound) << 40) | (std::uint64_t(entry.best_move) << 42);
}

TranspositionEntry unpack_payload(std::uint64_t key, std::uint64_t payload) {
    const auto score_bits = static_cast<std::uint32_t>(payload);
    TranspositionEntry entry;
    entry.key = key;
    std::memcpy(&entry.score, &score_bits, sizeof(entry.score));
    entry.depth = static_cast<std::uint8_t>(payload >> 32);
    entry.bound = static_cast<std::uint8_t>((payload >> 40) & 3u);
    entry.best_move = static_cast<std::uint32_t>((payload >> 42) & 0x3ffffu);
    return entry;
}
}

namespace {
using NativeWord = unsigned __int128;
struct alignas(16) NativeSlot {
    NativeWord value = 0;
};
static_assert(sizeof(NativeSlot) == 16 && alignof(NativeSlot) == 16, "CAS requires aligned 16-byte slots");
NativeWord pack(const TranspositionEntry& entry) {
    return NativeWord(entry.key) | (NativeWord(pack_payload(entry)) << 64);
}

TranspositionEntry unpack(NativeWord word) {
    return unpack_payload(static_cast<std::uint64_t>(word), static_cast<std::uint64_t>(word >> 64));
}

NativeWord snapshot(NativeSlot& slot) {
    // Even probes use CAS: an ordinary 128-bit load can tear on this target.
    return __sync_val_compare_and_swap(&slot.value, NativeWord(0), NativeWord(0));
}
}

struct SharedTranspositionTable::Impl {
    std::unique_ptr<NativeSlot[]> native;
};

bool SharedTranspositionTable::native_lock_free_supported() {
    unsigned int eax, ebx, ecx, edx;
    return __get_cpuid(1, &eax, &ebx, &ecx, &edx) && (ecx & bit_CMPXCHG16B) != 0;
}

SharedTranspositionTable::SharedTranspositionTable() : impl(std::make_unique<Impl>()) {
    if (!native_lock_free_supported())
        throw std::runtime_error("The transposition table requires CPU CMPXCHG16B support");
    impl->native = std::make_unique<NativeSlot[]>(kSize);
}
SharedTranspositionTable::~SharedTranspositionTable() = default;

TranspositionEntry SharedTranspositionTable::probe(std::uint64_t key) const {
    return unpack(snapshot(impl->native[key & (kSize - 1)]));
}

void SharedTranspositionTable::store(const TranspositionEntry& entry, int replacement_depth) {
    const int depth = replacement_depth < 0 ? entry.depth : replacement_depth;
    auto& target = impl->native[entry.key & (kSize - 1)];
    const NativeWord desired = pack(entry);
    NativeWord observed = snapshot(target);
    for (;;) {
        const auto live = unpack(observed);
        if (live.key == entry.key && depth < live.depth) return;
        const auto prior = __sync_val_compare_and_swap(&target.value, observed, desired);
        if (prior == observed) return;
        observed = prior;
    }
}

std::size_t SharedTranspositionTable::memory_bytes() const {
    return kSize * sizeof(NativeSlot) + sizeof(*this) + sizeof(Impl);
}
