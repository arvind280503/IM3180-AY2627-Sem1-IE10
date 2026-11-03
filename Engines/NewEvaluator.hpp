#pragma once

#include "../evaluator.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <utility>

class Board;

class NewEvaluator : public Evaluator {
public:
    // Middlegame / endgame score pair. Nested so it can't clash with the
    // TaperedScore declared in NewEvaluator.hpp.
    struct Tapered {
        int mg = 0;
        int eg = 0;

        constexpr Tapered operator+(const Tapered& o) const { return {mg + o.mg, eg + o.eg}; }
        constexpr Tapered operator-(const Tapered& o) const { return {mg - o.mg, eg - o.eg}; }
        constexpr Tapered operator*(int k) const { return {mg * k, eg * k}; }
        Tapered& operator+=(const Tapered& o) { mg += o.mg; eg += o.eg; return *this; }
        Tapered& operator-=(const Tapered& o) { mg -= o.mg; eg -= o.eg; return *this; }
    };

    NewEvaluator() = default;
    ~NewEvaluator() override = default;

    int evaluate(const Board& board) const override;

private:
    // Static terms, White's point of view.
    Tapered evaluate_position(const Board& board) const;
};