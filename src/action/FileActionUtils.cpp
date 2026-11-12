#include "watchflow/action/FileActionUtils.hpp"

#include <filesystem>
#include <thread>
#include <chrono>

using namespace std;

namespace watchflow {

// Find a unique filename if the file already exists
filesystem::path resolveDuplicatePath(
    const filesystem::path& target,
    DuplicateMode mode)
{
    // Overwrite → use the same filename
    if (mode == DuplicateMode::Overwrite)
        return target;

    // File doesn't exist → use the same filename
    if (!filesystem::exists(target))
        return target;

    // Skip → return empty path
    if (mode == DuplicateMode::Skip)
        return {};

    // Rename mode
    string name = target.stem().string();
    string extension = target.extension().string();

    for (int i = 1; i < 10000; i++)
    {
        filesystem::path newFile =
            target.parent_path() /
            (name + "-" + to_string(i) + extension);

        if (!filesystem::exists(newFile))
            return newFile;
    }

    return {};
}


// Check whether the file has stopped changing
bool waitUntilFileStable(
    const filesystem::path& file,
    const FileStabilityOptions& options)
{
    // Stability checking disabled
    if (!options.enabled)
        return true;

    // File must exist
    if (!filesystem::exists(file))
        return false;

    auto previousSize = filesystem::file_size(file);

    auto stableTime = chrono::steady_clock::now();
    auto startTime = chrono::steady_clock::now();

    while (chrono::steady_clock::now() - startTime < options.timeout)
    {
        // Wait 250 milliseconds
        this_thread::sleep_for(chrono::milliseconds(250));

        auto currentSize = filesystem::file_size(file);

        // File size changed
        if (currentSize != previousSize)
        {
            previousSize = currentSize;
            stableTime = chrono::steady_clock::now();
            continue;
        }

        // File has remained unchanged long enough
        if (chrono::steady_clock::now() - stableTime >= options.stableFor)
            return true;
    }

    // Timeout
    return false;
}

} // namespace watchflow