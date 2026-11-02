#pragma once

#include "../evaluator.hpp"

// A tactical alternative to the existing positional evaluators.
// Returns a side-to-move-relative score, matching the other evaluators.
class TacticalEvaluator final : public Evaluator {
public:
    explicit TacticalEvaluator(EngineConfig config = EngineConfig::standard());

    int evaluate(const Board& board) const override;

private:
    StandardEvaluator standard_evaluator_;

    // Centipawn adjustment computed from White's perspective.
    int evaluate_tactics(const Board& board) const;
};
