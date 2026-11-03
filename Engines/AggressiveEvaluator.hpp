#pragma once

#include "evaluator.hpp"

class Board;

class AggressiveEvaluator : public Evaluator {
public:
    // A score with a middlegame part (mg) and an endgame part (eg).
    struct Tapered {
        int mg{0};
        int eg{0};

        Tapered& operator+=(const Tapered& o) { mg += o.mg; eg += o.eg; return *this; }
        Tapered& operator-=(const Tapered& o) { mg -= o.mg; eg -= o.eg; return *this; }
        Tapered operator+(const Tapered& o) const { return {mg + o.mg, eg + o.eg}; }
        Tapered operator-(const Tapered& o) const { return {mg - o.mg, eg - o.eg}; }
        Tapered operator*(int k) const { return Tapered{mg * k, eg * k}; }
    };

    ~AggressiveEvaluator() override = default;

    // Score from the point of view of the side to move (negamax style).
    int evaluate(const Board& board) const override;

private:
    // Score from White's point of view (positive = good for White).
    Tapered evaluate_position(const Board& board) const;
};