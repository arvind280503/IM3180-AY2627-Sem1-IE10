#include "../board.hpp"
#include "../engine.hpp"
#include "../Engines/AggressiveEvaluator.hpp"
#include "../Engines/PositionalEvaluator.hpp"

#include <iostream>
#include <algorithm>
#include <atomic>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct BoardTestAccess {
    static void empty(Board& board, int side_to_move = 0, int moves_left = 2,
                      std::uint8_t rights = 0x0F) {
        for (auto& row : board.current_board)
            std::fill(row.begin(), row.end(), 0);

        board.turn = side_to_move;
        board.move_left = moves_left;
        board.game_status = 0;
        board.halfmove_clock = 0;
        board.fullmove_number = 1;
        board.castling_rights = rights;
        board.rollback.clear();
        board.last_move = Move{};
        board.refresh_hash();
    }

    static void set_piece(Board& board, int row, int column, int piece) {
        board.current_board[row][column] = piece;
        board.refresh_hash();
    }

    static void set_rights(Board& board, std::uint8_t rights) {
        board.castling_rights = rights;
        board.refresh_hash();
    }

    static Hash recomputed_hash(const Board& board) {
        return board.calculate_hash();
    }

    static auto counters(const Board& board) {
        return std::make_pair(board.halfmove_clock, board.fullmove_number);
    }

    static void customize_patterns(Board& board) {
        board.move_pattern[2] = {{1, 0}};
        board.pawn_move_pattern[1] = {{2, 0}};
        board.pawn_capture_pattern[1] = {{2, 1}};
    }

    static bool same_tables(const Board& left, const Board& right) {
        return left.piece_hash == right.piece_hash && left.turn_hash == right.turn_hash
            && left.move_left_hash == right.move_left_hash
            && left.castling_rights_hash == right.castling_rights_hash
            && left.move_pattern == right.move_pattern
            && left.pawn_move_pattern == right.pawn_move_pattern
            && left.pawn_capture_pattern == right.pawn_capture_pattern;
    }

    static std::size_t undo_size(const Board& board) {
        return board.rollback.size();
    }

    static bool captures_only_since(const Board& board, std::size_t history_size) {
        for (std::size_t i = history_size; i < board.rollback.size(); ++i)
            if (board.rollback[i].captured_piece == 0) return false;
        return true;
    }

    static int searched_turns(const Board& board, std::size_t root_history_size) {
        int turns = 0;
        for (std::size_t i = root_history_size; i < board.rollback.size(); ++i) {
            const int next_turn = i + 1 < board.rollback.size()
                ? board.rollback[i + 1].old_turn : board.turn;
            if (board.rollback[i].old_turn != next_turn) ++turns;
        }
        return turns;
    }

    static Move first_search_move(const Board& board) {
        return board.rollback.empty() ? Move{} : board.rollback.front().move;
    }

    static void force_slot_collisions(Board& board) {
        constexpr Hash mask = ~((Hash{1} << 20) - 1);
        for (auto* table : {&board.piece_hash, &board.turn_hash,
                            &board.move_left_hash, &board.castling_rights_hash})
            for (auto& item : *table) item.second &= mask;
        board.refresh_hash();
    }
};

struct EngineTestAccess {
    static void seed_entry(Engine& engine, const Engine::TranspositionEntry& entry) {
        // Force fixture overwrites, including shallower entries on a warmed table.
        engine.store_tt(entry, 255);
    }

    static auto root_entry(const Engine& engine, const Board& board) {
        const auto entry = engine.probe_tt(board.get_hash());
        return std::array<std::uint64_t, 5>{entry.key, static_cast<std::uint64_t>(entry.score),
            entry.depth, entry.bound, entry.best_move};
    }

    static int search_score(Engine& engine, Board& board, int target,
                            int alpha, int beta, Move* root_move = nullptr) {
        return engine.negamax_search(board, 0, target, alpha, beta, root_move);
    }

    static int search_score_from_depth(Engine& engine, Board& board, int depth, int target,
                                       int alpha, int beta) {
        return engine.negamax_search(board, depth, target, alpha, beta, nullptr);
    }

    static int search_score_at_ply(Engine& engine, Board& board, int target,
                                   int alpha, int beta, std::size_t action_ply) {
        return engine.negamax_search(board, 0, target, alpha, beta, nullptr, action_ply);
    }

    static std::vector<Move> ordered(Engine& engine, Board& board, std::size_t ply = 0,
                                     const Move& tt_move = Move{}) {
        return engine.order_moves(board, Engine::encode_move(tt_move),
            ply < engine.killer_moves.size() ? &engine.killer_moves[ply] : nullptr);
    }

    static auto killers(const Engine& engine) { return engine.killer_moves; }

    static void seed_killers(Engine& engine, std::size_t ply, const Move& first,
                             const Move& second = Move{}) {
        engine.killer_moves.resize(std::max(engine.killer_moves.size(), ply + 1));
        engine.killer_moves[ply] = {Engine::encode_move(first), Engine::encode_move(second)};
    }

    static std::uint32_t history_score(const Engine& engine, int side, const Move& move) {
        const int from = move.old_x * 8 + move.old_y;
        const int to = move.new_x * 8 + move.new_y;
        return engine.history_scores[side][from][to];
    }

    static void seed_history(Engine& engine, int side, const Move& move,
                             std::uint32_t score) {
        const int from = move.old_x * 8 + move.old_y;
        const int to = move.new_x * 8 + move.new_y;
        engine.history_scores[side][from][to] = score;
    }

    static void clear_history(Engine& engine) {
        engine.history_scores = {};
    }

    static bool history_empty(const Engine& engine) {
        for (const auto& side : engine.history_scores)
            for (const auto& from : side)
                for (std::uint32_t score : from)
                    if (score != 0) return false;
        return true;
    }

    static std::uint32_t packed(const Move& move) { return Engine::encode_move(move); }

    static int quiescence_score(Engine& engine, Board& board,
                                int alpha = NEGINF, int beta = INF, int capture_ply = 0) {
        return engine.quiescence_search(board, alpha, beta, capture_ply);
    }

    static void seed_ordering_hint(Engine& engine, const Board& board, const Move& move) {
        Engine::TranspositionEntry entry{};
        entry.key = board.get_hash();
        entry.depth = 0;
        entry.best_move = Engine::encode_move(move);
        seed_entry(engine, entry);
    }

    static int quiescence_limit() { return Engine::kMaxQuiescencePly; }

    static bool tt_is_empty(const Engine& engine) {
        for (std::size_t i = 0; i < Engine::kTTSize; ++i) {
            const auto entry = engine.probe_tt(i);
            if (entry.key || entry.score || entry.depth || entry.bound || entry.best_move)
                return false;
        }
        return true;
    }

    static std::vector<Engine::TranspositionEntry> tt_snapshot(const Engine& engine) {
        std::vector<Engine::TranspositionEntry> snapshot;
        snapshot.reserve(Engine::kTTSize);
        for (std::size_t i = 0; i < Engine::kTTSize; ++i)
            snapshot.push_back(engine.probe_tt(i));
        return snapshot;
    }

    static bool tt_matches_snapshot(const Engine& engine,
                                    const std::vector<Engine::TranspositionEntry>& snapshot) {
        for (std::size_t i = 0; i < Engine::kTTSize; ++i) {
            const auto actual = engine.probe_tt(i);
            const auto& expected = snapshot[i];
            if (actual.key != expected.key || actual.score != expected.score ||
                actual.depth != expected.depth || actual.bound != expected.bound ||
                actual.best_move != expected.best_move)
                return false;
        }
        return true;
    }

    static void seed_horizon_sentinel(Engine& engine, const Board& board) {
        Engine::TranspositionEntry entry{};
        entry.key = board.get_hash();
        entry.score = 987654;
        entry.depth = 255;
        entry.bound = static_cast<std::uint8_t>(Engine::BoundType::Exact);
        entry.best_move = Engine::encode_move(Move{3, 0, 3, 4});
        seed_entry(engine, entry);
    }

    static int horizon_score(Engine& engine, Board& board,
                             int alpha = NEGINF, int beta = INF) {
        return engine.negamax_search(board, 1, 1, alpha, beta, nullptr);
    }

    static int cancellable_horizon_score(Engine& engine, Board& board,
                                         const std::atomic<bool>* stop) {
        return engine.negamax_search(board, 1, 1, NEGINF, INF, nullptr, 0, stop);
    }

    static int cancellable_quiescence_score(Engine& engine, Board& board,
                                            const std::atomic<bool>* stop) {
        return engine.quiescence_search(board, NEGINF, INF, 0, stop);
    }

    static void seed_root_lower(Engine& engine, const Board& board, const Move& witness) {
        Engine::TranspositionEntry entry{};
        entry.key = board.get_hash();
        entry.score = 100;
        entry.depth = 2;
        entry.bound = static_cast<std::uint8_t>(Engine::BoundType::Lower);
        entry.best_move = Engine::encode_move(witness);
        seed_entry(engine, entry);
    }

    static void seed_bound(Engine& engine, const Board& board, const Move& move,
                           int score, int depth, bool lower) {
        Engine::TranspositionEntry entry{};
        entry.key = board.get_hash();
        entry.score = score;
        entry.depth = static_cast<std::uint8_t>(depth);
        entry.bound = static_cast<std::uint8_t>(lower ? Engine::BoundType::Lower
                                                       : Engine::BoundType::Upper);
        entry.best_move = Engine::encode_move(move);
        seed_entry(engine, entry);
    }

    static void weaken_root_to_lower_bound(Engine& engine, const Board& board) {
        auto entry = engine.probe_tt(board.get_hash());
        if (entry.key != board.get_hash()) throw std::runtime_error("root TT key missing");
        entry.score = -100;
        entry.bound = static_cast<std::uint8_t>(Engine::BoundType::Lower);
        seed_entry(engine, entry);
    }

    static bool root_is_exact_zero(const Engine& engine, const Board& board) {
        const auto entry = engine.probe_tt(board.get_hash());
        return entry.key == board.get_hash() && entry.score == 0 && entry.depth == 1
            && entry.bound == static_cast<std::uint8_t>(Engine::BoundType::Exact)
            && entry.best_move == Engine::encode_move(engine.get_best_move());
    }

    static Move cached_move(const Engine& engine, const Board& board) {
        const auto entry = engine.probe_tt(board.get_hash());
        if (entry.key != board.get_hash()) throw std::runtime_error("root TT key missing");
        return Engine::decode_move(entry.best_move);
    }
};

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void test_move_comparisons_are_complements() {
    const auto compare = [](const Move& left, const Move& right, bool equal) {
        require((left == right) == equal && (right == left) == equal,
                "Move equality disagrees with its fields");
        require((left != right) == !equal && (right != left) == !equal,
                "Move inequality is not the complement of equality");
    };

    const Move invalid;
    compare(invalid, Move{}, true);
    compare(invalid, Move{-1, -1, -1, -1, SpecialMove::None}, true);
    compare(invalid, Move{1, 2, 3, 4}, false);

    const SpecialMove specials[] = {
        SpecialMove::None, SpecialMove::CastleKingside, SpecialMove::CastleQueenside,
        SpecialMove::PromoteQueen, SpecialMove::PromoteRook,
        SpecialMove::PromoteBishop, SpecialMove::PromoteKnight
    };
    const auto coordinates = {&Move::old_x, &Move::old_y, &Move::new_x, &Move::new_y};
    for (const auto special : specials) {
        const Move original{1, 2, 3, 4, special};
        compare(original, Move{original}, true);
        for (const auto coordinate : coordinates) {
            Move changed = original;
            ++(changed.*coordinate);
            compare(original, changed, false);
        }
        for (const auto other_special : specials) {
            Move changed = original;
            changed.special_move = other_special;
            compare(original, changed, special == other_special);
        }
    }
}

bool play(Board& board, const Move& move) {
    return board.make_move(move);
}

class FixedEvaluator final : public Evaluator {
    int score;

public:
    explicit FixedEvaluator(int score) : score(score) {}

    int evaluate(const Board&) const override { return score; }
};

bool same_board(const Board& left, const Board& right);

void test_pre_cancelled_search_does_no_work_or_tt_writes() {
    class CountingEvaluator final : public Evaluator {
        int& calls;
    public:
        explicit CountingEvaluator(int& calls) : calls(calls) {}
        int evaluate(const Board&) const override { ++calls; return 0; }
    };
    Board board;
    require(board.make_move(1, 0, 2, 0), "pre-cancellation history fixture failed");
    const Board before = board;
    int calls = 0;
    Engine engine(1, 1, std::make_unique<CountingEvaluator>(calls));
    std::atomic<bool> stop{true};
    for (bool warm : {false, true}) {
        if (warm) {
            require(engine.find_best_move(board, 1), "pre-cancellation warmup found no move");
            require(engine.get_best_move().old_x != -1, "warmup failed to publish a move");
        }
        const auto table_before = EngineTestAccess::tt_snapshot(engine);
        const int calls_before = calls;
        require(!engine.find_best_move(board, 1, &stop),
                "pre-cancelled search must return false");
        require(calls == calls_before && engine.get_search_nodes() == 0 &&
                    engine.get_closed_window_nodes() == 0,
                "pre-cancelled search performed evaluator or node work");
        require(engine.get_best_move() == Move{}, "pre-cancelled search retained a stale move");
        require(EngineTestAccess::tt_matches_snapshot(engine, table_before),
                "pre-cancelled cold/warm search changed the TT");
        require(same_board(board, before) && board.export_fen() == before.export_fen() &&
                    BoardTestAccess::undo_size(board) == 1,
                "pre-cancelled search changed the input board or undo history");
    }
    Board rolled = board, expected = before;
    rolled.rollback_move();
    expected.rollback_move();
    require(same_board(rolled, expected) && rolled.export_fen() == expected.export_fen(),
            "pre-cancelled search damaged existing undo contents");
}

class CallbackEvaluator final : public Evaluator {
    std::function<int(const Board&)> callback;
public:
    explicit CallbackEvaluator(std::function<int(const Board&)> callback)
        : callback(std::move(callback)) {}
    int evaluate(const Board& board) const override { return callback(board); }
};

void test_main_search_cancellation_stops_before_node_publication_and_recovers() {
    for (int actions : {1, 2}) {
        for (int cancel_at : {1, 5}) {
            Board board;
            require(board.import_fen("7k/6p1/8/8/3N4/8/8/K7 w - " +
                        std::to_string(actions) + " 17 42"), "mid-search fixture import failed");
            const auto imported = board.export_fen();
            require(board.make_move(board.legal_moves().front()), "mid-search history action failed");
            const Board before = board;
            const auto history_size = BoardTestAccess::undo_size(board);
            const int root_actions = board.get_moves_left();
            std::atomic<bool> stop{false};
            int calls = 0;
            bool enabled = true;
            bool saw_same_turn_actions = false;
            std::function<bool()> table_unchanged;
            Engine* pointer = nullptr;
            Engine engine(board.get_current_turn(), 1, std::make_unique<CallbackEvaluator>(
                [&](const Board& leaf) {
                    ++calls;
                    if (enabled && calls == cancel_at) {
                        saw_same_turn_actions = BoardTestAccess::undo_size(leaf) >= history_size + 2;
                        const auto snapshot = EngineTestAccess::tt_snapshot(*pointer);
                        table_unchanged = [&, snapshot] {
                            return EngineTestAccess::tt_matches_snapshot(engine, snapshot);
                        };
                        stop.store(true, std::memory_order_relaxed);
                    }
                    return 0;
                }));
            pointer = &engine;
            require(!engine.find_best_move(board, 1, &stop),
                    "mid-search cancellation must return false");
            require(calls == cancel_at && engine.get_best_move() == Move{},
                    "cancelled search evaluated another leaf or published an unfinished move");
            require(table_unchanged && table_unchanged(),
                    "an interrupted node published a TT bound after cancellation");
            if (root_actions == 2)
                require(saw_same_turn_actions, "cancellation fixture missed two same-turn actions");
            require(same_board(board, before) && board.export_fen() == before.export_fen() &&
                        BoardTestAccess::undo_size(board) == history_size,
                    "mid-search cancellation mutated input FEN/hash/undo");
            Board rolled = board, expected = before;
            rolled.rollback_move();
            expected.rollback_move();
            require(same_board(rolled, expected) && rolled.export_fen() == imported,
                    "mid-search cancellation damaged existing undo contents");
            enabled = false;
            stop.store(false, std::memory_order_relaxed);
            Engine reference(board.get_current_turn(), 1, std::make_unique<FixedEvaluator>(0));
            require(engine.find_best_move(board, 1, &stop) && reference.find_best_move(board, 1),
                    "the same Engine failed recovery after cancellation");
            require(engine.get_best_move() == reference.get_best_move() &&
                        EngineTestAccess::root_entry(engine, board) ==
                            EngineTestAccess::root_entry(reference, board),
                    "cancelled Engine recovery differs from a fresh equivalent evaluator");
        }
    }
}

void test_quiescence_cancellation_at_stand_pat_and_recursive_capture() {
    for (int cancel_at : {1, 2}) {
        Board board;
        require(board.import_fen("7k/8/8/8/R3r2r/8/8/K7 w - 2 17 42"),
                "quiescence-cancellation fixture import failed");
        const Board before = board;
        std::atomic<bool> stop{false};
        int calls = 0;
        std::size_t cancelled_history = 0;
        Engine engine(0, 1, std::make_unique<CallbackEvaluator>([&](const Board& leaf) {
            if (++calls == cancel_at) {
                cancelled_history = BoardTestAccess::undo_size(leaf);
                stop.store(true, std::memory_order_relaxed);
            }
            int material = 0;
            for (int row = 0; row < 8; ++row)
                for (int column = 0; column < 8; ++column)
                    if (std::abs(leaf.get_piece(row, column)) == 4)
                        material += leaf.get_piece(row, column) > 0 ? 500 : -500;
            return leaf.get_current_turn() == 0 ? material : -material;
        }));
        const auto table_before = EngineTestAccess::tt_snapshot(engine);
        Board isolated = board;
        bool interrupted = false;
        try { EngineTestAccess::cancellable_horizon_score(engine, isolated, &stop); }
        catch (...) { interrupted = true; }
        require(interrupted && stop.load() && calls == cancel_at,
                "quiescence failed to stop at stand-pat/recursive evaluator return");
        require(cancelled_history == static_cast<std::size_t>(cancel_at - 1),
                "quiescence cancellation missed its intended capture-chain checkpoint");
        require(EngineTestAccess::tt_matches_snapshot(engine, table_before),
                "cancelled quiescence changed the normal TT");
        require(same_board(board, before) && board.export_fen() == before.export_fen(),
                "cancelled quiescence changed the authoritative input");
    }
}

void test_cancelled_deeper_iteration_keeps_completed_root_bound_not_best_move() {
    Board board;
    require(board.import_fen("7k/6p1/8/8/3N4/8/8/K7 w - 2 17 42"),
            "iteration-cancellation fixture import failed");
    std::atomic<bool> stop{false};
    Engine* pointer = nullptr;
    std::array<std::uint64_t, 5> completed{};
    int calls_after_cancel = 0;
    Engine engine(0, 2, std::make_unique<CallbackEvaluator>([&](const Board& leaf) {
        if (stop.load(std::memory_order_relaxed)) ++calls_after_cancel;
        if (!stop.load(std::memory_order_relaxed) && BoardTestAccess::searched_turns(leaf, 0) == 2) {
            completed = EngineTestAccess::root_entry(*pointer, board);
            stop.store(true, std::memory_order_relaxed);
        }
        return 0;
    }));
    pointer = &engine;
    require(!engine.find_best_move(board, 2, &stop),
            "cancelled deeper iteration must return false, not its completed shallow move");
    require(stop.load() && completed[0] == board.get_hash() && completed[2] == 1 &&
                completed[4] != 0 && calls_after_cancel == 0,
            "iteration cancellation did not occur after a completed depth-one result");
    require(EngineTestAccess::root_entry(engine, board) == completed,
            "cancelled deeper iteration replaced the completed root TT bound");
    require(engine.get_best_move() == Move{}, "cancelled deeper iteration exposed a stale best move");
}

void test_cancellation_on_king_capture_and_no_move_evaluator_return() {
    for (int fixture : {0, 1, 2}) {
        const bool terminal = fixture == 1;
        Board board;
        BoardTestAccess::empty(board, 0, 2, 0);
        if (terminal) {
            BoardTestAccess::set_piece(board, 0, 0, 6);
            BoardTestAccess::set_piece(board, 3, 0, 4);
            BoardTestAccess::set_piece(board, 3, 4, -6);
        }
        if (fixture == 2) {
            BoardTestAccess::set_piece(board, 3, 0, 1);
            BoardTestAccess::set_piece(board, 6, 7, -1);
            require(board.legal_moves().size() == 1,
                    "last-child cancellation fixture must have exactly one legal action");
        }
        const Board before = board;
        std::atomic<bool> stop{false};
        int calls = 0;
        Engine engine(0, 1, std::make_unique<CallbackEvaluator>([&](const Board& leaf) {
            ++calls;
            require(leaf.has_game_ended() == terminal,
                    "king-capture/no-move cancellation reached the wrong evaluator branch");
            stop.store(true, std::memory_order_relaxed);
            return 123;
        }));
        const auto table_before = EngineTestAccess::tt_snapshot(engine);
        require(!engine.find_best_move(board, 1, &stop) && calls == 1 &&
                    engine.get_best_move() == Move{},
                "last-child/no-move cancellation published a result or continued evaluating");
        require(EngineTestAccess::tt_matches_snapshot(engine, table_before),
                "last-child/no-move cancellation published an incomplete root bound");
        require(same_board(board, before) && board.export_fen() == before.export_fen(),
                "last-child/no-move cancellation changed the caller's board");
    }
}

void test_quiescence_king_capture_and_terminal_entry_observe_cancellation() {
    for (bool terminal_entry : {false, true}) {
        Board board;
        require(board.import_fen("8/8/8/8/R3k3/8/8/K7 w - 2 17 42"),
                "quiescence terminal-cancellation fixture import failed");
        if (terminal_entry)
            require(board.make_move(3, 0, 3, 4), "terminal entry fixture capture failed");
        std::atomic<bool> stop{false};
        int calls = 0;
        bool saw_terminal = false;
        Engine engine(board.get_current_turn(), 1,
            std::make_unique<CallbackEvaluator>([&](const Board& leaf) {
                ++calls;
                if (leaf.has_game_ended()) {
                    saw_terminal = true;
                    stop.store(true, std::memory_order_relaxed);
                }
                return 0;
            }));
        Board isolated = board;
        bool interrupted = false;
        try { EngineTestAccess::cancellable_quiescence_score(engine, isolated, &stop); }
        catch (...) { interrupted = true; }
        require(interrupted && saw_terminal && calls == (terminal_entry ? 1 : 2),
                "quiescence king-capture/terminal entry returned a cancelled evaluator score");
        require(EngineTestAccess::tt_is_empty(engine), "terminal quiescence wrote to the TT");
    }
}

void test_evaluator_exceptions_are_not_converted_to_cancellation() {
    struct EvaluatorFailure { int marker; };
    for (bool request_stop : {false, true}) {
        Board board;
        const Board before = board;
        std::atomic<bool> stop{false};
        Engine engine(0, 1, std::make_unique<CallbackEvaluator>([&](const Board&) -> int {
            stop.store(request_stop, std::memory_order_relaxed);
            throw EvaluatorFailure{3180};
        }));
        bool propagated = false;
        try { engine.find_best_move(board, 1, &stop); }
        catch (const EvaluatorFailure& failure) { propagated = failure.marker == 3180; }
        require(propagated, "evaluator exception was swallowed or changed into cancellation");
        require(same_board(board, before) && board.export_fen() == before.export_fen(),
                "throwing evaluator changed the caller's position");
    }
}

void test_false_stop_token_matches_omitted_token_cold_and_warm() {
    for (int evaluator_kind : {0, 1, 2}) {
        auto make_evaluator = [=]() -> std::unique_ptr<Evaluator> {
            if (evaluator_kind == 1) return std::make_unique<AggressiveEvaluator>();
            if (evaluator_kind == 2) return std::make_unique<PositionalEvaluator>();
            return std::make_unique<StandardEvaluator>();
        };
        for (int actions : {1, 2}) {
            Board board;
            require(board.import_fen("7k/6p1/8/8/3N4/8/8/K7 w - " +
                        std::to_string(actions) + " 17 42"), "false-token fixture import failed");
            const Board before = board;
            Engine omitted(0, 2, make_evaluator()), disabled(0, 2, make_evaluator());
            std::atomic<bool> stop{false};
            for (int repeat = 0; repeat < 2; ++repeat) {
                require(omitted.find_best_move(board, 2) && disabled.find_best_move(board, 2, &stop),
                        "omitted/disabled-token search found no move");
                require(omitted.get_best_move() == disabled.get_best_move() &&
                            omitted.get_search_nodes() == disabled.get_search_nodes() &&
                            omitted.get_closed_window_nodes() == disabled.get_closed_window_nodes() &&
                            EngineTestAccess::root_entry(omitted, board) ==
                                EngineTestAccess::root_entry(disabled, board),
                        "false stop token changed move, score, bound, depth, or node traversal");
                require(same_board(board, before) && board.export_fen() == before.export_fen(),
                        "omitted/disabled-token search changed the input");
            }
        }
    }
}

class RootMoveRecordingEvaluator final : public Evaluator {
    std::vector<Move>& moves;
public:
    explicit RootMoveRecordingEvaluator(std::vector<Move>& moves) : moves(moves) {}
    int evaluate(const Board& board) const override {
        moves.push_back(BoardTestAccess::first_search_move(board));
        return 0;
    }
};

void test_fen_initial_export() {
    const Board board;
    require(board.export_fen() ==
                "rnbkqbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBKQBNR w KQkq 1 0 1",
            "initial variant FEN must preserve the actual king/queen layout");
}

void test_fen_import_replaces_position_at_a_new_root() {
    Board board;
    require(board.make_move(1, 0, 2, 0), "import history fixture failed");
    const std::string fen = "r2k3r/1p6/2n5/3q4/4B3/5N2/6P1/R2K3R b qK 2 17 42";
    require(board.import_fen(fen), "valid variant FEN import was rejected");
    require(board.export_fen() ==
                "r2k3r/1p6/2n5/3q4/4B3/5N2/6P1/R2K3R b Kq 2 17 42",
            "import must retain all six fields and normalize rights");
    require(board.get_piece(7, 0) == -4 && board.get_piece(0, 3) == 6 &&
                board.get_piece(3, 4) == 3 && board.get_piece(4, 3) == -5,
            "import mapped a rank or piece incorrectly");
    require(BoardTestAccess::undo_size(board) == 0 && board.get_last_move() == Move{},
            "import must clear undo history and last move");
    const auto hash = board.get_hash();
    require(hash == BoardTestAccess::recomputed_hash(board), "import hash is stale");
    const auto exported = board.export_fen();
    const auto legal = board.legal_moves();
    board.rollback_move();
    require(board.export_fen() == exported && board.get_hash() == hash,
            "rollback at imported root must be a no-op");
    Board copy;
    require(copy.import_fen(exported) && copy.get_hash() == hash && copy.legal_moves() == legal,
            "independent import must reproduce hash and ordered legal moves");
}

void test_fen_rejects_malformed_input_transactionally() {
    const std::string placement = "3k4/8/8/8/8/8/8/3K4";
    const std::vector<std::string> invalid = {
        placement + " w - 2 -1 1", placement + " w - 2 0 -1",
        placement + " w - 2 +1 1", placement + " w - 2 0 +1",
        placement + " w - 2 18446744073709551616 1",
        placement + " w - 2 0 18446744073709551616",
        placement + " w - 2 1x 1", placement + " w - 2 0 1x",
        placement + " w - 2 0 0", placement + " w - 2 0 1 extra",
        placement + " w - 2 0", "", placement,
        placement + " W - 2 0 1", placement + " x - 2 0 1",
        placement + " w KK 2 0 1", placement + " w K-K 2 0 1",
        placement + " w X 2 0 1", placement + " w - 0 0 1",
        placement + " w - 3 0 1", placement + " w - 01 0 1",
        placement + " w - - 0 1", placement + " w - e3 0 1",
        "3k4/8/8/8/8/8/3K4 w - 2 0 1",
        "3k4/8/8/8/8/8/8/8/3K4 w - 2 0 1",
        "/3k4/8/8/8/8/8/8/3K4 w - 2 0 1",
        placement + "/ w - 2 0 1",
        "3k4//8/8/8/8/8/3K4 w - 2 0 1",
        "3k4/9/8/8/8/8/8/3K4 w - 2 0 1",
        "3k4/0/8/8/8/8/8/3K4 w - 2 0 1",
        "3k4/44/8/8/8/8/8/3K4 w - 2 0 1",
        "3k4/7/8/8/8/8/8/3K4 w - 2 0 1",
        "3k4/8P/8/8/8/8/8/3K4 w - 2 0 1",
        "3k4/7X/8/8/8/8/8/3K4 w - 2 0 1"
    };
    for (const auto& fen : invalid) {
        Board board;
        require(board.make_move(0, 1, 2, 2), "invalid-import history fixture failed");
        const Board before = board;
        const auto exported = board.export_fen();
        const auto undo_size = BoardTestAccess::undo_size(board);
        require(!board.import_fen(fen), "malformed FEN accepted: " + fen);
        require(board.export_fen() == exported && board.get_hash() == before.get_hash() &&
                    board.get_last_move() == before.get_last_move() &&
                    board.get_game_status() == before.get_game_status() &&
                    BoardTestAccess::undo_size(board) == undo_size,
                "rejected import mutated state: " + fen);
        Board expected = before;
        board.rollback_move();
        expected.rollback_move();
        require(board.export_fen() == expected.export_fen() &&
                    board.get_hash() == expected.get_hash() &&
                    board.get_last_move() == expected.get_last_move(),
                "rejected import damaged existing rollback: " + fen);
    }
    Board board;
    require(board.import_fen("\t" + placement + "  w\t- 2 00017 00042\n") &&
                board.export_fen() == placement + " w - 2 17 42",
            "valid whitespace and unsigned decimal counters must normalize");
    require(board.import_fen(placement + " b - 1 18446744073709551615 18446744073709551615") &&
                board.export_fen() == placement + " b - 1 18446744073709551615 18446744073709551615",
            "uint64 maximum counters must round-trip");
}

void test_fen_quiet_action_counters_and_rollback() {
    for (int side : {0, 1}) {
        for (int actions : {1, 2}) {
            for (bool maximum : {false, true}) {
                Board board;
                const std::string placement = side == 0
                    ? "3k4/8/8/8/3N4/8/8/3K4" : "3k4/8/8/8/3n4/8/8/3K4";
                const std::string count = maximum ? "18446744073709551615" : "9";
                const std::string fen = placement + (side == 0 ? " w - " : " b - ")
                    + std::to_string(actions) + " " + count + " " + count;
                require(board.import_fen(fen), "quiet counter fixture import failed");
                const auto hash = board.get_hash();
                require(board.make_move(3, 3, 5, 4), "quiet counter move failed");
                const int next_side = actions == 1 ? 1 - side : side;
                const std::string half = maximum ? count : "10";
                const std::string full = maximum ? count : (side == 1 && actions == 1 ? "10" : "9");
                const std::string expected = (next_side == 0 ? " w - " : " b - ")
                    + std::to_string(actions == 1 ? 2 : 1) + " " + half + " " + full;
                const auto exported = board.export_fen();
                require(exported.substr(exported.find(' ')) == expected,
                        "quiet action counters must increment per action/black turn end and saturate");
                require(board.get_hash() == BoardTestAccess::recomputed_hash(board),
                        "counter update damaged incremental hash");
                board.rollback_move();
                require(board.export_fen() == fen && board.get_hash() == hash &&
                            board.get_last_move() == Move{} && BoardTestAccess::undo_size(board) == 0,
                        "rollback did not restore FEN counters");
            }
        }
    }
    Board board;
    require(board.make_move(0, 1, 2, 2), "opening quiet counter move failed");
    require(board.export_fen() ==
                "rnbkqbnr/pppppppp/8/8/8/2N5/PPPPPPPP/R1BKQBNR b KQkq 2 1 1",
            "opening white action must not increment fullmove number");
    const auto before = board.export_fen();
    require(board.make_move(7, 1, 5, 2) && board.make_move(7, 6, 5, 5),
            "black two-action counter sequence failed");
    require(board.export_fen() ==
                "r1bkqb1r/pppppppp/2n2n2/8/8/2N5/PPPPPPPP/R1BKQBNR w KQkq 2 3 2",
            "fullmove number must advance once after black's second action");
    board.rollback_move();
    board.rollback_move();
    require(board.export_fen() == before, "multi-action counter rollback failed");
}

Hash independent_fixed_hash(const Board& board) {
    Hash state = 0x31802627;
    std::array<Hash, 852> keys;
    for (auto& key : keys) {
        state += 0x9e3779b97f4a7c15ULL;
        Hash mixed = state;
        mixed = (mixed ^ (mixed >> 30)) * 0xbf58476d1ce4e5b9ULL;
        mixed = (mixed ^ (mixed >> 27)) * 0x94d049bb133111ebULL;
        key = mixed ^ (mixed >> 31);
    }
    Hash result = 0;
    for (int square = 0; square < 64; ++square)
        result ^= keys[(board.get_piece(square / 8, square % 8) + 6) * 64 + square];
    return result ^ keys[832 + board.get_current_turn()]
        ^ keys[834 + board.get_moves_left() - 1] ^ keys[836 + board.get_castling_rights()];
}

void require_fen_round_trip(Board& board, const std::string& context) {
    Board imported;
    const auto fen = board.export_fen();
    require(imported.import_fen(fen) && imported.export_fen() == fen,
            context + ": exported FEN did not round-trip");
    require(imported.get_hash() == board.get_hash() &&
                imported.get_hash() == BoardTestAccess::recomputed_hash(imported) &&
                imported.get_hash() == independent_fixed_hash(imported) &&
                imported.legal_moves() == board.legal_moves() &&
                imported.get_game_status() == board.get_game_status(),
            context + ": round-trip changed hash, legal moves, or terminal status");
}

void test_fen_pawn_capture_and_special_action_counters() {
    for (int side : {0, 1}) {
        const int sign = side == 0 ? 1 : -1;
        for (bool capture : {false, true}) {
            for (SpecialMove special : {SpecialMove::None, SpecialMove::PromoteQueen,
                                        SpecialMove::PromoteRook, SpecialMove::PromoteBishop,
                                        SpecialMove::PromoteKnight}) {
                Board board;
                BoardTestAccess::empty(board, side, 2, 0);
                BoardTestAccess::set_piece(board, 0, 3, 6);
                BoardTestAccess::set_piece(board, 7, 3, -6);
                const int from = special == SpecialMove::None ? 3 : (side == 0 ? 6 : 1);
                BoardTestAccess::set_piece(board, from, 0, sign);
                if (capture) BoardTestAccess::set_piece(board, from + sign, 1, -sign * 4);
                auto root = board.export_fen();
                root.replace(root.size() - 3, 3, "9 12");
                require(board.import_fen(root), "pawn counter fixture import failed");
                const auto hash = board.get_hash();
                const Move move{from, 0, from + sign, capture ? 1 : 0, special};
                require(board.make_move(move), "pawn/capture promotion counter move failed");
                const auto fen = board.export_fen();
                const int next_side = capture ? 1 - side : side;
                const std::string expected = (next_side == 0 ? " w - " : " b - ")
                    + std::string(capture ? "2" : "1") + " 0 "
                    + (capture && side == 1 ? "13" : "12");
                require(fen.substr(fen.find(' ')) == expected,
                        "every pawn action or capture must reset halfmove clock");
                require_fen_round_trip(board, "pawn/capture promotion");
                board.rollback_move();
                require(board.export_fen() == root && board.get_hash() == hash,
                        "pawn/capture promotion rollback lost metadata");
            }
        }
        Board board;
        BoardTestAccess::empty(board, side, 2, 0);
        BoardTestAccess::set_piece(board, 0, 3, 6);
        BoardTestAccess::set_piece(board, 7, 3, -6);
        BoardTestAccess::set_piece(board, 3, 0, sign * 4);
        BoardTestAccess::set_piece(board, 3, 2, -sign * 2);
        auto root = board.export_fen();
        root.replace(root.size() - 3, 3, "9 12");
        require(board.import_fen(root) && board.make_move(3, 0, 3, 2),
                "nonpawn capture fixture failed");
        const auto fen = board.export_fen();
        require(fen.substr(fen.find(' ')) == (side == 0 ? " b - 2 0 12" : " w - 2 0 13"),
                "nonpawn capture must reset clock and end the turn");
        require_fen_round_trip(board, "nonpawn capture");
        board.rollback_move();
        require(board.export_fen() == root, "nonpawn capture rollback lost counters");
        for (auto special : {SpecialMove::CastleKingside, SpecialMove::CastleQueenside}) {
            const std::string castle_root = "r2k3r/8/8/8/8/8/8/R2K3R"
                + std::string(side == 0 ? " w KQkq 1 9 12" : " b KQkq 1 9 12");
            require(board.import_fen(castle_root), "castle import failed");
            const int row = side == 0 ? 0 : 7;
            require(board.make_move({row, 3, row, special == SpecialMove::CastleKingside ? 5 : 1,
                                      special}), "imported castling move failed");
            const auto castled = board.export_fen();
            require(castled.substr(castled.find(' ')) ==
                        (side == 0 ? " b kq 2 10 12" : " w KQ 2 10 13"),
                    "castling must count as one quiet action and clear king rights");
            require_fen_round_trip(board, "castling");
            board.rollback_move();
            require(board.export_fen() == castle_root, "castling rollback lost FEN state");
        }
    }
}

void test_fen_terminal_king_capture_and_structural_validation() {
    for (int side : {0, 1}) {
        Board board;
        BoardTestAccess::empty(board, side, 2);
        BoardTestAccess::set_piece(board, 0, 3, 6);
        BoardTestAccess::set_piece(board, 7, 3, -6);
        const int from = side == 0 ? 6 : 1;
        const int to = side == 0 ? 7 : 0;
        BoardTestAccess::set_piece(board, from, 3, side == 0 ? 4 : -4);
        auto root = board.export_fen();
        root.replace(root.size() - 3, 3, "9 12");
        require(board.import_fen(root) && board.make_move(from, 3, to, 3),
                "terminal capture FEN fixture failed");
        require(board.get_game_status() == (side == 0 ? 1 : -1) &&
                    board.get_castling_rights() == 0x0F,
                "terminal capture fixture must retain captured king's rights");
        require_fen_round_trip(board, "terminal capture with retained rights");
        Board imported;
        require(imported.import_fen(board.export_fen()) && imported.has_game_ended() &&
                    imported.legal_moves().empty(), "one missing king must import as terminal");
        const auto terminal = imported.export_fen();
        require(!imported.make_move(0, 3, 1, 3) && imported.export_fen() == terminal,
                "terminal imported board must reject further actions without changing metadata");
        require(imported.import_fen(root) && !imported.has_game_ended(),
                "reimport with both kings must clear a terminal result");
        board.rollback_move();
        require(board.export_fen() == root && !board.has_game_ended(),
                "terminal capture rollback lost imported root");
    }
    Board board;
    require(board.import_fen("8/8/8/8/8/8/3k4/3K4 w KQkq 2 0 1") &&
                !board.has_game_ended() && board.valid_move(0, 3, 1, 3),
            "structural import must permit adjacent kings and rights without rooks");
    require(board.make_move(0, 3, 1, 3), "adjacent king capture fixture failed");
    const auto before = board.export_fen();
    const auto hash = board.get_hash();
    const auto last = board.get_last_move();
    for (const std::string placement : {"8/8/8/8/8/8/8/8", "3k4/8/8/8/8/8/8/3KK3",
                                        "3kk3/8/8/8/8/8/8/3K4", "8/8/8/8/8/8/8/3KK3",
                                        "3kk3/8/8/8/8/8/8/8"}) {
        require(!board.import_fen(placement + " w - 2 0 1"),
                "both missing or duplicate kings must be rejected");
        require(board.export_fen() == before && board.get_hash() == hash &&
                    board.get_game_status() == 1 && board.get_last_move() == last &&
                    BoardTestAccess::undo_size(board) == 1,
                "invalid king structure changed terminal state or history");
    }
    board.rollback_move();
    require(!board.has_game_ended() && board.get_piece(1, 3) == -6,
            "invalid king import damaged pre-existing terminal rollback");
}

void test_fen_metadata_is_not_position_identity() {
    for (int side : {0, 1}) {
        for (int actions : {1, 2}) {
            for (int rights = 0; rights < 16; ++rights) {
                std::string symbols;
                for (int bit = 3; bit >= 0; --bit)
                    if (rights & (1 << bit)) symbols += "KQkq"[bit];
                if (symbols.empty()) symbols = "-";
                const std::string prefix = "r2k3r/8/8/8/8/8/8/R2K3R"
                    + std::string(side == 0 ? " w " : " b ") + symbols + " "
                    + std::to_string(actions);
                Board low, high;
                require(low.import_fen(prefix + " 0 1") &&
                            high.import_fen(prefix + " 18446744073709551615 18446744073709551615"),
                        "metadata identity import failed");
                require(low.get_hash() == high.get_hash() &&
                            low.get_hash() == independent_fixed_hash(low) &&
                            high.get_hash() == independent_fixed_hash(high) &&
                            low.legal_moves() == high.legal_moves() &&
                            low.get_castling_rights() == rights,
                        "metadata affected identity or castling bits mapped incorrectly");
                require_fen_round_trip(low, "castling-rights matrix");
            }
        }
    }
    Board board;
    require(board.import_fen("3k4/8/8/8/3N4/8/8/3K4 b - 2 18446744073709551614 18446744073709551614"),
            "near-limit counter import failed");
    require(!board.make_move(3, 3, 5, 4), "wrong-side action should be rejected");
    require(board.export_fen() ==
                "3k4/8/8/8/3N4/8/8/3K4 b - 2 18446744073709551614 18446744073709551614",
            "rejected action changed counters");
    require(board.import_fen("3k4/8/8/8/3n4/8/8/3K4 b - 2 18446744073709551614 18446744073709551614") &&
                board.make_move(3, 3, 5, 4) && board.make_move(5, 4, 3, 3),
            "near-limit quiet sequence failed");
    require(board.export_fen() ==
                "3k4/8/8/8/3n4/8/8/3K4 w - 2 18446744073709551615 18446744073709551615",
            "counters must reach maximum then saturate");
}

void test_fen_import_preserves_existing_hash_and_rule_tables() {
    Board board;
    BoardTestAccess::force_slot_collisions(board);
    BoardTestAccess::customize_patterns(board);
    const Board before = board;
    const std::string fen = "3k4/8/8/8/3N4/8/P7/3K4 w - 2 8 9";
    require(board.import_fen(fen) && BoardTestAccess::same_tables(board, before),
            "import must not replace existing hash or movement-pattern maps");
    require(board.get_hash() == BoardTestAccess::recomputed_hash(board) &&
                (board.get_hash() & ((Hash{1} << 20) - 1)) == 0 &&
                board.get_hash() != independent_fixed_hash(board),
            "import must recompute within the existing altered hash space");
    const auto legal = board.legal_moves();
    require(board.valid_move(3, 3, 4, 3) && !board.valid_move(3, 3, 5, 4) &&
                board.valid_move(1, 0, 3, 0) && !board.valid_move(1, 0, 2, 0),
            "import reset custom movement patterns");
    const auto hash = board.get_hash();
    require(board.make_move(3, 3, 4, 3) &&
                board.get_hash() == BoardTestAccess::recomputed_hash(board),
            "altered-space incremental hash failed after import");
    board.rollback_move();
    require(board.export_fen() == fen && board.get_hash() == hash && board.legal_moves() == legal,
            "altered-space rollback failed after import");
    require(!board.import_fen("bad") && BoardTestAccess::same_tables(board, before),
            "failed import changed existing hash/rule tables");
}

void test_fen_reachable_sequence_round_trips_and_reimports() {
    Board board, imported;
    std::vector<std::string> previous;
    std::uint32_t selection = 0x3180;
    for (int action = 0; action < 64; ++action) {
        require_fen_round_trip(board, "reachable FEN action " + std::to_string(action));
        const auto fen = board.export_fen();
        require(imported.import_fen(fen) && imported.get_last_move() == Move{} &&
                    BoardTestAccess::undo_size(imported) == 0,
                "repeated import must establish a fresh root");
        const auto legal = board.legal_moves();
        if (legal.empty()) break;
        selection = selection * 1664525u + 1013904223u;
        const Move move = legal[selection % legal.size()];
        previous.push_back(fen);
        require(board.make_move(move) && imported.make_move(move) &&
                    board.export_fen() == imported.export_fen() &&
                    board.get_hash() == imported.get_hash(),
                "round-tripped position did not execute the same action");
    }
    while (!previous.empty()) {
        board.rollback_move();
        require(board.export_fen() == previous.back(), "reachable FEN undo did not restore all fields");
        require_fen_round_trip(board, "reachable FEN rollback");
        previous.pop_back();
    }
}

void test_fen_imported_metadata_survives_search_and_existing_undo() {
    for (const std::string counters : {"17 42", "18446744073709551614 18446744073709551614"}) {
        Board board;
        const std::string root = "3k4/8/8/2r5/2R5/8/8/3K4 w - 2 " + counters;
        require(board.import_fen(root), "imported search metadata fixture failed");
        require(board.make_move(0, 3, 0, 4), "first search fixture action failed");
        const Board first = board;
        require(board.make_move(0, 4, 0, 5), "second search fixture action failed");
        const Board before = board;
        const auto fen = board.export_fen();
        auto require_preserved = [&] {
            require(same_board(board, before) && board.export_fen() == fen &&
                        BoardTestAccess::undo_size(board) == 2,
                    "search changed imported metadata, hash, last move or existing undo");
        };
        Engine tactical(board.get_current_turn(), 1);
        EngineTestAccess::quiescence_score(tactical, board);
        require_preserved();
        Engine direct(board.get_current_turn(), 1);
        Move move;
        EngineTestAccess::search_score(direct, board, 1, NEGINF, INF, &move);
        require_preserved();
        Board checked = board;
        require(checked.make_move(move), "direct search from imported metadata returned invalid move");
        Engine iterative(board.get_current_turn(), 2);
        for (int repeat = 0; repeat < 2; ++repeat) {
            require(iterative.find_best_move(board) && iterative.get_search_nodes() != 0,
                    "cold/warm imported metadata search failed");
            require_preserved();
            Board selected = board;
            require(selected.make_move(iterative.get_best_move()),
                    "iterative search from imported metadata returned invalid move");
        }
        board.rollback_move();
        require(same_board(board, first) && board.export_fen() == first.export_fen(),
                "search damaged first existing undo entry");
        board.rollback_move();
        require(board.export_fen() == root && board.get_last_move() == Move{} &&
                    BoardTestAccess::undo_size(board) == 0,
                "search damaged imported root undo");
    }
}

void test_fen_binary_input_rejection_preserves_imported_undo() {
    Board board;
    const std::string root = "3k4/8/8/8/8/8/8/3K4 w - 2 17 42";
    require(board.import_fen(root) && board.make_move(0, 3, 0, 4),
            "binary input fixture import/first action failed");
    const Board first = board;
    require(board.make_move(0, 4, 0, 5), "binary input fixture second action failed");
    const Board before = board;
    std::vector<std::string> invalid;
    for (char byte : {'\0', static_cast<char>(0xFF)}) {
        auto placement = root;
        placement[0] = byte;
        invalid.push_back(placement);
        invalid.push_back(root + byte);
        auto counter = root;
        counter.insert(counter.size() - 2, 1, byte);
        invalid.push_back(counter);
    }
    invalid.push_back(std::string(4096, '8') + "/8/8/8/8/8/8/3K4 w - 2 0 1");
    for (const auto& fen : invalid) {
        require(!board.import_fen(fen), "malformed binary/oversized rank input accepted");
        require(same_board(board, before) && board.export_fen() == before.export_fen() &&
                    BoardTestAccess::undo_size(board) == 2,
                "malformed bytes changed imported position or undo history");
    }
    board.rollback_move();
    require(same_board(board, first), "malformed bytes damaged first undo entry");
    board.rollback_move();
    require(board.export_fen() == root && board.get_last_move() == Move{},
            "malformed bytes damaged imported root undo");
}

void test_quiet_cutoff_orders_subsequent_same_ply_sibling() {
    Board board;
    BoardTestAccess::empty(board, 0, 1, 0);
    BoardTestAccess::set_piece(board, 3, 3, 2);
    BoardTestAccess::set_piece(board, 6, 7, -1);
    const Move cutoff = board.legal_moves().back();
    std::vector<Move> searched;
    Engine engine(0, 1, std::make_unique<RootMoveRecordingEvaluator>(searched));
    EngineTestAccess::seed_ordering_hint(engine, board, cutoff);
    require(EngineTestAccess::search_score(engine, board, 1, NEGINF, 0) == 0,
            "quiet cutoff fixture has the wrong score");
    require(!searched.empty() && searched.front() == cutoff,
            "TT hint did not drive the quiet cutoff fixture");
    Board sibling = board;
    BoardTestAccess::set_piece(sibling, 6, 7, 0);
    BoardTestAccess::set_piece(sibling, 6, 6, -1);
    require(sibling.legal_moves().front() != cutoff,
            "killer fixture already has the cutoff move first");
    searched.clear();
    EngineTestAccess::search_score(engine, sibling, 1, NEGINF, 0);
    require(!searched.empty() && searched.front() == cutoff,
            "quiet cutoff was not searched first in a subsequent same-ply sibling");
}

void test_killer_recency_duplicate_slots_and_ply_isolation() {
    Board board;
    BoardTestAccess::empty(board, 0, 1, 0);
    BoardTestAccess::set_piece(board, 3, 3, 2);
    const auto legal = board.legal_moves();
    const Move a = legal[legal.size() - 1];
    const Move b = legal[legal.size() - 2];
    const Move c = legal[legal.size() - 3];
    Engine engine(0, 1, std::make_unique<FixedEvaluator>(0));
    auto cutoff = [&](const Move& move, std::size_t ply = 0) {
        EngineTestAccess::seed_ordering_hint(engine, board, move);
        require(EngineTestAccess::search_score_at_ply(engine, board, 1, NEGINF, 0, ply) == 0,
                "recency fixture failed to cut off");
    };
    cutoff(a);
    cutoff(b);
    cutoff(b);
    require(EngineTestAccess::killers(engine)[0] == std::array<std::uint32_t, 2>{
                EngineTestAccess::packed(b), EngineTestAccess::packed(a)},
            "duplicate cutoff must preserve the distinct second killer");
    cutoff(a);
    cutoff(c);
    require(EngineTestAccess::killers(engine)[0] == std::array<std::uint32_t, 2>{
                EngineTestAccess::packed(c), EngineTestAccess::packed(a)},
            "killers must move to front and evict the least recent distinct move");
    EngineTestAccess::clear_history(engine);
    std::vector<Move> expected{c, a};
    for (const Move& move : legal) if (move != c && move != a) expected.push_back(move);
    require(EngineTestAccess::ordered(engine, board) == expected,
            "first/second killers or remaining quiet stability are wrong");
    require(EngineTestAccess::ordered(engine, board, 1) == legal,
            "a killer leaked into another action ply");
    const auto before = EngineTestAccess::killers(engine);
    require(EngineTestAccess::ordered(engine, board, 4096) == legal &&
                EngineTestAccess::killers(engine) == before,
            "unallocated killer reads must be safe and must not grow storage");
    cutoff(b, 4096);
    EngineTestAccess::clear_history(engine);
    require(EngineTestAccess::killers(engine).size() == 4097 &&
                EngineTestAccess::ordered(engine, board, 4096).front() == b &&
                EngineTestAccess::ordered(engine, board) == expected,
            "killer recording must grow lazily without a fixed ply cap or cross-ply mutation");
}

void test_recursive_killers_use_true_action_ply() {
    for (int actions : {1, 2}) {
        Board board;
        BoardTestAccess::empty(board, 0, actions, 0);
        BoardTestAccess::set_piece(board, 3, 3, 1);
        BoardTestAccess::set_piece(board, 6, 7, -1);
        const Board before = board;
        Engine engine(0, 2, std::make_unique<FixedEvaluator>(0));
        EngineTestAccess::search_score(engine, board, actions == 2 ? 1 : 2,
                                      actions == 2 ? -1 : 0, actions == 2 ? 0 : 1);
        const auto killers = EngineTestAccess::killers(engine);
        require(killers.size() == static_cast<std::size_t>(actions == 2 ? 2 : 3),
                "recursive cutoff used completed-turn depth instead of true action ply");
        if (actions == 2) {
            require(killers[0][0] == EngineTestAccess::packed(Move{3, 3, 4, 3}) &&
                        killers[1][0] == EngineTestAccess::packed(Move{4, 3, 5, 3}),
                    "same-turn first/second actions must occupy separate killer plies");
        } else {
            require(killers[0][0] == 0 &&
                        killers[1][0] == EngineTestAccess::packed(Move{6, 7, 5, 7}) &&
                        killers[2][0] == EngineTestAccess::packed(Move{5, 7, 4, 7}),
                    "turn-changing action must advance ply before the opponent's two actions");
        }
        require(same_board(board, before) && BoardTestAccess::undo_size(board) == 0,
                "recursive killer fixture did not restore its root");
    }
}

void test_public_root_clears_killers_before_ordering() {
    Board board;
    BoardTestAccess::empty(board, 0, 1, 0);
    BoardTestAccess::set_piece(board, 3, 3, 2);
    BoardTestAccess::set_piece(board, 6, 7, -1);
    const auto legal = board.legal_moves();
    std::vector<Move> searched;
    Engine engine(0, 1, std::make_unique<RootMoveRecordingEvaluator>(searched));
    EngineTestAccess::seed_killers(engine, 0, legal.back());
    EngineTestAccess::seed_killers(engine, 4096, legal.back());
    require(engine.find_best_move(board, 1), "root-reset fixture found no move");
    require(!searched.empty() && searched.front() == legal.front() &&
                EngineTestAccess::killers(engine).empty(),
            "public root must discard prior killers before ordering, not after search");
    EngineTestAccess::seed_killers(engine, 3, legal.back());
    Board wrong_turn;
    BoardTestAccess::empty(wrong_turn, 1, 2, 0);
    require(!engine.find_best_move(wrong_turn) && EngineTestAccess::killers(engine).empty(),
            "wrong-turn public root must also clear prior killer hints");
}

void test_iterative_deepening_retains_killers_between_iterations() {
    class RetentionEvaluator final : public Evaluator {
        Engine*& engine;
        bool& observed;
        bool& retained;
    public:
        RetentionEvaluator(Engine*& engine, bool& observed, bool& retained)
            : engine(engine), observed(observed), retained(retained) {}
        int evaluate(const Board& board) const override {
            if (!observed && BoardTestAccess::searched_turns(board, 0) == 3) {
                observed = true;
                const auto killers = EngineTestAccess::killers(*engine);
                retained = std::any_of(killers.begin(), killers.end(), [](const auto& slots) {
                    return slots[0] != 0;
                });
            }
            return 0;
        }
    };
    Board board;
    BoardTestAccess::empty(board, 0, 1, 0);
    BoardTestAccess::set_piece(board, 3, 3, 2);
    BoardTestAccess::set_piece(board, 6, 7, -1);
    Engine* pointer = nullptr;
    bool observed = false, retained = false;
    Engine engine(0, 3, std::make_unique<RetentionEvaluator>(pointer, observed, retained));
    pointer = &engine;
    require(engine.find_best_move(board, 3), "iterative-retention fixture found no move");
    require(observed && retained,
            "first depth-three leaf must still see killers recorded by shallower iterations");
}

void test_public_root_clears_history_before_ordering() {
    Board board;
    BoardTestAccess::empty(board, 0, 1, 0);
    BoardTestAccess::set_piece(board, 3, 3, 2);
    BoardTestAccess::set_piece(board, 6, 7, -1);
    const auto legal = board.legal_moves();
    std::vector<Move> searched;
    Engine engine(0, 1, std::make_unique<RootMoveRecordingEvaluator>(searched));
    EngineTestAccess::seed_history(engine, 0, legal.back(), 100);
    require(engine.find_best_move(board, 1), "history root-reset fixture found no move");
    require(!searched.empty() && searched.front() == legal.front() &&
                EngineTestAccess::history_empty(engine),
            "public root must discard prior history before ordering, not after search");

    EngineTestAccess::seed_history(engine, 0, legal.back(), 100);
    Board wrong_turn;
    BoardTestAccess::empty(wrong_turn, 1, 2, 0);
    require(!engine.find_best_move(wrong_turn) && EngineTestAccess::history_empty(engine),
            "wrong-turn public root must also clear prior history scores");
}

void test_iterative_deepening_retains_history_between_iterations() {
    class RetentionEvaluator final : public Evaluator {
        Engine*& engine;
        bool& observed;
        bool& retained;
    public:
        RetentionEvaluator(Engine*& engine, bool& observed, bool& retained)
            : engine(engine), observed(observed), retained(retained) {}
        int evaluate(const Board& board) const override {
            if (!observed && BoardTestAccess::searched_turns(board, 0) == 3) {
                observed = true;
                retained = !EngineTestAccess::history_empty(*engine);
            }
            return 0;
        }
    };
    Board board;
    BoardTestAccess::empty(board, 0, 1, 0);
    BoardTestAccess::set_piece(board, 3, 3, 2);
    BoardTestAccess::set_piece(board, 6, 7, -1);
    Engine* pointer = nullptr;
    bool observed = false, retained = false;
    Engine engine(0, 3, std::make_unique<RetentionEvaluator>(pointer, observed, retained));
    pointer = &engine;
    require(engine.find_best_move(board, 3), "history iterative-retention fixture found no move");
    require(observed && retained,
            "first depth-three leaf must see history learned by shallower iterations");
}

void test_cutoff_heuristics_exclude_captures_and_promotions() {
    for (int side : {0, 1}) {
        const int sign = side == 0 ? 1 : -1;
        for (const auto promotion : {SpecialMove::PromoteQueen, SpecialMove::PromoteRook,
                                     SpecialMove::PromoteBishop, SpecialMove::PromoteKnight}) {
            for (bool capture : {false, true}) {
                Board board;
                BoardTestAccess::empty(board, side, capture ? 2 : 1, 0);
                const int from = side == 0 ? 6 : 1, to = side == 0 ? 7 : 0;
                BoardTestAccess::set_piece(board, from, 3, sign);
                if (capture) BoardTestAccess::set_piece(board, to, 4, -sign * 4);
                const Move cutoff{from, 3, to, capture ? 4 : 3, promotion};
                Engine engine(side, 1, std::make_unique<FixedEvaluator>(0));
                EngineTestAccess::seed_ordering_hint(engine, board, cutoff);
                require(EngineTestAccess::search_score(engine, board, 1, -1, 0) == 0,
                        "promotion cutoff fixture has the wrong score");
                require(EngineTestAccess::killers(engine).empty() &&
                            EngineTestAccess::history_empty(engine),
                        "quiet or capture promotion was recorded as a killer or history move");
                const auto normal = EngineTestAccess::ordered(engine, board);
                EngineTestAccess::seed_killers(engine, 0, cutoff);
                EngineTestAccess::seed_history(engine, side, cutoff, 100);
                require(EngineTestAccess::ordered(engine, board) == normal,
                        "a stale promotion hint was promoted by killer or history ordering");
            }
        }
        Board board;
        BoardTestAccess::empty(board, side, 2, 0);
        BoardTestAccess::set_piece(board, 3, 3, sign * 2);
        BoardTestAccess::set_piece(board, 5, 4, -sign);
        const Move capture{3, 3, 5, 4};
        Engine engine(side, 1, std::make_unique<FixedEvaluator>(0));
        EngineTestAccess::seed_killers(engine, 0, Move{3, 3, 1, 2});
        const auto before = EngineTestAccess::killers(engine);
        EngineTestAccess::seed_ordering_hint(engine, board, capture);
        require(EngineTestAccess::search_score(engine, board, 1, -1, 0) == 0 &&
                    EngineTestAccess::killers(engine) == before &&
                    EngineTestAccess::history_empty(engine),
                "ordinary capture cutoff changed killer slots or history scores");
    }
}

void test_quiet_castling_can_receive_cutoff_ordering_credit() {
    Board board;
    BoardTestAccess::empty(board, 0, 1);
    BoardTestAccess::set_piece(board, 0, 3, 6);
    BoardTestAccess::set_piece(board, 0, 7, 4);
    const Move castle{0, 3, 0, 5, SpecialMove::CastleKingside};
    Engine engine(0, 1, std::make_unique<FixedEvaluator>(0));
    EngineTestAccess::seed_ordering_hint(engine, board, castle);
    EngineTestAccess::search_score(engine, board, 1, -1, 0);
    require(EngineTestAccess::killers(engine).size() == 1 &&
                EngineTestAccess::ordered(engine, board).front() == castle &&
                EngineTestAccess::history_score(engine, 0, castle) == 1,
            "quiet castling cutoff must be eligible for killer and history ordering");
}

void test_stale_killer_hints_do_not_promote_illegal_or_capturing_moves() {
    Board board;
    BoardTestAccess::empty(board, 0, 2, 0);
    BoardTestAccess::set_piece(board, 3, 3, 6);
    BoardTestAccess::set_piece(board, 2, 2, 2);
    BoardTestAccess::set_piece(board, 4, 3, -4);
    const Move king_capture{3, 3, 4, 3};
    Engine engine(0, 1, std::make_unique<FixedEvaluator>(0));
    const auto normal = EngineTestAccess::ordered(engine, board);
    EngineTestAccess::seed_killers(engine, 0, king_capture, Move{2, 2, 7, 7});
    require(EngineTestAccess::ordered(engine, board) == normal,
            "stale capturing/illegal killers must not change normal ordering");
    const Move quiet = normal.back();
    EngineTestAccess::seed_killers(engine, 0, Move{2, 2, 7, 7}, quiet);
    auto expected = normal;
    const auto place = std::find(expected.begin(), expected.end(), quiet);
    const auto first_quiet = std::find_if(expected.begin(), expected.end(), [&](const Move& move) {
        return board.get_piece(move.new_x, move.new_y) == 0;
    });
    std::rotate(first_quiet, place, place + 1);
    require(EngineTestAccess::ordered(engine, board) == expected,
            "illegal first killer must not suppress a valid second killer or add an illegal move");
}

void test_tt_capture_killer_precedence_and_stability() {
    Board board;
    BoardTestAccess::empty(board, 0, 2, 0);
    BoardTestAccess::set_piece(board, 3, 3, 6);
    BoardTestAccess::set_piece(board, 2, 2, 2);
    BoardTestAccess::set_piece(board, 4, 3, -4);
    BoardTestAccess::set_piece(board, 4, 1, -4);
    const auto legal = board.legal_moves();
    std::vector<Move> captures, quiet;
    for (const Move& move : legal)
        (board.get_piece(move.new_x, move.new_y) ? captures : quiet).push_back(move);
    constexpr int values[] = {0, 1, 3, 3, 5, 9, 99999};
    auto mvv = [&](const Move& move) {
        return values[std::abs(board.get_piece(move.new_x, move.new_y))] * 8 -
            values[std::abs(board.get_piece(move.old_x, move.old_y))];
    };
    std::stable_sort(captures.begin(), captures.end(), [&](const Move& a, const Move& b) {
        return mvv(a) > mvv(b);
    });
    require(captures.size() >= 3 && mvv(captures.back()) < 0 && quiet.size() >= 3,
            "ordering fixture must include equal captures and a negative-MVV king capture");
    const Move first = quiet.back(), second = quiet[quiet.size() - 2], tt = quiet[1];
    Engine engine(0, 1, std::make_unique<FixedEvaluator>(0));
    EngineTestAccess::seed_killers(engine, 0, first, second);
    auto expected = captures;
    expected.push_back(first);
    expected.push_back(second);
    for (const Move& move : quiet) if (move != first && move != second) expected.push_back(move);
    require(EngineTestAccess::ordered(engine, board) == expected,
            "captures must precede both killers even with negative MVV; ties and rest must be stable");
    for (const Move& hint : {tt, second, captures.back()}) {
        auto with_tt = expected;
        const auto place = std::find(with_tt.begin(), with_tt.end(), hint);
        std::rotate(with_tt.begin(), place, place + 1);
        require(EngineTestAccess::ordered(engine, board, 0, hint) == with_tt,
                "TT move must lead without displacing capture/killer/rest relative ordering");
    }
    require(EngineTestAccess::ordered(engine, board, 0, Move{2, 2, 7, 7}) == expected,
            "illegal TT hint changed ordered legal moves");
}

void test_history_orders_only_remaining_quiet_moves() {
    Board board;
    BoardTestAccess::empty(board, 0, 2, 0);
    BoardTestAccess::set_piece(board, 3, 3, 6);
    BoardTestAccess::set_piece(board, 2, 2, 2);
    BoardTestAccess::set_piece(board, 4, 3, -4);
    BoardTestAccess::set_piece(board, 4, 1, -4);
    const auto legal = board.legal_moves();
    std::vector<Move> captures, quiet;
    for (const Move& move : legal)
        (board.get_piece(move.new_x, move.new_y) ? captures : quiet).push_back(move);
    constexpr int values[] = {0, 1, 3, 3, 5, 9, 99999};
    auto mvv = [&](const Move& move) {
        return values[std::abs(board.get_piece(move.new_x, move.new_y))] * 8 -
            values[std::abs(board.get_piece(move.old_x, move.old_y))];
    };
    std::stable_sort(captures.begin(), captures.end(), [&](const Move& a, const Move& b) {
        return mvv(a) > mvv(b);
    });
    require(captures.size() >= 3 && quiet.size() >= 7,
            "history ordering fixture lacks enough capture or quiet moves");

    const Move first_killer = quiet.back();
    const Move second_killer = quiet[quiet.size() - 2];
    std::vector<Move> remaining;
    for (const Move& move : quiet)
        if (move != first_killer && move != second_killer) remaining.push_back(move);
    const Move high = remaining.back();
    const Move tie_a = remaining[1];
    const Move tie_b = remaining[remaining.size() - 2];
    require(high != tie_a && high != tie_b && tie_a != tie_b,
            "history ordering fixture did not select distinct quiet moves");

    Engine engine(0, 1, std::make_unique<FixedEvaluator>(0));
    EngineTestAccess::seed_killers(engine, 0, first_killer, second_killer);
    EngineTestAccess::seed_history(engine, 0, high, 9);
    EngineTestAccess::seed_history(engine, 0, tie_a, 5);
    EngineTestAccess::seed_history(engine, 0, tie_b, 5);
    EngineTestAccess::seed_history(engine, 1, remaining.front(), 1000);

    auto expected = captures;
    expected.push_back(first_killer);
    expected.push_back(second_killer);
    expected.push_back(high);
    for (const Move& move : remaining)
        if (move == tie_a || move == tie_b) expected.push_back(move);
    for (const Move& move : remaining)
        if (move != high && move != tie_a && move != tie_b) expected.push_back(move);
    require(EngineTestAccess::ordered(engine, board) == expected,
            "history must rank only same-side non-killer quiet moves with stable ties");

    const Move tt = tie_b;
    auto with_tt = expected;
    const auto place = std::find(with_tt.begin(), with_tt.end(), tt);
    std::rotate(with_tt.begin(), place, place + 1);
    require(EngineTestAccess::ordered(engine, board, 0, tt) == with_tt,
            "TT move must lead without disturbing capture, killer, or history order");
}

void test_history_credits_only_the_current_searched_cutoff_move() {
    for (int side : {0, 1}) {
        Board board;
        BoardTestAccess::empty(board, side, 1, 0);
        BoardTestAccess::set_piece(board, 3, 3, side == 0 ? 2 : -2);
        const Board before = board;
        const auto legal = board.legal_moves();
        require(legal.size() >= 3, "history cutoff fixture lacks quiet alternatives");
        const Move cutoff = legal.back();

        Engine engine(side, 3, std::make_unique<FixedEvaluator>(0));
        EngineTestAccess::seed_ordering_hint(engine, board, cutoff);
        require(EngineTestAccess::search_score(engine, board, 3, -1, 0) == 0,
                "history cutoff fixture did not close its search window");
        require(EngineTestAccess::history_score(engine, side, cutoff) == 9,
                "current quiet cutoff move did not receive remaining-depth-squared credit");
        require(EngineTestAccess::history_score(engine, 1 - side, cutoff) == 0,
                "history cutoff credit was stored under the wrong moving side");
        for (const Move& move : legal)
            if (move != cutoff)
                require(EngineTestAccess::history_score(engine, side, move) == 0,
                        "a searched or unsearched non-cutoff sibling received history credit");

        require(EngineTestAccess::search_score(engine, board, 3, -1, 0) == 0 &&
                    EngineTestAccess::history_score(engine, side, cutoff) == 18,
                "a qualifying TT cutoff must reward its quiet move with the same depth-squared credit");
        require(same_board(board, before) && BoardTestAccess::undo_size(board) == 0,
                "history cutoff learning changed its input position");

        Engine no_cutoff(side, 1, std::make_unique<FixedEvaluator>(0));
        require(EngineTestAccess::search_score(no_cutoff, board, 1, NEGINF, INF) == 0,
                "full-window history fixture returned an unexpected score");
        for (const Move& move : legal)
            require(EngineTestAccess::history_score(no_cutoff, side, move) == 0,
                    "an alpha improvement without a beta cutoff received history credit");
    }
}

void test_cached_history_uses_requested_depth_and_requires_lower_witness() {
    for (int side : {0, 1}) {
        Board board;
        BoardTestAccess::empty(board, side, 1, 0);
        BoardTestAccess::set_piece(board, 3, 3, side == 0 ? 2 : -2);
        const auto legal = board.legal_moves();
        const Move move = legal.back();
        const Board before = board;
        Engine engine(side, 3, std::make_unique<FixedEvaluator>(0));
        EngineTestAccess::seed_bound(engine, board, move, 0, 5, true);
        require(EngineTestAccess::search_score_from_depth(engine, board, 1, 3, -1, 0) == 0 &&
                    EngineTestAccess::history_score(engine, side, move) == 4,
                "cached cutoff bonus must use current remaining depth, not stored depth");
        require(same_board(board, before), "TT history reward changed the board");

        Engine upper(side, 1, std::make_unique<FixedEvaluator>(0));
        EngineTestAccess::seed_bound(upper, board, move, -1, 5, false);
        require(EngineTestAccess::search_score(upper, board, 1, -1, 0) == -1 &&
                    EngineTestAccess::history_empty(upper),
                "upper-bound TT cutoff must not reward its stored move");
        Move upper_root_move;
        EngineTestAccess::search_score(upper, board, 1, -1, 0, &upper_root_move);
        require(EngineTestAccess::history_empty(upper),
                "a root with an already-closed upper-bound window must not reward a searched move");

        Engine illegal(side, 1, std::make_unique<FixedEvaluator>(0));
        EngineTestAccess::seed_bound(illegal, board, Move{0, 0, 7, 7}, 0, 5, true);
        require(EngineTestAccess::search_score(illegal, board, 1, -1, 0) == 0 &&
                    EngineTestAccess::history_empty(illegal),
                "illegal cached move must not receive history credit");

        Engine root(side, 1, std::make_unique<FixedEvaluator>(0));
        EngineTestAccess::seed_bound(root, board, move, 0, 5, true);
        Move root_move;
        EngineTestAccess::search_score(root, board, 1, -1, 0, &root_move);
        require(EngineTestAccess::history_score(root, side, move) == 1,
                "closed-window root must not award a second searched-move bonus");

        Engine saturated(side, 1, std::make_unique<FixedEvaluator>(0));
        EngineTestAccess::seed_bound(saturated, board, move, 0, 5, true);
        constexpr auto limit = std::numeric_limits<std::uint32_t>::max();
        EngineTestAccess::seed_history(saturated, side, move, limit);
        EngineTestAccess::search_score(saturated, board, 1, -1, 0);
        require(EngineTestAccess::history_score(saturated, side, move) == limit,
                "cached history reward must saturate without wrapping");
    }
}

void test_history_score_saturates_without_overflow() {
    Board board;
    BoardTestAccess::empty(board, 0, 1, 0);
    BoardTestAccess::set_piece(board, 3, 3, 2);
    const Move cutoff = board.legal_moves().back();
    constexpr auto limit = std::numeric_limits<std::uint32_t>::max();

    Engine near_limit(0, 3, std::make_unique<FixedEvaluator>(0));
    EngineTestAccess::seed_ordering_hint(near_limit, board, cutoff);
    EngineTestAccess::seed_history(near_limit, 0, cutoff, limit - 1);
    require(EngineTestAccess::search_score(near_limit, board, 3, -1, 0) == 0 &&
                EngineTestAccess::history_score(near_limit, 0, cutoff) == limit,
            "history accumulation must saturate instead of wrapping near its limit");

    Engine wide_bonus(0, 1, std::make_unique<FixedEvaluator>(0));
    EngineTestAccess::seed_ordering_hint(wide_bonus, board, cutoff);
    require(EngineTestAccess::search_score(wide_bonus, board, 70000, -1, 0) == 0 &&
                EngineTestAccess::history_score(wide_bonus, 0, cutoff) == limit,
            "history depth square must widen and clamp before multiplication overflows");
}

void test_history_uses_completed_turn_depth_for_same_turn_actions() {
    for (int side : {0, 1}) {
        Board board;
        BoardTestAccess::empty(board, side, 2, 0);
        BoardTestAccess::set_piece(board, 3, 3, side == 0 ? 2 : -2);
        const Board before = board;
        const Move first = board.legal_moves().back();
        Board child = board;
        require(child.make_move(first) && child.get_current_turn() == side,
                "same-turn history fixture did not preserve the moving side");
        const Move second = child.legal_moves().front();

        Engine engine(side, 2, std::make_unique<FixedEvaluator>(0));
        EngineTestAccess::seed_ordering_hint(engine, board, first);
        require(EngineTestAccess::search_score(engine, board, 2, -1, 0) == 0,
                "same-turn history fixture did not close its search window");
        require(EngineTestAccess::history_score(engine, side, first) == 4 &&
                    EngineTestAccess::history_score(engine, side, second) == 4,
                "both same-turn cutoff actions must use the current completed-turn depth");
        require(EngineTestAccess::history_score(engine, 1 - side, first) == 0 &&
                    EngineTestAccess::history_score(engine, 1 - side, second) == 0,
                "same-turn history credit used the post-move or engine side incorrectly");
        require(same_board(board, before) && BoardTestAccess::undo_size(board) == 0,
                "same-turn history learning changed its input position");

        Board horizon;
        BoardTestAccess::empty(horizon, side, 1, 0);
        BoardTestAccess::set_piece(horizon, 3, 3, side == 0 ? 2 : -2);
        const Move horizon_cutoff = horizon.legal_moves().front();
        Engine horizon_engine(side, 1, std::make_unique<FixedEvaluator>(0));
        require(EngineTestAccess::search_score_from_depth(
                    horizon_engine, horizon, 1, 1, -1, 0) == 0 &&
                    EngineTestAccess::history_score(
                        horizon_engine, side, horizon_cutoff) == 1,
                "a final action at the completed-turn horizon must receive nonzero credit");
    }
}

class MaterialOnlyEvaluator final : public Evaluator {
public:
    int evaluate(const Board& board) const override {
        if (board.has_game_ended()) {
            const int white_score = board.get_game_status() > 0 ? 100000 : -100000;
            return board.get_current_turn() == 0 ? white_score : -white_score;
        }
        constexpr int values[] = {0, 100, 300, 300, 500, 900, 0};
        int white_score = 0;
        for (int row = 0; row < 8; ++row) {
            for (int column = 0; column < 8; ++column) {
                const int piece = board.get_piece(row, column);
                white_score += piece > 0 ? values[piece] : -values[-piece];
            }
        }
        return board.get_current_turn() == 0 ? white_score : -white_score;
    }
};

class CaptureOnlyRecordingEvaluator final : public Evaluator {
    std::vector<Move>& evaluated_moves;
    std::size_t root_history_size;
    MaterialOnlyEvaluator material;
public:
    CaptureOnlyRecordingEvaluator(std::vector<Move>& moves, std::size_t history_size = 0)
        : evaluated_moves(moves), root_history_size(history_size) {}

    int evaluate(const Board& board) const override {
        if (!BoardTestAccess::captures_only_since(board, root_history_size))
            throw std::runtime_error("quiescence searched a quiet move");
        if (BoardTestAccess::undo_size(board) > root_history_size)
            evaluated_moves.push_back(board.get_last_move());
        return material.evaluate(board);
    }
};

class SpecialMoveEvaluator final : public Evaluator {
public:
    int evaluate(const Board& board) const override {
        if (board.get_last_move().special_move == SpecialMove::CastleKingside)
            return board.get_current_turn() == 1 ? -100000 : 100000;
        return 0;
    }
};

std::uint8_t all_castling_rights() {
    return static_cast<std::uint8_t>(CastlingRight::WhiteKingside)
        | static_cast<std::uint8_t>(CastlingRight::WhiteQueenside)
        | static_cast<std::uint8_t>(CastlingRight::BlackKingside)
        | static_cast<std::uint8_t>(CastlingRight::BlackQueenside);
}

bool same_board(const Board& left, const Board& right) {
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            if (left.get_piece(row, column) != right.get_piece(row, column))
                return false;
        }
    }
    return left.get_current_turn() == right.get_current_turn()
        && left.get_moves_left() == right.get_moves_left()
        && left.get_game_status() == right.get_game_status()
        && left.get_castling_rights() == right.get_castling_rights()
        && left.get_hash() == right.get_hash()
        && left.get_last_move() == right.get_last_move()
        && BoardTestAccess::counters(left) == BoardTestAccess::counters(right);
}

void require_hash_matches_recomputed(const Board& board, const std::string& context) {
    require(board.get_hash() == BoardTestAccess::recomputed_hash(board),
            context + ": stored hash differs from full recomputation");
}

void require_hash_transition(Board& board, const Move& move,
                             const std::string& context, bool expected_valid = true) {
    const Board before = board;
    const auto undo_size = BoardTestAccess::undo_size(board);
    require_hash_matches_recomputed(board, context + " before move");
    require(board.make_move(move) == expected_valid,
            context + ": move acceptance differed from expected");
    require_hash_matches_recomputed(board, context + " after move");
    require(BoardTestAccess::undo_size(board) == undo_size + (expected_valid ? 1 : 0),
            context + ": unexpected undo history size");
    if (expected_valid) {
        board.rollback_move();
        require_hash_matches_recomputed(board, context + " after rollback");
    }
    require(same_board(board, before) && BoardTestAccess::undo_size(board) == undo_size,
            context + ": move/rollback or rejected move changed the original state");
}

std::vector<Move> require_moves_match_exhaustive_scan(
    Board& board, const std::string& context) {
    require_hash_matches_recomputed(board, context + " before generation");
    const Board before = board;
    const auto generated = board.legal_moves();
    require(same_board(board, before), context + ": move generation changed the board");

    // Keep the old exhaustive traversal as an independent candidate-generation
    // oracle. A pawn reaching the final rank yields one move per promotion piece,
    // in the same value order the generator emits (Queen, Rook, Bishop, Knight).
    Board probe = board;
    std::vector<Move> expected;
    const SpecialMove promotions[] = {
        SpecialMove::PromoteQueen, SpecialMove::PromoteRook,
        SpecialMove::PromoteBishop, SpecialMove::PromoteKnight
    };
    for (int from = 0; from < 64; ++from) {
        for (int to = 0; to < 64; ++to) {
            const int fx = from / 8, fy = from % 8, tx = to / 8, ty = to % 8;
            const Move move{fx, fy, tx, ty};
            if (!probe.valid_move(move.old_x, move.old_y, move.new_x, move.new_y))
                continue;
            const int piece = probe.get_piece(fx, fy);
            const bool pawn_promo = (piece == 1 || piece == -1) && tx == (piece > 0 ? 7 : 0);
            if (pawn_promo) {
                for (const SpecialMove promo : promotions) {
                    Move promo_move = move;
                    promo_move.special_move = promo;
                    require(probe.make_move(promo_move), context + ": validated promotion was rejected");
                    require_hash_matches_recomputed(probe, context + " after generated move");
                    expected.push_back(probe.get_last_move());
                    probe.rollback_move();
                    require_hash_matches_recomputed(probe, context + " after generated rollback");
                }
            } else {
                require(probe.make_move(move), context + ": validated move was rejected");
                require_hash_matches_recomputed(probe, context + " after generated move");
                expected.push_back(probe.get_last_move());
                probe.rollback_move();
                require_hash_matches_recomputed(probe, context + " after generated rollback");
            }
        }
    }
    require(same_board(probe, before), context + ": oracle did not restore the board");
    require(generated == expected,
            context + ": generated moves differ in coordinates, order, or special tags"
                + " (generated " + std::to_string(generated.size())
                + ", expected " + std::to_string(expected.size()) + ")");
    return generated;
}

void test_move_generation_matches_exhaustive_scan() {
    for (int side = 0; side < 2; ++side) {
        for (int actions = 1; actions <= 2; ++actions) {
            Board board;
            const int sign = side == 0 ? 1 : -1;
            const std::string context = "side " + std::to_string(side)
                + ", actions " + std::to_string(actions);
            auto place = [&](int row, int column, int piece) {
                BoardTestAccess::set_piece(
                    board, side == 0 ? row : 7 - row, column, sign * piece);
            };

            for (int piece = 1; piece <= 6; ++piece) {
                BoardTestAccess::empty(board, side, actions, 0);
                place(3, 3, piece);
                place(3, 5, 1);
                place(3, 1, -1);
                place(3, 0, -4); // An enemy piece beyond another enemy is unreachable.
                place(5, 3, 1);
                place(1, 3, -1);
                place(5, 5, 1);
                place(1, 1, -1);
                place(4, 2, -1);
                place(4, 4, 1);
                require_moves_match_exhaustive_scan(
                    board, context + ", centre piece " + std::to_string(piece));

                BoardTestAccess::empty(board, side, actions, 0);
                place(0, 0, piece);
                place(0, 3, 1);
                place(0, 5, -4);
                place(2, 2, -1);
                place(4, 4, -4);
                require_moves_match_exhaustive_scan(
                    board, context + ", corner piece " + std::to_string(piece));
            }

            BoardTestAccess::empty(board, side, actions);
            place(6, 0, 1);
            place(6, 6, 1);
            place(7, 1, -4);
            place(7, 6, 2);
            place(7, 7, -4);
            require_moves_match_exhaustive_scan(board, context + ", promotions");

            BoardTestAccess::empty(board, side, actions);
            place(0, 3, 6);
            place(0, 0, 4);
            place(0, 7, 4);
            require_moves_match_exhaustive_scan(board, context + ", both castles");

            BoardTestAccess::set_rights(board, 0);
            require_moves_match_exhaustive_scan(board, context + ", no castling rights");

            BoardTestAccess::set_rights(board, all_castling_rights());
            place(0, 2, 2);
            place(0, 4, -2);
            require_moves_match_exhaustive_scan(board, context + ", blocked castles");

            place(0, 2, 0);
            place(0, 4, 0);
            place(0, 0, 0);
            require_moves_match_exhaustive_scan(board, context + ", missing rook");
        }

        Board terminal;
        const int sign = side == 0 ? 1 : -1;
        BoardTestAccess::empty(terminal, side);
        BoardTestAccess::set_piece(terminal, 3, 3, sign * 4);
        BoardTestAccess::set_piece(terminal, 3, 5, -sign * 6);
        require(terminal.make_move(3, 3, 3, 5), "terminal fixture king capture failed");
        require(terminal.has_game_ended(), "king capture did not end the fixture");
        require(require_moves_match_exhaustive_scan(terminal, "terminal position").empty(),
                "terminal position generated a move");
    }
}

void test_move_generation_on_reachable_positions() {
    Board board;
    const Board initial = board;
    std::vector<Hash> previous_hashes;
    std::uint32_t selection = 0x3180;
    for (int ply = 0; ply < 64; ++ply) {
        const auto moves = require_moves_match_exhaustive_scan(
            board, "reachable position " + std::to_string(ply));
        if (moves.empty()) break;
        selection = selection * 1664525u + 1013904223u;
        previous_hashes.push_back(board.get_hash());
        require(board.make_move(moves[selection % moves.size()]),
                "generated move was rejected during deterministic play");
        require_hash_matches_recomputed(board, "deterministic play");
    }
    require_moves_match_exhaustive_scan(board, "final reachable position");
    while (!previous_hashes.empty()) {
        const Hash expected = previous_hashes.back();
        previous_hashes.pop_back();
        board.rollback_move();
        require(board.get_hash() == expected, "sequence rollback restored the wrong hash");
        require_hash_matches_recomputed(board, "sequence rollback");
    }
    require(same_board(board, initial), "sequence rollback did not restore the initial state");
}

void test_hash_consistency_for_move_types() {
    for (int side = 0; side < 2; ++side) {
        const int sign = side == 0 ? 1 : -1;
        const int home_row = side == 0 ? 0 : 7;
        const int other_home_row = 7 - home_row;
        const int promotion_row = other_home_row - sign;
        for (int actions = 1; actions <= 2; ++actions) {
            Board board;
            const std::string context = "hash side " + std::to_string(side)
                + ", actions " + std::to_string(actions);
            BoardTestAccess::empty(board, side, actions);
            BoardTestAccess::set_piece(board, 3, 3, sign * 2);
            BoardTestAccess::set_piece(board, 5, 4, -sign);
            require_hash_transition(board, {3, 3, 5, 2}, context + ", quiet knight");
            require_hash_transition(board, {3, 3, 5, 4}, context + ", knight capture",
                                    actions == 2);
            require_hash_transition(board, {-1, 3, 5, 2}, context + ", invalid source", false);
            require_hash_transition(board, {3, 3, 5, 2, SpecialMove::PromoteQueen},
                                    context + ", invalid special tag", false);

            BoardTestAccess::empty(board, side, actions);
            BoardTestAccess::set_piece(board, home_row, 3, sign * 6);
            for (const int column : {0, 7}) {
                BoardTestAccess::set_piece(board, home_row, column, sign * 4);
                BoardTestAccess::set_piece(board, other_home_row, column, -sign * 4);
            }
            require_hash_transition(board, {home_row, 3, home_row, 1,
                                            SpecialMove::CastleQueenside},
                                    context + ", queenside castle");
            require_hash_transition(board, {home_row, 3, home_row, 5,
                                            SpecialMove::CastleKingside},
                                    context + ", kingside castle");
            require_hash_transition(board, {home_row, 3, home_row + sign, 3},
                                    context + ", king loses both castling rights");
            for (const int column : {0, 7}) {
                require_hash_transition(board, {home_row, column, home_row + sign, column},
                                        context + ", rook loses castling right");
                require_hash_transition(board, {home_row, column, other_home_row, column},
                                        context + ", both rooks lose castling rights",
                                        actions == 2);
            }

            BoardTestAccess::empty(board, side, actions);
            BoardTestAccess::set_piece(board, promotion_row, 6, sign);
            require_hash_transition(board, {promotion_row, 6, other_home_row, 6,
                                            SpecialMove::PromoteQueen},
                                    context + ", quiet promotion");
            for (const int captured_piece : {4, 6}) {
                BoardTestAccess::set_piece(board, other_home_row, 7, -sign * captured_piece);
                require_hash_transition(board, {promotion_row, 6, other_home_row, 7,
                                                SpecialMove::PromoteQueen},
                                        context + ", promotion captures "
                                            + std::to_string(captured_piece), actions == 2);
            }
        }
    }
}

void setup_castling_board(Board& board) {
    BoardTestAccess::empty(board);
    BoardTestAccess::set_piece(board, 0, 3, 6);
    BoardTestAccess::set_piece(board, 0, 0, 4);
    BoardTestAccess::set_piece(board, 0, 7, 4);
}

void test_castling_and_rights() {
    {
        Board board;
        setup_castling_board(board);
        Board before = board;

        require(board.valid_move(0, 3, 0, 5),
                "kingside castling should be valid on a clear board");
        require(board.make_move({0, 3, 0, 5, SpecialMove::CastleKingside}),
                "kingside castling move was rejected");
        require(board.get_piece(0, 5) == 6 && board.get_piece(0, 4) == 4
                    && board.get_piece(0, 3) == 0 && board.get_piece(0, 7) == 0,
                "kingside castling did not move king and rook correctly");
        require(board.get_last_move().special_move == SpecialMove::CastleKingside,
                "kingside castling metadata was not recorded");
        require((board.get_castling_rights()
                 & (static_cast<std::uint8_t>(CastlingRight::WhiteKingside)
                    | static_cast<std::uint8_t>(CastlingRight::WhiteQueenside))) == 0,
                "king movement did not clear white castling rights");

        board.rollback_move();
        require(same_board(board, before),
                "rollback did not restore a kingside castle completely");
    }

    {
        Board board;
        setup_castling_board(board);

        require(board.valid_move(0, 3, 0, 1),
                "queenside castling should be valid on a clear board");
        require(board.make_move({0, 3, 0, 1, SpecialMove::CastleQueenside}),
                "queenside castling move was rejected");
        require(board.get_piece(0, 1) == 6 && board.get_piece(0, 2) == 4
                    && board.get_piece(0, 3) == 0 && board.get_piece(0, 0) == 0,
                "queenside castling did not move king and rook correctly");
    }

    {
        Board board;
        setup_castling_board(board);
        BoardTestAccess::set_piece(board, 0, 4, 2);
        require(!board.valid_move(0, 3, 0, 5),
                "castling should fail when its path is occupied");
    }

    {
        Board board;
        setup_castling_board(board);
        require(board.make_move(0, 3, 1, 3), "king move for rights test failed");
        require(board.make_move(1, 3, 0, 3), "king return for rights test failed");
        BoardTestAccess::empty(board, 0, 2, board.get_castling_rights());
        BoardTestAccess::set_piece(board, 0, 3, 6);
        BoardTestAccess::set_piece(board, 0, 7, 4);
        require(!board.valid_move(0, 3, 0, 5),
                "castling should fail after the king moved and returned");
    }

    {
        Board board;
        setup_castling_board(board);
        require(board.make_move(0, 7, 1, 7), "rook move for rights test failed");
        require(board.make_move(1, 7, 0, 7), "rook return for rights test failed");
        BoardTestAccess::empty(board, 0, 2, board.get_castling_rights());
        BoardTestAccess::set_piece(board, 0, 3, 6);
        BoardTestAccess::set_piece(board, 0, 7, 4);
        require(!board.valid_move(0, 3, 0, 5),
                "castling should fail after the rook moved and returned");
    }

    {
        Board board;
        BoardTestAccess::empty(board, 1);
        BoardTestAccess::set_piece(board, 0, 3, 6);
        BoardTestAccess::set_piece(board, 0, 7, 4);
        BoardTestAccess::set_piece(board, 1, 7, -4);
        require(board.make_move(1, 7, 0, 7),
                "black rook should be able to capture the original white rook");
        require(!board.valid_move(0, 3, 0, 5),
                "castling should fail after the original rook was captured");
    }

    {
        Board board;
        setup_castling_board(board);
        const Hash with_rights = board.get_hash();
        BoardTestAccess::set_rights(
            board,
            static_cast<std::uint8_t>(all_castling_rights()
                & ~static_cast<std::uint8_t>(CastlingRight::WhiteKingside)));
        require(board.get_hash() != with_rights,
                "castling rights should affect the position hash");
    }
}

void test_promotion_and_metadata() {
    {
        Board board;
        BoardTestAccess::empty(board);
        BoardTestAccess::set_piece(board, 6, 0, 1);
        require(board.make_move({6, 0, 7, 0, SpecialMove::PromoteQueen}),
                "quiet promotion was rejected");
        require(board.get_piece(7, 0) == 5
                    && board.get_last_move().special_move == SpecialMove::PromoteQueen,
                "quiet promotion did not produce a tagged queen");
        board.rollback_move();
        require(board.get_piece(6, 0) == 1 && board.get_piece(7, 0) == 0,
                "quiet promotion rollback did not restore the pawn");
    }

    {
        Board board;
        BoardTestAccess::empty(board);
        BoardTestAccess::set_piece(board, 6, 0, 1);
        BoardTestAccess::set_piece(board, 7, 1, -4);
        Board before = board;

        require(board.make_move({6, 0, 7, 1, SpecialMove::PromoteQueen}),
                "capture-promotion was rejected");
        require(board.get_piece(7, 1) == 5 && board.get_last_move().special_move
                    == SpecialMove::PromoteQueen,
                "capture-promotion did not produce a queen");
        require(board.get_piece(6, 0) == 0,
                "capture-promotion left the original pawn behind");
        board.rollback_move();
        require(same_board(board, before),
                "capture-promotion rollback did not restore pawn and capture");
    }

    {
        Board board;
        BoardTestAccess::empty(board);
        BoardTestAccess::set_piece(board, 0, 3, 6);
        BoardTestAccess::set_piece(board, 0, 0, 4);
        BoardTestAccess::set_piece(board, 0, 7, 4);
        BoardTestAccess::set_piece(board, 6, 6, 1);

        const auto moves = board.legal_moves();
        const auto castle = std::find_if(moves.begin(), moves.end(), [](const Move& move) {
            return move.old_x == 0 && move.old_y == 3
                && move.new_x == 0 && move.new_y == 5;
        });
        const auto promotion = std::find_if(moves.begin(), moves.end(), [](const Move& move) {
            return move.old_x == 6 && move.old_y == 6
                && move.new_x == 7 && move.new_y == 6;
        });

        require(castle != moves.end()
                    && castle->special_move == SpecialMove::CastleKingside,
                "legal_moves did not tag kingside castling");
        require(promotion != moves.end()
                    && promotion->special_move == SpecialMove::PromoteQueen,
                "legal_moves did not tag promotion");
    }
}

void test_engine_searches_special_moves_without_mutating_root() {
    Board board;
    BoardTestAccess::empty(board, 0, 1);
    BoardTestAccess::set_piece(board, 0, 3, 6);
    BoardTestAccess::set_piece(board, 0, 0, 4);
    BoardTestAccess::set_piece(board, 0, 7, 4);
    BoardTestAccess::set_piece(board, 7, 3, -6);
    BoardTestAccess::set_rights(board, all_castling_rights());
    Board before = board;

    Engine engine(0, 1, std::make_unique<SpecialMoveEvaluator>());
    require(engine.find_best_move(board), "engine found no move on castle position");
    require(engine.get_best_move().special_move == SpecialMove::CastleKingside,
            "engine did not preserve/select the tagged castling move");
    require(same_board(board, before),
            "engine search mutated the authoritative board on a special move");
}

void test_board_owns_rules_only() {
    Board board;
    require(board.get_current_turn() == 0, "White should move first");
    require(board.get_moves_left() == 1, "White should have one opening action");
    require(board.get_piece(1, 0) == 1, "initial white pawn is missing");
    require(!board.has_game_ended(), "initial board is terminal");

    require(board.make_move(1, 0, 2, 0), "board rejected a legal quiet move");
    require(board.get_moves_left() == 2 && board.get_current_turn() == 1,
            "a completed opening action should hand control to Black");
}

void test_engines_take_turns_on_one_board() {
    Board board;
    Engine white(0, 1);
    Engine black(1, 1);

    require(white.is_turn(board), "White engine should start");
    require(!black.is_turn(board), "Black engine moved before its turn");
    require(!black.find_best_move(board), "Black engine searched out of turn");
    require(white.find_best_move(board), "White engine found no opening move");
    require(play(board, white.get_best_move()), "White engine move was rejected");
    require(!white.is_turn(board) && black.is_turn(board),
            "the same board should pass control to Black");
    require(black.find_best_move(board), "Black engine found no move");
    require(play(board, black.get_best_move()), "Black engine move was rejected");
}

void test_engine_configuration_is_independent() {
    EngineConfig aggressive = EngineConfig::standard();
    EngineConfig defensive = EngineConfig::standard();
    aggressive.connected_rook_bonus = 100;
    defensive.connected_rook_bonus = 5;

    Engine white(0, 1, aggressive);
    Engine black(1, 1, defensive);
    require(white.get_config().connected_rook_bonus == 100,
            "White engine lost its custom evaluation configuration");
    require(black.get_config().connected_rook_bonus == 5,
            "Black engine lost its custom evaluation configuration");
    require(white.get_config().connected_rook_bonus !=
                black.get_config().connected_rook_bonus,
            "engines unexpectedly share evaluation configuration");
}

void test_custom_evaluator_is_used() {
    Board board;
    Engine engine(0, 1, std::make_unique<FixedEvaluator>(1234));

    require(engine.evaluate(board) == 1234,
            "Engine did not delegate evaluation to its custom Evaluator");
    require(dynamic_cast<const FixedEvaluator*>(&engine.get_evaluator()) != nullptr,
            "Engine did not retain its custom Evaluator");
}

void test_new_evaluator_is_injectable() {
    Board board;
    Engine engine(0, 1, std::make_unique<AggressiveEvaluator>());

    require(dynamic_cast<const AggressiveEvaluator*>(&engine.get_evaluator()) != nullptr,
            "AggressiveEvaluator was not retained by the engine");
    require(engine.evaluate(board) == engine.get_evaluator().evaluate(board),
            "Engine did not delegate evaluation to AggressiveEvaluator");
}

void test_search_does_not_mutate_position() {
    Board board;
    Engine engine(0, 2);
    Hash before_hash = board.get_hash();
    int before_turn = board.get_current_turn();
    int before_actions = board.get_moves_left();

    require(engine.find_best_move(board), "search found no legal move");
    require(board.get_hash() == before_hash &&
                board.get_current_turn() == before_turn &&
                board.get_moves_left() == before_actions,
            "engine search mutated the authoritative board");
}

void test_move_rollback_restores_hash() {
    Board board;
    const Hash before = board.get_hash();
    require(board.make_move(1, 0, 2, 0), "quiet move for rollback test was rejected");
    require(board.get_hash() != before, "a move should change the position hash");
    board.rollback_move();
    require(board.get_hash() == before && board.get_current_turn() == 0 &&
                board.get_moves_left() == 1,
            "rolling back a quiet move should restore the complete position");
}

void test_independent_boards_share_position_hashes() {
    Board first;
    Board second;
    require(first.get_hash() == second.get_hash(),
            "independent initial boards must have identical position hashes");
    const Hash initial_hash = first.get_hash();
    std::uint32_t selection = 0x3180;
    int played = 0;
    for (; played < 40; ++played) {
        const auto moves = first.legal_moves();
        if (moves.empty()) break;
        selection = selection * 1664525u + 1013904223u;
        const Move move = moves[selection % moves.size()];
        require(first.make_move(move) && second.make_move(move),
                "independent boards rejected the same legal move");
        require(first.get_hash() == second.get_hash(),
                "identical move sequences must produce identical hashes");
        require_hash_matches_recomputed(first, "fixed hash first board");
        require_hash_matches_recomputed(second, "fixed hash second board");
    }
    for (int i = 0; i < played; ++i) {
        first.rollback_move();
        second.rollback_move();
        require(first.get_hash() == second.get_hash(),
                "independent rollback must preserve matching hashes");
    }
    require(first.get_hash() == initial_hash && second.get_hash() == initial_hash,
            "independent boards must restore the fixed initial hash");
}

void test_tt_persistence_across_undo_and_new_game() {
    Board board;
    const Board initial_board = board;
    Engine engine(0, 2);

    require(engine.find_best_move(board, 2), "engine found no opening move");
    const Move first_best = engine.get_best_move();
    const auto cold_nodes = engine.get_search_nodes();

    require(board.make_move(first_best), "engine opening move was rejected");
    require(board.make_move(6, 3, 5, 3), "black quiet move 1 was rejected");
    require(board.make_move(6, 4, 5, 4), "black quiet move 2 was rejected");
    const Board after_black = board;
    require(engine.find_best_move(after_black, 2), "engine found no second move");

    require(engine.find_best_move(initial_board, 2),
            "re-search of an undone position returned no move (warm-TT root bug)");
    require(engine.get_best_move() == first_best,
            "undo re-search must reproduce the cached root best move");

    Board fresh_game;
    require(EngineTestAccess::cached_move(engine, fresh_game) == first_best,
            "an independent initial board must match the cached root key");
    require(engine.find_best_move(fresh_game, 2),
            "engine found no move when reusing the TT in a new game");
    require(engine.get_best_move() == first_best && engine.get_search_nodes() < cold_nodes,
            "new-game search must retain and reuse the cached result");

}

void test_search_completes_shallow_iterations_before_final_depth() {
    class DepthRecordingEvaluator final : public Evaluator {
        std::vector<int>& depths;
    public:
        explicit DepthRecordingEvaluator(std::vector<int>& depths) : depths(depths) {}
        int evaluate(const Board& board) const override {
            depths.push_back(BoardTestAccess::searched_turns(board, 0));
            return 0;
        }
    };

    Board board;
    const Board before = board;
    std::vector<int> depths;
    Engine engine(0, 2, std::make_unique<DepthRecordingEvaluator>(depths));
    require(engine.find_best_move(board), "iterative search found no opening move");
    require(std::find(depths.begin(), depths.end(), 1) != depths.end(),
            "search skipped the depth-1 iteration before depth 2");
    require(std::find(depths.begin(), depths.end(), 2) != depths.end(),
            "search reused shallow scores instead of reaching depth 2");
    require(same_board(board, before), "iterative search mutated the root");
}

void test_shallow_tt_move_orders_deeper_search_without_reusing_its_score() {
    const Move shallow_best{1, 0, 2, 0};
    const Move deep_best{1, 1, 2, 1};
    class HorizonEvaluator final : public Evaluator {
        Move shallow_best;
        Move deep_best;
        Move& first_deep_move;
    public:
        HorizonEvaluator(Move shallow, Move deep, Move& first_deep)
            : shallow_best(shallow), deep_best(deep), first_deep_move(first_deep) {}
        int evaluate(const Board& board) const override {
            const int depth = BoardTestAccess::searched_turns(board, 0);
            const Move first = BoardTestAccess::first_search_move(board);
            if (depth == 2 && first_deep_move.old_x == -1) first_deep_move = first;
            int white_score = first == shallow_best ? 100 : (first == deep_best ? -100 : 0);
            if (depth >= 2) white_score = -white_score;
            return board.get_current_turn() == 0 ? white_score : -white_score;
        }
    };

    Board board;
    Move first_deep_move{};
    Engine engine(0, 2, std::make_unique<HorizonEvaluator>(
        shallow_best, deep_best, first_deep_move));
    require(engine.find_best_move(board, 1), "shallow search found no move");
    require(engine.get_best_move() == shallow_best, "shallow fixture chose the wrong move");
    require(engine.find_best_move(board, 2), "deeper search found no move");
    require(first_deep_move == shallow_best,
            "depth-1 TT best move was not searched first at depth 2");
    require(engine.get_best_move() == deep_best,
            "depth-2 search reused the shallow score instead of finding the new best move");
    require(engine.find_best_move(board, 2) && engine.get_best_move() == deep_best,
            "warm iterative re-search lost the final-depth best move");
}

int exhaustive_capture_score(Board& board, const Evaluator& evaluator, int capture_ply = 0) {
    if (board.has_game_ended()) return evaluator.evaluate(board);
    const int turn = board.get_current_turn();
    const auto moves = board.legal_moves();
    for (const Move& move : moves) {
        if (std::abs(board.get_piece(move.new_x, move.new_y)) != 6) continue;
        require(board.make_move(move), "capture oracle rejected king capture");
        const int score = evaluator.evaluate(board);
        const bool changed = board.get_current_turn() != turn;
        board.rollback_move();
        return changed ? -score : score;
    }
    int best = evaluator.evaluate(board);
    if (capture_ply >= EngineTestAccess::quiescence_limit()) return best;
    for (const Move& move : moves) {
        if (board.get_piece(move.new_x, move.new_y) == 0) continue;
        require(board.make_move(move), "capture oracle rejected a generated capture");
        const bool changed = board.get_current_turn() != turn;
        const int child = exhaustive_capture_score(board, evaluator, capture_ply + 1);
        best = std::max(best, changed ? -child : child);
        board.rollback_move();
    }
    return best;
}

int exhaustive_score(Board& board, const Evaluator& evaluator, int depth, int target) {
    if (board.has_game_ended()) return evaluator.evaluate(board);
    if (board.get_moves_left() == 2 && depth >= target)
        return exhaustive_capture_score(board, evaluator);
    const int turn = board.get_current_turn();
    int best = NEGINF;
    for (const Move& move : board.legal_moves()) {
        require(board.make_move(move), "oracle rejected a generated move");
        const bool changed = board.get_current_turn() != turn;
        const int score = exhaustive_score(board, evaluator, depth + (changed ? 1 : 0), target);
        best = std::max(best, changed ? -score : score);
        board.rollback_move();
    }
    return best == NEGINF ? evaluator.evaluate(board) : best;
}

template<class Evaluation>
void require_pvs_matches_minimax(Board board, int target, const std::string& context) {
    const Board before = board;
    const auto history_size = BoardTestAccess::undo_size(board);
    const int side = board.get_current_turn();
    const Evaluation evaluator;
    Board oracle = board;
    const int exact = exhaustive_score(oracle, evaluator, 0, target);
    auto restored = [&] {
        require(same_board(board, before) && BoardTestAccess::undo_size(board) == history_size,
                context + ": search changed root state or undo history");
        require_hash_matches_recomputed(board, context);
    };
    auto optimal = [&](const Move& move, const std::string& phase) {
        Board selected = board;
        require(selected.make_move(move), context + phase + ": illegal root move");
        const bool changed = selected.get_current_turn() != side;
        const int child = exhaustive_score(selected, evaluator, changed ? 1 : 0, target);
        require((changed ? -child : child) == exact,
                context + phase + ": root move is not minimax-optimal");
    };
    // Fresh tables separate fail-low/high probes; each then becomes a warm root.
    for (const auto window : {std::pair<int, int>{NEGINF, INF},
                              {exact - 1, exact}, {exact, exact + 1},
                              {exact - 1, exact + 1},
                              {exact - 100, exact - 99}, {exact + 99, exact + 100}}) {
        Engine engine(side, target, std::make_unique<Evaluation>());
        Move move{};
        const bool full_window = window.first == NEGINF && window.second == INF;
        const std::string phase = " [" + std::to_string(window.first) + ","
            + std::to_string(window.second) + "]";
        const int actual = EngineTestAccess::search_score(
            engine, board, target, window.first, window.second, full_window ? &move : nullptr);
        require(exact <= window.first ? exact <= actual && actual <= window.first
                : (exact >= window.second ? window.second <= actual && actual <= exact
                                          : actual == exact),
                context + phase + ": invalid score/bound, expected " + std::to_string(exact)
                    + ", got " + std::to_string(actual));
        restored();
        if (full_window) optimal(move, phase);
        for (int repeat = 0; repeat < 2; ++repeat) {
            move = Move{};
            const int warm = EngineTestAccess::search_score(
                engine, board, target, NEGINF, INF, &move);
            require(warm == exact, context + phase + ": warm root score, expected "
                    + std::to_string(exact) + ", got " + std::to_string(warm));
            restored();
            optimal(move, phase + " warm");
        }
        require(engine.find_best_move(board, target), context + phase + ": no iterative move");
        restored();
        optimal(engine.get_best_move(), phase + " iterative");
    }
}

void test_killer_ordered_search_matches_independent_minimax() {
    for (int side : {0, 1}) {
        for (int actions : {1, 2}) {
            Board board;
            BoardTestAccess::empty(board, side, actions, 0);
            BoardTestAccess::set_piece(board, 1, 0, 6);
            BoardTestAccess::set_piece(board, 6, 7, -6);
            BoardTestAccess::set_piece(board, 3, 3, 1);
            BoardTestAccess::set_piece(board, 4, 4, -1);
            const std::string context = "killer PositionalEvaluator side " + std::to_string(side)
                + ", actions " + std::to_string(actions);
            const PositionalEvaluator evaluator;
            Board oracle = board;
            const int exact = exhaustive_score(oracle, evaluator, 0, 2);
            Engine engine(side, 2, std::make_unique<PositionalEvaluator>());
            std::vector<Move> quiet;
            for (const Move& move : board.legal_moves())
                if (board.get_piece(move.new_x, move.new_y) == 0 &&
                    move.special_move < SpecialMove::PromoteQueen) quiet.push_back(move);
            require(quiet.size() >= 2, context + ": not enough quiet candidates");
            const auto normal_order = EngineTestAccess::ordered(engine, board);
            EngineTestAccess::seed_killers(engine, 0, quiet.back(), quiet[quiet.size() - 2]);
            require(EngineTestAccess::ordered(engine, board) != normal_order,
                    context + ": seeded killers did not alter traversal");
            const Board before = board;
            Move selected{};
            require(EngineTestAccess::search_score(engine, board, 2, NEGINF, INF, &selected) == exact,
                    context + ": killer-ordered score differs from independent minimax");
            Board child = board;
            require(child.make_move(selected), context + ": selected move is illegal");
            const bool changed = child.get_current_turn() != side;
            const int score = exhaustive_score(child, evaluator, changed ? 1 : 0, 2);
            require((changed ? -score : score) == exact && same_board(board, before) &&
                        BoardTestAccess::undo_size(board) == 0,
                    context + ": nonoptimal move or mutated root");
            require_pvs_matches_minimax<PositionalEvaluator>(board, 2, context);
        }
    }
}

void test_history_ordered_search_matches_independent_minimax() {
    for (int side : {0, 1}) {
        for (int actions : {1, 2}) {
            Board board;
            BoardTestAccess::empty(board, side, actions, 0);
            BoardTestAccess::set_piece(board, 1, 0, 6);
            BoardTestAccess::set_piece(board, 6, 7, -6);
            BoardTestAccess::set_piece(board, 3, 3, 1);
            BoardTestAccess::set_piece(board, 4, 4, -1);
            const std::string context = "history PositionalEvaluator side " + std::to_string(side)
                + ", actions " + std::to_string(actions);
            const PositionalEvaluator evaluator;
            Board oracle = board;
            const int exact = exhaustive_score(oracle, evaluator, 0, 2);
            Engine engine(side, 2, std::make_unique<PositionalEvaluator>());
            std::vector<Move> quiet;
            for (const Move& move : board.legal_moves())
                if (board.get_piece(move.new_x, move.new_y) == 0 &&
                    move.special_move < SpecialMove::PromoteQueen) quiet.push_back(move);
            require(quiet.size() >= 2, context + ": not enough quiet candidates");
            const auto normal_order = EngineTestAccess::ordered(engine, board);
            EngineTestAccess::seed_history(engine, side, quiet.back(), 100);
            require(EngineTestAccess::ordered(engine, board) != normal_order,
                    context + ": seeded history did not alter traversal");
            Engine independent(side, 2, std::make_unique<PositionalEvaluator>());
            require(EngineTestAccess::ordered(independent, board) == normal_order &&
                        EngineTestAccess::history_empty(independent),
                    context + ": history leaked into another Engine instance");

            const Board before = board;
            Move selected{};
            require(EngineTestAccess::search_score(engine, board, 2, NEGINF, INF, &selected) == exact,
                    context + ": history-ordered score differs from independent minimax");
            Board child = board;
            require(child.make_move(selected), context + ": selected move is illegal");
            const bool changed = child.get_current_turn() != side;
            const int score = exhaustive_score(child, evaluator, changed ? 1 : 0, 2);
            require((changed ? -score : score) == exact && same_board(board, before) &&
                        BoardTestAccess::undo_size(board) == 0,
                    context + ": nonoptimal move or mutated root");
        }
    }
}

void test_quiescence_does_not_consume_or_change_move_ordering_hints() {
    Board board;
    BoardTestAccess::empty(board, 0, 2, 0);
    BoardTestAccess::set_piece(board, 0, 0, 6);
    BoardTestAccess::set_piece(board, 7, 7, -6);
    BoardTestAccess::set_piece(board, 3, 0, 5);
    BoardTestAccess::set_piece(board, 3, 4, -4);
    BoardTestAccess::set_piece(board, 4, 5, -1);
    const Board before = board;
    std::vector<Move> normal, with_killers;
    Engine fresh(0, 1, std::make_unique<CaptureOnlyRecordingEvaluator>(normal));
    const int expected = EngineTestAccess::quiescence_score(fresh, board);
    Engine engine(0, 1, std::make_unique<CaptureOnlyRecordingEvaluator>(with_killers));
    EngineTestAccess::seed_killers(engine, 0, Move{3, 0, 3, 4}, Move{3, 0, 2, 0});
    EngineTestAccess::seed_killers(engine, 1, Move{4, 5, 3, 4});
    const Move history_hint{3, 0, 2, 0};
    EngineTestAccess::seed_history(engine, 0, history_hint, 123);
    const auto killers = EngineTestAccess::killers(engine);
    for (const auto window : {std::pair<int, int>{NEGINF, INF}, {-1000, 0}, {0, 1}}) {
        with_killers.clear();
        const int actual = EngineTestAccess::quiescence_score(
            engine, board, window.first, window.second);
        require(EngineTestAccess::killers(engine) == killers &&
                    EngineTestAccess::history_score(engine, 0, history_hint) == 123 &&
                    same_board(board, before) && BoardTestAccess::undo_size(board) == 0 &&
                    EngineTestAccess::tt_is_empty(engine),
                "capture-only quiescence changed killer/history hints, TT, or root state");
        if (window.first == NEGINF)
            require(actual == expected && with_killers == normal,
                    "quiescence score or capture visitation changed with populated killer hints");
    }
}

void test_pvs_turn_changed_research_matches_minimax() {
    Board board;
    BoardTestAccess::empty(board, 0, 1, 0);
    BoardTestAccess::set_piece(board, 0, 0, 6);
    BoardTestAccess::set_piece(board, 7, 7, -6);
    BoardTestAccess::set_piece(board, 2, 2, 2);
    BoardTestAccess::set_piece(board, 4, 3, -2);
    require_pvs_matches_minimax<AggressiveEvaluator>(board, 1, "PVS turn-changed AggressiveEvaluator");
    require_pvs_matches_minimax<StandardEvaluator>(board, 1, "PVS turn-changed StandardEvaluator");
    require_pvs_matches_minimax<PositionalEvaluator>(board, 1, "PVS turn-changed PositionalEvaluator");
}

void test_pvs_same_turn_research_matches_minimax() {
    Board board;
    BoardTestAccess::empty(board, 0, 2, 0);
    BoardTestAccess::set_piece(board, 0, 0, 6);
    BoardTestAccess::set_piece(board, 7, 7, -6);
    BoardTestAccess::set_piece(board, 2, 2, 1);
    BoardTestAccess::set_piece(board, 4, 3, -1);
    require_pvs_matches_minimax<StandardEvaluator>(board, 2, "PVS same-turn StandardEvaluator");
    require_pvs_matches_minimax<AggressiveEvaluator>(board, 2, "PVS same-turn AggressiveEvaluator");
    require_pvs_matches_minimax<PositionalEvaluator>(board, 2, "PVS same-turn PositionalEvaluator");
}

void test_iterative_search_matches_exhaustive_final_depth() {
    for (int side : {0, 1}) {
        for (int actions : {1, 2}) {
            Board board;
            BoardTestAccess::empty(board, side, actions, 0);
            BoardTestAccess::set_piece(board, 1, 0, 6);
            BoardTestAccess::set_piece(board, 6, 7, -6);
            BoardTestAccess::set_piece(board, 3, 3, 1);
            BoardTestAccess::set_piece(board, 4, 4, -1);
            const Board before = board;
            const auto history_size = BoardTestAccess::undo_size(board);
            Engine engine(side, 3);
            for (int target : {1, 2, 3, 3}) {
                Board oracle = board;
                const int expected = exhaustive_score(oracle, engine.get_evaluator(), 0, target);
                require(engine.find_best_move(board, target), "iterative oracle fixture found no move");
                Board selected = board;
                require(selected.make_move(engine.get_best_move()), "iterative search returned an illegal move");
                const bool changed = selected.get_current_turn() != side;
                const int child = exhaustive_score(selected, engine.get_evaluator(), changed ? 1 : 0, target);
                require((changed ? -child : child) == expected,
                        "iterative best move differs from exhaustive minimax at final depth");
                require(same_board(board, before) && BoardTestAccess::undo_size(board) == history_size,
                        "iterative search altered root state or undo history");
            }
        }
    }
}

void test_horizon_search_finds_free_rook_capture() {
    for (int side : {0, 1}) {
        Board board;
        BoardTestAccess::empty(board, side, 2, 0);
        BoardTestAccess::set_piece(board, 0, 0, 6);
        BoardTestAccess::set_piece(board, 7, 7, -6);
        BoardTestAccess::set_piece(board, 3, 0, 4);
        BoardTestAccess::set_piece(board, 3, 4, -4);
        const Board before = board;
        const auto history_size = BoardTestAccess::undo_size(board);
        Engine engine(side, 1, std::make_unique<MaterialOnlyEvaluator>());
        require(engine.evaluate(board) == 0, "free-rook fixture material is not equal");
        require(EngineTestAccess::horizon_score(engine, board) == 500,
                "horizon search must discover the free rook capture for either side");
        require(same_board(board, before) && BoardTestAccess::undo_size(board) == history_size,
                "horizon capture search changed the authoritative position");
    }
}

void test_quiescence_resolves_king_capture_before_stand_pat_or_limit() {
    for (int side : {0, 1}) {
        Board board;
        BoardTestAccess::empty(board, side, 2, 0);
        BoardTestAccess::set_piece(board, side == 0 ? 0 : 3, 0, 6);
        BoardTestAccess::set_piece(board, side == 0 ? 3 : 7, side == 0 ? 4 : 7, -6);
        BoardTestAccess::set_piece(board, 3, side == 0 ? 0 : 4, side == 0 ? 4 : -4);
        const Board before = board;
        Engine engine(side, 1, std::make_unique<MaterialOnlyEvaluator>());
        for (int capture_ply : {0, EngineTestAccess::quiescence_limit()}) {
            require(EngineTestAccess::quiescence_score(engine, board, NEGINF, INF, capture_ply)
                        == 100000,
                    "immediate king capture must be resolved even at the quiescence limit");
            require(EngineTestAccess::quiescence_score(engine, board, NEGINF, 0, capture_ply)
                        == 100000,
                    "stand-pat cutoff must not hide an immediate terminal win");
            require(same_board(board, before) && BoardTestAccess::undo_size(board) == 0,
                    "king-capture quiescence did not restore the board");
        }
    }
}

void test_quiescence_declines_losing_queen_capture() {
    Board board;
    BoardTestAccess::empty(board, 0, 2, 0);
    BoardTestAccess::set_piece(board, 0, 0, 6);
    BoardTestAccess::set_piece(board, 7, 7, -6);
    BoardTestAccess::set_piece(board, 3, 0, 5);
    BoardTestAccess::set_piece(board, 3, 4, -4);
    BoardTestAccess::set_piece(board, 4, 5, -1);
    const Board before = board;
    std::vector<Move> explored;
    Engine engine(0, 1, std::make_unique<CaptureOnlyRecordingEvaluator>(explored));
    require(EngineTestAccess::quiescence_score(engine, board) == 300,
            "quiescence must decline a rook capture that loses the queen to recapture");
    require(std::find(explored.begin(), explored.end(), Move{4, 5, 3, 4}) != explored.end(),
            "quiescence did not explore the pawn recapture");
    require(same_board(board, before) && BoardTestAccess::undo_size(board) == 0,
            "losing-trade search changed the board");
}

void test_quiescence_keeps_profitable_trade_after_recapture() {
    Board board;
    BoardTestAccess::empty(board, 0, 2, 0);
    BoardTestAccess::set_piece(board, 0, 0, 6);
    BoardTestAccess::set_piece(board, 7, 7, -6);
    BoardTestAccess::set_piece(board, 3, 3, 1);
    BoardTestAccess::set_piece(board, 4, 4, -4);
    BoardTestAccess::set_piece(board, 5, 5, -1);
    const Board before = board;
    std::vector<Move> explored;
    Engine engine(0, 1, std::make_unique<CaptureOnlyRecordingEvaluator>(explored));
    require(engine.evaluate(board) == -500, "profitable-trade fixture material is wrong");
    require(EngineTestAccess::quiescence_score(engine, board) == -100,
            "quiescence must include the pawn recapture after winning the rook");
    require(std::find(explored.begin(), explored.end(), Move{5, 5, 4, 4}) != explored.end(),
            "profitable-trade search omitted the opponent's recapture");
    require(same_board(board, before) && BoardTestAccess::undo_size(board) == 0,
            "profitable-trade search changed the board");
}

void test_quiescence_excludes_quiet_moves_castling_and_quiet_promotion() {
    Board board;
    BoardTestAccess::empty(board, 0, 2);
    BoardTestAccess::set_piece(board, 0, 3, 6);
    BoardTestAccess::set_piece(board, 0, 7, 4);
    BoardTestAccess::set_piece(board, 6, 3, 1);
    BoardTestAccess::set_piece(board, 7, 0, -6);
    const auto moves = board.legal_moves();
    require(std::any_of(moves.begin(), moves.end(), [](const Move& move) {
        return move.special_move == SpecialMove::CastleKingside;
    }), "quiet-only fixture must offer castling");
    require(std::any_of(moves.begin(), moves.end(), [](const Move& move) {
        return move.special_move == SpecialMove::PromoteQueen;
    }), "quiet-only fixture must offer a quiet promotion");
    const Board before = board;
    std::vector<Move> explored;
    Engine engine(0, 1, std::make_unique<CaptureOnlyRecordingEvaluator>(explored));
    require(EngineTestAccess::quiescence_score(engine, board) == 600 && explored.empty(),
            "quiescence must not play quiet moves, castling or quiet promotions");
    require(same_board(board, before), "quiet-only quiescence changed the board");
}

void test_quiescence_respects_one_action_capture_restriction() {
    Board board;
    BoardTestAccess::empty(board, 0, 1, 0);
    BoardTestAccess::set_piece(board, 0, 0, 6);
    BoardTestAccess::set_piece(board, 7, 7, -6);
    BoardTestAccess::set_piece(board, 3, 0, 4);
    BoardTestAccess::set_piece(board, 3, 4, -4);
    const Board before = board;
    std::vector<Move> explored;
    Engine engine(0, 1, std::make_unique<CaptureOnlyRecordingEvaluator>(explored));
    require(EngineTestAccess::quiescence_score(engine, board) == 0 && explored.empty(),
            "quiescence must not capture with only one action left");
    require(same_board(board, before), "one-action quiescence changed the board");
}

void test_quiescence_searches_capture_promotions_for_both_sides() {
    for (int side : {0, 1}) {
        Board board;
        const int sign = side == 0 ? 1 : -1;
        const int pawn_row = side == 0 ? 6 : 1;
        const int promotion_row = side == 0 ? 7 : 0;
        BoardTestAccess::empty(board, side, 2, 0);
        BoardTestAccess::set_piece(board, 0, 0, 6);
        BoardTestAccess::set_piece(board, 7, 7, -6);
        BoardTestAccess::set_piece(board, pawn_row, 3, sign);
        BoardTestAccess::set_piece(board, promotion_row, 4, -sign * 4);
        const Board before = board;
        std::vector<Move> explored;
        Engine engine(side, 1, std::make_unique<CaptureOnlyRecordingEvaluator>(explored));
        require(EngineTestAccess::quiescence_score(engine, board) == 900,
                "capture-only quiescence must retain the best capture promotion");
        for (const auto promotion : {SpecialMove::PromoteQueen, SpecialMove::PromoteRook,
                                     SpecialMove::PromoteBishop, SpecialMove::PromoteKnight}) {
            require(std::find(explored.begin(), explored.end(),
                              Move{pawn_row, 3, promotion_row, 4, promotion}) != explored.end(),
                    "quiescence omitted a legal capture underpromotion");
        }
        require(same_board(board, before) && BoardTestAccess::undo_size(board) == 0,
                "capture-promotion quiescence did not restore the board");
    }
}

void test_quiescence_limit_and_tt_isolation() {
    Board board;
    BoardTestAccess::empty(board, 0, 2, 0);
    BoardTestAccess::set_piece(board, 0, 0, 6);
    BoardTestAccess::set_piece(board, 7, 7, -6);
    BoardTestAccess::set_piece(board, 3, 0, 4);
    BoardTestAccess::set_piece(board, 3, 4, -4);
    const Board before = board;
    Engine engine(0, 1, std::make_unique<MaterialOnlyEvaluator>());
    require(EngineTestAccess::quiescence_score(engine, board, NEGINF, INF,
                                              EngineTestAccess::quiescence_limit()) == 0,
            "capture-ply budget must stop ordinary capture expansion");
    require(EngineTestAccess::quiescence_score(engine, board) == 500,
            "normal quiescence must still search the capture");
    require(EngineTestAccess::tt_is_empty(engine),
            "quiescence must not publish tactical-only entries into the normal TT");
    require(same_board(board, before), "bounded quiescence changed the board");
}

void test_quiescence_preserves_existing_undo_history() {
    Board board;
    BoardTestAccess::empty(board, 0, 2, 0);
    BoardTestAccess::set_piece(board, 0, 0, 6);
    BoardTestAccess::set_piece(board, 7, 7, -6);
    BoardTestAccess::set_piece(board, 3, 3, 1);
    BoardTestAccess::set_piece(board, 4, 4, -4);
    BoardTestAccess::set_piece(board, 5, 5, -1);
    const Board initial = board;
    require(board.make_move(3, 3, 4, 4), "existing-history fixture capture failed");
    const Board before = board;
    const auto history_size = BoardTestAccess::undo_size(board);
    std::vector<Move> explored;
    Engine engine(1, 1, std::make_unique<CaptureOnlyRecordingEvaluator>(explored, history_size));
    require(EngineTestAccess::quiescence_score(engine, board) == 100,
            "quiescence did not resolve the existing capture's recapture");
    require(same_board(board, before) && BoardTestAccess::undo_size(board) == history_size,
            "quiescence damaged existing undo history");
    require_hash_matches_recomputed(board, "quiescence with existing history");
    board.rollback_move();
    require(same_board(board, initial), "pre-existing undo no longer restores the board");
}

void test_quiescence_matches_exhaustive_capture_oracle_and_windows() {
    for (int side : {0, 1}) {
        for (int actions : {1, 2}) {
            Board board;
            BoardTestAccess::empty(board, side, actions, 0);
            BoardTestAccess::set_piece(board, 0, 0, 6);
            BoardTestAccess::set_piece(board, 7, 7, -6);
            BoardTestAccess::set_piece(board, 3, 0, 5);
            BoardTestAccess::set_piece(board, 3, 4, -4);
            BoardTestAccess::set_piece(board, 4, 5, -1);
            BoardTestAccess::set_piece(board, 2, 3, 1);
            const Board before = board;
            Engine engine(side, 1, std::make_unique<MaterialOnlyEvaluator>());
            Board oracle = board;
            const int exact = exhaustive_capture_score(oracle, engine.get_evaluator());
            require(EngineTestAccess::quiescence_score(engine, board) == exact,
                    "alpha-beta quiescence differs from exhaustive capture minimax");
            for (const auto window : {std::pair<int,int>{-1000, -800}, {-1000, 0},
                                      {-1000, 1000}, {1000, 1100}}) {
                const int actual = EngineTestAccess::quiescence_score(
                    engine, board, window.first, window.second);
                require(exact >= window.second ? window.second <= actual && actual <= exact
                        : (exact <= window.first ? exact <= actual && actual <= window.first
                                                : actual == exact),
                        "quiescence returned an invalid alpha-beta bound");
                require(same_board(board, before) && BoardTestAccess::undo_size(board) == 0,
                        "quiescence cutoff failed to restore the position");
            }
            require(EngineTestAccess::tt_is_empty(engine), "capture oracle tests polluted TT");
        }
    }
}

void test_normal_search_uses_quiescence_after_a_quiet_horizon_move() {
    Board board;
    BoardTestAccess::empty(board, 0, 1, 0);
    BoardTestAccess::set_piece(board, 0, 0, 6);
    BoardTestAccess::set_piece(board, 7, 6, -6);
    BoardTestAccess::set_piece(board, 3, 3, 5);
    BoardTestAccess::set_piece(board, 7, 3, -4);
    const Board before = board;
    Engine engine(0, 1, std::make_unique<MaterialOnlyEvaluator>());
    require(engine.find_best_move(board, 1), "quiet-horizon fixture search found no move");
    Board selected = board;
    require(selected.make_move(engine.get_best_move()), "quiet-horizon search returned illegal move");
    const int opponent_score = exhaustive_capture_score(selected, engine.get_evaluator());
    require(-opponent_score == 400,
            "depth-one search must avoid a quiet move that leaves the queen capturable");
    require(same_board(board, before) && BoardTestAccess::undo_size(board) == 0,
            "normal quiescence-enabled search changed its input position");
}

void test_quiescence_consumes_recursive_capture_budget() {
    class CaptureDepthEvaluator final : public Evaluator {
        std::size_t& maximum_history;
        MaterialOnlyEvaluator material;
    public:
        explicit CaptureDepthEvaluator(std::size_t& maximum) : maximum_history(maximum) {}
        int evaluate(const Board& board) const override {
            if (!BoardTestAccess::captures_only_since(board, 0))
                throw std::runtime_error("capture-chain search applied a quiet move");
            maximum_history = std::max(maximum_history, BoardTestAccess::undo_size(board));
            return material.evaluate(board);
        }
    };

    const int limit = EngineTestAccess::quiescence_limit();
    for (int start : {0, limit - 1, limit}) {
        Board board;
        BoardTestAccess::empty(board, 0, 2, 0);
        BoardTestAccess::set_piece(board, 0, 0, 6);
        BoardTestAccess::set_piece(board, 7, 7, -6);
        BoardTestAccess::set_piece(board, 3, 3, -4);
        for (int n = 0; n < 3; ++n) {
            BoardTestAccess::set_piece(board, 3, n, 4);
            BoardTestAccess::set_piece(board, n, 3, 4);
        }
        for (int n = 4; n < 8; ++n) {
            BoardTestAccess::set_piece(board, 3, n, -4);
            BoardTestAccess::set_piece(board, n, 3, -4);
        }
        const Board before = board;
        std::size_t maximum_history = 0;
        Engine engine(0, 1, std::make_unique<CaptureDepthEvaluator>(maximum_history));
        EngineTestAccess::quiescence_score(engine, board, NEGINF, INF, start);
        require(maximum_history == static_cast<std::size_t>(limit - start),
                "each recursive capture must consume one unit of the tactical budget");
        require(same_board(board, before) && BoardTestAccess::undo_size(board) == 0,
                "long capture chain failed to restore the root");
        require_hash_matches_recomputed(board, "bounded capture chain");
    }
}

void test_horizon_quiescence_ignores_and_preserves_populated_tt() {
    Board board;
    BoardTestAccess::empty(board, 0, 2, 0);
    BoardTestAccess::set_piece(board, 0, 0, 6);
    BoardTestAccess::set_piece(board, 7, 7, -6);
    BoardTestAccess::set_piece(board, 3, 0, 4);
    BoardTestAccess::set_piece(board, 3, 4, -4);
    const Board before = board;
    Engine engine(0, 1, std::make_unique<MaterialOnlyEvaluator>());
    EngineTestAccess::seed_horizon_sentinel(engine, board);
    const auto table_before = EngineTestAccess::tt_snapshot(engine);
    require(EngineTestAccess::horizon_score(engine, board) == 500,
            "horizon must enter quiescence before probing a matching normal TT entry");
    require(EngineTestAccess::tt_matches_snapshot(engine, table_before),
            "quiescence changed fields in a populated normal TT");
    require(same_board(board, before) && BoardTestAccess::undo_size(board) == 0,
            "populated-TT horizon test changed its input position");
}

void test_default_depth_is_four() {
    Engine engine(0);
    require(engine.get_search_depth() == 4,
            "default depth should be 4");
}

void test_equal_depth_result_replaces_cached_lower_bound() {
    Board board;
    Engine engine(0, 1, std::make_unique<FixedEvaluator>(0));
    require(engine.find_best_move(board, 1), "first equal-depth search found no move");
    EngineTestAccess::weaken_root_to_lower_bound(engine, board);
    require(engine.find_best_move(board, 1), "equal-depth re-search found no move");
    require(EngineTestAccess::root_is_exact_zero(engine, board),
            "equal-depth result must replace the cached lower bound with its exact result");
}

void test_retained_lower_bound_keeps_its_move_witness() {
    class BoundWitnessEvaluator final : public Evaluator {
    public:
        int evaluate(const Board& board) const override {
            const bool move_a = board.get_piece(2, 0) == 1;
            const bool move_b = board.get_piece(2, 1) == 1;
            if (board.get_current_turn() == 1)
                return -(move_a ? 200 : (move_b ? 100 : -200));
            return move_a ? (board.get_piece(6, 1) == -1 ? 100 : 0)
                          : (move_b ? 100 : -200);
        }
    };

    Board board;
    const Move witness{1, 1, 2, 1};
    Engine engine(0, 2, std::make_unique<BoundWitnessEvaluator>());
    Board oracle = board;
    require(exhaustive_score(oracle, engine.get_evaluator(), 0, 2) == 100,
            "retained lower-bound fixture has an invalid score");
    EngineTestAccess::seed_root_lower(engine, board, witness);
    require(engine.find_best_move(board, 2), "bound-witness search found no move");
    require(engine.get_best_move() == witness,
            "shallow iteration corrupted the deeper lower-bound move witness");
}

void test_colliding_children_cannot_corrupt_the_root_tt_move() {
    Board board;
    BoardTestAccess::force_slot_collisions(board);
    Engine engine(0, 2, std::make_unique<FixedEvaluator>(0));
    require(engine.find_best_move(board, 2), "forced-collision search found no move");
    require(EngineTestAccess::cached_move(engine, board) == engine.get_best_move(),
            "recursive slot collision stored another position's move under the root key");
}

} // namespace

int main() {
    try {
        test_move_comparisons_are_complements();
        test_pre_cancelled_search_does_no_work_or_tt_writes();
        test_main_search_cancellation_stops_before_node_publication_and_recovers();
        test_quiescence_cancellation_at_stand_pat_and_recursive_capture();
        test_cancelled_deeper_iteration_keeps_completed_root_bound_not_best_move();
        test_cancellation_on_king_capture_and_no_move_evaluator_return();
        test_quiescence_king_capture_and_terminal_entry_observe_cancellation();
        test_evaluator_exceptions_are_not_converted_to_cancellation();
        test_false_stop_token_matches_omitted_token_cold_and_warm();
        test_fen_initial_export();
        test_fen_import_replaces_position_at_a_new_root();
        test_fen_rejects_malformed_input_transactionally();
        test_fen_quiet_action_counters_and_rollback();
        test_fen_pawn_capture_and_special_action_counters();
        test_fen_terminal_king_capture_and_structural_validation();
        test_fen_metadata_is_not_position_identity();
        test_fen_import_preserves_existing_hash_and_rule_tables();
        test_fen_reachable_sequence_round_trips_and_reimports();
        test_fen_imported_metadata_survives_search_and_existing_undo();
        test_fen_binary_input_rejection_preserves_imported_undo();
        test_quiet_cutoff_orders_subsequent_same_ply_sibling();
        test_killer_recency_duplicate_slots_and_ply_isolation();
        test_recursive_killers_use_true_action_ply();
        test_public_root_clears_killers_before_ordering();
        test_iterative_deepening_retains_killers_between_iterations();
        test_public_root_clears_history_before_ordering();
        test_iterative_deepening_retains_history_between_iterations();
        test_cutoff_heuristics_exclude_captures_and_promotions();
        test_quiet_castling_can_receive_cutoff_ordering_credit();
        test_stale_killer_hints_do_not_promote_illegal_or_capturing_moves();
        test_tt_capture_killer_precedence_and_stability();
        test_history_orders_only_remaining_quiet_moves();
        test_history_credits_only_the_current_searched_cutoff_move();
        test_cached_history_uses_requested_depth_and_requires_lower_witness();
        test_history_score_saturates_without_overflow();
        test_history_uses_completed_turn_depth_for_same_turn_actions();
        test_killer_ordered_search_matches_independent_minimax();
        test_history_ordered_search_matches_independent_minimax();
        test_quiescence_does_not_consume_or_change_move_ordering_hints();
        test_board_owns_rules_only();
        test_engines_take_turns_on_one_board();
        test_engine_configuration_is_independent();
        test_custom_evaluator_is_used();
        test_new_evaluator_is_injectable();
        test_search_does_not_mutate_position();
        test_move_rollback_restores_hash();
        test_castling_and_rights();
        test_promotion_and_metadata();
        test_move_generation_matches_exhaustive_scan();
        test_move_generation_on_reachable_positions();
        test_hash_consistency_for_move_types();
        test_engine_searches_special_moves_without_mutating_root();
        test_independent_boards_share_position_hashes();
        test_tt_persistence_across_undo_and_new_game();
        test_search_completes_shallow_iterations_before_final_depth();
        test_shallow_tt_move_orders_deeper_search_without_reusing_its_score();
        test_pvs_turn_changed_research_matches_minimax();
        test_pvs_same_turn_research_matches_minimax();
        test_iterative_search_matches_exhaustive_final_depth();
        test_horizon_search_finds_free_rook_capture();
        test_quiescence_resolves_king_capture_before_stand_pat_or_limit();
        test_quiescence_declines_losing_queen_capture();
        test_quiescence_keeps_profitable_trade_after_recapture();
        test_quiescence_excludes_quiet_moves_castling_and_quiet_promotion();
        test_quiescence_respects_one_action_capture_restriction();
        test_quiescence_searches_capture_promotions_for_both_sides();
        test_quiescence_limit_and_tt_isolation();
        test_quiescence_preserves_existing_undo_history();
        test_quiescence_matches_exhaustive_capture_oracle_and_windows();
        test_normal_search_uses_quiescence_after_a_quiet_horizon_move();
        test_quiescence_consumes_recursive_capture_budget();
        test_horizon_quiescence_ignores_and_preserves_populated_tt();
        test_default_depth_is_four();
        test_equal_depth_result_replaces_cached_lower_bound();
        test_retained_lower_bound_keeps_its_move_witness();
        test_colliding_children_cannot_corrupt_the_root_tt_move();
        std::cout << "Passed: Board/Engine ownership, two-engine turns, custom configurations, and state isolation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
