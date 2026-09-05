#include"board.hpp"

#include<iostream>
#include<algorithm>
#include<cmath>

Board::Board(){
    this -> turn = 0;
    this -> move_left = 1;
    this -> game_status = 0;
    this -> board_hash = 0;

    std::uniform_int_distribution<Hash> dist(0, UINT64_MAX);
    for(int i = -6; i <= 6; ++i){
        for(int j = 0; j < 64; ++j){
            piece_hash[i * 64 + j] = dist(rng);
        }
    }
    turn_hash[0] = dist(rng); turn_hash[1] = dist(rng);
    move_left_hash[1] = dist(rng); move_left_hash[2] = dist(rng);

    for(auto& h: current_board[1]) h = 1;
    for(auto& h: current_board[6]) h = -1;
    current_board[0][0] = 4; current_board[0][1] = 2; current_board[0][2] = 3; current_board[0][3] = 6; current_board[0][4] = 5;
    current_board[0][5] = 3; current_board[0][6] = 2; current_board[0][7] = 4;

    for(int i = 0; i < 8; ++i){
        current_board[7][i] = -1 * current_board[0][i];
    }

    for(auto &i: current_board){
        for(auto& j: i){
            board_hash ^= piece_hash[j * 64 + (&j - &i[0]) + 
                (&i - &current_board[0]) * 8];
        }
    }
    board_hash ^= turn_hash[turn];
    board_hash ^= move_left_hash[move_left];

    piece_value[0] = 0; piece_value[1] = 1; piece_value[2] = 3; piece_value[3] = 3; piece_value[4] = 5; piece_value[5] = 9; piece_value[6] = 99;
    piece_value[-1] = -1; piece_value[-2] = -3; piece_value[-3] = -3; piece_value[-4] = -5; piece_value[-5] = -9; piece_value[-6] = -99;

    pawn_move_pattern[1].push_back({1, 0});
    pawn_capture_pattern[1].push_back({1, 1}); pawn_capture_pattern[1].push_back({1, -1});
    
    pawn_move_pattern[-1].push_back({-1, 0});
    pawn_capture_pattern[-1].push_back({-1, 1}); pawn_capture_pattern[-1].push_back({-1, -1});
    
    move_pattern[2].push_back({2, 1}); move_pattern[2].push_back({2, -1}); move_pattern[2].push_back({-2, 1}); move_pattern[2].push_back({-2, -1});
    move_pattern[2].push_back({1, 2}); move_pattern[2].push_back({1, -2}); move_pattern[2].push_back({-1, 2}); move_pattern[2].push_back({-1, -2}); 

    move_pattern[3].push_back({1, 1}); move_pattern[3].push_back({2, 2}); move_pattern[3].push_back({3, 3}); move_pattern[3].push_back({4, 4}); move_pattern[3].push_back({5, 5}); move_pattern[3].push_back({6, 6}); move_pattern[3].push_back({7, 7});
    move_pattern[3].push_back({-1, -1}); move_pattern[3].push_back({-2, -2}); move_pattern[3].push_back({-3, -3}); move_pattern[3].push_back({-4, -4}); move_pattern[3].push_back({-5, -5}); move_pattern[3].push_back({-6, -6}); move_pattern[3].push_back({-7, -7});
    move_pattern[3].push_back({1, -1}); move_pattern[3].push_back({2, -2}); move_pattern[3].push_back({3, -3}); move_pattern[3].push_back({4, -4}); move_pattern[3].push_back({5, -5}); move_pattern[3].push_back({6, -6}); move_pattern[3].push_back({7, -7});
    move_pattern[3].push_back({-1, 1}); move_pattern[3].push_back({-2, 2}); move_pattern[3].push_back({-3, 3}); move_pattern[3].push_back({-4, 4}); move_pattern[3].push_back({-5, 5}); move_pattern[3].push_back({-6, 6}); move_pattern[3].push_back({-7, 7});

    move_pattern[4].push_back({1, 0}); move_pattern[4].push_back({2, 0}); move_pattern[4].push_back({3, 0}); move_pattern[4].push_back({4, 0}); move_pattern[4].push_back({5, 0}); move_pattern[4].push_back({6, 0}); move_pattern[4].push_back({7, 0});
    move_pattern[4].push_back({-1, 0}); move_pattern[4].push_back({-2, 0}); move_pattern[4].push_back({-3, 0}); move_pattern[4].push_back({-4, 0}); move_pattern[4].push_back({-5, 0}); move_pattern[4].push_back({-6, 0}); move_pattern[4].push_back({-7, 0});
    move_pattern[4].push_back({0, 1}); move_pattern[4].push_back({0, 2}); move_pattern[4].push_back({0, 3}); move_pattern[4].push_back({0, 4}); move_pattern[4].push_back({0, 5}); move_pattern[4].push_back({0, 6}); move_pattern[4].push_back({0, 7});
    move_pattern[4].push_back({0, -1}); move_pattern[4].push_back({0, -2}); move_pattern[4].push_back({0, -3}); move_pattern[4].push_back({0, -4}); move_pattern[4].push_back({0, -5}); move_pattern[4].push_back({0, -6}); move_pattern[4].push_back({0, -7});   

    move_pattern[5].push_back({1, 0}); move_pattern[5].push_back({2, 0}); move_pattern[5].push_back({3, 0}); move_pattern[5].push_back({4, 0}); move_pattern[5].push_back({5, 0}); move_pattern[5].push_back({6, 0}); move_pattern[5].push_back({7, 0});
    move_pattern[5].push_back({-1, 0}); move_pattern[5].push_back({-2, 0}); move_pattern[5].push_back({-3, 0}); move_pattern[5].push_back({-4, 0}); move_pattern[5].push_back({-5, 0}); move_pattern[5].push_back({-6, 0}); move_pattern[5].push_back({-7, 0});
    move_pattern[5].push_back({0, 1}); move_pattern[5].push_back({0, 2}); move_pattern[5].push_back({0, 3}); move_pattern[5].push_back({0, 4}); move_pattern[5].push_back({0, 5}); move_pattern[5].push_back({0, 6}); move_pattern[5].push_back({0, 7});
    move_pattern[5].push_back({0, -1}); move_pattern[5].push_back({0, -2}); move_pattern[5].push_back({0, -3}); move_pattern[5].push_back({0, -4}); move_pattern[5].push_back({0, -5}); move_pattern[5].push_back({0, -6}); move_pattern[5].push_back({0, -7});
    move_pattern[5].push_back({1, 1}); move_pattern[5].push_back({2, 2}); move_pattern[5].push_back({3, 3}); move_pattern[5].push_back({4, 4}); move_pattern[5].push_back({5, 5}); move_pattern[5].push_back({6, 6}); move_pattern[5].push_back({7, 7});
    move_pattern[5].push_back({-1, -1}); move_pattern[5].push_back({-2, -2}); move_pattern[5].push_back({-3, -3}); move_pattern[5].push_back({-4, -4}); move_pattern[5].push_back({-5, -5}); move_pattern[5].push_back({-6, -6}); move_pattern[5].push_back({-7, -7});
    move_pattern[5].push_back({1, -1}); move_pattern[5].push_back({2, -2}); move_pattern[5].push_back({3, -3}); move_pattern[5].push_back({4, -4}); move_pattern[5].push_back({5, -5}); move_pattern[5].push_back({6, -6}); move_pattern[5].push_back({7, -7});
    move_pattern[5].push_back({-1, 1}); move_pattern[5].push_back({-2, 2}); move_pattern[5].push_back({-3, 3}); move_pattern[5].push_back({-4, 4}); move_pattern[5].push_back({-5, 5}); move_pattern[5].push_back({-6, 6}); move_pattern[5].push_back({-7, 7});

    move_pattern[6].push_back({1, 0}); move_pattern[6].push_back({-1, 0}); move_pattern[6].push_back({0, 1}); move_pattern[6].push_back({0, -1});
    move_pattern[6].push_back({1, 1}); move_pattern[6].push_back({1, -1}); move_pattern[6].push_back({-1, 1}); move_pattern[6].push_back({-1, -1});
    
    move_pattern[-2].push_back({2, 1}); move_pattern[-2].push_back({2, -1}); move_pattern[-2].push_back({-2, 1}); move_pattern[-2].push_back({-2, -1});
    move_pattern[-2].push_back({1, 2}); move_pattern[-2].push_back({1, -2}); move_pattern[-2].push_back({-1, 2}); move_pattern[-2].push_back({-1, -2}); 

    move_pattern[-3].push_back({1, 1}); move_pattern[-3].push_back({2, 2}); move_pattern[-3].push_back({3, 3}); move_pattern[-3].push_back({4, 4}); move_pattern[-3].push_back({5, 5}); move_pattern[-3].push_back({6, 6}); move_pattern[-3].push_back({7, 7});
    move_pattern[-3].push_back({-1, -1}); move_pattern[-3].push_back({-2, -2}); move_pattern[-3].push_back({-3, -3}); move_pattern[-3].push_back({-4, -4}); move_pattern[-3].push_back({-5, -5}); move_pattern[-3].push_back({-6, -6}); move_pattern[-3].push_back({-7, -7});
    move_pattern[-3].push_back({1, -1}); move_pattern[-3].push_back({2, -2}); move_pattern[-3].push_back({3, -3}); move_pattern[-3].push_back({4, -4}); move_pattern[-3].push_back({5, -5}); move_pattern[-3].push_back({6, -6}); move_pattern[-3].push_back({7, -7});
    move_pattern[-3].push_back({-1, 1}); move_pattern[-3].push_back({-2, 2}); move_pattern[-3].push_back({-3, 3}); move_pattern[-3].push_back({-4, 4}); move_pattern[-3].push_back({-5, 5}); move_pattern[-3].push_back({-6, 6}); move_pattern[-3].push_back({-7, 7});

    move_pattern[-4].push_back({1, 0}); move_pattern[-4].push_back({2, 0}); move_pattern[-4].push_back({3, 0}); move_pattern[-4].push_back({4, 0}); move_pattern[-4].push_back({5, 0}); move_pattern[-4].push_back({6, 0}); move_pattern[-4].push_back({7, 0});
    move_pattern[-4].push_back({-1, 0}); move_pattern[-4].push_back({-2, 0}); move_pattern[-4].push_back({-3, 0}); move_pattern[-4].push_back({-4, 0}); move_pattern[-4].push_back({-5, 0}); move_pattern[-4].push_back({-6, 0}); move_pattern[-4].push_back({-7, 0});
    move_pattern[-4].push_back({0, 1}); move_pattern[-4].push_back({0, 2}); move_pattern[-4].push_back({0, 3}); move_pattern[-4].push_back({0, 4}); move_pattern[-4].push_back({0, 5}); move_pattern[-4].push_back({0, 6}); move_pattern[-4].push_back({0, 7});
    move_pattern[-4].push_back({0, -1}); move_pattern[-4].push_back({0, -2}); move_pattern[-4].push_back({0, -3}); move_pattern[-4].push_back({0, -4}); move_pattern[-4].push_back({0, -5}); move_pattern[-4].push_back({0, -6}); move_pattern[-4].push_back({0, -7});   

    move_pattern[-5].push_back({1, 0}); move_pattern[-5].push_back({2, 0}); move_pattern[-5].push_back({3, 0}); move_pattern[-5].push_back({4, 0}); move_pattern[-5].push_back({5, 0}); move_pattern[-5].push_back({6, 0}); move_pattern[-5].push_back({7, 0});
    move_pattern[-5].push_back({-1, 0}); move_pattern[-5].push_back({-2, 0}); move_pattern[-5].push_back({-3, 0}); move_pattern[-5].push_back({-4, 0}); move_pattern[-5].push_back({-5, 0}); move_pattern[-5].push_back({-6, 0}); move_pattern[-5].push_back({-7, 0});
    move_pattern[-5].push_back({0, 1}); move_pattern[-5].push_back({0, 2}); move_pattern[-5].push_back({0, 3}); move_pattern[-5].push_back({0, 4}); move_pattern[-5].push_back({0, 5}); move_pattern[-5].push_back({0, 6}); move_pattern[-5].push_back({0, 7});
    move_pattern[-5].push_back({0, -1}); move_pattern[-5].push_back({0, -2}); move_pattern[-5].push_back({0, -3}); move_pattern[-5].push_back({0, -4}); move_pattern[-5].push_back({0, -5}); move_pattern[-5].push_back({0, -6}); move_pattern[-5].push_back({0, -7});
    move_pattern[-5].push_back({1, 1}); move_pattern[-5].push_back({2, 2}); move_pattern[-5].push_back({3, 3}); move_pattern[-5].push_back({4, 4}); move_pattern[-5].push_back({5, 5}); move_pattern[-5].push_back({6, 6}); move_pattern[-5].push_back({7, 7});
    move_pattern[-5].push_back({-1, -1}); move_pattern[-5].push_back({-2, -2}); move_pattern[-5].push_back({-3, -3}); move_pattern[-5].push_back({-4, -4}); move_pattern[-5].push_back({-5, -5}); move_pattern[-5].push_back({-6, -6}); move_pattern[-5].push_back({-7, -7});
    move_pattern[-5].push_back({1, -1}); move_pattern[-5].push_back({2, -2}); move_pattern[-5].push_back({3, -3}); move_pattern[-5].push_back({4, -4}); move_pattern[-5].push_back({5, -5}); move_pattern[-5].push_back({6, -6}); move_pattern[-5].push_back({7, -7});
    move_pattern[-5].push_back({-1, 1}); move_pattern[-5].push_back({-2, 2}); move_pattern[-5].push_back({-3, 3}); move_pattern[-5].push_back({-4, 4}); move_pattern[-5].push_back({-5, 5}); move_pattern[-5].push_back({-6, 6}); move_pattern[-5].push_back({-7, 7});

    move_pattern[-6].push_back({1, 0}); move_pattern[-6].push_back({-1, 0}); move_pattern[-6].push_back({0, 1}); move_pattern[-6].push_back({0, -1});
    move_pattern[-6].push_back({1, 1}); move_pattern[-6].push_back({1, -1}); move_pattern[-6].push_back({-1, 1}); move_pattern[-6].push_back({-1, -1});
}


auto Board::begin() -> std::vector<std::vector<int>>::iterator{
    return current_board.begin();
}

auto Board::end() -> std::vector<std::vector<int>>::iterator{
    return current_board.end();
}

auto Board::cbegin() const -> std::vector<std::vector<int>>::const_iterator{
    return current_board.begin();
}

auto Board::cend() const -> std::vector<std::vector<int>>::const_iterator{
    return current_board.end();
}

bool Board::inboard(int x, int y) { return (x >= 0 && y >= 0 && x < 8 && y < 8); }

bool Board::check_game_ended(){
    if(game_status){
        std::cout << "Game ended\n";
        if(game_status > 0) std::cout << "White won\n";
        else std::cout << "Black won\n";
        return true;
    }
    return false;
}

void Board::get_turn(){
    if(!turn){
        std::cout << "White turn\n";
    }
    else std::cout << "Black turn\n";
    std::cout << move_left << ' ' << "move(s) left\n";
}

bool Board::valid_move(int old_x, int old_y, int new_x, int new_y){
    if(!inboard(new_x, new_y) || !inboard(old_x, old_y)) return false;
    
    int piece = current_board[old_x][old_y];
    if(!piece) return false; 
    if(turn && piece > 0) return false;
    if(!turn && piece < 0) return false;

    int destination_piece = current_board[new_x][new_y];
    
    if(destination_piece != 0 && (piece > 0) == (destination_piece > 0)) return false;
    
    int dx = new_x - old_x;
    int dy = new_y - old_y;
    
    if(piece == 3 || piece == -3 || piece == 4 || piece == -4 || piece == 5 || piece == -5){
        int step_x = (dx > 0) - (dx < 0);
        int step_y = (dy > 0) - (dy < 0);
        for(int i = 1; i < std::max(std::abs(dx), std::abs(dy)); ++i){
            if(current_board[old_x + i * step_x][old_y + i * step_y] != 0) return false;
        }
    }

    if(piece == 1 || piece == -1) {
        if(destination_piece == 0) {
            auto& moves = pawn_move_pattern[piece];
            for(auto& move : moves) {
                if(move.first == dx && move.second == dy) {
                    return true;
                }
            }
            return false;
        }
        else {
            auto& captures = pawn_capture_pattern[piece];
            for(auto& capture : captures) {
                if(capture.first == dx && capture.second == dy) {
                    return true;
                }
            }
            return false;
        }
    }
    
    if(move_pattern.find(piece) == move_pattern.end()) return false;
    
    auto& patterns = move_pattern[piece];
    for(auto& pattern : patterns) {
        if(pattern.first == dx && pattern.second == dy) {
            return true;
        }
    }
    
    return false;
}

void Board::make_move(int old_x, int old_y, int new_x, int new_y){
    if(check_game_ended()) return;
    if(!valid_move(old_x, old_y, new_x, new_y)){
        std::cout << "Invalid move\n";
        return;
    }

    if(current_board[new_x][new_y]) { make_capture(old_x, old_y, new_x, new_y); return; }

    int piece = current_board[old_x][old_y];
    board_hash ^= piece_hash[piece * 64 + old_x * 8 + old_y];
    board_hash ^= piece_hash[0 * 64 + old_x * 8 + old_y];
    board_hash ^= turn_hash[turn];
    board_hash ^= move_left_hash[move_left];

    rollback.push_back(std::make_tuple(old_x, old_y, new_x, new_y, 
        current_board[new_x][new_y], this -> turn, this -> move_left));

    current_board[new_x][new_y] = piece;
    current_board[old_x][old_y] = 0;
    --move_left;
    if(!move_left) turn = 1 - turn, move_left = 2;

    board_hash ^= piece_hash[piece * 64 + new_x * 8 + new_y];
    board_hash ^= piece_hash[0 * 64 + new_x * 8 + new_y];
    board_hash ^= turn_hash[turn];
    board_hash ^= move_left_hash[move_left];
}

void Board::make_capture(int old_x, int old_y, int new_x, int new_y){
    if(check_game_ended()) return;

    if(move_left < 2) {
        std::cout << "Insufficient move left";
        return;
    }

    int piece = current_board[old_x][old_y], captured_piece = current_board[new_x][new_y];

    board_hash ^= piece_hash[piece * 64 + old_x * 8 + old_y];
    board_hash ^= piece_hash[captured_piece * 64 + new_x * 8 + new_y];
    board_hash ^= piece_hash[0 * 64 + old_x * 8 + old_y];
    board_hash ^= turn_hash[turn];
    board_hash ^= move_left_hash[move_left];

    rollback.push_back(std::make_tuple(old_x, old_y, new_x, new_y, 
        captured_piece, this -> turn, this -> move_left));

    if(std::abs(captured_piece) == 6) game_status = -1 * captured_piece / 6;

    current_board[new_x][new_y] = piece;
    current_board[old_x][old_y] = 0;
    turn = 1 - turn;

    board_hash ^= piece_hash[piece * 64 + new_x * 8 + new_y];
    board_hash ^= turn_hash[turn];
    board_hash ^= move_left_hash[move_left];
}

int Board::board_eval() {
    // Terminal State Check
    if (game_status != 0) {
        int winner_score = (game_status > 0) ? 999999 : -999999;
        return winner_score * (turn ? -1 : 1);
    }

    int raw_score = 0;
    int pawn_count_white[8] = {0};
    int pawn_count_black[8] = {0};

    int white_king_x = -1, white_king_y = -1;
    int black_king_x = -1, black_king_y = -1;

    //  Data structures & bonus lookup for new heuristics
    std::vector<std::pair<int, int>> white_rooks;
    std::vector<std::pair<int, int>> black_rooks;
    const int passed_pawn_bonus[8] = {0, 5, 15, 30, 60, 100, 160, 0};

    //   Board boundary check helper +++
    auto is_on_board = [](int r, int c) {
        return r >= 0 && r < 8 && c >= 0 && c < 8;
    };

    // Main Board Traversal
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            int piece = current_board[r][c];
            if (piece == 0) continue;

            int abs_p = std::abs(piece);
            bool is_white = (piece > 0);
            int side = is_white ? 1 : -1;
            int mat = piece_value[abs_p];
            int pst = 0;

            int sq_idx = is_white ? (r * 8 + c) : ((7 - r) * 8 + c);

            switch (abs_p) {
                case 1: // Pawn
                    pst = pawn_sq_table[sq_idx];
                    if (is_white) pawn_count_white[c]++;
                    else pawn_count_black[c]++;

                    // PASSED PAWN HEURISTIC 
                    {
                        bool is_passed = true;
                        int forward_dir = is_white ? 1 : -1;

                        for (int check_r = r + forward_dir; check_r >= 0 && check_r < 8; check_r += forward_dir) {
                            for (int check_c = c - 1; check_c <= c + 1; ++check_c) {
                                if (is_on_board(check_r, check_c)) {
                                    int enemy_pawn = is_white ? -1 : 1;
                                    if (current_board[check_r][check_c] == enemy_pawn) {
                                        is_passed = false;
                                        break;
                                    }
                                }
                            }
                            if (!is_passed) break;
                        }

                        if (is_passed) {
                            int rank_idx = is_white ? r : (7 - r);
                            raw_score += side * passed_pawn_bonus[rank_idx];
                        }
                    }
                    break;

                case 2: // Knight
                    pst = knight_sq_table[sq_idx];

                    // +++ ADDED: KNIGHT MOBILITY +++
                    {
                        int mobility = 0;
                        const int knight_moves[8][2] = {
                            {-2,-1}, {-2,1}, {-1,-2}, {-1,2},
                            { 1,-2}, { 1,2}, { 2,-1}, { 2,1}
                        };
                        for (auto& m : knight_moves) {
                            int nr = r + m[0], nc = c + m[1];
                            if (is_on_board(nr, nc)) {
                                int dest = current_board[nr][nc];
                                if (dest == 0 || (dest > 0 != is_white)) mobility++;
                            }
                        }
                        raw_score += side * (mobility * 4);
                    }
                    break;

                // +++ ADDED: BISHOP MOBILITY
                case 3: // Bishop
                    {
                        int mobility = 0;
                        const int bishop_dirs[4][2] = {{-1,-1}, {-1,1}, {1,-1}, {1,1}};
                        for (auto& d : bishop_dirs) {
                            int nr = r + d[0], nc = c + d[1];
                            while (is_on_board(nr, nc)) {
                                int dest = current_board[nr][nc];
                                if (dest == 0) {
                                    mobility++;
                                } else {
                                    if (dest > 0 != is_white) mobility++;
                                    break;
                                }
                                nr += d[0]; nc += d[1];
                            }
                        }
                        raw_score += side * (mobility * 3);
                    }
                    break;

                case 4: // Rook
                    if (is_white) white_rooks.push_back({r, c});
                    else black_rooks.push_back({r, c});
                    break;

                case 6: // King
                    pst = king_sq_table[sq_idx];
                    if (is_white) { white_king_x = r; white_king_y = c; }
                    else { black_king_x = r; black_king_y = c; }
                    break;

                default:
                    break;
            }

            int total_piece_val = mat * 100 + (piece > 0 ? pst : -pst);
             raw_score += total_piece_val;
        }
    }

    // 3. Pawn Structure Penalties (Doubled / Isolated)
    for (int c = 0; c < 8; ++c) {
        if (pawn_count_white[c] > 1) raw_score -= (pawn_count_white[c] - 1) * 20;
        if (pawn_count_black[c] > 1) raw_score += (pawn_count_black[c] - 1) * 20;

        if (pawn_count_white[c] > 0) {
            bool left = (c > 0) && (pawn_count_white[c - 1] > 0);
            bool right = (c < 7) && (pawn_count_white[c + 1] > 0);
            if (!left && !right) raw_score -= 15;
        }
        if (pawn_count_black[c] > 0) {
            bool left = (c > 0) && (pawn_count_black[c - 1] > 0);
            bool right = (c < 7) && (pawn_count_black[c + 1] > 0);
            if (!left && !right) raw_score += 15;
        }
    }

    // +++ CONNECTED ROOKS EVALUATION +++
    auto eval_rook_connection = [&](const std::vector<std::pair<int, int>>& rooks) {
        if (rooks.size() < 2) return 0;
        int connection_score = 0;

        for (size_t i = 0; i < rooks.size(); ++i) {
            for (size_t j = i + 1; j < rooks.size(); ++j) {
                int r1 = rooks[i].first, c1 = rooks[i].second;
                int r2 = rooks[j].first, c2 = rooks[j].second;

                if (r1 == r2) { // Same Rank
                    bool blocked = false;
                    int start_c = std::min(c1, c2) + 1;
                    int end_c = std::max(c1, c2);
                    for (int col = start_c; col < end_c; ++col) {
                        if (current_board[r1][col] != 0) { blocked = true; break; }
                    }
                    if (!blocked) connection_score += 25;
                } 
                else if (c1 == c2) { // Same File (Doubled)
                    bool blocked = false;
                    int start_r = std::min(r1, r2) + 1;
                    int end_r = std::max(r1, r2);
                    for (int row = start_r; row < end_r; ++row) {
                        if (current_board[row][c1] != 0) { blocked = true; break; }
                    }
                    if (!blocked) connection_score += 25;
                }
            }
        }
        return connection_score;
    };

    raw_score += eval_rook_connection(white_rooks);
    raw_score -= eval_rook_connection(black_rooks);

    // 5. King Shield Evaluation
    auto eval_shield = [&](int k_x, int k_y, int p_type) {
        if (k_x == -1) return 0;
        int shield_score = 0;
        int forward_row = k_x + (p_type > 0 ? 1 : -1);

        if (forward_row >= 0 && forward_row < 8) {
            for (int dc = -1; dc <= 1; ++dc) {
                int sc = k_y + dc;
                if (sc >= 0 && sc < 8) {
                    if (current_board[forward_row][sc] == p_type) {
                        shield_score += 25;
                    }
                }
            }
        }
        return shield_score;
    };

    raw_score += eval_shield(white_king_x, white_king_y, 1);
    raw_score -= eval_shield(black_king_x, black_king_y, -1);

    // 6. Return relative to active player's turn
    return raw_score * (turn ? -1 : 1);
}
void Board::rollback_move(){
    if(rollback.empty()) return;
    auto [old_x, old_y, new_x, new_y, captured_piece, old_turn, old_move_left] = rollback.back();
    rollback.pop_back();

    board_hash ^= piece_hash[current_board[old_x][old_y] * 64 + old_x * 8 + old_y];
    board_hash ^= piece_hash[current_board[new_x][new_y] * 64 + new_x * 8 + new_y];
    board_hash ^= turn_hash[turn];
    board_hash ^= move_left_hash[move_left];

    current_board[old_x][old_y] = current_board[new_x][new_y];
    current_board[new_x][new_y] = captured_piece;
    if(std::abs(captured_piece) == 6) game_status = 0;
    this -> turn = old_turn;
    this -> move_left = old_move_left;

    board_hash ^= piece_hash[current_board[old_x][old_y] * 64 + old_x * 8 + old_y];
    board_hash ^= piece_hash[current_board[new_x][new_y] * 64 + new_x * 8 + new_y];
    board_hash ^= turn_hash[turn];
    board_hash ^= move_left_hash[move_left];
}

int Board::negamax(int move_remaining, int depth, int alpha, int beta){
    ++search_nodes;
    if(alpha >= beta) ++closed_window_nodes;
    if(move_remaining == 2 && depth >= 30) return board_eval();
    if(check_game_ended()) return board_eval();

    int best_score = NEGINF;

    for(auto& x: current_board){
        for(auto& y: x){
            auto& get_move_pattern = (std::abs(y) == 1) ? pawn_move_pattern[y] : move_pattern[y];
            for(auto& move: get_move_pattern){
                int new_x = (&x - &current_board[0]) + move.first;
                int new_y = (&y - &x[0]) + move.second;
                if(valid_move(&x - &current_board[0], &y - &x[0], new_x, new_y)){
                    if(current_board[new_x][new_y] != 0) continue;
                    make_move(&x - &current_board[0], &y - &x[0], new_x, new_y);
                    auto score = negamax(move_remaining == 1 ? 2 : 1, depth + (move_remaining == 1 ? 1 : 0), 
                        move_remaining == 2 ? alpha : -beta, move_remaining == 2 ? beta : -alpha) * (move_remaining == 1 ? -1 : 1);
                    best_score = std::max(best_score, score);
                    alpha = std::max(alpha, score);
                    rollback_move();
                    if(alpha >= beta) return best_score;
                }
            }
            if(move_remaining == 1) continue;
            auto& get_move_pattern_capture = (std::abs(y) == 1) ? pawn_capture_pattern[y] : move_pattern[y];
            for(auto& move: get_move_pattern_capture){
                int new_x = (&x - &current_board[0]) + move.first;
                int new_y = (&y - &x[0]) + move.second;
                if(valid_move(&x - &current_board[0], &y - &x[0], new_x, new_y)){
                    if(current_board[new_x][new_y] == 0) continue;
                    make_capture(&x - &current_board[0], &y - &x[0], new_x, new_y);
                    auto score = -negamax(2, depth + 1, -beta, -alpha);
                    best_score = std::max(best_score, score);
                    alpha = std::max(alpha, score);
                    rollback_move();
                    if(alpha >= beta) return best_score;
                }
            }
        }
    }

    return best_score;
}
