#include "engine.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <exception>
#include <thread>
#include <typeinfo>

namespace {
struct SearchCancelled {};
}

void Engine::check_cancelled(const std::atomic<bool>* stop) const {
    if ((stop && stop->load(std::memory_order_relaxed)) ||
        (parallel_abort && parallel_abort->load(std::memory_order_relaxed)))
        throw SearchCancelled{};
}

Engine::Engine(int engine_side, int search_depth,
               std::unique_ptr<Evaluator> engine_evaluator)
    : Engine(engine_side, search_depth, std::move(engine_evaluator),
             std::make_shared<SharedTranspositionTable>()) {}

Engine::Engine(int engine_side, int search_depth,
               std::unique_ptr<Evaluator> engine_evaluator,
               std::shared_ptr<SharedTranspositionTable> shared_table)
    : side(engine_side), default_search_depth(search_depth),
      evaluator(std::move(engine_evaluator)),
      shared_transposition(std::move(shared_table)) {
    if (side < 0 || side > 1) {
        throw std::invalid_argument("engine side must be 0 (White) or 1 (Black)");
    }
    if (!evaluator)
        evaluator = std::make_unique<StandardEvaluator>();
    set_search_depth(search_depth);
}

Engine::Engine(int engine_side, int search_depth, EngineConfig config)
    : Engine(engine_side, search_depth,
             std::make_unique<StandardEvaluator>(std::move(config))) {}

int Engine::get_side() const { return side; }
int Engine::get_search_depth() const { return default_search_depth; }
int Engine::get_search_threads() const { return search_threads; }

std::size_t Engine::get_tt_memory_bytes() const {
    return shared_transposition->memory_bytes();
}

Engine::TranspositionEntry Engine::probe_tt(Hash key) const {
    return shared_transposition->probe(key);
}

void Engine::store_tt(const TranspositionEntry& entry, int replacement_depth) {
    shared_transposition->store(entry, replacement_depth);
}

void Engine::set_search_threads(int threads) {
    if (threads < 1) throw std::invalid_argument("search threads must be positive");
    if (threads != search_threads) root_workers.clear();
    search_threads = threads;
}

void Engine::prepare_root_workers(std::size_t count) {
    while (root_workers.size() < count) {
        auto copy = evaluator->clone();
        if (!copy || typeid(*copy) != typeid(*evaluator))
            throw std::logic_error("parallel evaluator clone must preserve dynamic type");
        root_workers.push_back(std::unique_ptr<Engine>(new Engine(side, default_search_depth,
            std::move(copy), shared_transposition)));
    }
}

void Engine::set_search_depth(int search_depth) {
    if (search_depth < 1) {
        throw std::invalid_argument("search depth must be positive");
    }
    default_search_depth = search_depth;
}

const Evaluator& Engine::get_evaluator() const { return *evaluator; }

const EngineConfig& Engine::get_config() const {
    const auto* standard_evaluator =
        dynamic_cast<const StandardEvaluator*>(evaluator.get());
    if (!standard_evaluator) {
        throw std::logic_error(
            "get_config() is only available for StandardEvaluator");
    }
    return standard_evaluator->get_config();
}

bool Engine::is_turn(const Board& board) const {
    return !board.has_game_ended() && board.get_current_turn() == side;
}

std::size_t Engine::get_search_nodes() const { return search_nodes; }
std::size_t Engine::get_closed_window_nodes() const { return closed_window_nodes; }

int Engine::evaluate(const Board& board) const {
    return evaluator->evaluate(board);
}

std::uint32_t Engine::encode_move(const Move& move) {
    if (move.old_x < 0 || move.new_x < 0) return 0;
    const std::uint32_t from =
        static_cast<std::uint32_t>(move.old_x * 8 + move.old_y);
    const std::uint32_t to =
        static_cast<std::uint32_t>(move.new_x * 8 + move.new_y);
    // Highest used bit is 17: from at bit 12, to at bit 4, special at bit 0.
    return (from << 12) | (to << 4) |
           static_cast<std::uint32_t>(move.special_move);
}

Move Engine::decode_move(std::uint32_t packed) {
    Move move;
    const int from = static_cast<int>(packed >> 12);
    const int to = static_cast<int>((packed >> 4) & 0x3Fu);
    move.old_x = from / 8;
    move.old_y = from % 8;
    move.new_x = to / 8;
    move.new_y = to % 8;
    move.special_move = static_cast<SpecialMove>(packed & 0x7u);
    return move;
}

std::vector<Move> Engine::order_moves(Board& board,
                                      std::uint32_t tt_best,
                                      const std::array<std::uint32_t, 2>* killers) const {
    const std::vector<Move> legal = board.legal_moves();
    const std::size_t n = legal.size();

    // Piece values for MVV-LVA: pawn..king (king = 99999 in the standard
    // config, so a king capture sorts to the very top).
    constexpr int piece_value[7] = {0, 1, 3, 3, 5, 9, 99999};

    // Within-category priority: MVV-LVA for captures, recency for killers,
    // accumulated cutoff credit for the remaining quiet moves.
    std::vector<std::int64_t> scores(n, 0);
    std::vector<int> categories(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const Move& move = legal[i];
        const int victim = board.get_piece(move.new_x, move.new_y);
        if (victim == 0) {
            if (killers && move.special_move < SpecialMove::PromoteQueen) {
                const auto packed = encode_move(move);
                for (std::size_t slot = 0; slot < killers->size(); ++slot) {
                    if (packed == (*killers)[slot]) {
                        categories[i] = 1;
                        scores[i] = static_cast<int>(killers->size() - slot);
                        break;
                    }
                }
            }
            if (categories[i] == 0 && move.special_move < SpecialMove::PromoteQueen) {
                const int from = move.old_x * 8 + move.old_y;
                const int to = move.new_x * 8 + move.new_y;
                scores[i] = history_scores[board.get_current_turn()][from][to];
            }
            continue;
        }
        categories[i] = 2;
        const int attacker = board.get_piece(move.old_x, move.old_y);
        scores[i] = piece_value[std::abs(victim)] * 8 -
                    piece_value[std::abs(attacker)];
    }

    // Stable category/MVV-LVA sort preserves board order for ties.
    std::vector<std::size_t> idx(n);
    for (std::size_t i = 0; i < n; ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(),
                     [&scores, &categories](std::size_t a, std::size_t b) {
                         return categories[a] != categories[b]
                             ? categories[a] > categories[b] : scores[a] > scores[b];
                     });

    std::vector<Move> moves(n);
    for (std::size_t i = 0; i < n; ++i) moves[i] = legal[idx[i]];

    // TT best move first, if it is still legal.
    if (tt_best != 0) {
        const Move best = decode_move(tt_best);
        for (std::size_t i = 0; i < n; ++i) {
            if (moves[i] == best) {
                std::rotate(moves.begin(), moves.begin() + i, moves.begin() + i + 1);
                break;
            }
        }
    }
    return moves;
}

int Engine::quiescence_search(Board& board, int alpha, int beta, int capture_ply,
                               const std::atomic<bool>* stop) {
    check_cancelled(stop);
    ++search_nodes;
    if (alpha >= beta) ++closed_window_nodes;
    if (board.has_game_ended()) {
        const int score = evaluate(board);
        check_cancelled(stop);
        return score;
    }

    int best_score = evaluate(board);
    check_cancelled(stop);
    if (board.get_moves_left() != 2) return best_score;
    const int turn_before = board.get_current_turn();
    const std::vector<Move> moves = order_moves(board, 0);
    for (const Move& move : moves) {
        check_cancelled(stop);
        if (std::abs(board.get_piece(move.new_x, move.new_y)) != 6) continue;
        if (!board.make_move(move)) continue;
        const int terminal_score = board.get_current_turn() != turn_before
            ? -evaluate(board) : evaluate(board);
        board.rollback_move();
        check_cancelled(stop);
        return terminal_score;
    }

    if (capture_ply >= kMaxQuiescencePly || best_score >= beta) return best_score;
    alpha = std::max(alpha, best_score);

    for (const Move& move : moves) {
        check_cancelled(stop);
        if (board.get_piece(move.new_x, move.new_y) == 0) continue;
        if (!board.make_move(move)) continue;
        const bool turn_changed = board.get_current_turn() != turn_before;
        const int score = turn_changed
            ? -quiescence_search(board, -beta, -alpha, capture_ply + 1, stop)
            : quiescence_search(board, alpha, beta, capture_ply + 1, stop);
        board.rollback_move();
        check_cancelled(stop);
        best_score = std::max(best_score, score);
        alpha = std::max(alpha, score);
        if (alpha >= beta) break;
    }
    return best_score;
}

int Engine::negamax_search(Board& board, int depth, int max_depth,
                           int alpha, int beta, Move* root_move, std::size_t action_ply,
                           const std::atomic<bool>* stop) {
    check_cancelled(stop);
    ++search_nodes;
    if (alpha >= beta) ++closed_window_nodes;

    if (board.has_game_ended()) {
        const int score = evaluate(board);
        check_cancelled(stop);
        return score;
    }
    if (board.get_moves_left() == 2 && depth >= max_depth) {
        const int score = quiescence_search(board, alpha, beta, 0, stop);
        check_cancelled(stop);
        return score;
    }

    const Hash key = board.get_hash();
    const int original_alpha = alpha;
    const int original_beta = beta;
    const auto reward_history = [&](const Move& move) {
        const int from = move.old_x * 8 + move.old_y;
        const int to = move.new_x * 8 + move.new_y;
        const auto remaining_depth = static_cast<std::uint64_t>(
            std::max(1, max_depth - depth));
        const std::uint64_t bonus = remaining_depth * remaining_depth;
        auto& history = history_scores[board.get_current_turn()][from][to];
        history = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(history) + bonus,
            std::numeric_limits<std::uint32_t>::max()));
    };
    const TranspositionEntry entry = probe_tt(key);
    const std::uint32_t tt_best = entry.key == key ? entry.best_move : 0;
    if (entry.key == key && entry.depth >= max_depth - depth) {
        if (entry.bound == static_cast<std::uint8_t>(BoundType::Exact)) {
            if (root_move == nullptr) return entry.score;
            if (entry.best_move != 0) *root_move = decode_move(entry.best_move);
        } else if (entry.bound == static_cast<std::uint8_t>(BoundType::Lower)) {
            alpha = std::max(alpha, entry.score);
            if (alpha >= beta) {
                if (original_alpha < original_beta && entry.best_move != 0) {
                    const Move cached_move = decode_move(entry.best_move);
                    const auto legal = board.legal_moves();
                    if (std::find(legal.begin(), legal.end(), cached_move) != legal.end() &&
                        board.get_piece(cached_move.new_x, cached_move.new_y) == 0 &&
                        cached_move.special_move < SpecialMove::PromoteQueen)
                        reward_history(cached_move);
                }
                if (root_move == nullptr) return entry.score;
                if (entry.best_move != 0) *root_move = decode_move(entry.best_move);
            }
        } else {
            beta = std::min(beta, entry.score);
            if (alpha >= beta) {
                if (root_move == nullptr) return entry.score;
                if (entry.best_move != 0) *root_move = decode_move(entry.best_move);
            }
        }
    }

    int best_score = NEGINF;
    std::uint32_t best_packed_move = 0;
    bool found_move = false;
    const int turn_before = board.get_current_turn();

    const std::vector<Move> moves = order_moves(board, tt_best,
        action_ply < killer_moves.size() ? &killer_moves[action_ply] : nullptr);

    for (const Move& move : moves) {
        const bool window_was_open = alpha < beta;
        check_cancelled(stop);
        if (!board.make_move(move)) continue;

        bool turn_changed = board.get_current_turn() != turn_before;
        int child_score;
        if (turn_changed) {
            if (!found_move) {
                child_score = -negamax_search(board, depth + 1, max_depth,
                                              -beta, -alpha, nullptr, action_ply + 1, stop);
            } else {
                child_score = -negamax_search(board, depth + 1, max_depth,
                                              -alpha - 1, -alpha, nullptr, action_ply + 1, stop);
                if (alpha < child_score && child_score < beta)
                    child_score = -negamax_search(board, depth + 1, max_depth,
                                                  -beta, -child_score, nullptr, action_ply + 1, stop);
            }
        } else {
            if (!found_move) {
                child_score = negamax_search(board, depth, max_depth,
                                             alpha, beta, nullptr, action_ply + 1, stop);
            } else {
                child_score = negamax_search(board, depth, max_depth,
                                             alpha, alpha + 1, nullptr, action_ply + 1, stop);
                if (alpha < child_score && child_score < beta)
                    child_score = negamax_search(board, depth, max_depth,
                                                 alpha, beta, nullptr, action_ply + 1, stop);
            }
        }

        check_cancelled(stop);
        if (!found_move || child_score > best_score) {
            best_score = child_score;
            if (root_move) *root_move = move;
            best_packed_move = encode_move(move);
        }
        found_move = true;
        alpha = std::max(alpha, child_score);
        board.rollback_move();

        if (alpha >= beta) {
            if (board.get_piece(move.new_x, move.new_y) == 0 &&
                move.special_move < SpecialMove::PromoteQueen) {
                if (killer_moves.size() <= action_ply) killer_moves.resize(action_ply + 1);
                auto& killers = killer_moves[action_ply];
                const auto packed = encode_move(move);
                if (killers[0] != packed) {
                    killers[1] = killers[0];
                    killers[0] = packed;
                }
                if (window_was_open && child_score >= beta) reward_history(move);
            }
            break;
        }
    }

    if (!found_move) best_score = evaluate(board);
    check_cancelled(stop);
    const BoundType bound = best_score <= original_alpha
        ? BoundType::Upper
        : (best_score >= original_beta ? BoundType::Lower : BoundType::Exact);
    const int new_depth = std::max(0, max_depth - depth);
    store_tt({key, best_score, static_cast<std::uint8_t>(new_depth),
              static_cast<std::uint8_t>(bound), best_packed_move}, new_depth);
    return best_score;
}

int Engine::parallel_root_search(const Board& position, int max_depth, Move& root_move,
                                  const std::atomic<bool>* stop) {
    check_cancelled(stop);
    Board first_board = position;
    const Hash key = position.get_hash();
    const auto cached = probe_tt(key);
    const auto moves = order_moves(first_board, cached.key == key ? cached.best_move : 0,
        killer_moves.empty() ? nullptr : &killer_moves[0]);
    if (moves.size() < 2)
        return negamax_search(first_board, 0, max_depth, NEGINF, INF, &root_move, 0, stop);

    const std::size_t participants = std::min<std::size_t>(search_threads, moves.size() - 1);
    prepare_root_workers(participants - 1);
    ++search_nodes;
    const int turn = position.get_current_turn();
    const auto search_move = [&](Engine& context, Board& board, const Move& move, int alpha) {
        context.check_cancelled(stop);
        if (!board.make_move(move)) throw std::logic_error("generated root move is illegal");
        const bool changed = board.get_current_turn() != turn;
        const int score = changed
            ? -context.negamax_search(board, 1, max_depth, -INF, -alpha, nullptr, 1, stop)
            : context.negamax_search(board, 0, max_depth, alpha, INF, nullptr, 1, stop);
        board.rollback_move();
        context.check_cancelled(stop);
        return score;
    };
    const auto publish = [&](int score, const Move& move) {
        check_cancelled(stop);
        store_tt({key, score, static_cast<std::uint8_t>(max_depth),
            static_cast<std::uint8_t>(score <= NEGINF ? BoundType::Upper :
                (score >= INF ? BoundType::Lower : BoundType::Exact)), encode_move(move)}, max_depth);
        root_move = move;
        return score;
    };
    std::vector<int> scores(moves.size(), NEGINF);
    scores[0] = search_move(*this, first_board, moves[0], NEGINF);
    const int first_alpha = scores[0];
    if (first_alpha >= INF) return publish(first_alpha, moves[0]);
    std::atomic<std::size_t> next{1};
    std::atomic<bool> abort{false};
    std::vector<std::exception_ptr> failures(participants);
    std::vector<std::thread> threads;
    threads.reserve(participants - 1);
    parallel_abort = &abort;
    for (std::size_t i = 1; i < participants; ++i) root_workers[i - 1]->parallel_abort = &abort;
    const auto run = [&](std::size_t index) {
        Engine& context = index == 0 ? *this : *root_workers[index - 1];
        try {
            Board board = position;
            for (;;) {
                context.check_cancelled(stop);
                const auto job = next.fetch_add(1, std::memory_order_relaxed);
                if (job >= moves.size()) break;
                scores[job] = search_move(context, board, moves[job], first_alpha);
            }
        } catch (const SearchCancelled&) {
            abort.store(true, std::memory_order_relaxed);
        } catch (...) {
            failures[index] = std::current_exception();
            abort.store(true, std::memory_order_relaxed);
        }
    };
    std::exception_ptr launch_failure;
    try {
        for (std::size_t i = 1; i < participants; ++i) threads.emplace_back(run, i);
        run(0);
    } catch (...) {
        launch_failure = std::current_exception();
        abort.store(true, std::memory_order_relaxed);
    }
    for (auto& thread : threads) thread.join();
    parallel_abort = nullptr;
    for (std::size_t i = 1; i < participants; ++i) root_workers[i - 1]->parallel_abort = nullptr;
    if (launch_failure) std::rethrow_exception(launch_failure);
    for (const auto& failure : failures) if (failure) std::rethrow_exception(failure);
    check_cancelled(stop);
    if (abort.load(std::memory_order_relaxed)) throw SearchCancelled{};

    // Scores at/below first_alpha are upper bounds, never exact tie witnesses.
    int best_score = scores[0];
    Move selected = moves[0];
    for (std::size_t i = 1; i < moves.size(); ++i) {
        if (scores[i] > best_score) {
            best_score = scores[i];
            selected = moves[i];
        }
    }
    return publish(best_score, selected);
}

bool Engine::find_best_move(const Board& position, int requested_depth,
                            const std::atomic<bool>* stop) {
    int search_depth = requested_depth < 0 ? default_search_depth : requested_depth;
    if (search_depth < 1) throw std::invalid_argument("search depth must be positive");

    best_move = Move{};
    search_nodes = 0;
    closed_window_nodes = 0;
    killer_moves.clear();
    history_scores = {};
    for (auto& worker : root_workers) {
        worker->search_nodes = 0;
        worker->closed_window_nodes = 0;
        worker->killer_moves.clear();
        worker->history_scores = {};
    }
    const auto collect_counters = [&] {
        for (const auto& worker : root_workers) {
            search_nodes += worker->search_nodes;
            closed_window_nodes += worker->closed_window_nodes;
        }
    };

    if (stop && stop->load(std::memory_order_relaxed)) return false;
    if (!is_turn(position)) return false;

    try {
        check_cancelled(stop);
        Board search_board = position;
        for (int iteration_depth = 1; iteration_depth <= search_depth; ++iteration_depth) {
            check_cancelled(stop);
            Move iteration_move{};
            if (search_threads == 1)
                negamax_search(search_board, 0, iteration_depth,
                               NEGINF, INF, &iteration_move, 0, stop);
            else
                parallel_root_search(position, iteration_depth, iteration_move, stop);
            check_cancelled(stop);
            best_move = iteration_move;
        }
    } catch (const SearchCancelled&) {
        best_move = Move{};
        collect_counters();
        return false;
    } catch (...) {
        best_move = Move{};
        collect_counters();
        throw;
    }
    collect_counters();
    return best_move.old_x != -1;
}

const Move& Engine::get_best_move() const { return best_move; }
