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

#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <string_view>

namespace dgtl::utils::automata {

enum class TokenKind : uint8_t {
    EndOfFile = 0,
    Identifier,
    Number,
    Operator,
    Whitespace,
    StringLiteral,
    Unknown
};

struct Token {
    TokenKind kind;
    std::string_view text;
};

enum CharClass : uint8_t {
    CC_Null = 0,
    CC_Whitespace,
    CC_Alpha,
    CC_Digit,
    CC_Operator,
    CC_Quote,
    CC_Unknown
};

[[nodiscard]] constexpr std::array<CharClass, 256> build_char_classes() noexcept {
    std::array<CharClass, 256> map{};
    for (int i = 0; i < 256; ++i) {
        map[static_cast<size_t>(i)] = CC_Unknown;
    }

    map[static_cast<size_t>(' ')] = CC_Whitespace;
    map[static_cast<size_t>('\t')] = CC_Whitespace;
    map[static_cast<size_t>('\r')] = CC_Whitespace;
    map[static_cast<size_t>('\n')] = CC_Whitespace;
    map[static_cast<size_t>('\0')] = CC_Null;

    for (int i = '0'; i <= '9'; ++i) {
        map[static_cast<size_t>(i)] = CC_Digit;
    }
    for (int i = 'a'; i <= 'z'; ++i) {
        map[static_cast<size_t>(i)] = CC_Alpha;
    }
    for (int i = 'A'; i <= 'Z'; ++i) {
        map[static_cast<size_t>(i)] = CC_Alpha;
    }
    map[static_cast<size_t>('_')] = CC_Alpha;

    map[static_cast<size_t>('+')] = CC_Operator;
    map[static_cast<size_t>('-')] = CC_Operator;
    map[static_cast<size_t>('*')] = CC_Operator;
    map[static_cast<size_t>('/')] = CC_Operator;
    map[static_cast<size_t>('=')] = CC_Operator;
    map[static_cast<size_t>('<')] = CC_Operator;
    map[static_cast<size_t>('>')] = CC_Operator;
    map[static_cast<size_t>('!')] = CC_Operator;
    map[static_cast<size_t>('&')] = CC_Operator;
    map[static_cast<size_t>('|')] = CC_Operator;
    map[static_cast<size_t>('^')] = CC_Operator;
    map[static_cast<size_t>('~')] = CC_Operator;

    map[static_cast<size_t>('"')] = CC_Quote;
    map[static_cast<size_t>('\'')] = CC_Quote;
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

        switch (kCharMap[static_cast<uint8_t>(*cursor_)]) {
            case CC_Whitespace:
                while (cursor_ < end_ && kCharMap[static_cast<uint8_t>(*cursor_)] == CC_Whitespace) {
                    ++cursor_;
                }
                return {TokenKind::Whitespace, {start, static_cast<size_t>(cursor_ - start)}};
            case CC_Alpha:
                while (cursor_ < end_) {
                    const CharClass c = kCharMap[static_cast<uint8_t>(*cursor_)];
                    if (c == CC_Alpha || c == CC_Digit) {
                        ++cursor_;
                    } else {
                        break;
                    }
                }
                return {TokenKind::Identifier, {start, static_cast<size_t>(cursor_ - start)}};
            case CC_Digit:
                while (cursor_ < end_ && kCharMap[static_cast<uint8_t>(*cursor_)] == CC_Digit) {
                    ++cursor_;
                }
                return {TokenKind::Number, {start, static_cast<size_t>(cursor_ - start)}};
            case CC_Operator:
                ++cursor_;
                while (cursor_ < end_ && kCharMap[static_cast<uint8_t>(*cursor_)] == CC_Operator) {
                    ++cursor_;
                }
                return {TokenKind::Operator, {start, static_cast<size_t>(cursor_ - start)}};
            case CC_Quote: {
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
                return {TokenKind::StringLiteral, {start, static_cast<size_t>(cursor_ - start)}};
            }
            case CC_Null:
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

// ====================================================================
// LUT FSM MATCHER DECOUPLED BUILDER
// Isolated in detail namespace to guarantee GCC constexpr compliance
// ====================================================================
namespace detail {
    enum class MatcherCharClass : unsigned char {
        Other = 0,
        Digit = 1,
        Dot = 2,
        Ws = 3,
        Dash = 4,
        Gt = 5,
        HexUpper = 6,
        HexLower = 7,
    };

    [[nodiscard]] constexpr std::array<unsigned char, 256> build_matcher_lut() noexcept {
        std::array<unsigned char, 256> lut{};
        for (unsigned int c = '0'; c <= '9'; ++c) {
            lut[c] = static_cast<unsigned char>(MatcherCharClass::Digit);
        }
        lut[static_cast<unsigned char>('.')] = static_cast<unsigned char>(MatcherCharClass::Dot);
        lut[static_cast<unsigned char>(' ')] = static_cast<unsigned char>(MatcherCharClass::Ws);
        lut[static_cast<unsigned char>('\t')] = static_cast<unsigned char>(MatcherCharClass::Ws);
        lut[static_cast<unsigned char>('\n')] = static_cast<unsigned char>(MatcherCharClass::Ws);
        lut[static_cast<unsigned char>('\r')] = static_cast<unsigned char>(MatcherCharClass::Ws);
        lut[static_cast<unsigned char>('-')] = static_cast<unsigned char>(MatcherCharClass::Dash);
        lut[static_cast<unsigned char>('>')] = static_cast<unsigned char>(MatcherCharClass::Gt);
        for (unsigned int c = 'A'; c <= 'F'; ++c) {
            lut[c] = static_cast<unsigned char>(MatcherCharClass::HexUpper);
        }
        for (unsigned int c = 'a'; c <= 'f'; ++c) {
            lut[c] = static_cast<unsigned char>(MatcherCharClass::HexLower);
        }
        return lut;
    }

    inline constexpr auto kMatcherLut = build_matcher_lut();
} // namespace detail


class LUTFsmMatcher {
public:
    using CharClass = detail::MatcherCharClass;

    [[nodiscard]] static constexpr std::string_view capture_decimal_after_prefix(
        std::string_view text,
        std::string_view prefix,
        size_t start_pos = 0) noexcept {
        size_t pos = find_prefix(text, prefix, start_pos);
        if (pos == npos) [[unlikely]] {
            return {};
        }
        pos += prefix.size();
        return capture_decimal(text, pos);
    }

    [[nodiscard]] static constexpr std::string_view capture_decimal_relaxed_after_prefix(
        std::string_view text,
        std::string_view prefix,
        size_t start_pos = 0) noexcept {
        size_t pos = find_prefix(text, prefix, start_pos);
        if (pos == npos) [[unlikely]] {
            return {};
        }
        pos += prefix.size();
        while (pos < text.size()) {
            const CharClass cc = classify(text[pos]);
            if (cc == CharClass::Ws || text[pos] == ':' || text[pos] == '=') {
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
        size_t start_pos = 0) noexcept {
        size_t pos = find_prefix(text, prefix, start_pos);
        if (pos == npos) [[unlikely]] {
            return {};
        }
        pos += prefix.size();
        return capture_nodes_triplet(text, pos);
    }

    [[nodiscard]] static constexpr std::string_view capture_non_ws_after_prefix(
        std::string_view text,
        std::string_view prefix,
        size_t start_pos = 0) noexcept {
        size_t pos = find_prefix(text, prefix, start_pos);
        if (pos == npos) [[unlikely]] {
            return {};
        }
        pos += prefix.size();
        return capture_non_ws(text, pos);
    }

    [[nodiscard]] static constexpr std::string_view capture_hex_after_prefix(
        std::string_view text,
        std::string_view prefix,
        size_t start_pos = 0) noexcept {
        size_t pos = find_prefix(text, prefix, start_pos);
        if (pos == npos) [[unlikely]] {
            return {};
        }
        pos += prefix.size();
        return capture_hex(text, pos);
    }

private:
    static constexpr size_t npos = std::string_view::npos;

    [[nodiscard]] static constexpr inline CharClass classify(char c) noexcept {
        return static_cast<CharClass>(detail::kMatcherLut[static_cast<unsigned char>(c)]);
    }

    [[nodiscard]] static constexpr size_t find_prefix(std::string_view text, std::string_view prefix, size_t start_pos) noexcept {
        if (prefix.empty() || start_pos >= text.size()) [[unlikely]] {
            return npos;
        }
        return text.find(prefix, start_pos);
    }

    [[nodiscard]] static constexpr std::string_view capture_decimal(std::string_view text, size_t pos) noexcept {
        enum class State : unsigned char {
            Start = 0,
            IntPart = 1,
            DotSeen = 2,
            FracPart = 3,
            Fail = 4,
        };

        State st = State::Start;
        size_t begin = npos;
        size_t i = pos;

        for (; i < text.size(); ++i) {
            const CharClass cc = classify(text[i]);
            switch (st) {
                case State::Start:
                    if (cc == CharClass::Digit) {
                        begin = i;
                        st = State::IntPart;
                    } else {
                        return {};
                    }
                    break;
                case State::IntPart:
                    if (cc == CharClass::Digit) {
                        // stay
                    } else if (cc == CharClass::Dot) {
                        st = State::DotSeen;
                    } else {
                        return text.substr(begin, i - begin);
                    }
                    break;
                case State::DotSeen:
                    if (cc == CharClass::Digit) {
                        st = State::FracPart;
                    } else {
                        return {};
                    }
                    break;
                case State::FracPart:
                    if (cc == CharClass::Digit) {
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

    [[nodiscard]] static constexpr std::string_view capture_nodes_triplet(std::string_view text, size_t pos) noexcept {
        enum class State : unsigned char {
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
        size_t begin = npos;
        size_t i = pos;

        for (; i < text.size(); ++i) {
            const CharClass cc = classify(text[i]);
            switch (st) {
                case State::NeedD1:
                    if (cc == CharClass::Digit) {
                        begin = i;
                        st = State::InD1;
                    } else {
                        return {};
                    }
                    break;
                case State::InD1:
                    if (cc == CharClass::Digit) {
                        // stay
                    } else if (cc == CharClass::Dash) {
                        st = State::NeedGt1;
                    } else {
                        return {};
                    }
                    break;
                case State::NeedGt1:
                    if (cc == CharClass::Gt) {
                        st = State::NeedD2;
                    } else {
                        return {};
                    }
                    break;
                case State::NeedD2:
                    if (cc == CharClass::Digit) {
                        st = State::InD2;
                    } else {
                        return {};
                    }
                    break;
                case State::InD2:
                    if (cc == CharClass::Digit) {
                        // stay
                    } else if (cc == CharClass::Dash) {
                        st = State::NeedGt2;
                    } else {
                        return {};
                    }
                    break;
                case State::NeedGt2:
                    if (cc == CharClass::Gt) {
                        st = State::NeedD3;
                    } else {
                        return {};
                    }
                    break;
                case State::NeedD3:
                    if (cc == CharClass::Digit) {
                        st = State::InD3;
                    } else {
                        return {};
                    }
                    break;
                case State::InD3:
                    if (cc == CharClass::Digit) {
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

    [[nodiscard]] static constexpr std::string_view capture_non_ws(std::string_view text, size_t pos) noexcept {
        size_t i = pos;
        while (i < text.size() && classify(text[i]) == CharClass::Ws) {
            ++i;
        }
        const size_t begin = i;
        while (i < text.size() && classify(text[i]) != CharClass::Ws) {
            ++i;
        }
        if (i == begin) [[unlikely]] {
            return {};
        }
        return text.substr(begin, i - begin);
    }

    [[nodiscard]] static constexpr std::string_view capture_hex(std::string_view text, size_t pos) noexcept {
        size_t i = pos;
        if (i + 2 <= text.size() && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            i += 2;
        }
        const size_t begin = i;
        while (i < text.size()) {
            const CharClass cc = classify(text[i]);
            if (cc == CharClass::Digit || cc == CharClass::HexUpper || cc == CharClass::HexLower) {
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

namespace dgtl::utils {

using TokenKind = dgtl::utils::automata::TokenKind;
using Token = dgtl::utils::automata::Token;
using CharClass = dgtl::utils::automata::CharClass;
using FastScanner = dgtl::utils::automata::FastScanner;
using LUTFsmMatcher = dgtl::utils::automata::LUTFsmMatcher;

} // namespace dgtl::utils