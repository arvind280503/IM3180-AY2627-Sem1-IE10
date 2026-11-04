#include "OldEvaluator.hpp"
#include "../board.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

const std::array<int, 64> OldEvaluator::king_eg_sq_table = {
    -50, -40, -30, -30, -30, -30, -40, -50,
    -30, -20, -10,   0,   0, -10, -20, -30,
    -30, -10,  20,  30,  30,  20, -10, -30,
    -30, -10,  30,  40,  40,  30, -10, -30,
    -30, -10,  30,  40,  40,  30, -10, -30,
    -30, -10,  20,  30,  30,  20, -10, -30,
    -30, -30,   0,   0,   0,   0, -30, -30,
    -50, -30, -30, -30, -30, -30, -30, -50
};

OldEvaluator::OldEvaluator(EngineConfig evaluator_config)
    : standard_evaluator(evaluator_config),
      config(std::move(evaluator_config)) {}

int OldEvaluator::evaluate(const Board& board) const {
    return standard_evaluator.evaluate(board) + evaluate_new_heuristic(board);
}

int OldEvaluator::evaluate_new_heuristic(const Board& board) const {
    if (board.has_game_ended()) {
        const int winner_score = board.get_game_status() > 0 ? 999999 : -999999;
        return winner_score * (board.get_current_turn() ? -1 : 1);
    }

    int raw_score = 0;
    int pawn_count_white[8] = {0};
    int pawn_count_black[8] = {0};
    int white_king_x = -1, white_king_y = -1;
    int black_king_x = -1, black_king_y = -1;

    std::vector<std::pair<int, int>> white_rooks;
    std::vector<std::pair<int, int>> black_rooks;
    const std::array<int, 8> passed_pawn_bonus = {0, 5, 15, 30, 60, 100, 160, 0};

    int game_phase = 0;

    auto is_on_board = [](int row, int column) {
        return row >= 0 && row < 8 && column >= 0 && column < 8;
    };

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const int piece = std::abs(board.get_piece(row, column));
            if (piece == 2 || piece == 3) game_phase += 1;
            if (piece == 4) game_phase += 2;
            if (piece == 5) game_phase += 4;
        }
    }
    game_phase = std::min(game_phase, 24);

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const int piece = board.get_piece(row, column);
            if (piece == 0) continue;

            const int absolute_piece = std::abs(piece);
            const int material = config.piece_value[absolute_piece];
            int positional_score = 0;
            const int square_index = piece > 0
                ? row * 8 + column
                : (7 - row) * 8 + column;

            switch (absolute_piece) {
                case 1: {
                    positional_score = config.pawn_sq_table[square_index];
                    if (piece > 0) ++pawn_count_white[column];
                    else ++pawn_count_black[column];

                    bool passed = true;
                    const int forward = piece > 0 ? 1 : -1;
                    for (int check_row = row + forward;
                         check_row >= 0 && check_row < 8;
                         check_row += forward) {
                        for (int check_column = column - 1;
                             check_column <= column + 1; ++check_column) {
                            if (!is_on_board(check_row, check_column)) continue;
                            const int enemy_pawn = piece > 0 ? -1 : 1;
                            if (board.get_piece(check_row, check_column) == enemy_pawn) {
                                passed = false;
                                break;
                            }
                        }
                        if (!passed) break;
                    }
                    if (passed) {
                        const int rank = piece > 0 ? row : 7 - row;
                        raw_score += (piece > 0 ? 1 : -1) * passed_pawn_bonus[rank];
                    }
                    break;
                }
                case 2: {
                    positional_score = config.knight_sq_table[square_index];
                    const int knight_moves[8][2] = {
                        {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
                        {1, -2}, {1, 2}, {2, -1}, {2, 1}
                    };
                    int mobility = 0;
                    for (const auto& move : knight_moves) {
                        const int next_row = row + move[0];
                        const int next_column = column + move[1];
                        if (!is_on_board(next_row, next_column)) continue;
                        const int destination = board.get_piece(next_row, next_column);
                        if (destination == 0 || (destination > 0) != (piece > 0))
                            ++mobility;
                    }
                    raw_score += (piece > 0 ? 1 : -1) * mobility * 4;
                    break;
                }
                case 3: {
                    const int bishop_directions[4][2] = {
                        {-1, -1}, {-1, 1}, {1, -1}, {1, 1}
                    };
                    int mobility = 0;
                    for (const auto& direction : bishop_directions) {
                        int next_row = row + direction[0];
                        int next_column = column + direction[1];
                        while (is_on_board(next_row, next_column)) {
                            const int destination = board.get_piece(next_row, next_column);
                            if (destination == 0) {
                                ++mobility;
                            } else {
                                if ((destination > 0) != (piece > 0)) ++mobility;
                                break;
                            }
                            next_row += direction[0];
                            next_column += direction[1];
                        }
                    }
                    raw_score += (piece > 0 ? 1 : -1) * mobility * 3;
                    break;
                }
                case 4:
                    (piece > 0 ? white_rooks : black_rooks).push_back({row, column});
                    break;
                case 6: {
                    const int middlegame_score = config.king_sq_table[square_index];
                    const int endgame_score = king_eg_sq_table[square_index];
                    positional_score = (middlegame_score * game_phase
                        + endgame_score * (24 - game_phase)) / 24;

                    if (piece > 0) {
                        white_king_x = row;
                        white_king_y = column;
                    } else {
                        black_king_x = row;
                        black_king_y = column;
                    }
                    break;
                }
                default:
                    break;
            }

            const int total_piece_value = material * 100 + positional_score;
            raw_score += piece > 0 ? total_piece_value : -total_piece_value;
        }
    }

    for (int column = 0; column < 8; ++column) {
        if (pawn_count_white[column] > 1)
            raw_score -= (pawn_count_white[column] - 1) * 20;
        if (pawn_count_white[column] > 0) {
            const bool left = column > 0 && pawn_count_white[column - 1] > 0;
            const bool right = column < 7 && pawn_count_white[column + 1] > 0;
            if (!left && !right) raw_score -= 15;
        }

        if (pawn_count_black[column] > 1)
            raw_score += (pawn_count_black[column] - 1) * 20;
        if (pawn_count_black[column] > 0) {
            const bool left = column > 0 && pawn_count_black[column - 1] > 0;
            const bool right = column < 7 && pawn_count_black[column + 1] > 0;
            if (!left && !right) raw_score += 15;
        }
    }

    auto evaluate_rook_connection = [](const Board& position,
                                       const std::vector<std::pair<int, int>>& rooks) {
        int connection_score = 0;
        for (std::size_t first = 0; first < rooks.size(); ++first) {
            for (std::size_t second = first + 1; second < rooks.size(); ++second) {
                const int row1 = rooks[first].first;
                const int column1 = rooks[first].second;
                const int row2 = rooks[second].first;
                const int column2 = rooks[second].second;

                if (row1 == row2) {
                    bool blocked = false;
                    for (int column = std::min(column1, column2) + 1;
                         column < std::max(column1, column2); ++column) {
                        if (position.get_piece(row1, column) != 0) {
                            blocked = true;
                            break;
                        }
                    }
                    if (!blocked) connection_score += 25;
                } else if (column1 == column2) {
                    bool blocked = false;
                    for (int row = std::min(row1, row2) + 1;
                         row < std::max(row1, row2); ++row) {
                        if (position.get_piece(row, column1) != 0) {
                            blocked = true;
                            break;
                        }
                    }
                    if (!blocked) connection_score += 25;
                }
            }
        }
        return connection_score;
    };

    raw_score += evaluate_rook_connection(board, white_rooks);
    raw_score -= evaluate_rook_connection(board, black_rooks);

    auto evaluate_shield = [&](int king_row, int king_column, int pawn_type) {
        if (king_row == -1) return 0;

        const int forward_row = king_row + (pawn_type > 0 ? 1 : -1);
        int shield_score = 0;
        if (forward_row >= 0 && forward_row < 8) {
            for (int offset = -1; offset <= 1; ++offset) {
                const int column = king_column + offset;
                if (column >= 0 && column < 8
                    && board.get_piece(forward_row, column) == pawn_type) {
                    shield_score += 25;
                }
            }
        }
        return (shield_score * game_phase) / 24;
    };

    raw_score += evaluate_shield(white_king_x, white_king_y, 1);
    raw_score -= evaluate_shield(black_king_x, black_king_y, -1);

    if (game_phase < 12 && white_king_x != -1 && black_king_x != -1) {
        auto cornering_bonus = [](int winning_king_x, int winning_king_y,
                                  int losing_king_x, int losing_king_y) {
            const int enemy_center_distance = std::abs(losing_king_x - 3)
                + std::abs(losing_king_y - 3);
            const int king_distance = std::abs(winning_king_x - losing_king_x)
                + std::abs(winning_king_y - losing_king_y);
            return enemy_center_distance * 10 + (14 - king_distance) * 10;
        };

        if (raw_score > 200) {
            raw_score += cornering_bonus(white_king_x, white_king_y,
                                         black_king_x, black_king_y);
        } else if (raw_score < -200) {
            raw_score -= cornering_bonus(black_king_x, black_king_y,
                                         white_king_x, white_king_y);
        }
    }

    if (white_king_x != -1 && black_king_x != -1) {
        auto count_king_escapes = [&](int king_x, int king_y, int enemy_piece_sign) {
            int free_squares = 0;
            for (int delta_x = -1; delta_x <= 1; ++delta_x) {
                for (int delta_y = -1; delta_y <= 1; ++delta_y) {
                    if (delta_x == 0 && delta_y == 0) continue;
                    const int next_x = king_x + delta_x;
                    const int next_y = king_y + delta_y;
                    if (!is_on_board(next_x, next_y)) continue;

                    const int destination = board.get_piece(next_x, next_y);
                    if (destination == 0 || (destination > 0) == (enemy_piece_sign > 0))
                        ++free_squares;
                }
            }
            return free_squares;
        };

        const int black_king_escapes = count_king_escapes(
            black_king_x, black_king_y, 1);
        const int white_king_escapes = count_king_escapes(
            white_king_x, white_king_y, -1);

        raw_score += (8 - black_king_escapes) * 15;
        raw_score -= (8 - white_king_escapes) * 15;

        for (int row = 0; row < 8; ++row) {
            for (int column = 0; column < 8; ++column) {
                const int piece = board.get_piece(row, column);
                if (piece == 0) continue;

                if (piece == 5 || piece == 4 || piece == 6) {
                    const int distance = std::abs(row - black_king_x)
                        + std::abs(column - black_king_y);
                    raw_score += (14 - distance) * 8;
                } else if (piece == -5 || piece == -4 || piece == -6) {
                    const int distance = std::abs(row - white_king_x)
                        + std::abs(column - white_king_y);
                    raw_score -= (14 - distance) * 8;
                }
            }
        }
    }

    return raw_score * (board.get_current_turn() ? -1 : 1);
}
