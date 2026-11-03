#include "UI files/ui.hpp"
#include "Engines/NewEvaluator.hpp"
#include "Engines/SecondEvaluator.hpp"
#include "storage/local_store.hpp"
#include <iostream>

int main() {
    try {
        LocalStore store(std::filesystem::path(CHESS_DATA_DIRECTORY) / "chess.db");
        ChessUI ui(
            1,
            Engine::kDefaultSearchDepth,
            std::make_unique<SecondEvaluator>(),
            std::make_unique<NewEvaluator>()
            
        );
        ui.setAuthHandler([&](const std::string& username, const std::string& password, bool sign_up) {
            return store.authenticate(username, password, sign_up);
        });
        ui.setMatchHistoryProvider([&](const std::string& username) {
            std::vector<ChessUI::MatchSummary> summaries;
            for (const auto& game : store.games(username))
                summaries.push_back({game.white, game.black, game.result, game.date});
            return summaries;
        });
        ui.setMatchRecorder([&](const std::string& username, int side,
                               const ChessUI::MatchSummary& summary, const std::vector<Move>& moves) {
            SavedGame game;
            game.white = summary.white;
            game.black = summary.black;
            game.result = summary.result;
            game.date = summary.date;
            game.mainline = moves;
            store.save_game(username, side, game);
        });
        ui.run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Could not start Chess AI: " << error.what() << '\n';
        return 1;
    }
}