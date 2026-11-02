#include "../UI files/ui.hpp"
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct SearchGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool released = false;
    bool timedOut = false;
    std::thread::id worker;
    int evaluations = 0;
    bool snapshotStable = true;
    bool throwOnEvaluation = false;
    bool evaluatorDestroyed = false;
    bool destroyedBeforeRelease = false;

    void release() {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        changed.notify_all();
    }

    bool waitForEntry() {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, 1s, [&] { return entered; });
    }
};

class GatedEvaluator final : public Evaluator {
    std::shared_ptr<SearchGate> gate;
public:
    explicit GatedEvaluator(std::shared_ptr<SearchGate> gate) : gate(std::move(gate)) {}
    ~GatedEvaluator() override {
        std::lock_guard<std::mutex> lock(gate->mutex);
        gate->evaluatorDestroyed = true;
        gate->destroyedBeforeRelease = gate->entered && !gate->released;
    }
    int evaluate(const Board& board) const override {
        const auto before = board.export_fen();
        std::unique_lock<std::mutex> lock(gate->mutex);
        ++gate->evaluations;
        gate->worker = std::this_thread::get_id();
        gate->entered = true;
        gate->changed.notify_all();
        if (!gate->changed.wait_for(lock, 2s, [&] { return gate->released; })) {
            gate->timedOut = true;
            gate->released = true;
        }
        gate->snapshotStable = gate->snapshotStable && board.export_fen() == before;
        if (gate->throwOnEvaluation) throw std::runtime_error("controlled evaluator failure");
        return 0;
    }
};

struct ReleaseOnExit {
    std::shared_ptr<SearchGate> gate;
    ~ReleaseOnExit() { gate->release(); }
};

int fullSearchEvaluations(const Board& board) {
    auto gate = std::make_shared<SearchGate>();
    gate->release();
    Engine engine(board.get_current_turn(), 1, std::make_unique<GatedEvaluator>(gate));
    require(engine.find_best_move(board), "full-search counting control found no move");
    require(gate->evaluations > 10, "full-search control was too small to distinguish cancellation");
    return gate->evaluations;
}

struct ChessUITestAccess {
    static void start(ChessUI& ui) {
        ui.window.setVisible(false);
        ui.soundEffects = false;
        ui.startGame(0);
        require(ui.game.get_current_turn() == 0 && ui.game.get_moves_left() == 1 &&
                !ui.game.legal_moves().empty(), "initial White search fixture is invalid");
    }
    static void update(ChessUI& ui) { ui.updateEngine(); }
    static std::string position(const ChessUI& ui) { return ui.game.export_fen(); }
    static std::size_t moves(const ChessUI& ui) { return ui.historyTree.size(); }
    static bool stalled(const ChessUI& ui) { return ui.engineStalled; }
    static bool failed(const ChessUI& ui) { return ui.engineSearchFailed; }
    static void syntheticFailure(ChessUI& ui) { ui.engineStalled = ui.engineSearchFailed = true; }
    static bool apply(ChessUI& ui, const Move& move) { return ui.applyMove(move); }
    static std::uint64_t generation(const ChessUI& ui) { return ui.positionGeneration; }
    static bool stopRequested(const ChessUI& ui) {
        return ui.searchStop && ui.searchStop->load(std::memory_order_relaxed);
    }
    static bool pending(const ChessUI& ui) { return ui.searchFuture.valid(); }
    static bool ready(ChessUI& ui) {
        return ui.searchFuture.valid() && ui.searchFuture.wait_for(0s) == std::future_status::ready;
    }
    static void seedHistory(ChessUI& ui) {
        configure(ui, 0, 0);
        for (int action = 0; action < 4; ++action) {
            const auto legal = ui.game.legal_moves();
            require(!legal.empty() && ui.applyMove(legal.front()), "could not seed history");
        }
        require(!ui.game.has_game_ended() && ui.game.get_current_turn() == 0,
                "history fixture did not return to the White engine");
    }
    static void frame(ChessUI& ui) { ui.handleEvents(); ui.draw(); ui.updateEngine(); }
    static bool flipped(const ChessUI& ui) { return ui.manualBoardFlip; }
    static void configure(ChessUI& ui, int engineSide, int turn) {
        ui.startGame(engineSide);
        require(ui.game.import_fen(std::string("3k4/8/8/8/8/8/8/3K4 ") +
                (turn == 0 ? "w" : "b") + " - 2 0 1"), "side fixture import failed");
        ui.startingBoard = ui.game;
    }
    static void playerMovement(ChessUI& ui, int mode) {
        ui.playerMoveMode = mode == 0 ? ChessUI::PlayerMoveMode::Slide
            : (mode == 1 ? ChessUI::PlayerMoveMode::Drag : ChessUI::PlayerMoveMode::PointAndClick);
    }
    static void startHumanGame(ChessUI& ui) {
        ui.startGame(1);
        playerMovement(ui, 0);
    }
    static bool engineTurn(const ChessUI& ui) { return ui.engineTurn(); }
    static int historyParent(const ChessUI& ui) { return ui.historyTree.back().parent; }
    static std::string historyPosition(const ChessUI& ui, int node) {
        return ui.historyTree.at(node).move.boardAfterMove.export_fen();
    }
    static Board board(const ChessUI& ui) { return ui.game; }
    // Synthetic missing-invalidation mutation exercises the root-hash safety net.
    static void importWithoutInvalidation(ChessUI& ui) {
        require(ui.game.import_fen("3k4/8/8/8/8/8/8/3K4 w - 2 0 1"),
                "hash-only mutation fixture import failed");
    }
    static bool animating(const ChessUI& ui) { return ui.engineMoveAnimating; }
    static void noLegalMoves(ChessUI& ui) {
        require(ui.game.import_fen("PPPPPPPk/PPPPPPPP/PPPPPPPP/PPPPPPPP/PPPPPPPP/PPPPPPPP/PPPPPPPP/KPPPPPPP w - 1 0 1"),
                "no-legal-move fixture import failed");
        require(!ui.game.has_game_ended() && ui.game.legal_moves().empty(), "fixture has legal moves or is terminal");
        ui.startingBoard = ui.game;
    }

    static bool settings(const ChessUI& ui) { return ui.currentScreen == ChessUI::Screen::Settings; }
    static bool open(const ChessUI& ui) { return ui.window.isOpen(); }
#ifdef _WIN32
    static void click(ChessUI& ui, int x, int y) {
        SendMessageW(ui.window.getNativeHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        SendMessageW(ui.window.getNativeHandle(), WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        ui.handleEvents();
    }
    static void navigate(ChessUI& ui, int node) {
        const auto point = ui.getHistoryNodePositions().at(node);
        click(ui, static_cast<int>(point.x + 20), static_cast<int>(point.y - ui.historyScroll + 20));
        require(ui.currentHistoryNode == node && position(ui) == historyPosition(ui, node),
                "history click did not restore the selected board");
    }
    static void clickSquare(ChessUI& ui, int row, int col) {
        click(ui, ui.boardX + ui.displayCol(col) * ui.squareSize + ui.squareSize / 2,
                ui.boardY + ui.displayRow(row) * ui.squareSize + ui.squareSize / 2);
    }
    static void close(ChessUI& ui) {
        SendMessageW(ui.window.getNativeHandle(), WM_CLOSE, 0, 0);
        ui.run();
    }
#endif
};

void waitForResult(ChessUI& ui) {
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (!ChessUITestAccess::ready(ui) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(2ms);
    require(ChessUITestAccess::ready(ui), "worker did not finish within test deadline");
}

void consumeResultAndSlide(ChessUI& ui) {
    waitForResult(ui);
    const auto before = ChessUITestAccess::position(ui);
    const auto movesBefore = ChessUITestAccess::moves(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::animating(ui) && !ChessUITestAccess::pending(ui) &&
            ChessUITestAccess::moves(ui) == movesBefore && ChessUITestAccess::position(ui) == before,
            "valid worker result did not start an unapplied slide");
    std::this_thread::sleep_for(350ms);
    ChessUITestAccess::update(ui);
    require(!ChessUITestAccess::animating(ui) && ChessUITestAccess::moves(ui) == movesBefore + 1 &&
            ChessUITestAccess::position(ui) != before, "engine slide did not apply exactly one move");
}

void test_hash_only_mutation_cancels_and_discards_worker() {
    auto gate = std::make_shared<SearchGate>();
    ChessUI ui(0, 1, std::make_unique<GatedEvaluator>(gate));
    ReleaseOnExit release{gate};
    ChessUITestAccess::start(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    require(gate->waitForEntry(), "hash-only search never reached evaluator");
    ChessUITestAccess::importWithoutInvalidation(ui);
    const auto selected = ChessUITestAccess::position(ui);
    ChessUITestAccess::update(ui);
    gate->release();
    waitForResult(ui);
    ChessUITestAccess::update(ui);
    require(!ChessUITestAccess::pending(ui) && !ChessUITestAccess::animating(ui) &&
            !ChessUITestAccess::stalled(ui) && !ChessUITestAccess::failed(ui) &&
            ChessUITestAccess::position(ui) == selected && ChessUITestAccess::moves(ui) == 0,
            "hash-only mutation consumed an obsolete result or changed error state");
    std::lock_guard<std::mutex> lock(gate->mutex);
    require(gate->evaluations == 1 && !gate->timedOut,
            "hash-only mutation did not promptly cancel real search");
}

void test_successful_apply_immediately_invalidates_error_state() {
    ChessUI ui;
    ChessUITestAccess::start(ui);
    // Synthetic: UI input cannot apply an engine move after a failed search.
    ChessUITestAccess::syntheticFailure(ui);
    const auto generation = ChessUITestAccess::generation(ui);
    require(!ChessUITestAccess::apply(ui, Move{-1, -1, -1, -1}) &&
            ChessUITestAccess::stalled(ui) && ChessUITestAccess::failed(ui) &&
            ChessUITestAccess::generation(ui) == generation,
            "rejected move invalidated error state or position generation");
    require(ChessUITestAccess::apply(ui, ChessUITestAccess::board(ui).legal_moves().front()),
            "apply error reset fixture rejected a legal move");
    require(!ChessUITestAccess::stalled(ui) && !ChessUITestAccess::failed(ui) &&
            ChessUITestAccess::generation(ui) == generation + 1,
            "successful move did not immediately invalidate both errors and generation");
}

void test_destruction_without_close_cancels_before_joining() {
    auto gate = std::make_shared<SearchGate>();
    auto ui = std::make_unique<ChessUI>(0, 1, std::make_unique<GatedEvaluator>(gate));
    ReleaseOnExit release{gate};
    ChessUITestAccess::start(*ui);
    const int fullEvaluations = fullSearchEvaluations(ChessUITestAccess::board(*ui));
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(*ui);
    require(gate->waitForEntry(), "destructor fixture search never reached evaluator");
    std::thread releaser([gate] { std::this_thread::sleep_for(150ms); gate->release(); });
    const auto start = std::chrono::steady_clock::now();
    ui.reset();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    releaser.join();
    require(elapsed >= 100ms && elapsed < 1s, "destructor failed to join promptly after evaluator returned");
    require(gate->evaluations == 1 && gate->evaluatorDestroyed && !gate->destroyedBeforeRelease && !gate->timedOut,
            "destructor did not cancel before joining and destroying the evaluator");
    require(gate->evaluations * 10 < fullEvaluations, "destruction did not save most search evaluations");
    std::cout << "destructor_evaluations=" << gate->evaluations << " full=" << fullEvaluations << '\n';
}

void test_search_launch_does_not_block_ui() {
    auto gate = std::make_shared<SearchGate>();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    ReleaseOnExit release{gate};
    ChessUITestAccess::start(ui);
    const auto original = ChessUITestAccess::position(ui);
    std::this_thread::sleep_for(550ms);
    const auto start = std::chrono::steady_clock::now();
    ChessUITestAccess::update(ui);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    std::cout << "launch_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << '\n';
    require(elapsed < 400ms, "search launch blocked the UI on the gated evaluator");
    require(gate->waitForEntry(), "search did not reach the evaluator");
    {
        std::lock_guard<std::mutex> lock(gate->mutex);
        require(gate->worker != std::this_thread::get_id(), "search ran on the UI thread");
        require(!gate->timedOut, "test watchdog released search before assertion");
    }
    require(ChessUITestAccess::position(ui) == original, "worker changed the live board");
    const auto framesStart = std::chrono::steady_clock::now();
#ifdef _WIN32
    const bool flipped = ChessUITestAccess::flipped(ui);
    ChessUITestAccess::click(ui, 775, 38);
    require(ChessUITestAccess::flipped(ui) != flipped, "flip event was not handled during search");
#endif
    for (int frame = 0; frame < 3; ++frame) ChessUITestAccess::frame(ui);
    const auto framesElapsed = std::chrono::steady_clock::now() - framesStart;
    std::cout << "gated_frames_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(framesElapsed).count() << '\n';
    require(framesElapsed < 400ms, "event/draw/update frames blocked on search");
    std::lock_guard<std::mutex> lock(gate->mutex);
    require(gate->evaluations == 1 && !gate->timedOut, "frames launched duplicate workers or waited for watchdog");
}

void test_restart_same_position_discards_old_result() {
    auto gate = std::make_shared<SearchGate>();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    ReleaseOnExit release{gate};
    ChessUITestAccess::start(ui);
    const auto original = ChessUITestAccess::position(ui);
    const auto generation = ChessUITestAccess::generation(ui);
    const int fullEvaluations = fullSearchEvaluations(ChessUITestAccess::board(ui));
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    require(gate->waitForEntry(), "restart fixture search never started");
    const auto start = std::chrono::steady_clock::now();
    ChessUITestAccess::start(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::generation(ui) == generation + 1,
            "same-board restart did not advance the position generation");
    require(std::chrono::steady_clock::now() - start < 400ms, "restart waited for active search");
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    {
        std::lock_guard<std::mutex> lock(gate->mutex);
        require(gate->evaluations == 1 && !gate->timedOut, "restart launched overlapping search");
    }
    gate->release();
    waitForResult(ui);
    {
        std::lock_guard<std::mutex> lock(gate->mutex);
        require(gate->evaluations == 1, "restart did not cancel real search after evaluator returned");
        require(gate->evaluations * 10 < fullEvaluations, "restart did not save most search evaluations");
        std::cout << "restart_evaluations=" << gate->evaluations << " full=" << fullEvaluations << '\n';
    }
    ChessUITestAccess::update(ui);
    require(!ChessUITestAccess::stalled(ui) && !ChessUITestAccess::failed(ui),
            "cancelled search marked the restarted game as failed or stalled");
    require(ChessUITestAccess::moves(ui) == 0 && ChessUITestAccess::position(ui) == original,
            "restart to the same position consumed the old search result");
    ChessUITestAccess::update(ui);
    consumeResultAndSlide(ui);
    require(ChessUITestAccess::moves(ui) == 1, "replacement search did not apply its result");
    for (int frame = 0; frame < 5; ++frame) ChessUITestAccess::update(ui);
    require(ChessUITestAccess::moves(ui) == 1, "result was consumed more than once");
}

#ifdef _WIN32
void test_history_navigation_discards_old_result() {
    for (int node : {3, 0, 1}) {
        auto gate = std::make_shared<SearchGate>();
        ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate), std::make_unique<GatedEvaluator>(gate));
        ReleaseOnExit release{gate};
        ChessUITestAccess::start(ui);
        ChessUITestAccess::seedHistory(ui);
        const int fullEvaluations = fullSearchEvaluations(ChessUITestAccess::board(ui));
        std::this_thread::sleep_for(550ms);
        ChessUITestAccess::update(ui);
        require(gate->waitForEntry(), "navigation fixture search never started");
        const auto searched = ChessUITestAccess::position(ui);
        ChessUITestAccess::navigate(ui, node);
        const auto selected = ChessUITestAccess::position(ui);
        require((selected == searched) == (node == 3), "history same/different-board fixture is invalid");
        const bool engineTurn = ChessUITestAccess::engineTurn(ui);
        require(engineTurn == (node != 1), "history fixture selected the wrong controller");
        std::this_thread::sleep_for(550ms);
        ChessUITestAccess::update(ui);
        {
            std::lock_guard<std::mutex> lock(gate->mutex);
            require(gate->evaluations == 1 && !gate->timedOut,
                    "history selection overlapped the old worker or waited for its watchdog");
        }
        gate->release();
        waitForResult(ui);
        {
            std::lock_guard<std::mutex> lock(gate->mutex);
            require(gate->evaluations == 1, "history selection did not cancel real search after evaluator returned");
            require(gate->evaluations * 10 < fullEvaluations, "history selection did not save most search evaluations");
            std::cout << "history_evaluations=" << gate->evaluations << " full=" << fullEvaluations << '\n';
        }
        ChessUITestAccess::update(ui);
        require(!ChessUITestAccess::stalled(ui) && !ChessUITestAccess::failed(ui),
                "cancelled history search changed error flags");
        require(ChessUITestAccess::moves(ui) == 4 && ChessUITestAccess::position(ui) == selected &&
                !ChessUITestAccess::animating(ui) && !ChessUITestAccess::pending(ui),
                "history navigation consumed the invalidated search result");
        ChessUITestAccess::update(ui);
        require(ChessUITestAccess::pending(ui) == engineTurn,
                "selected history position did not continue according to its PvE turn");
        if (engineTurn) {
            consumeResultAndSlide(ui);
            require(ChessUITestAccess::moves(ui) == 5 && ChessUITestAccess::historyParent(ui) == node,
                    "history continuation did not branch from the selected node");
        } else {
            require(ChessUITestAccess::position(ui) == selected && ChessUITestAccess::moves(ui) == 4,
                    "human history turn advanced without input");
        }
        std::lock_guard<std::mutex> lock(gate->mutex);
        require(gate->snapshotStable && !gate->timedOut, "live navigation changed the worker board snapshot");
    }
}

void test_history_selection_resets_delay_without_play() {
    for (int node : {3, 0, 1}) {
        auto gate = std::make_shared<SearchGate>();
        gate->release();
        ChessUI ui(0, 1, std::make_unique<GatedEvaluator>(gate));
        ChessUITestAccess::start(ui);
        ChessUITestAccess::seedHistory(ui);
        std::this_thread::sleep_for(550ms);
        ChessUITestAccess::navigate(ui, node);
        const auto selected = ChessUITestAccess::position(ui);
        ChessUITestAccess::update(ui);
        require(!ChessUITestAccess::pending(ui) && !ChessUITestAccess::animating(ui) &&
                ChessUITestAccess::position(ui) == selected, "history selection skipped its reset delay");
        std::this_thread::sleep_for(550ms);
        ChessUITestAccess::update(ui);
        require(ChessUITestAccess::pending(ui) == (node != 1),
                "history selection required PLAY or searched a human turn");
        if (node != 1) {
            consumeResultAndSlide(ui);
            require(ChessUITestAccess::historyParent(ui) == node, "resumed history used the wrong parent");
        } else {
            require(ChessUITestAccess::moves(ui) == 4 && ChessUITestAccess::position(ui) == selected &&
                    gate->evaluations == 0, "human history turn invoked search or moved");
        }
    }
}
#endif

void test_worker_exception_is_handled_on_ui_thread() {
    auto gate = std::make_shared<SearchGate>();
    gate->throwOnEvaluation = true;
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    ReleaseOnExit release{gate};
    ChessUITestAccess::start(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    require(gate->waitForEntry(), "exception fixture search never started");
    gate->release();
    waitForResult(ui);
    bool escaped = false;
    try { ChessUITestAccess::update(ui); }
    catch (...) { escaped = true; }
    require(!escaped, "worker exception escaped UI result polling");
    require(ChessUITestAccess::stalled(ui) && ChessUITestAccess::failed(ui) && ChessUITestAccess::moves(ui) == 0 &&
            !ChessUITestAccess::pending(ui), "failed search was applied or not stopped");
    for (int frame = 0; frame < 5; ++frame) ChessUITestAccess::update(ui);
    require(!ChessUITestAccess::pending(ui), "failed search retried every frame");
    gate->throwOnEvaluation = false;
    ChessUITestAccess::start(ui);
    require(!ChessUITestAccess::stalled(ui) && !ChessUITestAccess::failed(ui),
            "restart did not immediately clear both search error flags");
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    consumeResultAndSlide(ui);
    require(ChessUITestAccess::moves(ui) == 1 && !ChessUITestAccess::stalled(ui) && !ChessUITestAccess::failed(ui),
            "restart did not recover after a handled worker exception");
}

void test_only_one_worker_across_different_engine_requests() {
    auto white = std::make_shared<SearchGate>();
    auto black = std::make_shared<SearchGate>();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(white), std::make_unique<GatedEvaluator>(black));
    ReleaseOnExit releaseWhite{white}, releaseBlack{black};
    ChessUITestAccess::start(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    require(white->waitForEntry(), "white search did not start");
    ChessUITestAccess::configure(ui, 1, 1);
    require(ChessUITestAccess::pending(ui), "switching engine sides dropped the outstanding future");
    std::this_thread::sleep_for(550ms);
    for (int frame = 0; frame < 5; ++frame) ChessUITestAccess::update(ui);
    require(ChessUITestAccess::pending(ui), "side restart lost the running old worker");
    {
        std::lock_guard<std::mutex> lock(white->mutex);
        require(white->evaluations == 1 && !white->timedOut,
                "switching engine sides duplicated the old search or waited for its watchdog");
    }
    {
        std::lock_guard<std::mutex> lock(black->mutex);
        require(!black->entered, "black worker overlapped an invalidated white worker");
    }
    white->release();
    waitForResult(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::moves(ui) == 0, "old white result applied to black position");
    ChessUITestAccess::update(ui);
    require(black->waitForEntry(), "replacement black search did not start after white finished");
    for (int frame = 0; frame < 5; ++frame) ChessUITestAccess::update(ui);
    {
        std::lock_guard<std::mutex> lock(black->mutex);
        require(black->worker != std::this_thread::get_id() && black->evaluations == 1 && !black->timedOut,
                "replacement Black engine ran on the UI thread or launched duplicate workers");
    }
    black->release();
    consumeResultAndSlide(ui);
    require(ChessUITestAccess::moves(ui) == 1 && !ChessUITestAccess::stalled(ui), "replacement black search did not apply");
}

void test_restart_discards_an_already_ready_result() {
    auto gate = std::make_shared<SearchGate>();
    gate->release();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    ChessUITestAccess::start(ui);
    const auto before = ChessUITestAccess::position(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    waitForResult(ui);
    ChessUITestAccess::start(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::moves(ui) == 0 && ChessUITestAccess::position(ui) == before &&
            !ChessUITestAccess::pending(ui), "restart consumed an already completed stale result");
}

void test_pve_sides_use_only_the_assigned_engine() {
    auto white = std::make_shared<SearchGate>();
    auto black = std::make_shared<SearchGate>();
    white->release();
    black->release();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(white), std::make_unique<GatedEvaluator>(black));
    ChessUITestAccess::start(ui);
    for (int side : {0, 1}) {
        for (int turn : {0, 1}) {
            ChessUITestAccess::configure(ui, side, turn);
            const auto before = ChessUITestAccess::position(ui);
            const int whiteBefore = white->evaluations, blackBefore = black->evaluations;
            std::this_thread::sleep_for(550ms);
            ChessUITestAccess::update(ui);
            const bool engineTurn = side == turn;
            require(ChessUITestAccess::pending(ui) == engineTurn,
                    "PvE side launched an unassigned engine or no engine");
            if (engineTurn) {
                waitForResult(ui);
                require(ChessUITestAccess::position(ui) == before && ChessUITestAccess::moves(ui) == 0,
                        "ready worker applied the move without UI consumption");
                consumeResultAndSlide(ui);
                require(ChessUITestAccess::moves(ui) == 1 && !ChessUITestAccess::stalled(ui),
                        "assigned engine move was not applied");
                // A warm TT search need not call its evaluator again.
                require(turn == 0 ? black->evaluations == blackBefore : white->evaluations == whiteBefore,
                        "worker touched the unassigned evaluator");
            } else {
                for (int frame = 0; frame < 5; ++frame) ChessUITestAccess::update(ui);
                require(!ChessUITestAccess::pending(ui) && ChessUITestAccess::position(ui) == before &&
                        white->evaluations == whiteBefore && black->evaluations == blackBefore,
                        "human turn invoked search");
            }
        }
    }
}

void test_delay_and_slide_gates_are_preserved() {
    auto gate = std::make_shared<SearchGate>();
    gate->release();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    for (int movement : {0, 1, 2}) {
        ChessUITestAccess::start(ui);
        ChessUITestAccess::playerMovement(ui, movement);
        const auto before = ChessUITestAccess::position(ui);
        ChessUITestAccess::update(ui);
        require(!ChessUITestAccess::pending(ui), "search skipped the existing pre-search delay");
        std::this_thread::sleep_for(350ms);
        ChessUITestAccess::update(ui);
        require(!ChessUITestAccess::pending(ui) && !ChessUITestAccess::animating(ui),
                "search launched before the existing half-second delay");
        std::this_thread::sleep_for(200ms);
        ChessUITestAccess::update(ui);
        waitForResult(ui);
        ChessUITestAccess::update(ui);
        require(ChessUITestAccess::animating(ui) && ChessUITestAccess::moves(ui) == 0 &&
                ChessUITestAccess::position(ui) == before && !ChessUITestAccess::pending(ui),
                "engine result skipped its mandatory slide for a player movement setting");
        std::this_thread::sleep_for(100ms);
        ChessUITestAccess::update(ui);
        require(ChessUITestAccess::animating(ui) && ChessUITestAccess::moves(ui) == 0 &&
                ChessUITestAccess::position(ui) == before, "slide applied its move too early");
        std::this_thread::sleep_for(250ms);
        ChessUITestAccess::update(ui);
        require(!ChessUITestAccess::animating(ui) && ChessUITestAccess::moves(ui) == 1,
                "slide did not apply exactly one move");
        ChessUITestAccess::update(ui);
        require(!ChessUITestAccess::pending(ui) && ChessUITestAccess::moves(ui) == 1,
                "animation started a duplicate search/move");
    }
}

void test_false_search_stops_without_applying_a_move() {
    auto gate = std::make_shared<SearchGate>();
    gate->release();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    ChessUITestAccess::start(ui);
    ChessUITestAccess::noLegalMoves(ui);
    const auto before = ChessUITestAccess::position(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    waitForResult(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::stalled(ui) && !ChessUITestAccess::failed(ui) &&
            ChessUITestAccess::position(ui) == before && ChessUITestAccess::moves(ui) == 0,
            "false search was treated as a move or an exception");
    ChessUITestAccess::update(ui);
    require(!ChessUITestAccess::pending(ui), "false search retried every frame");
}

void test_stale_exception_is_discarded_and_restart_recovers() {
    for (bool alreadyReady : {false, true}) {
        auto gate = std::make_shared<SearchGate>();
        gate->throwOnEvaluation = true;
        ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
        ReleaseOnExit release{gate};
        ChessUITestAccess::start(ui);
        std::this_thread::sleep_for(550ms);
        ChessUITestAccess::update(ui);
        require(gate->waitForEntry(), "stale-exception search never started");
        if (alreadyReady) {
            gate->release();
            waitForResult(ui);
        }
        ChessUITestAccess::start(ui);
        gate->release();
        waitForResult(ui);
        ChessUITestAccess::update(ui);
        require(!ChessUITestAccess::stalled(ui) && !ChessUITestAccess::failed(ui) &&
                !ChessUITestAccess::pending(ui) && ChessUITestAccess::moves(ui) == 0,
                "stale exception stopped the restarted game");
        gate->throwOnEvaluation = false;
        std::this_thread::sleep_for(550ms);
        ChessUITestAccess::update(ui);
        consumeResultAndSlide(ui);
        require(ChessUITestAccess::moves(ui) == 1 && !ChessUITestAccess::stalled(ui),
                "restart did not recover after stale exception");
    }
}

#ifdef _WIN32
void test_history_selection_immediately_clears_failed_search() {
    auto gate = std::make_shared<SearchGate>();
    gate->throwOnEvaluation = true;
    gate->release();
    ChessUI ui(0, 1, std::make_unique<GatedEvaluator>(gate));
    ChessUITestAccess::start(ui);
    ChessUITestAccess::seedHistory(ui);
    const auto selected = ChessUITestAccess::position(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    waitForResult(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::stalled(ui) && ChessUITestAccess::failed(ui),
            "history recovery fixture did not handle an evaluator exception");
    const auto generation = ChessUITestAccess::generation(ui);
    ChessUITestAccess::navigate(ui, 3);
    require(!ChessUITestAccess::stalled(ui) && !ChessUITestAccess::failed(ui) &&
            ChessUITestAccess::generation(ui) == generation + 1 &&
            ChessUITestAccess::position(ui) == selected && !ChessUITestAccess::pending(ui),
            "same-board history selection did not immediately clear both failure flags");
}

void test_settings_holds_ready_result_until_game_resumes() {
    auto gate = std::make_shared<SearchGate>();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    ReleaseOnExit release{gate};
    ChessUITestAccess::start(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    require(gate->waitForEntry(), "settings search never started");
    ChessUITestAccess::click(ui, 943, 38);
    require(ChessUITestAccess::settings(ui), "settings event not handled during search");
    ChessUITestAccess::frame(ui);
    gate->release();
    waitForResult(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::pending(ui) && ChessUITestAccess::moves(ui) == 0, "settings consumed ready search result");
    require(!ChessUITestAccess::animating(ui), "settings started engine animation");
    ChessUITestAccess::click(ui, 46, 54);
    require(!ChessUITestAccess::settings(ui), "settings back event did not return to the game");
    consumeResultAndSlide(ui);
    require(ChessUITestAccess::moves(ui) == 1, "return from settings lost the valid result");
}

void test_stale_search_does_not_delay_human_animation() {
    auto gate = std::make_shared<SearchGate>();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    ReleaseOnExit release{gate};
    ChessUITestAccess::start(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    require(gate->waitForEntry(), "human-animation search never started");
    ChessUITestAccess::startHumanGame(ui);
    Board expected = ChessUITestAccess::board(ui);
    require(expected.make_move(Move{1, 0, 2, 0}), "human click fixture is not legal");
    const auto before = ChessUITestAccess::position(ui);
    ChessUITestAccess::clickSquare(ui, 1, 0);
    ChessUITestAccess::clickSquare(ui, 2, 0);
    require(ChessUITestAccess::animating(ui) && ChessUITestAccess::moves(ui) == 0 &&
            ChessUITestAccess::position(ui) == before, "human move click did not start an unapplied animation");
    std::this_thread::sleep_for(350ms);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::moves(ui) == 1 && !ChessUITestAccess::animating(ui) &&
            ChessUITestAccess::position(ui) == expected.export_fen() && ChessUITestAccess::pending(ui),
            "invalidated search delayed the new human game's animation");
    {
        std::lock_guard<std::mutex> lock(gate->mutex);
        require(!gate->released && !gate->timedOut, "old search was not still gated during human move");
    }
    gate->release();
    waitForResult(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::moves(ui) == 1 && !ChessUITestAccess::animating(ui) &&
            !ChessUITestAccess::pending(ui) && ChessUITestAccess::position(ui) == expected.export_fen(),
            "old result changed the board after the real human slide");
}

void test_close_returns_but_destruction_joins_worker() {
    for (bool throws : {false, true}) {
        auto gate = std::make_shared<SearchGate>();
        gate->throwOnEvaluation = throws;
        auto ui = std::make_unique<ChessUI>(1, 1, std::make_unique<GatedEvaluator>(gate));
        ReleaseOnExit release{gate};
        ChessUITestAccess::start(*ui);
        const int fullEvaluations = fullSearchEvaluations(ChessUITestAccess::board(*ui));
        std::this_thread::sleep_for(550ms);
        ChessUITestAccess::update(*ui);
        require(gate->waitForEntry(), "shutdown search never started");
        const auto closeStart = std::chrono::steady_clock::now();
        ChessUITestAccess::close(*ui);
        require(!ChessUITestAccess::open(*ui) && std::chrono::steady_clock::now() - closeStart < 400ms,
                "window close/run waited for search");
        require(ChessUITestAccess::stopRequested(*ui), "close event did not request cooperative stop");
        std::thread releaser([gate] { std::this_thread::sleep_for(150ms); gate->release(); });
        const auto destroyStart = std::chrono::steady_clock::now();
        ui.reset();
        const auto elapsed = std::chrono::steady_clock::now() - destroyStart;
        releaser.join();
        require(elapsed >= 100ms, "UI destruction returned without joining active search");
        require(gate->evaluatorDestroyed && !gate->destroyedBeforeRelease && !gate->timedOut,
                "engine/evaluator destroyed before worker completed, or watchdog fired");
        require(gate->evaluations == 1, "closed search continued evaluating after the blocked evaluator returned");
        require(gate->evaluations * 10 < fullEvaluations, "close did not save most search evaluations");
        std::cout << "close_evaluations=" << gate->evaluations << " full=" << fullEvaluations << '\n';
        std::cout << "shutdown_join_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << '\n';
    }
}
#endif

int main() {
    try {
        test_destruction_without_close_cancels_before_joining();
        std::cout << "PASS destruction_without_close_cancels_before_joining\n";
        test_successful_apply_immediately_invalidates_error_state();
        std::cout << "PASS successful_apply_immediately_invalidates_error_state (synthetic failed engine input)\n";
        test_hash_only_mutation_cancels_and_discards_worker();
        std::cout << "PASS hash_only_mutation_cancels_and_discards_worker (synthetic missing invalidation)\n";
        test_search_launch_does_not_block_ui();
        std::cout << "PASS search_launch_does_not_block_ui\n";
        test_restart_same_position_discards_old_result();
        std::cout << "PASS restart_same_position_discards_old_result\n";
#ifdef _WIN32
        test_history_navigation_discards_old_result();
        std::cout << "PASS history_navigation_discards_old_result (same/different board)\n";
        test_history_selection_resets_delay_without_play();
        std::cout << "PASS history_selection_resets_delay_without_play\n";
#endif
        test_worker_exception_is_handled_on_ui_thread();
        std::cout << "PASS worker_exception_is_handled_on_ui_thread\n";
        test_only_one_worker_across_different_engine_requests();
        std::cout << "PASS only_one_worker_across_different_engine_requests\n";
        test_restart_discards_an_already_ready_result();
        std::cout << "PASS restart_discards_an_already_ready_result\n";
        test_pve_sides_use_only_the_assigned_engine();
        std::cout << "PASS pve_sides_use_only_the_assigned_engine\n";
        test_delay_and_slide_gates_are_preserved();
        std::cout << "PASS delay_and_slide_gates_are_preserved\n";
        test_false_search_stops_without_applying_a_move();
        std::cout << "PASS false_search_stops_without_applying_a_move\n";
        test_stale_exception_is_discarded_and_restart_recovers();
        std::cout << "PASS stale_exception_is_discarded_and_restart_recovers\n";
#ifdef _WIN32
        test_history_selection_immediately_clears_failed_search();
        std::cout << "PASS history_selection_immediately_clears_failed_search\n";
        test_settings_holds_ready_result_until_game_resumes();
        std::cout << "PASS settings_holds_ready_result_until_game_resumes\n";
        test_stale_search_does_not_delay_human_animation();
        std::cout << "PASS stale_search_does_not_delay_human_animation\n";
        test_close_returns_but_destruction_joins_worker();
        std::cout << "PASS close_returns_but_destruction_joins_worker\n";
#endif
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
