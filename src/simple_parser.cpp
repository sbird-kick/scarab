#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <chrono>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <thread>    // For std::this_thread::sleep_for
#include <stdexcept> // For std::runtime_error
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <tuple>

std::unordered_map<std::string, std::string> xed_to_scarab_iclass;

static void displayHelp(const std::string &programName)
{
    std::cout << "Usage: " << programName << " [--xed-path XED_PATH] [--trace-file TRACE_FILE] [--scarab-classes-file CLASSES FILE]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --xed-path                     Path to the xed executable (default: \"xed\")" << std::endl;
    std::cout << "  --trace-file                   Path to the trace file (default: \"pt_trace\")" << std::endl;
    std::cout << "  --scarab-classes-file                  File with XED [space] SCARAB mappings in new lines (default: \"scarab_classes.txt\")" << std::endl;
    
}

void initialize_dictionary(std::string scarab_class_file)
{
    // static const char* file_path = "/users/DTRDNT/datacenter-efficiency/scarab-sim/decoder/scarab_classes.txt";
    std::ifstream infile(scarab_class_file);

    // If the file doesn't exist, create it
    if (!infile)
    {
        std::ofstream outfile(scarab_class_file);
        if (!outfile)
        {
            std::cerr << "Failed to create file: " << scarab_class_file << std::endl;
            return;
        }
        // Optionally, you could add default content here
        outfile.close();
        std::cout << "File created: " << scarab_class_file << std::endl;
        return;
    }

    // File exists, process the content
    std::string line;
    while (std::getline(infile, line))
    {
        std::istringstream iss(line);
        std::string xed_iclass, scarab_iclass;

        if (!(iss >> xed_iclass >> scarab_iclass))
        {
            // Handle error in case the line format is incorrect
            std::cerr << "Error reading line: " << line << std::endl;
            continue;
        }

        // Do something with xed_iclass and scarab_iclass
        std::cout << "XED: " << xed_iclass << ", Scarab: " << scarab_iclass << std::endl;
        xed_to_scarab_iclass[xed_iclass] = scarab_iclass;
    }

    infile.close();
    return;
}

bool read_traces(std::vector<std::string>& addresses, std::vector<std::string>& hexes, std::string traceFileName)
{
    static const int max_read = 200;

    std::ifstream infile(traceFileName);

    bool continue_read = true;

    if (!infile)
    {
        std::cout << traceFileName << " does not exist" << std::endl;
        exit(1);
    }

    for(uint i = 0; i < max_read; i++)
    {
        std::string line;
        if (!std::getline(infile, line))
        {
            // If there are fewer than max_read lines in the file
            continue_read = false;
            break;
        }

        std::istringstream iss(line);

        std::string first_entry;
        std::string second_entry; // we don't care about it.
        std::string third_entry;

        // Read the first and second entries
        if (!(iss >> first_entry >> second_entry)) {
            std::cerr << "Error reading first two entries in line: " << line << std::endl;
            continue;
        }

        // Capture everything after the first two entries
        std::getline(iss, third_entry);

        // Remove leading and trailing spaces from third_entry
        third_entry.erase(third_entry.begin(), std::find_if_not(third_entry.begin(), third_entry.end(), ::isspace));
        third_entry.erase(std::find_if_not(third_entry.rbegin(), third_entry.rend(), ::isspace).base(), third_entry.end());

        // Remove all spaces from third_entry
        third_entry.erase(std::remove(third_entry.begin(), third_entry.end(), ' '), third_entry.end());

        addresses.push_back(first_entry);
        hexes.push_back(third_entry);

    }    
    return continue_read;
}

void decode_traces(std::vector<std::string>& addresses, std::vector<std::string>& hexes, std::vector<std::string>& xed_iclasses, std::string xedPath)
{
    const char* tmpfs_hex_file_path = "temp_hex.txt";

    static int xed_decode_errors = 0;

    std::string accumulated_hex_bytes = "";
    for(auto& one_hex : hexes)
    {
        accumulated_hex_bytes+= one_hex;
    }

    std::ofstream outfile(tmpfs_hex_file_path, std::ios::trunc);
    if (!outfile)
    {
        std::cerr << "Failed to create file: " << tmpfs_hex_file_path << std::endl;
        return;
    }

    outfile << accumulated_hex_bytes;

    if (!outfile) {
        std::cerr << "Failed to write to file: " << tmpfs_hex_file_path << std::endl;
    }

    outfile.close();

    // Construct xed command for batch processing
    std::string cmd = xedPath + " -64 -ih " + tmpfs_hex_file_path;
    
    // Open a pipe to execute the command and read output
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "Error: Failed to execute xed command.\n";
        return;
    }

    std::string result;
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }

    pclose(pipe);
    
    // std::cout << result << std::endl;
    std::istringstream lines_stream(result);
    std::string line;

    while (std::getline(lines_stream, line)) {
        if (line.empty() || line[0] == '#' || line.find("XDIS") != 0) {
            continue;
        }
        std::istringstream entry_stream(line);
        std::vector<std::string> entries;
        std::string entry;

        while (entry_stream >> entry) {
            entries.push_back(entry);
        }

        if (entries.size() >= 6) {
            // std::cout << entries[5] << std::endl; // Print the 6th entry (index 5)
            xed_iclasses.push_back(entries[5]);
        } else {
            // std::cout << "Less than 6 entries in line: " << line << std::endl;
        }
    }

    if(xed_iclasses.size() != addresses.size())
    {
        xed_decode_errors++;
        std::cout << "XED decode error, dumping batch [" << xed_iclasses.size() << " vs " << addresses.size() << "] -- "  << xed_decode_errors << std::endl;
        xed_iclasses.clear();
        addresses.clear();
        hexes.clear();
    }

    return;
}

void decode_all(std::string trace_file, std::string xedPath)
{

    std::vector<std::string> addresses, hexes;
    std::vector<std::string> xed_iclasses;

    static long running_count = 0;

    // Record the start time
    auto start_time = std::chrono::steady_clock::now();

    addresses.reserve(1000);
    hexes.reserve(1000);
    xed_iclasses.reserve(1000);

    while (read_traces(addresses, hexes, trace_file))
    {
        decode_traces(addresses, hexes, xed_iclasses, xedPath);
        if ((running_count % 1000) == 0)
            {
                // Calculate the elapsed time
                auto current_time = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
                
                // Calculate and print the rate
                double rate = (elapsed > 0) ? (static_cast<double>(running_count) / elapsed) : 0.0;
                std::cout << "Read " << running_count << " instructions at " << rate << " instructions/sec\r";
            }

        for(short k = 0; k < addresses.size(); k++)
        {
            auto& xed_iclass = xed_iclasses[k];
            if(xed_to_scarab_iclass.find(xed_iclass) == xed_to_scarab_iclass.end())
            {
                std::cout << "UNMAPPED ADDRESS: " << std::stoul(addresses[k], nullptr, 16) << " " << xed_iclasses[k] << "                                                                                                                               " << std::endl;
                exit(1);
            }
        }

        running_count += addresses.size();
        addresses.clear();
        hexes.clear();
        xed_iclasses.clear();
    }
    std::cout << std::endl;

    return;
}

int main(int argc, char *argv[])
{
    std::string xedPath = "xed";
    std::string filename = "pt_trace";
    std::string scarab_class_file = "scarab_classes.txt";
    
    // Parse command-line arguments
    std::vector<std::string> args(argv, argv + argc);
    auto helpFlag = std::find(args.begin(), args.end(), "--help");

    if (helpFlag != args.end())
    {
        displayHelp(args[0]);
        return 0;
    }

    auto xedPathArg = std::find(args.begin(), args.end(), "--xed-path");
    auto traceFileArg = std::find(args.begin(), args.end(), "--trace-file");
    auto scarabClassFileArg = std::find(args.begin(), args.end(), "--scarab-classes-file");
    
    if (xedPathArg != args.end() && std::next(xedPathArg) != args.end())
    {
        xedPath = *(std::next(xedPathArg));
    }

    if (traceFileArg != args.end() && std::next(traceFileArg) != args.end())
    {
        filename = *(std::next(traceFileArg));
    }

    if (scarabClassFileArg != args.end() && std::next(scarabClassFileArg) != args.end())
    {
        scarab_class_file = *(std::next(scarabClassFileArg));
    }

    // Handling unexpected arguments
    auto invalidArg = std::find_if(args.begin(), args.end(), [](const std::string& arg) {
        return arg.substr(0, 2) == "--" && arg != "--help" && 
                arg != "--xed-path" && arg != "--trace-file" && arg != "--scarab-classes-file";
    });

    if (invalidArg != args.end())
    {
        std::cerr << "Invalid argument: " << *invalidArg << std::endl;
        displayHelp(args[0]);
        return 1;
    }

    initialize_dictionary(scarab_class_file);

    std::vector<std::string> addresses, hexes;

    read_traces(addresses, hexes, filename);

    std::vector<std::string> xed_iclasses;

    decode_traces(addresses, hexes, xed_iclasses, xedPath);

    decode_all(filename, xedPath);

    return 0;
}