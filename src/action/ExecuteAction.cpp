#include "watchflow/action/ExecuteAction.hpp"
#include <cstdlib>

using namespace std;

namespace watchflow {

namespace {
// Replace {file} with actual file path
string replaceFile(string command, string file)
{
    size_t pos = command.find("{file}");

    if (pos != string::npos)
    {
        command.replace(pos, 6, file);
    }

    return command;
}
}

// Constructor
ExecuteAction::ExecuteAction(string command)
{
    command_ = command;
}

// Execute command
bool ExecuteAction::execute(const FileEvent& event)
{
    string command = replaceFile(
        command_,
        event.path.string()
    );

    int result = system(command.c_str());

    return result == 0;
}

} // namespace watchflow