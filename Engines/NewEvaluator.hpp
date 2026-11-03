#pragma once

class Board;

class NewEvaluator {
public:
    // A score with a middlegame part (mg) and an endgame part (eg).
    struct Tapered {
        int mg;
        int eg;

        Tapered& operator+=(const Tapered& o) { mg += o.mg; eg += o.eg; return *this; }
        Tapered& operator-=(const Tapered& o) { mg -= o.mg; eg -= o.eg; return *this; }
        Tapered operator*(int k) const { return Tapered{mg * k, eg * k}; }
    };

    // Score from the point of view of the side to move (negamax style).
    int evaluate(const Board& board) const;

private:
    // Score from White's point of view (positive = good for White).
    Tapered evaluate_position(const Board& board) const;
};