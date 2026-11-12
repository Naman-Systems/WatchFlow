#include "watchflow/action/NotificationAction.hpp"
#include <cstdlib>

using namespace std;

namespace watchflow {

namespace {
// Replace {file} with the actual filename
string replaceFile(string message, string file)
{
    size_t pos = message.find("{file}");

    if (pos != string::npos)
    {
        message.replace(pos, 6, file);
    }

    return message;
}
}


// Constructor
NotificationAction::NotificationAction(string message)
{
    message_ = message;
}


// Show notification
bool NotificationAction::execute(const FileEvent& event)
{
    // Get filename
    string file = event.path.filename().string();

    // Replace {file}
    string message = replaceFile(message_, file);

    // Create Linux notification command
    string command =
        "notify-send \"WatchFlow\" \"" + message + "\"";

    // Execute command
    int result = system(command.c_str());

    return result == 0;
}

}