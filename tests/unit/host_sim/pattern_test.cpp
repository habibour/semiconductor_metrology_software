#include "ssim/host_sim/pattern.hpp"

#include <gtest/gtest.h>

#include "ssim/secsgem/secs2/text_dump.hpp"

namespace ssim::host_sim {
namespace {

using ssim::secsgem::secs2::Item;
using ssim::secsgem::secs2::to_text;

TEST(ParseItem, TheStartCommandFromThePrdExample) {
    auto item = parse_item(R"(L[ A"START"  L[ L[ A"WAFER_ID" A"W042" ] ] ])");
    ASSERT_TRUE(item) << item.error().message;
    EXPECT_EQ(to_text(item.value()),
              R"(<L [2] <A "START"> <L [1] <L [2] <A "WAFER_ID"> <A "W042">>>>)");
}

TEST(ParseItem, NumbersOfEveryTypeIncludingHexAndNegatives) {
    EXPECT_EQ(parse_item("U4[2005]").value(), Item::u4(std::uint32_t{2005}));
    EXPECT_EQ(parse_item("U1[0x0A 255]").value(), Item::u1(std::vector<std::uint8_t>{10, 255}));
    EXPECT_EQ(parse_item("I2[-3, 4]").value(), Item::i2({-3, 4}));
    EXPECT_EQ(parse_item("I1[-128]").value(), Item::i1({-128}));
    EXPECT_EQ(parse_item("I8[-9000000000]").value(), Item::i8({-9000000000LL}));
    EXPECT_EQ(parse_item("F4[1.5]").value(), Item::f4(1.5f));
    EXPECT_EQ(parse_item("F8[-2.25 1e3]").value(), Item::f8({-2.25, 1000.0}));
    EXPECT_EQ(parse_item("B[0 0x0A]").value(), Item::binary({0, 10}));
    EXPECT_EQ(parse_item("BOOLEAN[true 0]").value(), Item::boolean({1, 0}));
    EXPECT_EQ(parse_item("L[]").value(), Item::list({}));
    EXPECT_EQ(parse_item("A[]").value(), Item::ascii(""));
}

TEST(ParseItem, StringsWithEscapes) {
    EXPECT_EQ(parse_item(R"(A"say \"hi\" \\ done")").value(), Item::ascii("say \"hi\" \\ done"));
    EXPECT_EQ(parse_item(R"(A"")").value(), Item::ascii(""));
}

TEST(ParseItem, WhiteSpaceIsFlexible) {
    EXPECT_EQ(parse_item("  L[A\"a\"\n\tA\"b\"]  ").value(),
              Item::list({Item::ascii("a"), Item::ascii("b")}));
}

TEST(ParseItem, BadInputIsAnErrorWithAPosition) {
    for (const char* bad :
         {"", "U4", "U4[", "U4[abc]", "U4[-1]", "U1[256]", "I1[128]", "B[300]", "L[ A\"x\"",
          "A\"unclosed", "Q9[1]", "U4[1] extra", "BOOLEAN[maybe]", "A[text]", "F4[1.5x]", "L[ ]]",
          "A\"bad \\n escape\"", "U4[99999999999]"}) {
        auto r = parse_item(bad);
        EXPECT_FALSE(r) << "accepted: " << bad;
        if (!r) {
            EXPECT_EQ(r.error().code, kErrPatternSyntax) << bad;
        }
    }
}

TEST(ParseItem, WildcardsAreRejectedWhenBuildingAMessage) {
    EXPECT_FALSE(parse_item("*"));
    EXPECT_FALSE(parse_item("A[*]"));
    EXPECT_FALSE(parse_item("L[*]"));
    EXPECT_FALSE(parse_item("L[ U4[*] ]"));
}

TEST(Pattern, PrdExampleForS1F14MatchesTheRealReply) {
    auto pattern = parse_pattern("L[ B[0] L[ A[*] A[*] ] ]");
    ASSERT_TRUE(pattern);
    const Item reply = Item::list(
        {Item::binary({0}), Item::list({Item::ascii("SSIM-128"), Item::ascii("0.1.0")})});
    EXPECT_TRUE(matches(pattern.value(), reply));
    EXPECT_FALSE(
        matches(pattern.value(),
                Item::list({Item::binary({1}), Item::list({Item::ascii("a"), Item::ascii("b")})})));
}

TEST(Pattern, WildcardsAndExactValuesMix) {
    EXPECT_TRUE(matches(parse_pattern("*").value(), Item::u4(std::uint32_t{1})));
    EXPECT_TRUE(matches(parse_pattern("*").value(), Item::list({})));
    EXPECT_TRUE(matches(parse_pattern("U4[*]").value(), Item::u4({1, 2, 3})));
    EXPECT_FALSE(matches(parse_pattern("U4[*]").value(), Item::u2(std::uint16_t{1})));
    EXPECT_TRUE(matches(parse_pattern("L[*]").value(), Item::list({Item::ascii("x")})));
    EXPECT_FALSE(matches(parse_pattern("L[*]").value(), Item::ascii("x")));
    EXPECT_TRUE(matches(parse_pattern("A[*]").value(), Item::ascii("")));
}

TEST(Pattern, ListsMatchByExactLengthAndItemByItem) {
    auto p = parse_pattern("L[ B[4] L[] ]").value();
    EXPECT_TRUE(matches(p, Item::list({Item::binary({4}), Item::list({})})));
    EXPECT_FALSE(matches(p, Item::list({Item::binary({4})})));  // too short
    EXPECT_FALSE(matches(p, Item::list({Item::binary({4}), Item::list({}), Item::list({})})));
    EXPECT_FALSE(matches(p, Item::list({Item::binary({0}), Item::list({})})));
    EXPECT_FALSE(matches(p, Item::binary({4})));
}

TEST(Pattern, ExactValueMustBeTheSameTypeAndValue) {
    EXPECT_TRUE(matches(parse_pattern("U4[7]").value(), Item::u4(std::uint32_t{7})));
    EXPECT_FALSE(matches(parse_pattern("U4[7]").value(), Item::u2(std::uint16_t{7})));
    EXPECT_FALSE(matches(parse_pattern("U4[7]").value(), Item::u4(std::uint32_t{8})));
    EXPECT_TRUE(matches(parse_pattern(R"(A"W042")").value(), Item::ascii("W042")));
    EXPECT_FALSE(matches(parse_pattern(R"(A"W042")").value(), Item::ascii("W043")));
}

}  // namespace
}  // namespace ssim::host_sim
