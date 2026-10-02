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
#include <array>
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <initializer_list>
#include <utility>

export module dgtl.lut_fsm;

// ====================================================================
// CORE TOKEN DEFINITIONS
// ====================================================================
export namespace dgtl::utils::automata {

enum class TokenKind : std::uint8_t {
    EndOfFile = 0,
    Identifier = 1,
    Number = 2,
    Operator = 3,
    Whitespace = 4,
    StringLiteral = 5,
    CharLiteral = 6,
    Delimiter = 7,
    Comment = 8,
    Unknown = 9
};

struct Token {
    TokenKind kind{TokenKind::Unknown};
    std::string_view text{};

    [[nodiscard]] constexpr TokenKind get_kind() const noexcept { return kind; }
    [[nodiscard]] constexpr std::string_view get_text() const noexcept { return text; }
    [[nodiscard]] constexpr bool is_unknown() const noexcept { return kind == TokenKind::Unknown; }
    [[nodiscard]] constexpr bool is_eof() const noexcept { return kind == TokenKind::EndOfFile; }
    [[nodiscard]] constexpr bool is(TokenKind k) const noexcept { return kind == k; }
    [[nodiscard]] constexpr bool is_not(TokenKind k) const noexcept { return kind != k; }
    [[nodiscard]] constexpr bool empty() const noexcept { return text.empty(); }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return text.size(); }
};

enum class CharClass : std::uint8_t {
    Null = 0,
    Whitespace = 1,
    Alpha = 2,
    Digit = 3,
    Operator = 4,
    Quote = 5,
    Delimiter = 6,
    Unknown = 7
};

[[nodiscard]] constexpr std::array<CharClass, 256> build_char_classes() noexcept {
    std::array<CharClass, 256> map{};
    for (std::size_t i = 0; i < 256; ++i) {
        map[i] = CharClass::Unknown;
    }

    map[static_cast<std::size_t>('\0')] = CharClass::Null;
    map[static_cast<std::size_t>(' ')]  = CharClass::Whitespace;
    map[static_cast<std::size_t>('\t')] = CharClass::Whitespace;
    map[static_cast<std::size_t>('\r')] = CharClass::Whitespace;
    map[static_cast<std::size_t>('\n')] = CharClass::Whitespace;
    map[static_cast<std::size_t>('\v')] = CharClass::Whitespace;
    map[static_cast<std::size_t>('\f')] = CharClass::Whitespace;

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
    map[static_cast<std::size_t>('$')] = CharClass::Alpha;

    // Operators
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
    map[static_cast<std::size_t>('%')] = CharClass::Operator;
    map[static_cast<std::size_t>('?')] = CharClass::Operator;

    // Quotes
    map[static_cast<std::size_t>('"')] = CharClass::Quote;
    map[static_cast<std::size_t>('\'')] = CharClass::Quote;

    // Delimiters
    map[static_cast<std::size_t>('(')] = CharClass::Delimiter;
    map[static_cast<std::size_t>(')')] = CharClass::Delimiter;
    map[static_cast<std::size_t>('[')] = CharClass::Delimiter;
    map[static_cast<std::size_t>(']')] = CharClass::Delimiter;
    map[static_cast<std::size_t>('{')] = CharClass::Delimiter;
    map[static_cast<std::size_t>('}')] = CharClass::Delimiter;
    map[static_cast<std::size_t>(',')] = CharClass::Delimiter;
    map[static_cast<std::size_t>(';')] = CharClass::Delimiter;
    map[static_cast<std::size_t>(':')] = CharClass::Delimiter;
    map[static_cast<std::size_t>('.')] = CharClass::Delimiter;

    return map;
}

inline constexpr auto kCharMap = build_char_classes();

// ====================================================================
// ULTRA-HIGH PERFORMANCE CONSTEXPR 2D BIT MATRIX LUT
// ====================================================================
template <std::size_t Rows, std::size_t Cols>
class BitMatrixLUT {
    static constexpr std::size_t kWordsPerRow = (Cols + 63) / 64;
    static constexpr std::size_t kTotalWords = Rows * kWordsPerRow;
    std::array<std::uint64_t, kTotalWords> words_{};

public:
    constexpr BitMatrixLUT() noexcept = default;

    constexpr BitMatrixLUT(std::initializer_list<std::pair<std::size_t, std::size_t>> coords) noexcept {
        for (const auto& [r, c] : coords) {
            set(r, c, true);
        }
    }

    [[nodiscard]] constexpr bool get(std::size_t row, std::size_t col) const noexcept {
        if (row >= Rows || col >= Cols) [[unlikely]] return false;
        const std::size_t word_idx = row * kWordsPerRow + (col / 64);
        const std::size_t bit_idx = col % 64;
        return (words_[word_idx] & (1ULL << bit_idx)) != 0;
    }

    constexpr void set(std::size_t row, std::size_t col, bool val = true) noexcept {
        if (row >= Rows || col >= Cols) [[unlikely]] return;
        const std::size_t word_idx = row * kWordsPerRow + (col / 64);
        const std::size_t bit_idx = col % 64;
        if (val) {
            words_[word_idx] |= (1ULL << bit_idx);
        } else {
            words_[word_idx] &= ~(1ULL << bit_idx);
        }
    }

    [[nodiscard]] constexpr std::uint64_t row_word(std::size_t row, std::size_t word_idx = 0) const noexcept {
        if (row >= Rows || word_idx >= kWordsPerRow) [[unlikely]] return 0;
        return words_[row * kWordsPerRow + word_idx];
    }

    [[nodiscard]] static constexpr std::size_t rows() noexcept { return Rows; }
    [[nodiscard]] static constexpr std::size_t cols() noexcept { return Cols; }
};

// ====================================================================
// CACHE-LINE PACKED CONSTEXPR 2D DENSE TABLE LUT
// ====================================================================
template <typename T, std::size_t Rows, std::size_t Cols>
class DenseLUT2D {
    std::array<T, Rows * Cols> data_{};

public:
    constexpr DenseLUT2D() noexcept = default;

    constexpr explicit DenseLUT2D(T default_val) noexcept {
        data_.fill(default_val);
    }

    [[nodiscard]] constexpr const T& at(std::size_t r, std::size_t c) const noexcept {
        return data_[r * Cols + c];
    }

    [[nodiscard]] constexpr const T& operator()(std::size_t r, std::size_t c) const noexcept {
        return data_[r * Cols + c];
    }

    [[nodiscard]] constexpr T& operator()(std::size_t r, std::size_t c) noexcept {
        return data_[r * Cols + c];
    }

    constexpr void set(std::size_t r, std::size_t c, T val) noexcept {
        if (r < Rows && c < Cols) {
            data_[r * Cols + c] = val;
        }
    }

    [[nodiscard]] constexpr T get_or(std::size_t r, std::size_t c, T default_val) const noexcept {
        if (r >= Rows || c >= Cols) return default_val;
        return data_[r * Cols + c];
    }

    [[nodiscard]] static constexpr std::size_t rows() noexcept { return Rows; }
    [[nodiscard]] static constexpr std::size_t cols() noexcept { return Cols; }
};

// ====================================================================
// GENERIC ZERO-OVERHEAD STATE TRANSITION FSM ENGINE
// ====================================================================
template <typename StateEnum, typename EventEnum, std::size_t NumStates, std::size_t NumEvents>
class StateTransitionFSM {
    DenseLUT2D<StateEnum, NumStates, NumEvents> transitions_;
    BitMatrixLUT<NumStates, NumEvents> legal_;
    BitMatrixLUT<NumStates, 1> terminal_;

public:
    constexpr explicit StateTransitionFSM(StateEnum error_state) noexcept
        : transitions_(error_state) {}

    constexpr StateTransitionFSM& add_transition(StateEnum from, EventEnum evt, StateEnum to) noexcept {
        const auto r = static_cast<std::size_t>(from);
        const auto c = static_cast<std::size_t>(evt);
        transitions_.set(r, c, to);
        legal_.set(r, c, true);
        return *this;
    }

    constexpr StateTransitionFSM& set_terminal(StateEnum state, bool is_term = true) noexcept {
        terminal_.set(static_cast<std::size_t>(state), 0, is_term);
        return *this;
    }

    [[nodiscard]] constexpr StateEnum step(StateEnum current, EventEnum evt) const noexcept {
        const auto r = static_cast<std::size_t>(current);
        const auto c = static_cast<std::size_t>(evt);
        return transitions_.get_or(r, c, current);
    }

    [[nodiscard]] constexpr bool can_transition(StateEnum current, EventEnum evt) const noexcept {
        const auto r = static_cast<std::size_t>(current);
        const auto c = static_cast<std::size_t>(evt);
        return legal_.get(r, c);
    }

    [[nodiscard]] constexpr bool is_terminal(StateEnum state) const noexcept {
        return terminal_.get(static_cast<std::size_t>(state), 0);
    }
};

// ====================================================================
// INDUSTRIAL-GRADE FAST SCANNER
// ====================================================================
class FastScanner {
    const char* cursor_;
    const char* end_;

    [[nodiscard]] static constexpr bool is_hex_char(char c) noexcept {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    [[nodiscard]] static constexpr bool is_digit_char(char c) noexcept {
        return c >= '0' && c <= '9';
    }

public:
    constexpr explicit FastScanner(std::string_view source) noexcept
        : cursor_(source.data()), end_(source.data() + source.size()) {}

    [[nodiscard]] constexpr Token next() noexcept {
        if (cursor_ >= end_) [[unlikely]] {
            return {TokenKind::EndOfFile, {}};
        }
        const char* start = cursor_;
        const auto cc = kCharMap[static_cast<std::uint8_t>(*cursor_)];

        switch (cc) {
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

            case CharClass::Digit: {
                // Check for hexadecimal: 0x... / 0X...
                if (*cursor_ == '0' && cursor_ + 1 < end_ && (*(cursor_ + 1) == 'x' || *(cursor_ + 1) == 'X')) {
                    cursor_ += 2;
                    while (cursor_ < end_ && is_hex_char(*cursor_)) {
                        ++cursor_;
                    }
                    return {TokenKind::Number, {start, static_cast<std::size_t>(cursor_ - start)}};
                }
                // Check for binary: 0b... / 0B...
                if (*cursor_ == '0' && cursor_ + 1 < end_ && (*(cursor_ + 1) == 'b' || *(cursor_ + 1) == 'B')) {
                    cursor_ += 2;
                    while (cursor_ < end_ && (*cursor_ == '0' || *cursor_ == '1')) {
                        ++cursor_;
                    }
                    return {TokenKind::Number, {start, static_cast<std::size_t>(cursor_ - start)}};
                }
                // Decimal integer with optional fraction and exponent
                while (cursor_ < end_ && is_digit_char(*cursor_)) {
                    ++cursor_;
                }
                // Fractional part (must be followed by a digit to avoid eating dot in 123.method())
                if (cursor_ < end_ && *cursor_ == '.' && cursor_ + 1 < end_ && is_digit_char(*(cursor_ + 1))) {
                    ++cursor_; // consume '.'
                    while (cursor_ < end_ && is_digit_char(*cursor_)) {
                        ++cursor_;
                    }
                }
                // Exponent part
                if (cursor_ < end_ && (*cursor_ == 'e' || *cursor_ == 'E')) {
                    const char* probe = cursor_ + 1;
                    if (probe < end_ && (*probe == '+' || *probe == '-')) {
                        ++probe;
                    }
                    if (probe < end_ && is_digit_char(*probe)) {
                        cursor_ = probe;
                        while (cursor_ < end_ && is_digit_char(*cursor_)) {
                            ++cursor_;
                        }
                    }
                }
                return {TokenKind::Number, {start, static_cast<std::size_t>(cursor_ - start)}};
            }

            case CharClass::Quote: {
                const char quote_type = *cursor_;
                ++cursor_;
                bool closed = false;
                while (cursor_ < end_) {
                    if (*cursor_ == '\\') {
                        if (cursor_ + 1 < end_) {
                            cursor_ += 2;
                        } else {
                            ++cursor_;
                            break; // Trailing backslash at EOF
                        }
                    } else if (*cursor_ == quote_type) {
                        ++cursor_;
                        closed = true;
                        break;
                    } else {
                        ++cursor_;
                    }
                }
                if (!closed) {
                    return {TokenKind::Unknown, {start, static_cast<std::size_t>(cursor_ - start)}};
                }
                const TokenKind kind = (quote_type == '\'') ? TokenKind::CharLiteral : TokenKind::StringLiteral;
                return {kind, {start, static_cast<std::size_t>(cursor_ - start)}};
            }

            case CharClass::Operator: {
                // Check comments
                if (*cursor_ == '/' && cursor_ + 1 < end_) {
                    if (*(cursor_ + 1) == '/') {
                        // Line comment
                        cursor_ += 2;
                        while (cursor_ < end_ && *cursor_ != '\n' && *cursor_ != '\r') {
                            ++cursor_;
                        }
                        return {TokenKind::Comment, {start, static_cast<std::size_t>(cursor_ - start)}};
                    }
                    if (*(cursor_ + 1) == '*') {
                        // Block comment
                        cursor_ += 2;
                        while (cursor_ + 1 < end_) {
                            if (*cursor_ == '*' && *(cursor_ + 1) == '/') {
                                cursor_ += 2;
                                return {TokenKind::Comment, {start, static_cast<std::size_t>(cursor_ - start)}};
                            }
                            ++cursor_;
                        }
                        cursor_ = end_;
                        return {TokenKind::Comment, {start, static_cast<std::size_t>(cursor_ - start)}};
                    }
                }

                // Check standard 2-character compound operators
                if (cursor_ + 1 < end_) {
                    const char c0 = *cursor_;
                    const char c1 = *(cursor_ + 1);
                    if ((c0 == '-' && c1 == '>') ||
                        (c0 == '=' && c1 == '=') ||
                        (c0 == '!' && c1 == '=') ||
                        (c0 == '<' && c1 == '=') ||
                        (c0 == '>' && c1 == '=') ||
                        (c0 == '&' && c1 == '&') ||
                        (c0 == '|' && c1 == '|') ||
                        (c0 == '<' && c1 == '<') ||
                        (c0 == '>' && c1 == '>') ||
                        (c0 == '+' && c1 == '=') ||
                        (c0 == '-' && c1 == '=') ||
                        (c0 == '*' && c1 == '=') ||
                        (c0 == '/' && c1 == '=') ||
                        (c0 == ':' && c1 == ':')) {
                        cursor_ += 2;
                        return {TokenKind::Operator, {start, 2}};
                    }
                }

                // Single operator character
                ++cursor_;
                return {TokenKind::Operator, {start, 1}};
            }

            case CharClass::Delimiter:
                ++cursor_;
                return {TokenKind::Delimiter, {start, 1}};

            case CharClass::Null:
            case CharClass::Unknown:
            default: [[unlikely]]
                ++cursor_;
                return {TokenKind::Unknown, {start, 1}};
        }
    }

    [[nodiscard]] constexpr Token peek() const noexcept {
        FastScanner copy = *this;
        return copy.next();
    }

    constexpr void skip_whitespace() noexcept {
        while (cursor_ < end_ && kCharMap[static_cast<std::uint8_t>(*cursor_)] == CharClass::Whitespace) {
            ++cursor_;
        }
    }

    [[nodiscard]] constexpr bool is_eof() const noexcept {
        return cursor_ >= end_;
    }

    [[nodiscard]] constexpr std::string_view remaining() const noexcept {
        if (cursor_ >= end_) return {};
        return {cursor_, static_cast<std::size_t>(end_ - cursor_)};
    }

    constexpr void reset(std::string_view source) noexcept {
        cursor_ = source.data();
        end_ = source.data() + source.size();
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
    Plus = 8,
    Colon = 9,
    Equal = 10,
    Tilde = 11,
    Exp = 12,
    Alpha = 13,
};

[[nodiscard]] constexpr std::array<std::uint8_t, 256> build_matcher_lut() noexcept {
    std::array<std::uint8_t, 256> lut{};
    for (std::size_t i = 0; i < 256; ++i) {
        lut[i] = static_cast<std::uint8_t>(MatcherCharClass::Other);
    }
    for (unsigned int c = '0'; c <= '9'; ++c) {
        lut[c] = static_cast<std::uint8_t>(MatcherCharClass::Digit);
    }
    lut[static_cast<std::uint8_t>('.')] = static_cast<std::uint8_t>(MatcherCharClass::Dot);
    lut[static_cast<std::uint8_t>(' ')] = static_cast<std::uint8_t>(MatcherCharClass::Ws);
    lut[static_cast<std::uint8_t>('\t')] = static_cast<std::uint8_t>(MatcherCharClass::Ws);
    lut[static_cast<std::uint8_t>('\n')] = static_cast<std::uint8_t>(MatcherCharClass::Ws);
    lut[static_cast<std::uint8_t>('\r')] = static_cast<std::uint8_t>(MatcherCharClass::Ws);
    lut[static_cast<std::uint8_t>('-')] = static_cast<std::uint8_t>(MatcherCharClass::Dash);
    lut[static_cast<std::uint8_t>('+')] = static_cast<std::uint8_t>(MatcherCharClass::Plus);
    lut[static_cast<std::uint8_t>('>')] = static_cast<std::uint8_t>(MatcherCharClass::Gt);
    lut[static_cast<std::uint8_t>('=')] = static_cast<std::uint8_t>(MatcherCharClass::Equal);
    lut[static_cast<std::uint8_t>(':')] = static_cast<std::uint8_t>(MatcherCharClass::Colon);
    lut[static_cast<std::uint8_t>('~')] = static_cast<std::uint8_t>(MatcherCharClass::Tilde);
    lut[static_cast<std::uint8_t>('e')] = static_cast<std::uint8_t>(MatcherCharClass::Exp);
    lut[static_cast<std::uint8_t>('E')] = static_cast<std::uint8_t>(MatcherCharClass::Exp);

    for (unsigned int c = 'A'; c <= 'F'; ++c) {
        if (c != 'E') {
            lut[c] = static_cast<std::uint8_t>(MatcherCharClass::HexUpper);
        }
    }
    for (unsigned int c = 'a'; c <= 'f'; ++c) {
        if (c != 'e') {
            lut[c] = static_cast<std::uint8_t>(MatcherCharClass::HexLower);
        }
    }
    for (unsigned int c = 'g'; c <= 'z'; ++c) {
        lut[c] = static_cast<std::uint8_t>(MatcherCharClass::Alpha);
    }
    for (unsigned int c = 'G'; c <= 'Z'; ++c) {
        lut[c] = static_cast<std::uint8_t>(MatcherCharClass::Alpha);
    }
    lut[static_cast<std::uint8_t>('_')] = static_cast<std::uint8_t>(MatcherCharClass::Alpha);

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
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
            ++pos;
        }
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
            if (cc == MatcherCharClass::Ws || cc == MatcherCharClass::Colon || 
                cc == MatcherCharClass::Equal || cc == MatcherCharClass::Tilde) {
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

    [[nodiscard]] static constexpr std::string_view capture_non_ws_trimmed_after_prefix(
        std::string_view text,
        std::string_view prefix,
        std::string_view trim_chars = ",;:)",
        std::size_t start_pos = 0) noexcept {
        std::string_view val = capture_non_ws_after_prefix(text, prefix, start_pos);
        while (!val.empty() && trim_chars.find(val.back()) != std::string_view::npos) {
            val.remove_suffix(1);
        }
        return val;
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

    [[nodiscard]] static constexpr std::string_view capture_signed_int_after_prefix(
        std::string_view text,
        std::string_view prefix,
        std::size_t start_pos = 0) noexcept {
        std::size_t pos = find_prefix(text, prefix, start_pos);
        if (pos == npos) [[unlikely]] {
            return {};
        }
        pos += prefix.size();
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
            ++pos;
        }
        return capture_signed_int(text, pos);
    }

    [[nodiscard]] static constexpr std::string_view capture_identifier_after_prefix(
        std::string_view text,
        std::string_view prefix,
        std::size_t start_pos = 0) noexcept {
        std::size_t pos = find_prefix(text, prefix, start_pos);
        if (pos == npos) [[unlikely]] {
            return {};
        }
        pos += prefix.size();
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
            ++pos;
        }
        return capture_identifier(text, pos);
    }

    [[nodiscard]] static constexpr std::string_view capture_delimited_after_prefix(
        std::string_view text,
        std::string_view prefix,
        char open_delim,
        char close_delim,
        std::size_t start_pos = 0) noexcept {
        std::size_t pos = find_prefix(text, prefix, start_pos);
        if (pos == npos) [[unlikely]] {
            return {};
        }
        pos += prefix.size();
        while (pos < text.size() && text[pos] != open_delim) {
            ++pos;
        }
        if (pos >= text.size()) return {};
        ++pos; // skip open delim
        const std::size_t begin = pos;
        while (pos < text.size() && text[pos] != close_delim) {
            ++pos;
        }
        if (pos >= text.size()) return {};
        return text.substr(begin, pos - begin);
    }

private:
    static constexpr std::size_t npos = std::string_view::npos;

    [[nodiscard]] static constexpr inline MatcherCharClass classify(char c) noexcept {
        return static_cast<MatcherCharClass>(detail::kMatcherLut[static_cast<std::uint8_t>(c)]);
    }

    [[nodiscard]] static constexpr inline bool is_ws(char c) noexcept {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    [[nodiscard]] static constexpr inline bool is_digit(char c) noexcept {
        return c >= '0' && c <= '9';
    }

    [[nodiscard]] static constexpr inline bool is_hex_char(char c) noexcept {
        const auto cc = classify(c);
        return cc == MatcherCharClass::Digit || cc == MatcherCharClass::HexUpper ||
               cc == MatcherCharClass::HexLower || cc == MatcherCharClass::Exp;
    }

    [[nodiscard]] static constexpr std::size_t find_prefix(
        std::string_view text,
        std::string_view prefix,
        std::size_t start_pos) noexcept {
        if (prefix.empty() || start_pos >= text.size()) [[unlikely]] {
            return npos;
        }
        return text.find(prefix, start_pos);
    }

    [[nodiscard]] static constexpr std::string_view capture_decimal(
        std::string_view text,
        std::size_t pos) noexcept {
        if (pos >= text.size()) [[unlikely]] return {};

        std::size_t i = pos;
        const std::size_t begin = i;

        // Optional leading sign
        if (text[i] == '+' || text[i] == '-') {
            ++i;
            if (i >= text.size()) return {};
        }

        // Must see either digit or dot followed by digit
        if (i < text.size() && text[i] == '.') {
            if (i + 1 < text.size() && is_digit(text[i + 1])) {
                // Leading dot floating point (e.g. .5, +.25)
                i += 2;
                while (i < text.size() && is_digit(text[i])) ++i;
            } else {
                return {};
            }
        } else if (i < text.size() && is_digit(text[i])) {
            while (i < text.size() && is_digit(text[i])) ++i;
            // Optional fractional part
            if (i < text.size() && text[i] == '.') {
                if (i + 1 < text.size() && is_digit(text[i + 1])) {
                    i += 2;
                    while (i < text.size() && is_digit(text[i])) ++i;
                }
                // If trailing dot has no digits (e.g. "123."), we stop at 123
            }
        } else {
            return {};
        }

        // Optional scientific exponent: e/E (+/-) digits
        if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
            std::size_t exp_probe = i + 1;
            if (exp_probe < text.size() && (text[exp_probe] == '+' || text[exp_probe] == '-')) {
                ++exp_probe;
            }
            if (exp_probe < text.size() && is_digit(text[exp_probe])) {
                i = exp_probe + 1;
                while (i < text.size() && is_digit(text[i])) ++i;
            }
        }

        return text.substr(begin, i - begin);
    }

    [[nodiscard]] static constexpr std::string_view capture_nodes_triplet(
        std::string_view text,
        std::size_t pos) noexcept {
        if (pos >= text.size()) [[unlikely]] return {};

        std::size_t i = pos;
        // Skip leading whitespace before Node1
        while (i < text.size() && is_ws(text[i])) ++i;
        if (i >= text.size() || !is_digit(text[i])) return {};

        const std::size_t begin = i;

        // Node 1 digits
        while (i < text.size() && is_digit(text[i])) ++i;

        // Skip whitespace before first arrow
        while (i < text.size() && is_ws(text[i])) ++i;
        // Expect '->'
        if (i >= text.size() || text[i] != '-') return {};
        ++i;
        while (i < text.size() && is_ws(text[i])) ++i;
        if (i >= text.size() || text[i] != '>') return {};
        ++i;

        // Skip whitespace before Node 2
        while (i < text.size() && is_ws(text[i])) ++i;
        if (i >= text.size() || !is_digit(text[i])) return {};
        // Node 2 digits
        while (i < text.size() && is_digit(text[i])) ++i;

        // Skip whitespace before second arrow
        while (i < text.size() && is_ws(text[i])) ++i;
        // Expect '->'
        if (i >= text.size() || text[i] != '-') return {};
        ++i;
        while (i < text.size() && is_ws(text[i])) ++i;
        if (i >= text.size() || text[i] != '>') return {};
        ++i;

        // Skip whitespace before Node 3
        while (i < text.size() && is_ws(text[i])) ++i;
        if (i >= text.size() || !is_digit(text[i])) return {};
        // Node 3 digits
        while (i < text.size() && is_digit(text[i])) ++i;

        return text.substr(begin, i - begin);
    }

    [[nodiscard]] static constexpr std::string_view capture_non_ws(
        std::string_view text,
        std::size_t pos) noexcept {
        std::size_t i = pos;
        while (i < text.size() && is_ws(text[i])) {
            ++i;
        }
        const std::size_t begin = i;
        while (i < text.size() && !is_ws(text[i])) {
            ++i;
        }
        if (i == begin) [[unlikely]] {
            return {};
        }
        return text.substr(begin, i - begin);
    }

    [[nodiscard]] static constexpr std::string_view capture_hex(
        std::string_view text,
        std::size_t pos) noexcept {
        std::size_t i = pos;
        while (i < text.size() && is_ws(text[i])) {
            ++i;
        }
        if (i >= text.size()) return {};

        const std::size_t start = i;
        // Check optional 0x or 0X prefix
        if (i + 1 < text.size() && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            i += 2;
        }
        const std::size_t digits_start = i;
        while (i < text.size() && is_hex_char(text[i])) {
            ++i;
        }
        if (i == digits_start) [[unlikely]] {
            return {};
        }
        return text.substr(start, i - start);
    }

    [[nodiscard]] static constexpr std::string_view capture_signed_int(
        std::string_view text,
        std::size_t pos) noexcept {
        std::size_t i = pos;
        while (i < text.size() && is_ws(text[i])) ++i;
        if (i >= text.size()) return {};

        const std::size_t begin = i;
        if (text[i] == '+' || text[i] == '-') {
            ++i;
        }
        const std::size_t digits_start = i;
        while (i < text.size() && is_digit(text[i])) {
            ++i;
        }
        if (i == digits_start) return {};
        return text.substr(begin, i - begin);
    }

    [[nodiscard]] static constexpr std::string_view capture_identifier(
        std::string_view text,
        std::size_t pos) noexcept {
        std::size_t i = pos;
        while (i < text.size() && is_ws(text[i])) ++i;
        if (i >= text.size()) return {};

        const std::size_t begin = i;
        const auto cc = classify(text[i]);
        if (cc != MatcherCharClass::Alpha && text[i] != '$') {
            return {};
        }
        ++i;
        while (i < text.size()) {
            const auto c = classify(text[i]);
            if (c == MatcherCharClass::Alpha || c == MatcherCharClass::Digit || 
                c == MatcherCharClass::HexUpper || c == MatcherCharClass::HexLower ||
                c == MatcherCharClass::Exp || text[i] == '$') {
                ++i;
            } else {
                break;
            }
        }
        return text.substr(begin, i - begin);
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

template <std::size_t Rows, std::size_t Cols>
using BitMatrixLUT = dgtl::utils::automata::BitMatrixLUT<Rows, Cols>;

template <typename T, std::size_t Rows, std::size_t Cols>
using DenseLUT2D = dgtl::utils::automata::DenseLUT2D<T, Rows, Cols>;

template <typename StateEnum, typename EventEnum, std::size_t NumStates, std::size_t NumEvents>
using StateTransitionFSM = dgtl::utils::automata::StateTransitionFSM<StateEnum, EventEnum, NumStates, NumEvents>;

} // namespace dgtl::utils