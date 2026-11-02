#include "../board.hpp"
#include "../evaluator.hpp"
#include "../Engines/TacticalEvaluator.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
Board from_fen(const std::string& fen) {
    Board board;
    if (!board.import_fen(fen)) {
        throw std::runtime_error("Failed to load test FEN: " + fen);
    }
    return board;
}

int tactical_adjustment(const Board& board,
                        const TacticalEvaluator& tactical,
                        const StandardEvaluator& baseline) {
    // The tactical evaluator adds a tactical adjustment to StandardEvaluator.
    return tactical.evaluate(board) - baseline.evaluate(board);
}
}

int main() {
    try {
        TacticalEvaluator tactical;
        StandardEvaluator baseline;

        // 1. Standard starting position: scores must be finite (int) and
        // swapping just the side to move should reverse their sign.
        Board start_white = from_fen(
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq 1 0 1");
        Board start_black = from_fen(
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq 1 0 1");
        const int white_score = tactical.evaluate(start_white);
        const int black_score = tactical.evaluate(start_black);
        std::cout << "Starting position (white to move): " << white_score << '\n';
        std::cout << "Starting position (black to move): " << black_score << '\n';
        if (white_score != -black_score) {
            throw std::runtime_error("Score does not reverse with side to move");
        }

        // 2. A white rook on d4 is attacked by a bishop on g7.
        // Moving that bishop to g6 removes the attack. Compare only the
        // incremental tactical adjustment to exclude positional differences
        // already captured by StandardEvaluator.
        Board rook_attacked = from_fen(
            "7k/6b1/8/8/3R4/8/8/K7 w - 1 0 1");
        Board rook_safe = from_fen(
            "7k/8/6b1/8/3R4/8/8/K7 w - 1 0 1");
        const int attacked_adjustment = tactical_adjustment(
            rook_attacked, tactical, baseline);
        const int safe_adjustment = tactical_adjustment(
            rook_safe, tactical, baseline);
        std::cout << "Tactical adjustment (rook attacked): "
                  << attacked_adjustment << '\n';
        std::cout << "Tactical adjustment (rook safe): "
                  << safe_adjustment << '\n';
        if (attacked_adjustment >= safe_adjustment) {
            throw std::runtime_error(
                "Attacked, undefended rook was not penalized more than safe rook");
        }

        std::cout << "PASS: tactical evaluator behavioral smoke tests\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
