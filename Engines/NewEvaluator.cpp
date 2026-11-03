#include "NewEvaluator.hpp"
#include "../board.hpp"

namespace {

using Tapered = NewEvaluator::Tapered;

constexpr int kWhiteForward = 1;

constexpr std::array<int, 7> kValue = {0, 100, 320, 330, 500, 900, 0};

constexpr std::array<int, 8> kPassedMg = {0, 5, 10, 20, 35, 60, 100, 0};
constexpr std::array<int, 8> kPassedEg = {0, 10, 20, 40, 70, 120, 200, 0};

constexpr Tapered kBishopPair{30, 50};
constexpr Tapered kDoubledPawn{-10, -20};
constexpr Tapered kIsolatedPawn{-10, -15};
constexpr Tapered kRookOpenFile{20, 10};
constexpr Tapered kRookSemiOpenFile{10, 5};
constexpr int kKingShieldMg = 10;

inline int advance_rank(int row, bool white) {
    return (white == (kWhiteForward > 0)) ? row : 7 - row;
}

inline int forward_dir(bool white) { return white ? kWhiteForward : -kWhiteForward; }

inline int centrality(int row, int col) {
    const int d = std::max(std::abs(2 * row - 7), std::abs(2 * col - 7));
    return (7 - d) / 2;
}

inline int side_index(bool white) { return white ? 0 : 1; }

Tapered placement(int abs_piece, int adv, int cen) {
    switch (abs_piece) {
        case 1: return {5 * adv + 2 * cen, 10 * adv};
        case 2: return {10 * cen - (adv == 0 ? 8 : 0), 8 * cen};
        case 3: return {8 * cen - (adv == 0 ? 8 : 0), 6 * cen};
        case 4: return {(adv == 6 ? 15 : 0) + 2 * cen, adv == 6 ? 10 : 0};
        case 5: return {3 * cen, 5 * cen};
        case 6: return {-10 * adv, 12 * cen};
        default: return {0, 0};
    }
}

inline void add_for(Tapered& total, const Tapered& v, bool white) {
    if (white) total += v;
    else total -= v;
}

}

int NewEvaluator::evaluate(const Board& board) const {
    if (board.has_game_ended()) {
        const int winner_score = board.get_game_status() > 0 ? 999999 : -999999;
        return winner_score * (board.get_current_turn() ? -1 : 1);
    }

    const Tapered score = evaluate_position(board);

    int game_phase = 0;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            const int p = std::abs(board.get_piece(r, c));
            if (p == 2 || p == 3) game_phase += 1;
            else if (p == 4) game_phase += 2;
            else if (p == 5) game_phase += 4;
        }
    }
    game_phase = std::min(game_phase, 24);

    const int interpolated = (score.mg * game_phase + score.eg * (24 - game_phase)) / 24;
    return interpolated * (board.get_current_turn() ? -1 : 1);
}

NewEvaluator::Tapered NewEvaluator::evaluate_position(const Board& board) const {
    Tapered score{0, 0};

    int pawn_rows[2][8] = {};
    int pawn_count[2][8] = {};
    int bishops[2] = {0, 0};
    int king_row[2] = {-1, -1};
    int king_col[2] = {-1, -1};

    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            const int piece = board.get_piece(row, col);
            if (piece == 0) continue;

            const bool white = piece > 0;
            const int abs_piece = std::abs(piece);
            const int side = side_index(white);

            const int base = kValue[abs_piece];
            Tapered s{base, base};
            s += placement(abs_piece, advance_rank(row, white), centrality(row, col));
            add_for(score, s, white);

            if (abs_piece == 1) {
                pawn_rows[side][col] |= 1 << row;
                ++pawn_count[side][col];
            } else if (abs_piece == 3) {
                ++bishops[side];
            } else if (abs_piece == 6) {
                king_row[side] = row;
                king_col[side] = col;
            }
        }
    }

    if (bishops[0] >= 2) score += kBishopPair;
    if (bishops[1] >= 2) score -= kBishopPair;

    for (int side = 0; side < 2; ++side) {
        const bool white = side == 0;
        for (int col = 0; col < 8; ++col) {
            const int n = pawn_count[side][col];
            if (n == 0) continue;

            if (n > 1) add_for(score, kDoubledPawn * (n - 1), white);

            const bool left = col > 0 && pawn_count[side][col - 1] > 0;
            const bool right = col < 7 && pawn_count[side][col + 1] > 0;
            if (!left && !right) add_for(score, kIsolatedPawn * n, white);
        }
    }

    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            const int piece = board.get_piece(row, col);
            if (piece == 0) continue;

            const bool white = piece > 0;
            const int abs_piece = std::abs(piece);
            const int side = side_index(white);
            const int enemy = 1 - side;

            if (abs_piece == 1) {
                const bool up = forward_dir(white) > 0;
                const int ahead = up ? ((0xFF << (row + 1)) & 0xFF) : ((1 << row) - 1);

                bool passed = true;
                for (int fc = col - 1; fc <= col + 1 && passed; ++fc) {
                    if (fc < 0 || fc > 7) continue;
                    if (pawn_rows[enemy][fc] & ahead) passed = false;
                }
                if (passed) {
                    const int adv = advance_rank(row, white);
                    add_for(score, Tapered{kPassedMg[adv], kPassedEg[adv]}, white);
                }
            } else if (abs_piece == 4) {
                const bool own_pawn = pawn_count[side][col] > 0;
                const bool enemy_pawn = pawn_count[enemy][col] > 0;
                if (!own_pawn && !enemy_pawn) add_for(score, kRookOpenFile, white);
                else if (!own_pawn) add_for(score, kRookSemiOpenFile, white);
            }
        }
    }

    for (int side = 0; side < 2; ++side) {
        if (king_row[side] == -1) continue;
        const bool white = side == 0;
        const int shield_row = king_row[side] + forward_dir(white);
        if (shield_row < 0 || shield_row > 7) continue;

        int shield = 0;
        for (int dc = -1; dc <= 1; ++dc) {
            const int cc = king_col[side] + dc;
            if (cc < 0 || cc > 7) continue;
            if (board.get_piece(shield_row, cc) == (white ? 1 : -1)) ++shield;
        }
        add_for(score, Tapered{shield * kKingShieldMg, 0}, white);
    }

    return score;
}