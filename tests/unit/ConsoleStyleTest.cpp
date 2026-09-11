#include <gtest/gtest.h>

#include <array>
#include <string>

#include "shared/Console.h"
#include "shared/ConsoleStyle.h"

// Spec 005: ConsoleStyle is pure string formatting with no OS/console dependency (Architecture
// Context), so these tests never touch a real terminal, tty, or environment variable -- per the
// project's cpp-testing skill guidance to isolate unit tests from real OS state. ConsoleIO's tty
// detection / NO_COLOR reading / VT-mode enabling are exercised separately (they need a real
// console/env and are out of scope for this pure-function suite).

using buddyshare::shared::ConsoleStyle;
using buddyshare::shared::MessageStyle;

namespace {

constexpr std::array<MessageStyle, 6> kAllStyles = {
    MessageStyle::plain,   MessageStyle::header, MessageStyle::success,
    MessageStyle::warning, MessageStyle::error,  MessageStyle::info,
};

// --- should_colorize (Acceptance Criterion 1) -------------------------------------------------

TEST(ConsoleStyleTest, ShouldColorize_TtyAndNoColorUnset_ReturnsTrue) {
    EXPECT_TRUE(ConsoleStyle::should_colorize(/*is_tty=*/true, /*no_color_env_set=*/false));
}

TEST(ConsoleStyleTest, ShouldColorize_NotATty_ReturnsFalseRegardlessOfNoColor) {
    EXPECT_FALSE(ConsoleStyle::should_colorize(/*is_tty=*/false, /*no_color_env_set=*/false));
    EXPECT_FALSE(ConsoleStyle::should_colorize(/*is_tty=*/false, /*no_color_env_set=*/true));
}

TEST(ConsoleStyleTest, ShouldColorize_TtyButNoColorSet_ReturnsFalse) {
    EXPECT_FALSE(ConsoleStyle::should_colorize(/*is_tty=*/true, /*no_color_env_set=*/true));
}

// --- apply_style (Acceptance Criteria 2 & 3) --------------------------------------------------

TEST(ConsoleStyleTest, ApplyStyle_ColorizeFalse_ReturnsTextUnchangedForEveryStyle) {
    const std::string text = "Pushed to https://github.com/alice/myrepo.git";
    for (const MessageStyle style : kAllStyles) {
        EXPECT_EQ(ConsoleStyle::apply_style(text, style, /*colorize=*/false), text)
            << "style " << static_cast<int>(style) << " must not alter text when colorize=false";
    }
}

// Edge case: empty text (e.g. a blank line print) must round-trip to empty, not crash or grow.
TEST(ConsoleStyleTest, ApplyStyle_ColorizeFalse_EmptyText_ReturnsEmpty) {
    for (const MessageStyle style : kAllStyles) {
        EXPECT_EQ(ConsoleStyle::apply_style("", style, /*colorize=*/false), "");
    }
}

TEST(ConsoleStyleTest, ApplyStyle_ColorizeTrue_StillContainsOriginalTextForEveryStyle) {
    const std::string text = "Encrypted chapter 1 for 2 reader(s).";
    for (const MessageStyle style : kAllStyles) {
        const std::string result = ConsoleStyle::apply_style(text, style, /*colorize=*/true);
        EXPECT_NE(result.find(text), std::string::npos)
            << "style " << static_cast<int>(style) << " must wrap, not replace, the text";
    }
}

// Edge case: colorizing must actually add bytes (ANSI wrapping), not just return the text
// verbatim -- otherwise apply_style(colorize=true) would be indistinguishable from
// apply_style(colorize=false) and no visual distinction would ever reach the terminal.
TEST(ConsoleStyleTest, ApplyStyle_ColorizeTrue_AddsWrappingBytesRatherThanEchoingTextVerbatim) {
    const std::string text = "some status message";
    for (const MessageStyle style : kAllStyles) {
        const std::string result = ConsoleStyle::apply_style(text, style, /*colorize=*/true);
        EXPECT_GT(result.size(), text.size())
            << "style " << static_cast<int>(style)
            << " must add ANSI wrapping bytes when colorize=true";
    }
}

// Edge case: an empty message with colorize=true must not crash even though there's no visible
// text to wrap.
TEST(ConsoleStyleTest, ApplyStyle_ColorizeTrue_EmptyText_DoesNotCrash) {
    for (const MessageStyle style : kAllStyles) {
        EXPECT_NO_THROW(ConsoleStyle::apply_style("", style, /*colorize=*/true));
    }
}

// Edge case (color-blind accessibility guidance in the spec's Edge Cases section): styles must
// still render visually distinctly from one another for the same text, otherwise the color
// accent carries no information at all.
TEST(ConsoleStyleTest, ApplyStyle_ColorizeTrue_SuccessAndErrorRenderDifferently) {
    const std::string text = "status";
    const std::string success_rendered =
        ConsoleStyle::apply_style(text, MessageStyle::success, /*colorize=*/true);
    const std::string error_rendered =
        ConsoleStyle::apply_style(text, MessageStyle::error, /*colorize=*/true);
    EXPECT_NE(success_rendered, error_rendered);
}

// --- make_banner --------------------------------------------------------------------------

TEST(ConsoleStyleTest, MakeBanner_ContainsTheGivenTitleVerbatim) {
    const std::string banner = ConsoleStyle::make_banner("Writer Menu");
    EXPECT_NE(banner.find("Writer Menu"), std::string::npos);
}

TEST(ConsoleStyleTest, MakeBanner_DifferentTitles_ProduceDifferentBanners) {
    EXPECT_NE(ConsoleStyle::make_banner("Writer Menu"), ConsoleStyle::make_banner("Reader Menu"));
}

// make_banner returns plain text; ANSI styling is layered on separately via apply_style(...,
// MessageStyle::header) when printed (spec Behavior item 2) -- the banner text itself must
// never embed escape bytes.
TEST(ConsoleStyleTest, MakeBanner_ReturnsPlainText_ContainsNoAnsiEscapeByte) {
    const std::string banner = ConsoleStyle::make_banner("Writer Menu");
    EXPECT_EQ(banner.find('\x1B'), std::string::npos)
        << "make_banner must return plain text; styling is applied separately via apply_style";
}

// Edge case: an empty title must not crash.
TEST(ConsoleStyleTest, MakeBanner_EmptyTitle_DoesNotCrash) {
    EXPECT_NO_THROW(ConsoleStyle::make_banner(""));
}

}  // namespace
