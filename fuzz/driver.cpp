// A small driver for the fuzz targets where libFuzzer isn't used (MSVC, or
// without TETA_WOT_LIBFUZZER): it runs each seed, then a fixed number of
// random mutations of them, from a fixed random seed, so that a run is
// repeatable and CTest can run it on every platform. It has no coverage
// feedback: libFuzzer (Clang) does the deep fuzzing.
//
//     fuzz-json [--iterations N] [--seed S] [--seconds T] [--max-length L] [--dict FILE] SEEDS...
//
// SEEDS are files (one input each), directories of them, or `.seeds` files:
// one seed per line, a JSON string (the input as UTF-8) or `{"hex": ...}`
// (`make_seeds.py` writes them). `--dict` reads a libFuzzer dictionary, whose tokens mutations
// insert. A broken invariant or an uncaught exception prints the input,
// writes it to `failure-<target>.bin`, and exits with 1.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "fuzz.hpp"
#include "teta/wot/json.hpp"

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("can't read " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), {}};
}

void add_seeds(const std::filesystem::path& path, std::vector<std::string>& seeds) {
    if (std::filesystem::is_directory(path)) {
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        for (const auto& file : files) {
            add_seeds(file, seeds);
        }
    } else if (path.extension() == ".seeds") {
        std::istringstream lines(read_file(path));
        for (std::string line; std::getline(lines, line);) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            const auto seed = teta::wot::json::parse(line);
            if (seed.is_string()) {
                seeds.push_back(seed.get<std::string>());
            } else {
                const auto hex = seed.at("hex").get<std::string>();
                std::string bytes;
                for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
                    bytes += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
                }
                seeds.push_back(bytes);
            }
        }
    } else {
        seeds.push_back(read_file(path));
    }
}

// A libFuzzer dictionary: `"token"` or `name="token"` per line, with `\\`,
// `\"` and `\xNN` escapes.
std::vector<std::string> read_dictionary(const std::filesystem::path& path) {
    std::vector<std::string> tokens;
    std::istringstream lines(read_file(path));
    for (std::string line; std::getline(lines, line);) {
        const auto first = line.find('"');
        const auto last = line.rfind('"');
        if (line.empty() || line.front() == '#' || first == std::string::npos || last <= first) {
            continue;
        }
        std::string token;
        for (std::size_t i = first + 1; i < last; ++i) {
            if (line[i] == '\\' && i + 1 < last) {
                ++i;
                if (line[i] == 'x' && i + 2 < last) {
                    token += static_cast<char>(std::stoi(line.substr(i + 1, 2), nullptr, 16));
                    i += 2;
                } else {
                    token += line[i];
                }
            } else {
                token += line[i];
            }
        }
        tokens.push_back(token);
    }
    return tokens;
}

class Mutator {
  public:
    Mutator(std::uint64_t seed, std::vector<std::string> dictionary, std::size_t max_length)
        : random_(seed), dictionary_(std::move(dictionary)), max_length_(max_length) {}

    std::string mutate(std::string input, const std::vector<std::string>& seeds) {
        const auto count = 1 + below(4);
        for (std::size_t i = 0; i < count; ++i) {
            once(input, seeds);
        }
        if (input.size() > max_length_) {
            input.resize(max_length_);
        }
        return input;
    }

    std::size_t below(std::size_t n) { return n == 0 ? 0 : static_cast<std::size_t>(random_() % n); }

  private:
    void once(std::string& input, const std::vector<std::string>& seeds) {
        static constexpr std::array<char, 24> interesting = {
            '\0', '\x7f', '\x80', '\xc3', '\xed', '\xff', '"', '\\', '{',  '}', '[', ']',
            ',',  ':',    '0',    '9',    '-',    'e',    '.', ' ',  '\n', 't', 'n', 'u'};
        static constexpr std::array<std::string_view, 9> numbers = {
            "0",        "-1",   "9223372036854775807", "18446744073709551616", "1e309", "-0.0", "1.5",
            "4.9e-324", "1_000"};
        const auto at = below(input.size() + 1);
        switch (below(9)) {
            case 0:  // flip a bit
                if (!input.empty()) {
                    input[below(input.size())] ^= static_cast<char>(1 << below(8));
                }
                break;
            case 1:  // an interesting byte
                if (!input.empty()) {
                    input[below(input.size())] = interesting[below(interesting.size())];
                }
                break;
            case 2:  // random bytes
                for (auto n = 1 + below(4); n > 0; --n) {
                    input.insert(input.begin() + static_cast<std::ptrdiff_t>(below(input.size() + 1)),
                                 static_cast<char>(below(256)));
                }
                break;
            case 3:  // erase a range
                input.erase(std::min(at, input.size()), 1 + below(8));
                break;
            case 4: {  // duplicate a range
                const auto from = below(input.size() + 1);
                input.insert(at, input.substr(from, 1 + below(16)));
                break;
            }
            case 5:  // a dictionary token
                if (!dictionary_.empty()) {
                    input.insert(at, dictionary_[below(dictionary_.size())]);
                }
                break;
            case 6:  // a dictionary token over what is there
                if (!dictionary_.empty()) {
                    const auto& token = dictionary_[below(dictionary_.size())];
                    input.replace(std::min(at, input.size()), token.size(), token);
                }
                break;
            case 7: {  // splice another seed in
                const auto& other = seeds[below(seeds.size())];
                const auto from = below(other.size() + 1);
                input = input.substr(0, at) + other.substr(from);
                break;
            }
            default: {  // a number
                const auto digit = input.find_first_of("0123456789", at);
                const auto end = digit == std::string::npos
                                     ? std::string::npos
                                     : input.find_first_not_of("0123456789.eE+-", digit);
                const auto number = numbers[below(numbers.size())];
                if (digit == std::string::npos) {
                    input.insert(at, number);
                } else {
                    input.replace(digit, end == std::string::npos ? std::string::npos : end - digit, number);
                }
            }
        }
    }

    std::mt19937_64 random_;
    std::vector<std::string> dictionary_;
    std::size_t max_length_;
};

// Runs one input; on a failure, reports it and exits.
void run(const std::string& name, const std::string& input) {
    std::string what;
    try {
        fuzz::one_input(input);
        return;
    } catch (const fuzz::Failure& failure) {
        what = std::string("broken invariant: ") + failure.what();
    } catch (const std::exception& error) {
        what = std::string("uncaught exception: ") + error.what();
    } catch (...) {
        what = "uncaught exception";
    }
    const auto file = "failure-" + name + ".bin";
    std::ofstream(file, std::ios::binary) << input;
    std::string escaped;
    teta::wot::json::append_string(escaped, std::string(input.begin(), input.end()));
    std::cerr << name << ": " << what << "\n  input (" << input.size() << " bytes, saved as " << file
              << "): " << escaped << "\n";
    std::exit(1);
}

}  // namespace

int main(int argc, char** argv) {
    std::string name = std::filesystem::path(argv[0]).stem().string();
    std::uint64_t iterations = 10000;
    std::uint64_t seed = 1;
    std::size_t max_length = 4096;
    double seconds = 0;
    std::vector<std::string> seeds;
    std::vector<std::string> dictionary;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto value = [&]() -> std::string {
                if (i + 1 >= argc) {
                    throw std::runtime_error(arg + " needs a value");
                }
                return argv[++i];
            };
            if (arg == "--iterations") {
                iterations = std::stoull(value());
            } else if (arg == "--seed") {
                seed = std::stoull(value());
            } else if (arg == "--seconds") {
                seconds = std::stod(value());
            } else if (arg == "--max-length") {
                max_length = std::stoull(value());
            } else if (arg == "--dict") {
                dictionary = read_dictionary(value());
            } else {
                add_seeds(arg, seeds);
            }
        }
    } catch (const std::exception& error) {
        std::cerr << name << ": " << error.what() << "\n";
        return 2;
    }
    if (seeds.empty()) {
        seeds.emplace_back();
    }
    for (const auto& input : seeds) {
        run(name, input);
    }
    Mutator mutator(seed, dictionary, max_length);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    std::uint64_t done = 0;
    std::string previous = seeds.front();
    for (; done < iterations; ++done) {
        if (seconds > 0 && std::chrono::steady_clock::now() > deadline) {
            break;
        }
        // Mostly a seed, sometimes the last input again: mutations compound.
        const auto& base = mutator.below(4) == 0 ? previous : seeds[mutator.below(seeds.size())];
        previous = mutator.mutate(base, seeds);
        run(name, previous);
    }
    std::cout << name << ": " << seeds.size() << " seeds and " << done << " mutations (seed " << seed
              << "), no failures\n";
    return 0;
}
