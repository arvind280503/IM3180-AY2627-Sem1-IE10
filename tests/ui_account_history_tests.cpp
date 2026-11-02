#include "../UI files/ui.hpp"
#include "../storage/local_store.hpp"
#include <algorithm>
#include <filesystem>
#include <random>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct ChessUITestAccess {
    static void hide(ChessUI& ui) { ui.window.setVisible(false); ui.soundEffects = false; }
    static void submit(ChessUI& ui, const std::string& username, const std::string& password, bool sign_up) {
        ui.accountInput = username;
        ui.accountPassword = password;
        ui.accountSignUpMode = sign_up;
        ui.submitAccountForm();
    }
    static bool signed_in(const ChessUI& ui) { return ui.accountSignedIn; }
    static const std::string& name(const ChessUI& ui) { return ui.accountName; }
    static bool password_cleared(const ChessUI& ui) { return ui.accountPassword.empty(); }
    static void start_pve(ChessUI& ui, int engine_side) { ui.startGame(engine_side); }
    static bool apply(ChessUI& ui, const Move& move) { return ui.applyMove(move); }
    static void record(ChessUI& ui) { ui.recordCompletedMatch(); }
    static void return_to_root(ChessUI& ui) {
        ui.game = ui.startingBoard;
        ui.currentHistoryNode = -1;
        ui.rebuildLinearHistoryFromNode(-1);
    }
    static void identity(ChessUI& ui, const std::string& username) {
        ui.accountName = username;
        ui.accountSignedIn = !username.empty();
    }
    static std::size_t tree_size(const ChessUI& ui) { return ui.historyTree.size(); }
    static void open_history(ChessUI& ui) { ui.openMatchHistory(); }
    static const auto& summaries(const ChessUI& ui) { return ui.matchSummaries; }
    static const std::string& history_message(const ChessUI& ui) { return ui.matchHistoryMessage; }
    static const std::string& save_message(const ChessUI& ui) { return ui.matchSaveMessage; }
    static void choose_side(ChessUI& ui) { ui.currentScreen = ChessUI::Screen::ChooseSide; }
    static bool account_screen(const ChessUI& ui) { return ui.currentScreen == ChessUI::Screen::Account; }
    static bool choose_side_screen(const ChessUI& ui) { return ui.currentScreen == ChessUI::Screen::ChooseSide; }
#ifdef _WIN32
    static void click(ChessUI& ui, int x, int y) {
        const auto pixel = ui.window.mapCoordsToPixel(
            {static_cast<float>(x), static_cast<float>(y)}, ui.logicalView);
        SendMessageW(ui.window.getNativeHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pixel.x, pixel.y));
        SendMessageW(ui.window.getNativeHandle(), WM_LBUTTONUP, 0, MAKELPARAM(pixel.x, pixel.y));
        ui.handleEvents();
    }
#endif
};

void test_demo_account_does_not_bypass_authentication(ChessUI& ui, LocalStore& store) {
    bool called = false;
    ui.setAuthHandler([&](const std::string& username, const std::string& password, bool sign_up) {
        called = true;
        return store.authenticate(username, password, sign_up);
    });
    ChessUITestAccess::submit(ui, "jarvis", "jarvis1234", false);
    require(called && !ChessUITestAccess::signed_in(ui), "demo account bypassed authentication");
    ui.setAuthHandler([&](const std::string& name, const std::string& password, bool sign_up) {
        return store.authenticate(name, password, sign_up);
    });
    ChessUITestAccess::submit(ui, "alice", "abc", true);
    require(ChessUITestAccess::signed_in(ui) && ChessUITestAccess::name(ui) == "alice" &&
                ChessUITestAccess::password_cleared(ui), "successful sign up did not update UI state");
}

std::vector<Move> completed_mainline() {
    Board board;
    std::vector<Move> moves;
    std::mt19937 random(3180);
    for (int action = 0; action < 4096 && !board.has_game_ended(); ++action) {
        auto legal = board.legal_moves();
        require(!legal.empty(), "fixture has no legal move");
        const auto king_capture = std::find_if(legal.begin(), legal.end(), [&](const Move& move) {
            return std::abs(board.get_piece(move.new_x, move.new_y)) == 6;
        });
        const Move selected = king_capture != legal.end() ? *king_capture : legal[random() % legal.size()];
        require(board.make_move(selected), "fixture move rejected");
        moves.push_back(board.get_last_move());
    }
    require(board.has_game_ended(), "fixture did not finish");
    return moves;
}

void play(ChessUI& ui, const std::vector<Move>& moves) {
    for (const Move& move : moves)
        require(ChessUITestAccess::apply(ui, move), "UI rejected a legal fixture action");
}

void test_terminal_branch_saved_once_for_original_account(ChessUI& ui, LocalStore& store) {
    int recordings = 0;
    ui.setMatchRecorder([&](const std::string& owner, int side,
                            const ChessUI::MatchSummary& summary, const std::vector<Move>& moves) {
        ++recordings;
        SavedGame game;
        game.white = summary.white;
        game.black = summary.black;
        game.result = summary.result;
        game.date = summary.date;
        game.mainline = moves;
        store.save_game(owner, side, game);
    });
    ChessUITestAccess::start_pve(ui, 1);
    ChessUITestAccess::record(ui);
    require(recordings == 0, "unfinished match was recorded");
    Board root;
    require(ChessUITestAccess::apply(ui, root.legal_moves().front()), "alternative branch rejected");
    ChessUITestAccess::return_to_root(ui);
    ChessUITestAccess::identity(ui, "bob");
    const auto expected = completed_mainline();
    play(ui, expected);
    require(recordings == 1, "terminal mainline was not recorded exactly once");
    ChessUITestAccess::record(ui);
    require(recordings == 1, "same match was saved twice");
    const auto saved = store.games("alice");
    require(saved.size() == 1 && saved[0].mainline == expected && saved[0].player_side == 0 &&
                saved[0].white == "alice" && saved[0].black == "Engine" && !saved[0].date.empty(),
            "saved history includes another branch or has the wrong player association");
    require(ChessUITestAccess::tree_size(ui) > expected.size() && store.games("bob").empty(),
            "fixture did not branch or account switch reassigned the match");
    ChessUITestAccess::identity(ui, "alice");
}

void test_history_provider_receives_current_account(ChessUI& ui, LocalStore& store) {
    ui.setMatchHistoryProvider([&](const std::string& username) {
        std::vector<ChessUI::MatchSummary> summaries;
        for (const auto& game : store.games(username))
            summaries.push_back({game.white, game.black, game.result, game.date});
        return summaries;
    });
    ChessUITestAccess::open_history(ui);
    require(ChessUITestAccess::summaries(ui).size() == store.games("alice").size(),
            "history provider was not called for the signed-in account");
    ChessUITestAccess::identity(ui, "bob");
    ChessUITestAccess::open_history(ui);
    require(ChessUITestAccess::summaries(ui).empty(), "previous account's history remained visible");
    ui.setMatchHistoryProvider([](const std::string&) -> std::vector<ChessUI::MatchSummary> {
        throw std::runtime_error("test read failure");
    });
    ChessUITestAccess::open_history(ui);
    require(!ChessUITestAccess::history_message(ui).empty(), "history read failure was hidden");
    ChessUITestAccess::identity(ui, "alice");
}

void test_pve_records_only_signed_in_human_sides(ChessUI& ui) {
    int recordings = 0;
    int recorded_side = -1;
    std::string owner, white, black;
    ui.setMatchRecorder([&](const std::string& username, int side, const ChessUI::MatchSummary& summary,
                            const std::vector<Move>&) {
        ++recordings;
        recorded_side = side;
        owner = username;
        white = summary.white;
        black = summary.black;
    });
    const auto moves = completed_mainline();
    for (int engine_side : {0, 1}) {
        ChessUITestAccess::start_pve(ui, engine_side);
        const int previous = recordings;
        play(ui, moves);
        require(recordings == previous + 1 && owner == "alice" && recorded_side == 1 - engine_side &&
                    (recorded_side == 0 ? white == "alice" && black == "Engine"
                                        : white == "Engine" && black == "alice"),
                "player-vs-engine side or labels are wrong");
    }
    const int previous = recordings;
    ChessUITestAccess::identity(ui, "");
    for (int engine_side : {0, 1}) {
        ChessUITestAccess::start_pve(ui, engine_side);
        play(ui, moves);
        ChessUITestAccess::record(ui);
        require(recordings == previous, "guest player-vs-engine game was assigned to an account");
    }
    ChessUITestAccess::identity(ui, "alice");
    ui.setMatchRecorder({});
}

void test_save_failure_retries_without_duplicate_game(ChessUI& ui, LocalStore& store) {
    const auto previous_count = store.games("alice").size();
    int attempts = 0;
    ui.setMatchRecorder([&](const std::string& owner, int side,
                            const ChessUI::MatchSummary& summary, const std::vector<Move>& moves) {
        if (++attempts == 1) throw std::runtime_error("test save failure");
        SavedGame game;
        game.white = summary.white;
        game.black = summary.black;
        game.result = summary.result;
        game.date = summary.date;
        game.mainline = moves;
        store.save_game(owner, side, game);
    });
    ui.setMatchHistoryProvider([&](const std::string& username) {
        std::vector<ChessUI::MatchSummary> summaries;
        for (const auto& game : store.games(username))
            summaries.push_back({game.white, game.black, game.result, game.date});
        return summaries;
    });
    ChessUITestAccess::start_pve(ui, 1);
    play(ui, completed_mainline());
    require(attempts == 1 && !ChessUITestAccess::save_message(ui).empty() &&
                store.games("alice").size() == previous_count, "save failure was hidden or persisted a game");
    ChessUITestAccess::open_history(ui);
    require(attempts == 2 && ChessUITestAccess::save_message(ui).empty() &&
                store.games("alice").size() == previous_count + 1 &&
                ChessUITestAccess::summaries(ui).size() == previous_count + 1,
            "opening history did not retry and show the saved game");
    ChessUITestAccess::open_history(ui);
    ChessUITestAccess::record(ui);
    require(attempts == 2 && store.games("alice").size() == previous_count + 1,
            "successful retry allowed duplicate saves");
    ui.setMatchRecorder({});
}

#ifdef _WIN32
void test_choose_side_profile_and_logout_clicks(ChessUI& ui) {
    ChessUITestAccess::choose_side(ui);
    ChessUITestAccess::click(ui, 1113, 40);
    require(ChessUITestAccess::account_screen(ui), "side selection profile button did not open accounts");
    bool logged_out = false;
    ui.setLogoutHandler([&] { logged_out = true; });
    ChessUITestAccess::click(ui, 600, 520);
    require(logged_out && !ChessUITestAccess::signed_in(ui) && ChessUITestAccess::name(ui).empty() &&
                ChessUITestAccess::password_cleared(ui) && ChessUITestAccess::summaries(ui).empty() &&
                ChessUITestAccess::choose_side_screen(ui), "sign-out click did not clear session and history");
    ui.setLogoutHandler({});
    ChessUITestAccess::click(ui, 1113, 40);
    require(ChessUITestAccess::account_screen(ui), "signed-out user cannot open login from side selection");
    ChessUITestAccess::submit(ui, "alice", "abc", false);
    require(ChessUITestAccess::signed_in(ui), "account could not sign back in after logout");
}
#endif

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path directory(argv[1]);
    try {
        std::filesystem::remove_all(directory);
        {
            LocalStore store(directory / "chess.db");
            ChessUI ui(1, 1);
            ChessUITestAccess::hide(ui);
            test_demo_account_does_not_bypass_authentication(ui, store);
            test_terminal_branch_saved_once_for_original_account(ui, store);
            test_history_provider_receives_current_account(ui, store);
            test_pve_records_only_signed_in_human_sides(ui);
            test_save_failure_retries_without_duplicate_game(ui, store);
#ifdef _WIN32
            test_choose_side_profile_and_logout_clicks(ui);
#endif
        }
        std::filesystem::remove_all(directory);
        std::cout << "UI account/history tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::filesystem::remove_all(directory);
        return 1;
    }
}
