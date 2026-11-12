#include "watchflow/rule/RuleEngine.hpp"
#include <iostream>

using namespace std;

namespace watchflow {

void RuleEngine::addRule(Rule rule) {
    rules_.push_back(move(rule));
}

vector<pair<string, bool>> RuleEngine::onEvent(
    const FileEvent& event
) const {

    vector<pair<string, bool>> results;

    for (const auto& rule : rules_) {

        // Check if rule matches the event
        if (!rule.matches(event)) {
            continue;
        }

        cout << "[RULE] " << rule.name() << "\n";

        // Execute the rule
        bool success = rule.execute(event);

        if (success) {
            cout << "[SUCCESS]\n";
        } else {
            cout << "[FAILED]\n";
        }

        // Store result
        results.push_back({rule.name(), success});
    }

    return results;
}

}