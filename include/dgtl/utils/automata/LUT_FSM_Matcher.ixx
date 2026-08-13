/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>

// Global Module Fragment: Include lightweight traditional headers here
#include <array>
#include <cstdint>
#include <cstddef>
#include <string_view>

export module dgtl.lut_fsm;

export namespace dgtl::utils::automata {

enum class TokenKind : std::uint8_t {
    EndOfFile = 0,
    Identifier,
    Number,
    Operator,
    Whitespace,
    StringLiteral,
    Unknown
};

struct Token {
    TokenKind kind{TokenKind::Unknown};
    std::string_view text;

    [[nodiscard]] TokenKind get_kind() const noexcept { return kind; }
    [[nodiscard]] std::string_view get_text() const noexcept { return text; }
    [[nodiscard]] bool is_unknown() const noexcept { return kind == TokenKind::Unknown; }
};

enum class CharClass : std::uint8_t {
    Null = 0,
    Whitespace,
    Alpha,
    Digit,
    Operator,
    Quote,
    Unknown
};

[[nodiscard]] constexpr std::array<CharClass, 256> build_char_classes() noexcept {
    std::array<CharClass, 256> map{};
    for (int i = 0; i < 256; ++i) {
        map[static_cast<std::size_t>(i)] = CharClass::Unknown;
    }

    map[static_cast<std::size_t>(' ')] = CharClass::Whitespace;
    map[static_cast<std::size_t>('\t')] = CharClass::Whitespace;
    map[static_cast<std::size_t>('\r')] = CharClass::Whitespace;
    map[static_cast<std::size_t>('\n')] = CharClass::Whitespace;
    map[static_cast<std::size_t>('\0')] = CharClass::Null;

    for (int i = '0'; i <= '9'; ++i) {
        map[static_cast<std::size_t>(i)] = CharClass::Digit;
    }
    for (int i = 'a'; i <= 'z'; ++i) {
        map[static_cast<std::size_t>(i)] = CharClass::Alpha;
    }
    for (int i = 'A'; i <= 'Z'; ++i) {
        map[static_cast<std::size_t>(i)] = CharClass::Alpha;
    }
    map[static_cast<std::size_t>('_')] = CharClass::Alpha;

    map[static_cast<std::size_t>('+')] = CharClass::Operator;
    map[static_cast<std::size_t>('-')] = CharClass::Operator;
    map[static_cast<std::size_t>('*')] = CharClass::Operator;
    map[static_cast<std::size_t>('/')] = CharClass::Operator;
    map[static_cast<std::size_t>('=')] = CharClass::Operator;
    map[static_cast<std::size_t>('<')] = CharClass::Operator;
    map[static_cast<std::size_t>('>')] = CharClass::Operator;
    map[static_cast<std::size_t>('!')] = CharClass::Operator;
    map[static_cast<std::size_t>('&')] = CharClass::Operator;
    map[static_cast<std::size_t>('|')] = CharClass::Operator;
    map[static_cast<std::size_t>('^')] = CharClass::Operator;
    map[static_cast<std::size_t>('~')] = CharClass::Operator;

    map[static_cast<std::size_t>('"')] = CharClass::Quote;
    map[static_cast<std::size_t>('\'')] = CharClass::Quote;
    return map;
}

inline constexpr auto kCharMap = build_char_classes();

class FastScanner {
    const char* cursor_;
    const char* end_;

public:
    constexpr explicit FastScanner(std::string_view source) noexcept
        : cursor_(source.data()), end_(source.data() + source.size()) {}

    [[nodiscard]] constexpr Token next() noexcept {
        if (cursor_ >= end_) [[unlikely]] {
            return {TokenKind::EndOfFile, {}};
        }
        const char* start = cursor_;

        switch (kCharMap[static_cast<std::uint8_t>(*cursor_)]) {
            case CharClass::Whitespace:
                while (cursor_ < end_ && kCharMap[static_cast<std::uint8_t>(*cursor_)] == CharClass::Whitespace) {
                    ++cursor_;
                }
                return {TokenKind::Whitespace, {start, static_cast<std::size_t>(cursor_ - start)}};
            case CharClass::Alpha:
                while (cursor_ < end_) {
                    const CharClass c = kCharMap[static_cast<std::uint8_t>(*cursor_)];
                    if (c == CharClass::Alpha || c == CharClass::Digit) {
                        ++cursor_;
                    } else {
                        break;
                    }
                }
                return {TokenKind::Identifier, {start, static_cast<std::size_t>(cursor_ - start)}};
            case CharClass::Digit:
                while (cursor_ < end_ && kCharMap[static_cast<std::uint8_t>(*cursor_)] == CharClass::Digit) {
                    ++cursor_;
                }
                return {TokenKind::Number, {start, static_cast<std::size_t>(cursor_ - start)}};
            case CharClass::Operator:
                ++cursor_;
                while (cursor_ < end_ && kCharMap[static_cast<std::uint8_t>(*cursor_)] == CharClass::Operator) {
                    ++cursor_;
                }
                return {TokenKind::Operator, {start, static_cast<std::size_t>(cursor_ - start)}};
            case CharClass::Quote: {
                const char quote_type = *cursor_;
                ++cursor_;
                while (cursor_ < end_ && *cursor_ != quote_type) {
                    if (*cursor_ == '\\' && (cursor_ + 1 < end_)) {
                        cursor_ += 2;
                    } else {
                        ++cursor_;
                    }
                }
                if (cursor_ < end_) {
                    ++cursor_;
                }
                return {TokenKind::StringLiteral, {start, static_cast<std::size_t>(cursor_ - start)}};
            }
            case CharClass::Null:
                ++cursor_;
                return {TokenKind::Unknown, {start, 1}};
            default: [[unlikely]]
                ++cursor_;
                return {TokenKind::Unknown, {start, 1}};
        }
    }

    [[nodiscard]] constexpr bool is_eof() const noexcept {
        return cursor_ >= end_;
    }
};

} // namespace dgtl::utils::automata


// ====================================================================
// LUT FSM MATCHER INTERNAL IMPLEMENTATION DETAILS
// ====================================================================
namespace dgtl::utils::automata::detail {

enum class MatcherCharClass : std::uint8_t {
    Other = 0,
    Digit = 1,
    Dot = 2,
    Ws = 3,
    Dash = 4,
    Gt = 5,
    HexUpper = 6,
    HexLower = 7,
};

[[nodiscard]] constexpr std::array<std::uint8_t, 256> build_matcher_lut() noexcept {
    std::array<std::uint8_t, 256> lut{};
    for (unsigned int c = '0'; c <= '9'; ++c) {
        lut[c] = static_cast<std::uint8_t>(MatcherCharClass::Digit);
    }
    lut[static_cast<std::uint8_t>('.')] = static_cast<std::uint8_t>(MatcherCharClass::Dot);
    lut[static_cast<std::uint8_t>(' ')] = static_cast<std::uint8_t>(MatcherCharClass::Ws);
    lut[static_cast<std::uint8_t>('\t')] = static_cast<std::uint8_t>(MatcherCharClass::Ws);
    lut[static_cast<std::uint8_t>('\n')] = static_cast<std::uint8_t>(MatcherCharClass::Ws);
    lut[static_cast<std::uint8_t>('\r')] = static_cast<std::uint8_t>(MatcherCharClass::Ws);
    lut[static_cast<std::uint8_t>('-')] = static_cast<std::uint8_t>(MatcherCharClass::Dash);
    lut[static_cast<std::uint8_t>('>')] = static_cast<std::uint8_t>(MatcherCharClass::Gt);
    for (unsigned int c = 'A'; c <= 'F'; ++c) {
        lut[c] = static_cast<std::uint8_t>(MatcherCharClass::HexUpper);
    }
    for (unsigned int c = 'a'; c <= 'f'; ++c) {
        lut[c] = static_cast<std::uint8_t>(MatcherCharClass::HexLower);
    }
    return lut;
}

inline constexpr auto kMatcherLut = build_matcher_lut();

} // namespace dgtl::utils::automata::detail


// ====================================================================
// PUBLIC AUTOMATA SCANNER LAYERS
// ====================================================================
export namespace dgtl::utils::automata {

class LUTFsmMatcher {
public:
    using MatcherCharClass = detail::MatcherCharClass;

    [[nodiscard]] static constexpr std::string_view capture_decimal_after_prefix(
        std::string_view text,
        std::string_view prefix,
        std::size_t start_pos = 0) noexcept {
        std::size_t pos = find_prefix(text, prefix, start_pos);
        if (pos == npos) [[unlikely]] {
            return {};
        }
        pos += prefix.size();
        return capture_decimal(text, pos);
    }

    [[nodiscard]] static constexpr std::string_view capture_decimal_relaxed_after_prefix(
        std::string_view text,
        std::string_view prefix,
        std::size_t start_pos = 0) noexcept {
        std::size_t pos = find_prefix(text, prefix, start_pos);
        if (pos == npos) [[unlikely]] {
            return {};
        }
        pos += prefix.size();
        while (pos < text.size()) {
            const MatcherCharClass cc = classify(text[pos]);
            if (cc == MatcherCharClass::Ws || text[pos] == ':' || text[pos] == '=') {
                ++pos;
                continue;
            }
            break;
        }
        return capture_decimal(text, pos);
    }

    [[nodiscard]] static constexpr std::string_view capture_nodes_triplet_after_prefix(
        std::string_view text,
        std::string_view prefix,
        std::size_t start_pos = 0) noexcept {
        std::size_t pos = find_prefix(text, prefix, start_pos);
        if (pos == npos) [[unlikely]] {
            return {};
        }
        pos += prefix.size();
        return capture_nodes_triplet(text, pos);
    }

    [[nodiscard]] static constexpr std::string_view capture_non_ws_after_prefix(
        std::string_view text,
        std::string_view prefix,
        std::size_t start_pos = 0) noexcept {
        std::size_t pos = find_prefix(text, prefix, start_pos);
        if (pos == npos) [[unlikely]] {
            return {};
        }
        pos += prefix.size();
        return capture_non_ws(text, pos);
    }

    [[nodiscard]] static constexpr std::string_view capture_hex_after_prefix(
        std::string_view text,
        std::string_view prefix,
        std::size_t start_pos = 0) noexcept {
        std::size_t pos = find_prefix(text, prefix, start_pos);
        if (pos == npos) [[unlikely]] {
            return {};
        }
        pos += prefix.size();
        return capture_hex(text, pos);
    }

private:
    static constexpr std::size_t npos = std::string_view::npos;

    [[nodiscard]] static constexpr inline MatcherCharClass classify(char c) noexcept {
        return static_cast<MatcherCharClass>(detail::kMatcherLut[static_cast<std::uint8_t>(c)]);
    }

    [[nodiscard]] static constexpr std::size_t find_prefix(std::string_view text, std::string_view prefix, std::size_t start_pos) noexcept {
        if (prefix.empty() || start_pos >= text.size()) [[unlikely]] {
            return npos;
        }
        return text.find(prefix, start_pos);
    }

    [[nodiscard]] static constexpr std::string_view capture_decimal(std::string_view text, std::size_t pos) noexcept {
        enum class State : std::uint8_t {
            Start = 0,
            IntPart = 1,
            DotSeen = 2,
            FracPart = 3,
            Fail = 4,
        };

        State st = State::Start;
        std::size_t begin = npos;
        std::size_t i = pos;

        for (; i < text.size(); ++i) {
            const MatcherCharClass cc = classify(text[i]);
            switch (st) {
                case State::Start:
                    if (cc == MatcherCharClass::Digit) {
                        begin = i;
                        st = State::IntPart;
                    } else {
                        return {};
                    }
                    break;
                case State::IntPart:
                    if (cc == MatcherCharClass::Digit) {
                        // stay
                    } else if (cc == MatcherCharClass::Dot) {
                        st = State::DotSeen;
                    } else {
                        return text.substr(begin, i - begin);
                    }
                    break;
                case State::DotSeen:
                    if (cc == MatcherCharClass::Digit) {
                        st = State::FracPart;
                    } else {
                        return {};
                    }
                    break;
                case State::FracPart:
                    if (cc == MatcherCharClass::Digit) {
                        // stay
                    } else {
                        return text.substr(begin, i - begin);
                    }
                    break;
                case State::Fail:
                default: [[unlikely]]
                    return {};
            }
        }

        if (begin == npos) [[unlikely]] {
            return {};
        }
        if (st == State::DotSeen) [[unlikely]] {
            return {};
        }
        return text.substr(begin, i - begin);
    }

    [[nodiscard]] static constexpr std::string_view capture_nodes_triplet(std::string_view text, std::size_t pos) noexcept {
        enum class State : std::uint8_t {
            NeedD1 = 0,
            InD1 = 1,
            NeedGt1 = 2,
            NeedD2 = 3,
            InD2 = 4,
            NeedGt2 = 5,
            NeedD3 = 6,
            InD3 = 7,
            Fail = 8,
        };

        State st = State::NeedD1;
        std::size_t begin = npos;
        std::size_t i = pos;

        for (; i < text.size(); ++i) {
            const MatcherCharClass cc = classify(text[i]);
            switch (st) {
                case State::NeedD1:
                    if (cc == MatcherCharClass::Digit) {
                        begin = i;
                        st = State::InD1;
                    } else {
                        return {};
                    }
                    break;
                case State::InD1:
                    if (cc == MatcherCharClass::Digit) {
                        // stay
                    } else if (cc == MatcherCharClass::Dash) {
                        st = State::NeedGt1;
                    } else {
                        return {};
                    }
                    break;
                case State::NeedGt1:
                    if (cc == MatcherCharClass::Gt) {
                        st = State::NeedD2;
                    } else {
                        return {};
                    }
                    break;
                case State::NeedD2:
                    if (cc == MatcherCharClass::Digit) {
                        st = State::InD2;
                    } else {
                        return {};
                    }
                    break;
                case State::InD2:
                    if (cc == MatcherCharClass::Digit) {
                        // stay
                    } else if (cc == MatcherCharClass::Dash) {
                        st = State::NeedGt2;
                    } else {
                        return {};
                    }
                    break;
                case State::NeedGt2:
                    if (cc == MatcherCharClass::Gt) {
                        st = State::NeedD3;
                    } else {
                        return {};
                    }
                    break;
                case State::NeedD3:
                    if (cc == MatcherCharClass::Digit) {
                        st = State::InD3;
                    } else {
                        return {};
                    }
                    break;
                case State::InD3:
                    if (cc == MatcherCharClass::Digit) {
                        // stay
                    } else {
                        return text.substr(begin, i - begin);
                    }
                    break;
                case State::Fail:
                default: [[unlikely]]
                    return {};
            }
        }

        if (begin == npos || st != State::InD3) [[unlikely]] {
            return {};
        }
        return text.substr(begin, i - begin);
    }

    [[nodiscard]] static constexpr std::string_view capture_non_ws(std::string_view text, std::size_t pos) noexcept {
        std::size_t i = pos;
        while (i < text.size() && classify(text[i]) == MatcherCharClass::Ws) {
            ++i;
        }
        const std::size_t begin = i;
        while (i < text.size() && classify(text[i]) != MatcherCharClass::Ws) {
            ++i;
        }
        if (i == begin) [[unlikely]] {
            return {};
        }
        return text.substr(begin, i - begin);
    }

    [[nodiscard]] static constexpr std::string_view capture_hex(std::string_view text, std::size_t pos) noexcept {
        std::size_t i = pos;
        if (i + 2 <= text.size() && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            i += 2;
        }
        const std::size_t begin = i;
        while (i < text.size()) {
            const MatcherCharClass cc = classify(text[i]);
            if (cc == MatcherCharClass::Digit || cc == MatcherCharClass::HexUpper || cc == MatcherCharClass::HexLower) {
                ++i;
                continue;
            }
            break;
        }
        if (i == begin) [[unlikely]] {
            return {};
        }
        return text.substr(pos, i - pos);
    }
};

} // namespace dgtl::utils::automata

// ================================
// COMPATIBILITY ALIAS SCOPE
// ================================
export namespace dgtl::utils {

using TokenKind = dgtl::utils::automata::TokenKind;
using Token = dgtl::utils::automata::Token;
using CharClass = dgtl::utils::automata::CharClass;
using FastScanner = dgtl::utils::automata::FastScanner;
using LUTFsmMatcher = dgtl::utils::automata::LUTFsmMatcher;

} // namespace dgtl::utils