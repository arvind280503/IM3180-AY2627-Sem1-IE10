#include "AggressiveEvaluator.hpp"
#include "../board.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace {

using Tapered = AggressiveEvaluator::Tapered;

constexpr int kWhiteForward = 1;

// Piece ids: 1 pawn, 2 knight, 3 bishop, 4 rook, 5 queen, 6 king
constexpr std::array<int, 7> kValue = {0, 100, 310, 330, 520, 950, 0};

// Piece-square tables.
constexpr int kPawnPst[8][8] = {
    {  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  0,  0,  0},
    {  0,  0,  5, 10, 10,  5,  0,  0},
    {  0,  5, 10, 20, 20, 10,  5,  0},
    {  5, 10, 15, 30, 30, 15, 10,  5},
    { 15, 20, 25, 35, 35, 25, 20, 15},
    { 40, 40, 40, 40, 40, 40, 40, 40},
    {  0,  0,  0,  0,  0,  0,  0,  0}};
// Endgame pawn bonus depends on rank only: pawns become very valuable near promotion.
constexpr int kPawnEgByRank[8] = {0, 0, 10, 20, 35, 60, 90, 0};

constexpr int kKnightPst[8][8] = {
    {-50, -40, -30, -30, -30, -30, -40, -50},
    {-40, -20,   0,   5,   5,   0, -20, -40},
    {-30,   5,  10,  15,  15,  10,   5, -30},
    {-30,   0,  15,  20,  20,  15,   0, -30},
    {-30,   5,  15,  20,  20,  15,   5, -30},
    {-30,   0,  10,  15,  15,  10,   0, -30},
    {-40, -20,   0,   0,   0,   0, -20, -40},
    {-50, -40, -30, -30, -30, -30, -40, -50}};

constexpr int kBishopPst[8][8] = {
    {-20, -10, -10, -10, -10, -10, -10, -20},
    {-10,   5,   0,   0,   0,   0,   5, -10},
    {-10,  10,  10,  10,  10,  10,  10, -10},
    {-10,   0,  10,  10,  10,  10,   0, -10},
    {-10,   5,   5,  10,  10,   5,   5, -10},
    {-10,   0,   5,  10,  10,   5,   0, -10},
    {-10,   0,   0,   0,   0,   0,   0, -10},
    {-20, -10, -10, -10, -10, -10, -10, -20}};

constexpr int kRookPst[8][8] = {
    {  0,  0,  0,  5,  5,  0,  0,  0},
    { -5,  0,  0,  0,  0,  0,  0, -5},
    { -5,  0,  0,  0,  0,  0,  0, -5},
    { -5,  0,  0,  0,  0,  0,  0, -5},
    { -5,  0,  0,  0,  0,  0,  0, -5},
    { -5,  0,  0,  0,  0,  0,  0, -5},
    {  5, 10, 10, 10, 10, 10, 10,  5},
    {  0,  0,  0,  0,  0,  0,  0,  0}};

constexpr int kQueenPst[8][8] = {
    {-20, -10, -10,  -5,  -5, -10, -10, -20},
    {-10,   0,   0,   0,   0,   0,   0, -10},
    {-10,   0,   5,   5,   5,   5,   0, -10},
    { -5,   0,   5,   5,   5,   5,   0,  -5},
    { -5,   0,   5,   5,   5,   5,   0,  -5},
    {-10,   0,   5,   5,   5,   5,   0, -10},
    {-10,   0,   0,   0,   0,   0,   0, -10},
    {-20, -10, -10,  -5,  -5, -10, -10, -20}};

// King: hide in the middlegame, walk to the centre in the endgame.
constexpr int kKingMgPst[8][8] = {
    { 20,  30,  10,   0,   0,  10,  30,  20},
    { 20,  20,   0,   0,   0,   0,  20,  20},
    {-10, -20, -20, -20, -20, -20, -20, -10},
    {-20, -30, -30, -40, -40, -30, -30, -20},
    {-30, -40, -40, -50, -50, -40, -40, -30},
    {-30, -40, -40, -50, -50, -40, -40, -30},
    {-30, -40, -40, -50, -50, -40, -40, -30},
    {-30, -40, -40, -50, -50, -40, -40, -30}};

constexpr int kKingEgPst[8][8] = {
    {-50, -30, -30, -30, -30, -30, -30, -50},
    {-30, -30,   0,   0,   0,   0, -30, -30},
    {-30, -10,  20,  30,  30,  20, -10, -30},
    {-30, -10,  30,  40,  40,  30, -10, -30},
    {-30, -10,  30,  40,  40,  30, -10, -30},
    {-30, -10,  20,  30,  30,  20, -10, -30},
    {-30, -30,   0,   0,   0,   0, -30, -30},
    {-50, -30, -30, -30, -30, -30, -30, -50}};


constexpr Tapered kBishopPair{25, 40};
constexpr Tapered kDoubledPawn{-8, -12};
constexpr Tapered kIsolatedPawn{-6, -8};
constexpr Tapered kSupportedPawn{6, 8};       // pawn defended by another pawn
constexpr Tapered kRookOpenFile{25, 15};
constexpr Tapered kRookSemiOpenFile{12, 8};

constexpr int kHangingDivisor = 8;            // loose piece costs value / 8
constexpr int kMaxKingUnits = 12;             // cap on king-zone attack count

// Mobility: (reachable squares - baseline) * weight
constexpr int kKnightBase = 4, kBishopBase = 6, kRookBase = 7, kQueenBase = 13;
constexpr Tapered kKnightMob{4, 4};
constexpr Tapered kBishopMob{5, 5};
constexpr Tapered kRookMob{2, 4};
constexpr Tapered kQueenMob{1, 2};

// Direction tables
constexpr int kKnightDr[8] = {2, 2, -2, -2, 1, 1, -1, -1};
constexpr int kKnightDc[8] = {1, -1, 1, -1, 2, -2, 2, -2};
constexpr int kDiagDr[4] = {1, 1, -1, -1};
constexpr int kDiagDc[4] = {1, -1, 1, -1};
constexpr int kOrthoDr[4] = {1, -1, 0, 0};
constexpr int kOrthoDc[4] = {0, 0, 1, -1};


// Helpers
inline bool in_bounds(int r, int c) { return r >= 0 && r < 8 && c >= 0 && c < 8; }
inline int side_index(bool white) { return white ? 0 : 1; }
inline int forward_dir(bool white) { return white ? kWhiteForward : -kWhiteForward; }

// Rows advanced from the side's own back rank (0..7)
inline int advance_rank(int row, bool white) {
    return (white == (kWhiteForward > 0)) ? row : 7 - row;
}

inline void add_for(Tapered& total, const Tapered& v, bool white) {
    if (white) total += v;
    else total -= v;
}

Tapered pst(int abs_piece, int adv, int col) {
    switch (abs_piece) {
        case 1: return Tapered{kPawnPst[adv][col], kPawnEgByRank[adv]};
        case 2: return Tapered{kKnightPst[adv][col], kKnightPst[adv][col]};
        case 3: return Tapered{kBishopPst[adv][col], kBishopPst[adv][col]};
        case 4: return Tapered{kRookPst[adv][col], kRookPst[adv][col]};
        case 5: return Tapered{kQueenPst[adv][col], kQueenPst[adv][col]};
        case 6: return Tapered{kKingMgPst[adv][col], kKingEgPst[adv][col]};
        default: return Tapered{0, 0};
    }
}

// mobility
int slide(const Board& board, int row, int col, bool white,
          const int* dr, const int* dc, int n, int (*attacks)[8]) {
    int mobility = 0;
    for (int d = 0; d < n; ++d) {
        int r = row + dr[d];
        int c = col + dc[d];
        while (in_bounds(r, c)) {
            ++attacks[r][c];
            const int target = board.get_piece(r, c);
            if (target == 0) {
                ++mobility;
            } else {
                if ((target > 0) != white) ++mobility; // can capture it
                break;                                 // ray is blocked
            }
            r += dr[d];
            c += dc[d];
        }
    }
    return mobility;
}

} // namespace

int AggressiveEvaluator::evaluate(const Board& board) const {
    if (board.has_game_ended()) {
        const int status = board.get_game_status();
        if (status == 0) return 0;
        const int winner_score = status > 0 ? 999999 : -999999;
        return winner_score * (board.get_current_turn() ? -1 : 1);
    }

    const Tapered score = evaluate_position(board);

    // Game phase: 24 = full material (middlegame), 0 = bare kings (endgame)
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

AggressiveEvaluator::Tapered AggressiveEvaluator::evaluate_position(const Board& board) const {
    Tapered score{0, 0};

    int pawn_count[2][8] = {};
    int bishops[2] = {0, 0};
    int king_row[2] = {-1, -1};
    int king_col[2] = {-1, -1};
    int attacks[2][8][8] = {}; // attacks[side][r][c] = how many of side's pieces attack (r, c)

    // Pass 1: collect pawns per file, bishops, and king squares.
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            const int piece = board.get_piece(row, col);
            if (piece == 0) continue;
            const int side = side_index(piece > 0);
            const int a = std::abs(piece);
            if (a == 1) ++pawn_count[side][col];
            else if (a == 3) ++bishops[side];
            else if (a == 6) { king_row[side] = row; king_col[side] = col; }
        }
    }

    // Bishop pair
    if (bishops[0] >= 2) score += kBishopPair;
    if (bishops[1] >= 2) score -= kBishopPair;

    // Light pawn structure: doubled and isolated pawns
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

    // Pass 2: material + piece-square tables + mobility, and fill attack maps.
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            const int piece = board.get_piece(row, col);
            if (piece == 0) continue;

            const bool white = piece > 0;
            const int a = std::abs(piece);
            const int side = side_index(white);
            const int enemy = 1 - side;
            const int fwd = forward_dir(white);
            const int adv = advance_rank(row, white);

            const int base = kValue[a];
            Tapered s{base, base};
            s += pst(a, adv, col);

            switch (a) {
                case 1: { // pawn
                    for (int dc = -1; dc <= 1; dc += 2) {
                        // squares this pawn attacks
                        if (in_bounds(row + fwd, col + dc)) ++attacks[side][row + fwd][col + dc];
                        // supported by a friendly pawn behind it diagonally?
                        if (in_bounds(row - fwd, col + dc) &&
                            board.get_piece(row - fwd, col + dc) == piece) {
                            s += kSupportedPawn;
                        }
                    }
                    // passed pawn: no enemy pawn ahead on the same or adjacent files
                    bool passed = true;
                    for (int r = row + fwd; r >= 0 && r < 8 && passed; r += fwd) {
                        for (int c = col - 1; c <= col + 1; ++c) {
                            if (in_bounds(r, c) && board.get_piece(r, c) == -piece) {
                                passed = false;
                                break;
                            }
                        }
                    }
                    if (passed) s += Tapered{adv * adv, adv * adv * 3};
                    break;
                }
                case 2: { // knight
                    int mobility = 0;
                    for (int k = 0; k < 8; ++k) {
                        const int r = row + kKnightDr[k];
                        const int c = col + kKnightDc[k];
                        if (!in_bounds(r, c)) continue;
                        ++attacks[side][r][c];
                        const int t = board.get_piece(r, c);
                        if (t == 0 || (t > 0) != white) ++mobility;
                    }
                    s += kKnightMob * (mobility - kKnightBase);
                    break;
                }
                case 3: { // bishop
                    const int mobility = slide(board, row, col, white, kDiagDr, kDiagDc, 4, attacks[side]);
                    s += kBishopMob * (mobility - kBishopBase);
                    break;
                }
                case 4: { // rook
                    const int mobility = slide(board, row, col, white, kOrthoDr, kOrthoDc, 4, attacks[side]);
                    s += kRookMob * (mobility - kRookBase);
                    const bool own_pawn = pawn_count[side][col] > 0;
                    const bool enemy_pawn = pawn_count[enemy][col] > 0;
                    if (!own_pawn && !enemy_pawn) s += kRookOpenFile;
                    else if (!own_pawn) s += kRookSemiOpenFile;
                    break;
                }
                case 5: { // queen
                    const int m1 = slide(board, row, col, white, kDiagDr, kDiagDc, 4, attacks[side]);
                    const int m2 = slide(board, row, col, white, kOrthoDr, kOrthoDc, 4, attacks[side]);
                    s += kQueenMob * (m1 + m2 - kQueenBase);
                    break;
                }
                case 6: { // king: only record which squares it guards
                    for (int dr = -1; dr <= 1; ++dr) {
                        for (int dc = -1; dc <= 1; ++dc) {
                            if (dr == 0 && dc == 0) continue;
                            if (in_bounds(row + dr, col + dc)) ++attacks[side][row + dr][col + dc];
                        }
                    }
                    break;
                }
                default: break;
            }

            add_for(score, s, white);
        }
    }

    // Pass 3: terms that need the COMPLETE attack maps.
    // 3a) Hanging pieces
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            const int piece = board.get_piece(row, col);
            if (piece == 0) continue;
            const int a = std::abs(piece);
            if (a == 6) continue; // kings are handled by the king-zone term
            const bool white = piece > 0;
            const int side = side_index(white);
            const int enemy = 1 - side;
            if (attacks[enemy][row][col] > 0 && attacks[side][row][col] == 0) {
                const int penalty = kValue[a] / kHangingDivisor;
                add_for(score, Tapered{-penalty, -penalty}, white);
            }
        }
    }

    // 3b) King-zone pressure: count enemy attacks on the squares around each king
    //     (3x3 area plus one extra row in front). More attacks = quadratic danger.
    for (int side = 0; side < 2; ++side) {
        if (king_row[side] == -1) continue;
        const bool white = side == 0;
        const int enemy = 1 - side;
        const int fwd = forward_dir(white);
        const int row_offsets[4] = {-1, 0, 1, 2 * fwd};

        int units = 0;
        for (int i = 0; i < 4; ++i) {
            const int r = king_row[side] + row_offsets[i];
            if (r < 0 || r > 7) continue;
            for (int dc = -1; dc <= 1; ++dc) {
                const int c = king_col[side] + dc;
                if (c < 0 || c > 7) continue;
                units += attacks[enemy][r][c];
            }
        }
        units = std::min(units, kMaxKingUnits);
        const int danger = units * units / 2;
        // The attacked side loses points, i.e. the attacker gains them.
        add_for(score, Tapered{danger, 0}, !white);
    }

    return score;
}