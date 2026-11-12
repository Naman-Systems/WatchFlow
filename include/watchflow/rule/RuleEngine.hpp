#pragma once

#include "watchflow/core/FileEvent.hpp"
#include "watchflow/rule/Rule.hpp"
#include <string>
#include <utility>
#include <vector>

namespace watchflow {

class RuleEngine {
public:
    void addRule(Rule rule);
    std::vector<const Rule*> match(const FileEvent& event) const;
    std::vector<std::pair<std::string, bool>> onEvent(const FileEvent& event) const;
    const std::vector<Rule>& rules() const;

private:
    std::vector<Rule> rules_;
};

} // namespace watchflow
