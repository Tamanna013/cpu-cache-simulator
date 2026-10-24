#include "TraceReader.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace {

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::vector<std::string> split_tokens(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream in(line);
    std::string token;
    while (in >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

bool is_operation(const std::string& token) {
    return token.size() == 1 && (token[0] == 'R' || token[0] == 'r' || token[0] == 'W' ||
                                 token[0] == 'w');
}

}  // namespace

std::uint64_t parse_trace_address(const std::string& token) {
    if (token.empty()) {
        throw TraceParseError("empty address");
    }
    if (token[0] == '-' || token[0] == '+') {
        throw TraceParseError("address must be a non-negative hexadecimal value: " + token);
    }

    if (token.size() < 3 || !(token[0] == '0' && (token[1] == 'x' || token[1] == 'X'))) {
        throw TraceParseError("address must be hexadecimal with a 0x prefix: " + token);
    }

    const std::string hex = token.substr(2);
    if (hex.empty()) {
        throw TraceParseError("malformed hexadecimal address: " + token);
    }
    for (char ch : hex) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) {
            throw TraceParseError("malformed hexadecimal address: " + token);
        }
    }

    try {
        std::size_t idx = 0;
        const unsigned long long value = std::stoull(hex, &idx, 16);
        if (idx != hex.size()) {
            throw TraceParseError("malformed hexadecimal address: " + token);
        }
        return static_cast<std::uint64_t>(value);
    } catch (const std::out_of_range&) {
        throw TraceParseError("address is out of range: " + token);
    } catch (const std::invalid_argument&) {
        throw TraceParseError("malformed hexadecimal address: " + token);
    }
}

std::vector<MemoryAccess> read_trace(std::istream& in) {
    std::vector<MemoryAccess> accesses;
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(in, line)) {
        ++line_number;
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        const auto tokens = split_tokens(trimmed);
        MemoryAccess access;
        std::string address_token;
        if (tokens.size() == 1) {
            address_token = tokens[0];
            access.kind = AccessKind::Read;
        } else if (tokens.size() == 2 && is_operation(tokens[0])) {
            address_token = tokens[1];
            access.kind = (tokens[0][0] == 'W' || tokens[0][0] == 'w') ? AccessKind::Write
                                                                      : AccessKind::Read;
        } else {
            throw TraceParseError("malformed trace line " + std::to_string(line_number) + ": " +
                                  trimmed);
        }

        try {
            access.address = parse_trace_address(address_token);
            accesses.push_back(access);
        } catch (const TraceParseError& error) {
            throw TraceParseError("line " + std::to_string(line_number) + ": " + error.what());
        }
    }

    if (in.bad()) {
        throw TraceParseError("failed to read trace input");
    }

    return accesses;
}

std::vector<MemoryAccess> read_trace_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw TraceParseError("could not open trace file: " + path);
    }
    return read_trace(file);
}
