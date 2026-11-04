#pragma once

#include "evaluator.hpp"

#include <array>
#include <memory>

class Board;

class OldEvaluator : public Evaluator {
private:
    StandardEvaluator standard_evaluator;
    EngineConfig config;

    static const std::array<int, 64> king_eg_sq_table;

    int evaluate_new_heuristic(const Board& board) const;

public:
    explicit OldEvaluator(EngineConfig evaluator_config = EngineConfig::standard());
    ~OldEvaluator() override = default;

    int evaluate(const Board& board) const override;
};