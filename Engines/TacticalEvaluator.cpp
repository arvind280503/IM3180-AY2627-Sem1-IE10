#include "TacticalEvaluator.hpp"
#include "../board.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace {

constexpr int kBoardSize = 8;
constexpr int kSquares = 64;
constexpr int kNoAttacker = 100000;

// Piece encoding used by the supplied evaluators:
// +/-1 pawn, +/-2 knight, +/-3 bishop, +/-4 rook, +/-5 queen, +/-6 king.
constexpr std::array<int, 7> kPieceValue = {0, 100, 320, 330, 500, 900, 20000};

constexpr std::array<std::array<int, 2>, 8> kKnightOffsets = {{
    {{-2, -1}}, {{-2, 1}}, {{-1, -2}}, {{-1, 2}},
    {{ 1, -2}}, {{ 1, 2}}, {{ 2, -1}}, {{ 2, 1}}
}};
constexpr std::array<std::array<int, 2>, 8> kKingOffsets = {{
    {{-1, -1}}, {{-1, 0}}, {{-1, 1}}, {{0, -1}},
    {{0, 1}}, {{1, -1}}, {{1, 0}}, {{1, 1}}
}};
constexpr std::array<std::array<int, 2>, 8> kRayDirections = {{
    {{-1, -1}}, {{-1, 1}}, {{1, -1}}, {{1, 1}},
    {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}}
}};

bool on_board(int row, int col) {
    return row >= 0 && row < kBoardSize && col >= 0 && col < kBoardSize;
}

int square_of(int row, int col) { return row * kBoardSize + col; }

// This matches the supplied NewEvaluator and SecondEvaluator conventions.
// Update if the repository represents White as advancing towards lower rows.
constexpr int kWhitePawnForward = 1;

struct AttackMaps {
    // side 0 = White (positive piece codes), side 1 = Black.
    std::array<std::array<int, kSquares>, 2> count{};
    std::array<std::array<int, kSquares>, 2> cheapest{};

    AttackMaps() {
        for (auto& side : cheapest) side.fill(kNoAttacker);
    }

    void add(int side, int row, int col, int attacker_value) {
        if (!on_board(row, col)) return;
        const int sq = square_of(row, col);
        ++count[side][sq];
        cheapest[side][sq] = std::min(cheapest[side][sq], attacker_value);
    }
};

AttackMaps create_attack_maps(const Board& board) {
    AttackMaps maps;

    for (int row = 0; row < kBoardSize; ++row) {
        for (int col = 0; col < kBoardSize; ++col) {
            const int piece = board.get_piece(row, col);
            if (piece == 0) continue;

            const int side = piece > 0 ? 0 : 1;
            const int type = std::abs(piece);
            if (type < 1 || type > 6) continue;
            const int value = kPieceValue[type];

            if (type == 1) {
                const int forward = piece > 0 ? kWhitePawnForward : -kWhitePawnForward;
                maps.add(side, row + forward, col - 1, value);
                maps.add(side, row + forward, col + 1, value);
            } else if (type == 2) {
                for (const auto& offset : kKnightOffsets)
                    maps.add(side, row + offset[0], col + offset[1], value);
            } else if (type == 6) {
                for (const auto& offset : kKingOffsets)
                    maps.add(side, row + offset[0], col + offset[1], value);
            } else {
                for (int d = 0; d < 8; ++d) {
                    const bool diagonal = d < 4;
                    if (type == 3 && !diagonal) continue;
                    if (type == 4 && diagonal) continue;

                    int r = row + kRayDirections[d][0];
                    int c = col + kRayDirections[d][1];
                    while (on_board(r, c)) {
                        maps.add(side, r, c, value);
                        // The first occupied square is defended/attacked;
                        // pieces behind it are not directly attacked.
                        if (board.get_piece(r, c) != 0) break;
                        r += kRayDirections[d][0];
                        c += kRayDirections[d][1];
                    }
                }
            }
        }
    }

    return maps;
}

int king_pressure(const AttackMaps& maps, int king_row, int king_col,
                  int enemy_side) {
    if (!on_board(king_row, king_col)) return 0;

    int pressure = 0;
    for (const auto& offset : kKingOffsets) {
        const int row = king_row + offset[0];
        const int col = king_col + offset[1];
        if (!on_board(row, col)) continue;
        // Cap multiple attacks on one square to avoid extreme scores.
        pressure += 6 * std::min(maps.count[enemy_side][square_of(row, col)], 2);
    }
    // An attack on the king's current square is especially urgent.
    pressure += 15 * std::min(maps.count[enemy_side][square_of(king_row, king_col)], 2);
    return pressure;
}

}  // namespace

TacticalEvaluator::TacticalEvaluator(EngineConfig config)
    : standard_evaluator_(std::move(config)) {}

int TacticalEvaluator::evaluate(const Board& board) const {
    const int base = standard_evaluator_.evaluate(board);
    // Let the underlying evaluator handle checkmate, stalemate and draws.
    if (board.has_game_ended()) return base;

    const int white_perspective = evaluate_tactics(board);
    return base + (board.get_current_turn() ? -white_perspective : white_perspective);
}

int TacticalEvaluator::evaluate_tactics(const Board& board) const {
    const AttackMaps maps = create_attack_maps(board);
    int score = 0;  // positive favours White
    int king_row[2] = {-1, -1};
    int king_col[2] = {-1, -1};

    for (int row = 0; row < kBoardSize; ++row) {
        for (int col = 0; col < kBoardSize; ++col) {
            const int piece = board.get_piece(row, col);
            if (piece == 0) continue;

            const int side = piece > 0 ? 0 : 1;
            const int enemy = 1 - side;
            const int type = std::abs(piece);
            if (type < 1 || type > 6) continue;
            const int sq = square_of(row, col);

            if (type == 6) {
                king_row[side] = row;
                king_col[side] = col;
                continue;
            }

            if (maps.count[enemy][sq] == 0) continue;

            const bool defended = maps.count[side][sq] > 0;
            const int value = kPieceValue[type];
            int vulnerability = 0;

            if (!defended) {
                // Unprotected pieces are particularly vulnerable.
                vulnerability = std::min(value / 5, 150);
            } else if (maps.cheapest[enemy][sq] < value) {
                // A cheaper attacker may force a favourable exchange.
                // This is a heuristic, not static exchange evaluation.
                vulnerability = std::min(value / 10, 75);
            }

            // Penalise vulnerability of White's pieces; reward it for Black's.
            score += side == 0 ? -vulnerability : vulnerability;
        }
    }

    // Attacks on the enemy king's surrounding squares.
    // Taper king pressure down as non-pawn material disappears.
    int phase = 0;
    for (int row = 0; row < kBoardSize; ++row) {
        for (int col = 0; col < kBoardSize; ++col) {
            const int type = std::abs(board.get_piece(row, col));
            if (type == 2 || type == 3) ++phase;
            else if (type == 4) phase += 2;
            else if (type == 5) phase += 4;
        }
    }
    phase = std::min(phase, 24);
    const int pressure_on_black = king_pressure(maps, king_row[1], king_col[1], 0);
    const int pressure_on_white = king_pressure(maps, king_row[0], king_col[0], 1);
    score += (pressure_on_black - pressure_on_white) * (12 + phase) / 36;

    // Central influence. Occupation is intentionally not scored here, since
    // StandardEvaluator already handles piece-square placement.
    for (int row = 2; row <= 5; ++row) {
        for (int col = 2; col <= 5; ++col) {
            const int sq = square_of(row, col);
            const int influence = std::min(maps.count[0][sq], 2)
                                - std::min(maps.count[1][sq], 2);
            const bool core_center = (row == 3 || row == 4)
                                  && (col == 3 || col == 4);
            score += influence * (core_center ? 4 : 1);
        }
    }

    return score;
}
