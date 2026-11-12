#include "watchflow/rule/RuleEngine.hpp"
#include <iostream>

using namespace std;

namespace watchflow {

void RuleEngine::addRule(Rule rule) {
    rules_.push_back(move(rule));
}

vector<const Rule*> RuleEngine::match(const FileEvent& event) const {
    vector<const Rule*> matched;
    for (const auto& rule : rules_) {
        if (rule.matches(event)) {
            matched.push_back(&rule);
        }
    }
    return matched;
}

const vector<Rule>& RuleEngine::rules() const {
    return rules_;
}

vector<pair<string, bool>> RuleEngine::onEvent(
    const FileEvent& event
) const {

    vector<pair<string, bool>> results;
    auto matched = match(event);

    for (const auto* rule : matched) {
        cout << "[RULE] " << rule->name() << "\n";

        // Execute the rule
        bool success = rule->execute(event);

        if (success) {
            cout << "[SUCCESS]\n";
        } else {
            cout << "[FAILED]\n";
        }

        // Store result
        results.push_back({rule->name(), success});
    }

    return results;
}

}