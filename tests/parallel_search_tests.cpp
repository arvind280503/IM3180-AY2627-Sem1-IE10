#include "engine.hpp"
#include "Engines/OldEvaluator.hpp"
#include "Engines/PositionalEvaluator.hpp"
#include "Engines/AggressiveEvaluator.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <typeinfo>
#include <vector>
#include <utility>


void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct BoardTestAccess {
    static Move first(const Board& board) { return board.rollback.front().move; }
    static Move action(const Board& board, std::size_t index) { return board.rollback.at(index).move; }
    static std::size_t undo_size(const Board& board) { return board.rollback.size(); }
    static int turns(const Board& board) {
        int result = 0;
        for (std::size_t i = 0; i < board.rollback.size(); ++i) {
            const int next = i + 1 < board.rollback.size() ? board.rollback[i + 1].old_turn : board.turn;
            result += next != board.rollback[i].old_turn;
        }
        return result;
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
    static Move first(Engine& engine, Board& board) { return engine.order_moves(board, 0).front(); }
    static std::size_t entry_size() { return sizeof(Engine::TranspositionEntry); }
    static int root_depth(const Engine& engine, const Board& board) {
        const auto entry = engine.probe_tt(board.get_hash());
        return entry.key == board.get_hash() ? entry.depth : -1;
    }
    static int root_score(const Engine& engine, const Board& board) {
        const auto entry = engine.probe_tt(board.get_hash());
        require(entry.key == board.get_hash() && entry.bound == 0, "root is not cached Exact");
        require(Engine::decode_move(entry.best_move) == engine.get_best_move(), "root score/move cache incoherent");
        return entry.score;
    }
    static std::size_t worker_count(const Engine& engine) { return engine.root_workers.size(); }
    static bool distinct_tables(const Engine& a, const Engine& b) {
        return a.shared_transposition != b.shared_transposition;
    }
    static bool false_upper(const Engine& engine, const Board& board, int legal_value) {
        const auto entry = engine.probe_tt(board.get_hash());
        if (entry.key == board.get_hash() && entry.depth >= 1 &&
            entry.bound == static_cast<std::uint8_t>(Engine::BoundType::Upper) &&
            entry.score < legal_value) return true;
        for (const auto& worker : engine.root_workers)
            if (false_upper(*worker, board, legal_value)) return true;
        return false;
    }
    static bool root_lower(const Engine& engine, const Board& board, int score) {
        const auto entry = engine.probe_tt(board.get_hash());
        return entry.key == board.get_hash() && entry.score == score && entry.depth == 1 &&
            entry.bound == static_cast<std::uint8_t>(Engine::BoundType::Lower) &&
            Engine::decode_move(entry.best_move) == engine.get_best_move();
    }
    static void seed_root_hint(Engine& engine, const Board& board, const Move& move) {
        Engine::TranspositionEntry entry{};
        entry.key = board.get_hash();
        entry.best_move = Engine::encode_move(move);
        engine.store_tt(entry);
    }
    static void seed_child_killer(Engine& engine, const Move& move) {
        engine.killer_moves.resize(2);
        engine.killer_moves[1][0] = Engine::encode_move(move);
    }
    static void parallel_iteration(Engine& engine, const Board& board, int depth) {
        Move move;
        engine.parallel_root_search(board, depth, move, nullptr);
    }
    static bool root_ply_has_no_cutoff_killers(const Engine& engine) {
        const auto empty = [](const Engine& context) {
            return context.killer_moves.empty() || context.killer_moves[0] == std::array<std::uint32_t, 2>{};
        };
        if (!empty(engine)) return false;
        for (const auto& worker : engine.root_workers) if (!empty(*worker)) return false;
        return true;
    }
    static bool private_contexts(const Engine& engine) {
        std::set<const void*> evaluators{engine.evaluator.get()};
        std::set<const void*> histories{&engine.history_scores};
        std::set<const void*> killers{&engine.killer_moves};
        for (const auto& worker : engine.root_workers) {
            if (worker->shared_transposition != engine.shared_transposition ||
                !evaluators.insert(worker->evaluator.get()).second ||
                !histories.insert(&worker->history_scores).second ||
                !killers.insert(&worker->killer_moves).second) return false;
        }
        return true;
    }
};


struct Activity {
    std::mutex mutex;
    std::condition_variable changed;
    int active = 0;
    int peak = 0;
    int bound = 3;
    int clones = 0;
    bool opened = false;
    bool first_serial = true;
    bool private_instances = true;
    Move first;
    std::set<const void*> busy;
    std::map<const void*, std::set<std::thread::id>> threads;
};

class CountingEvaluator : public Evaluator {
    std::shared_ptr<Activity> activity;
public:
    explicit CountingEvaluator(std::shared_ptr<Activity> value) : activity(std::move(value)) {}
    std::unique_ptr<Evaluator> clone() const override {
        std::lock_guard<std::mutex> lock(activity->mutex);
        ++activity->clones;
        return std::make_unique<CountingEvaluator>(activity);
    }
    int evaluate(const Board& board) const override {
        std::unique_lock<std::mutex> lock(activity->mutex);
        ++activity->active;
        activity->peak = std::max(activity->peak, activity->active);
        activity->private_instances &= activity->busy.insert(this).second;
        activity->threads[this].insert(std::this_thread::get_id());
        if (BoardTestAccess::first(board) == activity->first) {
            activity->first_serial &= activity->active == 1;
        } else if (!activity->opened) {
            if (activity->active >= activity->bound) {
                activity->opened = true;
                activity->changed.notify_all();
            } else {
                activity->changed.wait_for(lock, std::chrono::seconds(2), [&] { return activity->opened; });
                activity->opened = true;
                activity->changed.notify_all();
            }
        }
        activity->busy.erase(this);
        --activity->active;
        return 0;
    }
};

void test_bounded_real_concurrency() {
    for (int count : {2, 3}) {
        Board board;
        require(board.import_fen("7k/6p1/8/8/3N4/8/8/K7 w - 1 0 1"), "fixture import");
        auto activity = std::make_shared<Activity>();
        activity->bound = count;
        Engine engine(0, 1, std::make_unique<CountingEvaluator>(activity));
        activity->first = EngineTestAccess::first(engine, board);
        engine.set_search_threads(count);
        require(engine.find_best_move(board), "parallel search found no move");
        require(activity->peak == count, "configured root search did not run bounded concurrent evaluations");
        require(activity->clones == count - 1, "worker evaluators were not independently cloned");
        require(activity->threads.size() == static_cast<std::size_t>(count), "not all private evaluator instances participated");
        require(activity->private_instances && activity->first_serial, "first move overlapped or evaluator instance shared");
        require(activity->active == 0, "evaluators still active after search");
        require(engine.get_best_move() == activity->first, "ordered-root deterministic tie failed");
        require(EngineTestAccess::private_contexts(engine), "worker mutable context is shared");
        const auto clones = activity->clones;
        require(engine.find_best_move(board, 2), "reused worker depth-2 search failed");
        require(EngineTestAccess::root_ply_has_no_cutoff_killers(engine), "child cutoff recorded at root action ply");
        require(engine.find_best_move(board, 2), "reused worker warm search failed");
        require(activity->clones == clones, "contexts/evaluators not reused across iterations and roots");
        require(EngineTestAccess::root_ply_has_no_cutoff_killers(engine), "child cutoff recorded at root action ply");
    }
}

struct EvaluationFailure { int value = 3180; };
struct FaultControl {
    std::mutex mutex;
    std::condition_variable changed;
    std::atomic<bool> armed{true};
    std::atomic<bool> stop{false};
    Move first;
    bool cancel = false;
    bool opened = false;
    int active = 0;
    int entered = 0;
};
class FaultEvaluator final : public Evaluator {
    std::shared_ptr<FaultControl> control;
public:
    explicit FaultEvaluator(std::shared_ptr<FaultControl> value) : control(std::move(value)) {}
    std::unique_ptr<Evaluator> clone() const override { return std::make_unique<FaultEvaluator>(control); }
    int evaluate(const Board& board) const override {
        if (!control->armed.load() || BoardTestAccess::turns(board) < 2 ||
            BoardTestAccess::first(board) == control->first) return 0;
        std::unique_lock<std::mutex> lock(control->mutex);
        ++control->active;
        ++control->entered;
        if (control->active >= 2) {
            control->opened = true;
            control->changed.notify_all();
        } else {
            control->changed.wait_for(lock, std::chrono::seconds(2), [&] { return control->opened; });
        }
        --control->active;
        if (control->cancel) { control->stop.store(true); return 0; }
        throw EvaluationFailure{};
    }
};

void test_exception_invalidates_completed_iteration() {
    Board board;
    require(board.import_fen("7k/6p1/8/8/3N4/8/8/K7 w - 1 0 1"), "fault fixture");
    const auto fen = board.export_fen();
    const auto hash = board.get_hash();
    auto control = std::make_shared<FaultControl>();
    Engine engine(0, 2, std::make_unique<FaultEvaluator>(control));
    engine.set_search_threads(3);
    control->first = EngineTestAccess::first(engine, board);
    bool caught = false;
    try { engine.find_best_move(board); }
    catch (const EvaluationFailure& error) { caught = error.value == 3180; }
    require(caught, "ordinary evaluator exception was swallowed");
    require(control->entered >= 2 && control->active == 0, "throw path did not join active workers");
    require(EngineTestAccess::root_depth(engine, board) == 1, "unfinished depth-2 root published");
    require(engine.get_best_move() == Move{}, "ordinary exception retained stale completed-iteration best move");
    require(board.export_fen() == fen && board.get_hash() == hash && BoardTestAccess::undo_size(board) == 0,
            "fault search mutated input");
    control->armed.store(false);
    require(engine.find_best_move(board) && EngineTestAccess::root_depth(engine, board) == 2,
            "search after exception did not recover");
}

void test_cancellation_discards_unfinished_root() {
    for (int actions : {1, 2}) {
        Board board;
        require(board.import_fen("7k/6p1/8/8/3N4/8/8/K7 w - " + std::to_string(actions) + " 0 1"), "cancel fixture");
        auto control = std::make_shared<FaultControl>();
        control->cancel = true;
        Engine engine(0, 2, std::make_unique<FaultEvaluator>(control));
        engine.set_search_threads(3);
        control->first = EngineTestAccess::first(engine, board);
        require(!engine.find_best_move(board, 2, &control->stop), "worker cancellation returned a move");
        require(control->entered >= 2 && control->active == 0, "cancel did not join evaluators");
        require(engine.get_best_move() == Move{} && EngineTestAccess::root_depth(engine, board) == 1,
                "cancel published unfinished iteration");
        control->stop.store(false);
        control->armed.store(false);
        require(engine.find_best_move(board, 2, &control->stop), "cancel recovery failed");
        control->stop.store(true);
        require(!engine.find_best_move(board, 2, &control->stop) && engine.get_best_move() == Move{},
                "pre-cancel did not clear old result");
    }
}

int oracle_capture(Board& board, const Evaluator& evaluator, int ply = 0) {
    if (board.has_game_ended()) return evaluator.evaluate(board);
    const int side = board.get_current_turn();
    const auto moves = board.legal_moves();
    for (const auto& move : moves) {
        if (std::abs(board.get_piece(move.new_x, move.new_y)) != 6) continue;
        require(board.make_move(move), "oracle king capture illegal");
        const int score = evaluator.evaluate(board);
        const bool changed = board.get_current_turn() != side;
        board.rollback_move();
        return changed ? -score : score;
    }
    int best = evaluator.evaluate(board);
    if (board.get_moves_left() != 2 || ply >= 8) return best;
    for (const auto& move : moves) {
        if (board.get_piece(move.new_x, move.new_y) == 0) continue;
        require(board.make_move(move), "oracle capture illegal");
        const bool changed = board.get_current_turn() != side;
        const int score = oracle_capture(board, evaluator, ply + 1);
        board.rollback_move();
        best = std::max(best, changed ? -score : score);
    }
    return best;
}

int oracle(Board& board, const Evaluator& evaluator, int depth, int target) {
    if (board.has_game_ended()) return evaluator.evaluate(board);
    if (board.get_moves_left() == 2 && depth >= target) return oracle_capture(board, evaluator);
    const int side = board.get_current_turn();
    int best = 0;
    bool found = false;
    for (const auto& move : board.legal_moves()) {
        require(board.make_move(move), "oracle move illegal");
        const bool changed = board.get_current_turn() != side;
        const int score = oracle(board, evaluator, depth + (changed ? 1 : 0), target);
        board.rollback_move();
        const int value = changed ? -score : score;
        if (!found || value > best) best = value;
        found = true;
    }
    return found ? best : evaluator.evaluate(board);
}

void test_independent_oracle_cold_warm_both_sides_actions() {
    int fixtures = 0;
    for (int placement : {0, 1}) for (int side : {0, 1}) for (int actions : {1, 2}) for (int target : {1, 2}) {
        Board board;
        const std::string pieces = placement == 0 ? "7k/8/8/8/R3r3/8/8/K7 " :
            (side == 0 ? "7k/6p1/8/8/3N4/8/8/K7 " : "7k/8/8/3n4/8/8/1P6/K7 ");
        require(board.import_fen(pieces + std::string(side ? "b" : "w") +
                    " - " + std::to_string(actions) + " 17 42"), "oracle fixture");
        const auto fen = board.export_fen();
        const auto hash = board.get_hash();
        const auto legal = board.legal_moves();
        bool quiet = false, capture = false;
        for (const auto& move : legal) {
            quiet |= board.get_piece(move.new_x, move.new_y) == 0;
            capture |= board.get_piece(move.new_x, move.new_y) != 0;
        }
        require(quiet && (placement == 1 || actions == 1 || capture), "fixture misses root branch types");
        for (int kind : {0, 1, 2, 3}) {
            std::unique_ptr<Evaluator> eval;
            if (kind == 0) eval = std::make_unique<StandardEvaluator>();
            if (kind == 1) eval = std::make_unique<OldEvaluator>();
            if (kind == 2) eval = std::make_unique<PositionalEvaluator>();
            if (kind == 3) eval = std::make_unique<AggressiveEvaluator>();
            Board reference = board;
            const int exact = oracle(reference, *eval, 0, target);
            require(reference.export_fen() == fen && reference.get_hash() == hash, "oracle did not undo");
            for (int count : {1, 2, 4}) {
                Engine engine(side, target, eval->clone());
                engine.set_search_threads(count);
                Move cold{};
                for (int warm : {0, 1}) {
                    require(engine.find_best_move(board), "oracle fixture no best move");
                    require(std::find(legal.begin(), legal.end(), engine.get_best_move()) != legal.end(), "selected move illegal");
                    require(EngineTestAccess::root_score(engine, board) == exact &&
                            EngineTestAccess::root_depth(engine, board) == target, "root score/depth disagrees with minimax");
                    Board child = board;
                    require(child.make_move(engine.get_best_move()), "selected action rejected");
                    const bool changed = child.get_current_turn() != side;
                    const int selected = oracle(child, *eval, changed ? 1 : 0, target);
                    require((changed ? -selected : selected) == exact, "selected move is not optimal (bound mistaken for exact)");
                    if (!warm) cold = engine.get_best_move();
                    else require(engine.get_best_move() == cold, "warm optimal tie changed");
                    require(board.export_fen() == fen && board.get_hash() == hash &&
                            board.legal_moves() == legal && BoardTestAccess::undo_size(board) == 0,
                            "search mutated root FEN/hash/undo/legal actions");
                }
            }
            ++fixtures;
        }
    }
    std::cout << "oracle fixture/evaluator/depth combinations: " << fixtures << '\n';
}

class LegacyEvaluator final : public Evaluator {
public:
    int evaluate(const Board&) const override { return 0; }
};
class DerivedPositional : public PositionalEvaluator {};
class DerivedOld : public OldEvaluator {};
class DerivedAggressive : public AggressiveEvaluator {};
class CloneablePositional final : public PositionalEvaluator {
public:
    int evaluate(const Board&) const override { return 42; }
    std::unique_ptr<Evaluator> clone() const override { return std::make_unique<CloneablePositional>(*this); }
};
class SlicingPositional final : public PositionalEvaluator {
public:
    std::unique_ptr<Evaluator> clone() const override { return std::make_unique<PositionalEvaluator>(); }
};
class NullClone final : public Evaluator {
public:
    int evaluate(const Board&) const override { return 0; }
    std::unique_ptr<Evaluator> clone() const override { return nullptr; }
};

void test_configuration_legacy_and_clone_contracts() {
    Board board;
    require(board.import_fen("7k/6p1/8/8/3N4/8/8/K7 w - 1 0 1"), "legacy fixture");
    Engine legacy(0, 1, std::make_unique<LegacyEvaluator>());
    require(legacy.get_search_threads() == 1 && legacy.find_best_move(board), "legacy evaluator default path broken");
    for (int invalid : {0, -1}) {
        bool caught = false;
        try { legacy.set_search_threads(invalid); } catch (const std::invalid_argument&) { caught = true; }
        require(caught && legacy.get_search_threads() == 1, "invalid thread setting accepted or changed state");
    }
    legacy.set_search_threads(2);
    bool caught = false;
    try { legacy.find_best_move(board); } catch (const std::logic_error&) { caught = true; }
    require(caught && legacy.get_best_move() == Move{}, "unsupported parallel evaluator not rejected cleanly");
    legacy.set_search_threads(1);
    require(legacy.find_best_move(board) && EngineTestAccess::worker_count(legacy) == 0, "one-thread fallback after rejection failed");

    for (int kind : {0, 1, 2, 3, 4}) {
        std::unique_ptr<Evaluator> evaluator;
        if (kind == 0) evaluator = std::make_unique<DerivedPositional>();
        if (kind == 1) evaluator = std::make_unique<SlicingPositional>();
        if (kind == 2) evaluator = std::make_unique<NullClone>();
        if (kind == 3) evaluator = std::make_unique<DerivedOld>();
        if (kind == 4) evaluator = std::make_unique<DerivedAggressive>();
        Engine engine(0, 1, std::move(evaluator));
        require(engine.find_best_move(board), "custom evaluator sequential API broken");
        engine.set_search_threads(2);
        bool rejected = false;
        try { engine.find_best_move(board); } catch (const std::logic_error&) { rejected = true; }
        require(rejected && engine.get_best_move() == Move{}, "sliced/null/unsupported derived clone accepted");
    }
    CloneablePositional derived;
    auto clone = derived.clone();
    require(dynamic_cast<CloneablePositional*>(clone.get()) && clone->evaluate(board) == 42, "derived clone lost type or state");
    Engine supported(0, 1, std::make_unique<CloneablePositional>());
    supported.set_search_threads(2);
    require(supported.find_best_move(board), "explicit derived clone rejected");

    auto config = EngineConfig::standard();
    config.piece_value[2] += 271;
    config.knight_mobility_weight += 17;
    config.knight_sq_table[27] += 93;
    StandardEvaluator standard(config);
    OldEvaluator custom(config);
    PositionalEvaluator positional;
    AggressiveEvaluator aggressive;
    StandardEvaluator default_standard;
    require(standard.evaluate(board) != default_standard.evaluate(board), "config fixture not discriminating");
    for (const Evaluator* evaluator : std::vector<const Evaluator*>{&standard, &custom, &positional, &aggressive}) {
        auto copy = evaluator->clone();
        require(copy.get() != evaluator && typeid(*copy) == typeid(*evaluator), "built-in clone lost dynamic type/identity");
        require(copy->evaluate(board) == evaluator->evaluate(board), "built-in clone lost configuration");
        Board child = board;
        require(child.make_move(child.legal_moves().back()), "clone probe action illegal");
        require(copy->evaluate(child) == evaluator->evaluate(child), "built-in clone diverged after action");
    }
    auto standard_copy = standard.clone();
    const auto& copied_config = dynamic_cast<StandardEvaluator&>(*standard_copy).get_config();
    require(copied_config.knight_mobility_weight == config.knight_mobility_weight &&
            copied_config.piece_value == config.piece_value && copied_config.knight_sq_table == config.knight_sq_table,
            "standard clone configuration fields differ");

    auto activity = std::make_shared<Activity>();
    activity->opened = true;
    Engine bounded(0, 1, std::make_unique<CountingEvaluator>(activity));
    require(board.import_fen("7k/8/8/8/8/8/8/K7 w - 1 0 1"), "job bound fixture");
    bounded.set_search_threads(1000);
    activity->first = EngineTestAccess::first(bounded, board);
    require(bounded.find_best_move(board), "bounded small root failed");
    require(activity->clones <= static_cast<int>(board.legal_moves().size()) - 1 &&
            activity->peak <= static_cast<int>(board.legal_moves().size()), "worker allocation exceeds root jobs");
    bounded.set_search_threads(1);
    require(EngineTestAccess::worker_count(bounded) == 0 && bounded.find_best_move(board), "thread configuration did not release contexts");
}

void test_existing_undo_history_is_untouched() {
    Board board;
    require(board.import_fen("7k/6p1/8/8/3N4/8/8/K7 w - 2 17 42") &&
            board.make_move(board.legal_moves().back()), "history fixture");
    const auto fen = board.export_fen();
    const auto hash = board.get_hash();
    const auto last = board.get_last_move();
    const auto count = BoardTestAccess::undo_size(board);
    Board expected = board;
    expected.rollback_move();
    Engine engine(board.get_current_turn(), 2);
    engine.set_search_threads(4);
    require(engine.find_best_move(board) && engine.find_best_move(board), "history search failed");
    require(board.export_fen() == fen && board.get_hash() == hash && board.get_last_move() == last &&
            BoardTestAccess::undo_size(board) == count, "existing undo stack/last action changed");
    board.rollback_move();
    require(board.export_fen() == expected.export_fen() && board.get_hash() == expected.get_hash(),
            "search corrupted caller rollback record");
}

void test_changed_roots_collisions_and_evaluator_namespaces() {
    for (bool collisions : {false, true}) for (int side : {0, 1}) for (int count : {1, 2, 4}) {
        Board board;
        const std::string pieces = side == 0 ? "7k/6p1/8/8/3N4/8/8/K7 w" :
                                               "7k/8/8/3n4/8/8/1P6/K7 b";
        require(board.import_fen(pieces + " - 2 0 1"), "changed root fixture");
        if (collisions) BoardTestAccess::force_slot_collisions(board);
        Engine engine(side, 1);
        engine.set_search_threads(count);
        const auto check = [&] {
            const auto fen = board.export_fen();
            const auto hash = board.get_hash();
            const auto undo = BoardTestAccess::undo_size(board);
            Board reference = board;
            const auto exact = oracle(reference, engine.get_evaluator(), 0, 1);
            for (int warm : {0, 1}) {
                require(engine.find_best_move(board), "changed root search failed");
                require(EngineTestAccess::root_score(engine, board) == exact, "changed root cache disagrees with oracle");
                Board child = board;
                require(child.make_move(engine.get_best_move()), "changed root selected illegal action");
                const bool changed = child.get_current_turn() != side;
                const int value = oracle(child, engine.get_evaluator(), changed ? 1 : 0, 1);
                require((changed ? -value : value) == exact, "colliding/changed root selected nonoptimal bound witness");
                require(board.export_fen() == fen && board.get_hash() == hash && BoardTestAccess::undo_size(board) == undo,
                        "changed root input/history mutated");
            }
        };
        check();
        const auto original = board.get_hash();
        bool moved = false;
        for (const auto& action : board.legal_moves()) {
            if (board.get_piece(action.new_x, action.new_y) == 0 && action.special_move == SpecialMove::None) {
                require(board.make_move(action), "changed root action rejected");
                moved = true;
                break;
            }
        }
        require(moved && board.get_hash() != original && board.get_current_turn() == side && board.get_moves_left() == 1,
                "changed root fixture did not change hash/history without changing side");
        check();
        board.rollback_move();
        require(board.get_hash() == original, "changed root undo failed");
        check();
    }
    Board board;
    require(board.import_fen("7k/6p1/8/8/3N4/8/8/K7 w - 1 0 1"), "namespace fixture");
    auto config = EngineConfig::standard();
    config.piece_value[2] += 977;
    Engine first(0, 1), second(0, 1, config), black(1, 1, std::make_unique<OldEvaluator>());
    first.set_search_threads(4); second.set_search_threads(4); black.set_search_threads(4);
    require(EngineTestAccess::distinct_tables(first, second) && EngineTestAccess::distinct_tables(first, black) &&
            EngineTestAccess::distinct_tables(second, black), "independent side/evaluator namespaces share a TT");
    int scores[2]{};
    for (int pass : {0, 1}) {
        int i = 0;
        for (Engine* engine : std::vector<Engine*>{&first, &second}) {
            Board reference = board;
            const int exact = oracle(reference, engine->get_evaluator(), 0, 1);
            require(engine->find_best_move(board) && EngineTestAccess::root_score(*engine, board) == exact,
                    "configured evaluator namespace polluted by another Engine");
            scores[i++] = exact;
        }
    }
    require(scores[0] != scores[1], "namespace evaluators do not discriminate scores");
}

struct FirstActionTrace {
    std::mutex mutex;
    Move action;
    bool recorded = false;
};
class ActionTraceEvaluator final : public Evaluator {
    std::shared_ptr<FirstActionTrace> trace;
public:
    explicit ActionTraceEvaluator(std::shared_ptr<FirstActionTrace> state) : trace(std::move(state)) {}
    std::unique_ptr<Evaluator> clone() const override { return std::make_unique<ActionTraceEvaluator>(trace); }
    int evaluate(const Board& board) const override {
        std::lock_guard<std::mutex> lock(trace->mutex);
        if (!trace->recorded) {
            trace->recorded = true;
            trace->action = BoardTestAccess::action(board, 1);
        }
        return 0;
    }
};
void test_root_child_uses_action_ply_one() {
    for (int actions : {1, 2}) {
        Board board;
        require(board.import_fen("7k/6p1/8/8/3N4/8/8/K7 w - " + std::to_string(actions) + " 0 1"), "ply fixture");
        auto trace = std::make_shared<FirstActionTrace>();
        Engine engine(0, 2, std::make_unique<ActionTraceEvaluator>(trace));
        engine.set_search_threads(2);
        Board child = board;
        require(child.make_move(EngineTestAccess::first(engine, board)), "ply first action illegal");
        const auto legal = child.legal_moves();
        require(legal.size() > 1 && !(legal.front() == legal.back()), "ply fixture not discriminating");
        EngineTestAccess::seed_child_killer(engine, legal.back());
        EngineTestAccess::parallel_iteration(engine, board, actions == 2 ? 1 : 2);
        require(trace->recorded && trace->action == legal.back(), "root child did not consume action-ply-1 killer");
    }
}

void test_root_infinity_keeps_sibling_bounds_valid() {
    Board board;
    require(board.import_fen("8/8/8/8/R3k3/8/8/K7 w - 2 0 1"), "infinity fixture");
    const auto fen = board.export_fen();
    const auto hash = board.get_hash();
    Board sibling = board;
    require(sibling.make_move(Move{0, 0, 0, 1}), "quiet root sibling");
    Board alternative = sibling;
    require(alternative.make_move(Move{0, 1, 0, 2}), "better sibling continuation");
    EngineConfig config{};
    config.king_sq_table[2] = 100;
    StandardEvaluator configured(config);
    require(-configured.evaluate(alternative) == 100, "sibling witness value");
    const Move winning{3, 0, 3, 4};
    Board terminal = board;
    require(terminal.make_move(winning) && terminal.has_game_ended(), "winning capture");
    for (int kind : {0, 1}) for (int count : {2, 4}) {
        std::unique_ptr<Evaluator> evaluator;
        if (kind == 0) evaluator = std::make_unique<StandardEvaluator>(config);
        else evaluator = std::make_unique<OldEvaluator>(config);
        const int winning_score = -evaluator->evaluate(terminal);
        require(winning_score >= INF, "terminal fixture must reach search ceiling");
        Engine engine(0, 1, std::move(evaluator));
        engine.set_search_threads(count);
        for (int warm : {0, 1}) {
            require(engine.find_best_move(board) && engine.get_best_move() == winning, "immediate win not selected");
            require(!EngineTestAccess::false_upper(engine, sibling, 100), "closed root window published false sibling Upper bound");
            require(engine.get_closed_window_nodes() == 0, "root sibling dispatched with closed window");
            require(EngineTestAccess::root_lower(engine, board, winning_score), "root fail-high not cached as coherent Lower bound");
            require(board.export_fen() == fen && board.get_hash() == hash, "infinity search changed input");
        }
        Engine later(0, 1, engine.get_evaluator().clone());
        later.set_search_threads(count);
        EngineTestAccess::seed_root_hint(later, board, Move{0, 0, 0, 1});
        require(later.find_best_move(board) && later.get_best_move() == winning, "later winning root action not selected");
        require(EngineTestAccess::root_lower(later, board, winning_score), "later root fail-high cached as Exact");
    }
}

void test_oracle_preserves_forced_loss_scores() {
    Board board;
    require(board.import_fen("7k/8/8/8/8/1q6/8/K7 w - 1 0 1"), "forced-loss fixture");
    const auto fen = board.export_fen();
    const auto hash = board.get_hash();
    const auto legal = board.legal_moves();
    require(legal.size() == 3, "forced-loss fixture must have three legal actions");
    for (int kind : {0, 1}) {
        std::unique_ptr<Evaluator> evaluator;
        if (kind == 0) evaluator = std::make_unique<StandardEvaluator>();
        else evaluator = std::make_unique<OldEvaluator>();
        int expected = 0;
        for (const auto& move : legal) {
            Board child = board;
            require(child.make_move(move) && child.get_current_turn() == 1, "forced-loss turn change");
            bool captured_king = false;
            for (const auto& reply : child.legal_moves()) {
                if (child.get_piece(reply.new_x, reply.new_y) != 6) continue;
                Board terminal = child;
                require(terminal.make_move(reply) && terminal.has_game_ended(), "forced terminal loss");
                expected = evaluator->evaluate(terminal);
                require(expected <= NEGINF && -oracle_capture(child, *evaluator) == expected,
                        "forced-loss child value must match actual terminal capture");
                captured_king = true;
                break;
            }
            require(captured_king, "every legal root action must lose the king");
        }
        Board copy = board;
        require(oracle(copy, *evaluator, 0, 1) == expected, "oracle confused forced loss with no legal actions");
        require(copy.export_fen() == fen && copy.get_hash() == hash && copy.legal_moves() == legal,
                "forced-loss oracle changed input");
    }
    Board blocked;
    require(blocked.import_fen("PPPPPPPk/PPPPPPPP/PPPPPPPP/PPPPPPPP/PPPPPPPP/PPPPPPPP/PPPPPPPP/KPPPPPPP w - 1 0 1") &&
            blocked.legal_moves().empty() && !blocked.has_game_ended(), "no-legal-action fixture");
    StandardEvaluator evaluator;
    require(oracle(blocked, evaluator, 0, 1) == evaluator.evaluate(blocked), "oracle lost true no-action fallback");
}

int main() {
    try {
        std::cout << "TT backend: native coherent CAS\n";
        test_oracle_preserves_forced_loss_scores();
        std::cout << "PASS oracle forced losses at and below negative infinity\n";
        test_root_infinity_keeps_sibling_bounds_valid();
        std::cout << "PASS root infinity sibling cache bounds\n";
        test_bounded_real_concurrency();
        std::cout << "PASS bounded real concurrency\n";
        test_exception_invalidates_completed_iteration();
        std::cout << "PASS exception cleanup and recovery\n";
        test_cancellation_discards_unfinished_root();
        std::cout << "PASS cancellation cleanup and recovery\n";
        test_independent_oracle_cold_warm_both_sides_actions();
        std::cout << "PASS independent oracle cold/warm sides/actions\n";
        test_configuration_legacy_and_clone_contracts();
        std::cout << "PASS configuration legacy API and cloning\n";
        test_existing_undo_history_is_untouched();
        std::cout << "PASS existing undo stack restoration\n";
        test_changed_roots_collisions_and_evaluator_namespaces();
        std::cout << "PASS changed roots, forced slot collisions and evaluator namespaces\n";
        test_root_child_uses_action_ply_one();
        std::cout << "PASS root child action ply on both turn branches\n";
        std::cout << "Decoded TT record bytes: " << EngineTestAccess::entry_size() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
