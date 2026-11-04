#pragma once

#include "board.hpp"
#include "evaluator.hpp"
#include "transposition_table.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <atomic>
#include <memory>
#include <deque>
#include <vector>

class Engine {
    friend struct EngineTestAccess;

    enum class BoundType : std::uint8_t { Exact = 0, Lower = 1, Upper = 2 };

    static constexpr std::size_t kTTSize = SharedTranspositionTable::kSize;

    using TranspositionEntry = ::TranspositionEntry;

    static std::uint32_t encode_move(const Move& move);
    static Move decode_move(std::uint32_t packed);
    std::vector<Move> order_moves(Board& board, std::uint32_t tt_best,
        const std::array<std::uint32_t, 2>* killers = nullptr) const;

    int side;
    int default_search_depth;
    std::unique_ptr<Evaluator> evaluator;
    Move best_move{};
    std::size_t search_nodes = 0;
    std::size_t closed_window_nodes = 0;
    std::shared_ptr<SharedTranspositionTable> shared_transposition;
    TranspositionEntry probe_tt(Hash key) const;
    void store_tt(const TranspositionEntry& entry, int replacement_depth = -1);
    Engine(int side, int search_depth, std::unique_ptr<Evaluator> evaluator,
           std::shared_ptr<SharedTranspositionTable> shared_table);
    std::vector<std::array<std::uint32_t, 2>> killer_moves;
    std::array<std::array<std::array<std::uint32_t, 64>, 64>, 2> history_scores{};
    int search_threads = 1;
    // Contexts/tables persist across searches, until configuration changes or destruction.
    std::vector<std::unique_ptr<Engine>> root_workers;
    const std::atomic<bool>* parallel_abort = nullptr;
    void check_cancelled(const std::atomic<bool>* stop) const;
    void prepare_root_workers(std::size_t count);
    int parallel_root_search(const Board& position, int max_depth, Move& root_move,
                             const std::atomic<bool>* stop);

    static constexpr int kMaxQuiescencePly = 8;
    int quiescence_search(Board& board, int alpha, int beta, int capture_ply,
                           const std::atomic<bool>* stop = nullptr);

    int negamax_search(Board& board, int depth, int max_depth,
                       int alpha, int beta, Move* root_move = nullptr,
                       std::size_t action_ply = 0, const std::atomic<bool>* stop = nullptr);

    std::deque<std::pair<int,Hash>> evaluation_cache;

public:
    static constexpr int kDefaultSearchDepth = 3;

    explicit Engine(int side, int search_depth = kDefaultSearchDepth,
                    std::unique_ptr<Evaluator> evaluator = nullptr);
    Engine(int side, int search_depth, EngineConfig config);

    int get_side() const;
    int get_search_depth() const;
    void set_search_depth(int search_depth);
    // Total search participants, including the caller; configure only while idle.
    int get_search_threads() const;
    void set_search_threads(int threads);
    // Unique CAS slots + table objects, excluding allocator/shared_ptr
    // bookkeeping and Engine contexts; not process RSS.
    std::size_t get_tt_memory_bytes() const;
    const Evaluator& get_evaluator() const;
    const EngineConfig& get_config() const;

    bool is_turn(const Board& board) const;
    int evaluate(const Board& board) const;
    bool find_best_move(const Board& position, int search_depth = -1,
                        const std::atomic<bool>* stop = nullptr);
    const Move& get_best_move() const;
    std::size_t get_search_nodes() const;
    std::size_t get_closed_window_nodes() const;
};
