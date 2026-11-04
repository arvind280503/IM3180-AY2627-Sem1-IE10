#include "engine.hpp"

#include <iostream>
#include <array>
#include <atomic>
#include <limits>
#include <thread>
#include <vector>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct EngineTestAccess {
    using Entry = Engine::TranspositionEntry;
    static Entry probe(const Engine& engine, Hash key) {
        return engine.probe_tt(key);
    }
    static void store(Engine& engine, const Entry& entry) {
        engine.store_tt(entry);
    }
    static void workers(Engine& engine, std::size_t count) { engine.prepare_root_workers(count); }
    static Engine& worker(Engine& engine, std::size_t i) { return *engine.root_workers.at(i); }
    static std::size_t worker_count(const Engine& engine) { return engine.root_workers.size(); }
    static const void* table(const Engine& engine) { return engine.shared_transposition.get(); }
    static int search(Engine& engine, Board& board, int depth) {
        return engine.negamax_search(board, 0, depth, NEGINF, INF);
    }
    static bool isolated_contexts(const Engine& root) {
        for (const auto& context : root.root_workers) {
            if (root.evaluator.get() == context->evaluator.get() ||
                &root.history_scores == &context->history_scores ||
                &root.killer_moves == &context->killer_moves) return false;
        }
        return true;
    }
};

void test_group_publication() {
    Engine root(0, 1), independent(0, 1);
    EngineTestAccess::workers(root, 3);
    EngineTestAccess::Entry marker{0x123456789abcdef0ULL, -391, 7, 1, 0x3ffff};
    EngineTestAccess::store(root, marker);
    for (std::size_t i = 0; i < 3; ++i) {
        auto& worker = EngineTestAccess::worker(root, i);
        const auto observed = EngineTestAccess::probe(worker, marker.key);
        require(observed.key == marker.key && observed.score == marker.score &&
                observed.best_move == marker.best_move, "root publication not visible coherently to worker");
    }
    require(EngineTestAccess::probe(independent, marker.key).key != marker.key,
            "table leaked between independent Engines");
    marker.score = 542;
    marker.depth = 8;
    EngineTestAccess::store(EngineTestAccess::worker(root, 2), marker);
    require(EngineTestAccess::probe(root, marker.key).score == marker.score,
            "worker publication not visible to root");
}

using Entry = EngineTestAccess::Entry;
void test_default_native_cas_sharing() {
    Engine engine(0, 1);
    require(EngineTestAccess::table(engine) != nullptr,
            "default Engine must own the coherent shared CAS table");
    const auto bytes = engine.get_tt_memory_bytes();
    require(bytes >= SharedTranspositionTable::kSize * 16 &&
            bytes < SharedTranspositionTable::kSize * 17,
            "default CAS table must use compact 16-byte slots");
    EngineTestAccess::workers(engine, 3);
    require(engine.get_tt_memory_bytes() == bytes,
            "default CAS workers must not allocate additional tables");
    for (std::size_t i = 0; i < 3; ++i)
        require(EngineTestAccess::table(EngineTestAccess::worker(engine, i)) ==
                EngineTestAccess::table(engine), "default worker must share the CAS table");
}

bool same(const Entry& a, const Entry& b) {
    return a.key == b.key && a.score == b.score && a.depth == b.depth &&
           a.bound == b.bound && a.best_move == b.best_move;
}

void test_full_width_records_and_replacement() {
    Engine engine(0, 1);
    for (auto key : {0ULL, 0xffffffffffffffffULL, 0x8000000000000000ULL}) {
        for (int score : {0, -1, 1, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
            for (std::uint8_t bound : {0, 1, 2}) for (std::uint32_t move : {0u, 0x3ffffu, 0x3f03fu}) {
                const Entry value{key, score, 255, bound, move};
                EngineTestAccess::store(engine, value);
                require(same(EngineTestAccess::probe(engine, key), value), "full-width entry did not roundtrip");
            }
        }
    }
    const Entry deep{0xabcdef1234500123ULL, -271, 230, 1, 0x3ffff};
    EngineTestAccess::store(engine, deep);
    Entry shallow = deep;
    shallow.score = 842; shallow.depth = 229; shallow.bound = 2; shallow.best_move = 0;
    EngineTestAccess::store(engine, shallow);
    require(same(EngineTestAccess::probe(engine, deep.key), deep), "shallower store corrupted retained bound/move witness");
    shallow.depth = deep.depth;
    EngineTestAccess::store(engine, shallow);
    require(same(EngineTestAccess::probe(engine, deep.key), shallow), "equal-depth replacement rejected");
    shallow.key ^= 1ULL << 63; shallow.depth = 0;
    EngineTestAccess::store(engine, shallow);
    require(same(EngineTestAccess::probe(engine, deep.key), shallow) &&
            EngineTestAccess::probe(engine, deep.key).key != deep.key, "full-key collision false hit or not replaced immediately");
}
void test_depth_conversion_preserves_historical_replacement() {
    Board board;
    require(board.import_fen("PPPPPPPk/PPPPPPPP/PPPPPPPP/PPPPPPPP/PPPPPPPP/PPPPPPPP/PPPPPPPP/KPPPPPPP w - 1 0 1") &&
            board.legal_moves().empty() && !board.has_game_ended(), "depth-conversion no-action fixture");
    Engine engine(0, 1);
    EngineTestAccess::store(engine, {board.get_hash(), -42, 255, 0, 0x3ffff});
    const int value = EngineTestAccess::search(engine, board, 256);
    const auto entry = EngineTestAccess::probe(engine, board.get_hash());
    require(entry.depth == 0 && entry.score == value && entry.best_move == 0,
            "replacement compared truncated depth instead of historical remaining-depth value");
}

void test_ownership_memory_and_cache_lifetime() {
    Engine engine(0, 1);
    const auto bytes = engine.get_tt_memory_bytes();
    const auto* table = EngineTestAccess::table(engine);
    EngineTestAccess::workers(engine, 3);
    require(engine.get_tt_memory_bytes() == bytes, "shared workers allocated extra tables");
    for (std::size_t i = 0; i < 3; ++i)
        require(EngineTestAccess::table(EngineTestAccess::worker(engine, i)) == table,
                "shared worker ownership incorrect");
    require(EngineTestAccess::isolated_contexts(engine), "non-TT worker state shared");
    const Entry marker{0x1234abcd98765ULL, -99, 3, 2, 0x3ffff};
    EngineTestAccess::store(engine, marker);
    engine.set_search_threads(engine.get_search_threads() + 1);
    require(EngineTestAccess::worker_count(engine) == 0 && EngineTestAccess::table(engine) == table &&
            same(EngineTestAccess::probe(engine, marker.key), marker), "thread reconfiguration discarded shared cache");
    EngineTestAccess::workers(engine, 3);
    require(engine.get_tt_memory_bytes() == bytes && EngineTestAccess::isolated_contexts(engine),
            "recreated workers changed shared table memory or context isolation");
    require(same(EngineTestAccess::probe(EngineTestAccess::worker(engine, 2), marker.key), marker),
            "recreated worker lost persistent cache");
    std::cout << "unique CAS TT bytes root/four contexts: " << bytes << '/' << engine.get_tt_memory_bytes() << '\n';
}

void test_collision_contention_coherent_witnesses() {
    const std::array<Entry, 4> witnesses{{
        {0x1000000000000321ULL, std::numeric_limits<int>::min(), 0, 0, 0},
        {0x2000000000000321ULL, std::numeric_limits<int>::max(), 255, 1, 0x3ffff},
        {0x8000000000000321ULL, -3180, 73, 2, 0x3f03f},
        {0xf000000000000321ULL, 9331, 181, 0, 0x10101}
    }};
    Engine engine(0, 1);
    EngineTestAccess::workers(engine, 3);
    EngineTestAccess::store(engine, witnesses.front());
    std::atomic<int> ready{0};
    std::atomic<bool> go{false}, coherent{true};
    std::vector<std::thread> threads;
    for (int i = 0; i < 6; ++i) threads.emplace_back([&, i] {
        ++ready;
        while (!go.load()) std::this_thread::yield();
        Engine& context = i % 4 == 0 ? engine : EngineTestAccess::worker(engine, i % 4 - 1);
        for (int n = 0; n < 30000; ++n) {
            if (i < 4) EngineTestAccess::store(context, witnesses[i]);
            const auto entry = EngineTestAccess::probe(context, witnesses[i % 4].key);
            bool valid = false;
            for (const auto& witness : witnesses) valid |= same(entry, witness);
            if (!valid) coherent.store(false);
        }
    });
    while (ready.load() != 6) std::this_thread::yield();
    go.store(true);
    for (auto& thread : threads) thread.join();
    require(coherent.load(), "contended full-key collision produced mixed key/score/depth/bound/move");
    const auto last = EngineTestAccess::probe(engine, witnesses.front().key);
    int matches = 0;
    for (const auto& witness : witnesses) matches += last.key == witness.key;
    require(matches == 1, "full-key collisions produced false matching keys");
}

Entry depth_witness(std::uint8_t depth, unsigned writer) {
    return {0xfedcba9876500456ULL, -318000 + 997 * depth + static_cast<int>(writer), depth,
            static_cast<std::uint8_t>(writer % 3), (std::uint32_t(depth) << 10) | writer};
}

void test_same_key_concurrent_depth_ordering() {
    Engine engine(0, 1);
    EngineTestAccess::workers(engine, 3);
    EngineTestAccess::store(engine, depth_witness(0, 0));
    std::atomic<int> ready{0};
    std::atomic<bool> go{false}, ordered{true}, coherent{true};
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 6; ++i) threads.emplace_back([&, i] {
        ++ready;
        while (!go.load()) std::this_thread::yield();
        Engine& context = i % 4 == 0 ? engine : EngineTestAccess::worker(engine, i % 4 - 1);
        unsigned previous = 0;
        for (unsigned n = 0; n < 30000; ++n) {
            if (i < 4) EngineTestAccess::store(context, depth_witness(static_cast<std::uint8_t>(n * 37 + i * 61), i));
            const auto entry = EngineTestAccess::probe(context, depth_witness(0, 0).key);
            if (entry.depth < previous) ordered.store(false);
            previous = entry.depth;
            bool valid = false;
            for (unsigned writer = 0; writer < 4; ++writer) valid |= same(entry, depth_witness(entry.depth, writer));
            if (!valid) coherent.store(false);
        }
    });
    while (ready.load() != 6) std::this_thread::yield();
    go.store(true);
    for (auto& thread : threads) thread.join();
    require(ordered.load(), "concurrent shallower same-key store displaced a deeper live entry");
    require(coherent.load(), "same-key equal-depth replacement mixed bound/move witnesses");
    require(EngineTestAccess::probe(engine, depth_witness(0, 0).key).depth == 255,
            "max-depth concurrent publication lost");
}

int main(int argc, char** argv) {
    try {
        test_default_native_cas_sharing();
        std::cout << "PASS default native CAS ownership and compact worker sharing\n";
        if (argc > 1) {
            const std::string test = argv[1];
            if (test == "stress-depth") test_same_key_concurrent_depth_ordering();
            else if (test == "stress-collisions") test_collision_contention_coherent_witnesses();
            else throw std::invalid_argument("unknown stress selection");
            std::cout << "PASS " << test << '\n';
            return 0;
        }

        test_group_publication();
        std::cout << "PASS shared group publication and Engine isolation\n";
        test_full_width_records_and_replacement();
        std::cout << "PASS full-width records and depth/collision replacement\n";
        test_depth_conversion_preserves_historical_replacement();
        std::cout << "PASS historical depth-conversion replacement policy\n";
        test_ownership_memory_and_cache_lifetime();
        std::cout << "PASS ownership, unique memory and cache lifetime\n";
        test_collision_contention_coherent_witnesses();
        std::cout << "PASS contended full-key collision coherent witnesses\n";
        test_same_key_concurrent_depth_ordering();
        std::cout << "PASS contended same-key depth ordering and witnesses\n";

    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
