#include "watchflow/logging/FileLogger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <stdexcept>

using namespace std;

namespace watchflow {


// Write current timestamp
void writeTimestamp(ofstream& output)
{
    auto now = chrono::system_clock::now();

    time_t currentTime =
        chrono::system_clock::to_time_t(now);

    output << "["
           << put_time(localtime(&currentTime), "%F %T")
           << "] ";
}


// Constructor
FileLogger::FileLogger(filesystem::path file)
{
    // Create parent directory if needed
    if (!file.parent_path().empty())
    {
        error_code ec;

        filesystem::create_directories(
            file.parent_path(),
            ec
        );

        if (ec)
        {
            throw runtime_error(
                "Cannot create log directory"
            );
        }
    }

    // Open log file
    output_.open(file, ios::app);

    if (!output_)
    {
        throw runtime_error(
            "Cannot open log file: " + file.string()
        );
    }
}


// Log file event
void FileLogger::logEvent(const FileEvent& event)
{
    lock_guard<mutex> lock(mutex_);
    writeTimestamp(output_);

    output_ << toString(event.type)
            << " "
            << event.path
            << "\n";

    output_.flush();
}


// Log rule result
void FileLogger::logRuleResult(
    const string& ruleName,
    bool success)
{
    lock_guard<mutex> lock(mutex_);
    writeTimestamp(output_);

    output_ << "RULE "
            << ruleName
            << " "
            << (success ? "SUCCESS" : "FAILED")
            << "\n";

    output_.flush();
}

void FileLogger::logMessage(const string& message)
{
    lock_guard<mutex> lock(mutex_);
    writeTimestamp(output_);
    output_ << message << "\n";
    output_.flush();
}

}