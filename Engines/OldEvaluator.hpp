#pragma once

#include "evaluator.hpp"

#include <array>
#include <memory>
#include <typeinfo>

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
    std::unique_ptr<Evaluator> clone() const override {
        if (typeid(*this) != typeid(OldEvaluator))
            throw std::logic_error("OldEvaluator subclasses must override clone()");
        return std::make_unique<OldEvaluator>(*this);
    }
};
