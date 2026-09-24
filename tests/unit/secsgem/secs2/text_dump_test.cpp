#include "ssim/secsgem/secs2/text_dump.hpp"

#include <gtest/gtest.h>

#include <string>

namespace ssim::secsgem::secs2 {
namespace {

TEST(TextDump, ScalarsAndStrings) {
    EXPECT_EQ(to_text(Item::ascii("W001")), "<A \"W001\">");
    EXPECT_EQ(to_text(Item::u4(std::uint32_t{2005})), "<U4 2005>");
    EXPECT_EQ(to_text(Item::i2({-2, 3})), "<I2 -2 3>");
    EXPECT_EQ(to_text(Item::boolean({1, 0})), "<BOOLEAN true false>");
    EXPECT_EQ(to_text(Item::binary({0x0A, 0xFF})), "<B 0x0A 0xFF>");
    EXPECT_EQ(to_text(Item::f4(1.5f)), "<F4 1.5>");
}

TEST(TextDump, ListsShowTheirCount) {
    EXPECT_EQ(to_text(Item::list({})), "<L [0]>");
    EXPECT_EQ(to_text(Item::list({Item::ascii("a"), Item::list({Item::u1(std::uint8_t{7})})})),
              "<L [2] <A \"a\"> <L [1] <U1 7>>>");
}

TEST(TextDump, StringsAreEscapedSoALogLineStaysOneLine) {
    EXPECT_EQ(to_text(Item::ascii("a\"b\\c\n")), "<A \"a\\\"b\\\\c\\x0A\">");
}

TEST(TextDump, LongInputIsCutWithANote) {
    EXPECT_EQ(to_text(Item::ascii(std::string(100, 'x'))),
              "<A \"" + std::string(kDumpMaxChars, 'x') + "...\">");

    std::vector<std::uint32_t> many(50, 1);
    const std::string text = to_text(Item::u4(many));
    EXPECT_NE(text.find("... (50 total)"), std::string::npos);
    EXPECT_LT(text.size(), 200u);

    Item::List entries(40, Item::u1(std::uint8_t{1}));
    EXPECT_NE(to_text(Item::list(entries)).find("... (40 entries)"), std::string::npos);
}

TEST(TextDump, MessageShowsStreamFunctionAndWBit) {
    Message s1f1;
    s1f1.stream = 1;
    s1f1.function = 1;
    s1f1.w_bit = true;
    EXPECT_EQ(to_text(s1f1), "S1F1 W");

    Message s6f11;
    s6f11.stream = 6;
    s6f11.function = 11;
    s6f11.w_bit = false;
    s6f11.body = Item::list({Item::u4(std::uint32_t{1})});
    EXPECT_EQ(to_text(s6f11), "S6F11 <L [1] <U4 1>>");
}

}  // namespace
}  // namespace ssim::secsgem::secs2
