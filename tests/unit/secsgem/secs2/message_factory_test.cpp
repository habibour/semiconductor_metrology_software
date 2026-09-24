#include "ssim/secsgem/secs2/message_factory.hpp"

#include <gtest/gtest.h>

namespace ssim::secsgem::secs2 {
namespace {

Message make(std::uint8_t stream, std::uint8_t function, std::optional<Item> body = std::nullopt) {
    Message m;
    m.stream = stream;
    m.function = function;
    m.body = std::move(body);
    return m;
}

// A dummy pair of messages is enough to prove the mechanism; the real
// catalogue is registered on Day 5.
MessageFactory make_factory() {
    MessageFactory factory;
    factory.register_message(1, 1, "S1F1", [](const std::optional<Item>& body) {
        return !body.has_value();  // no body allowed
    });
    factory.register_message(1, 2, "S1F2", [](const std::optional<Item>& body) {
        return body.has_value() && body->is_list() && body->count() == 2;
    });
    return factory;
}

TEST(MessageFactory, RegisteredMessageWithLegalBodyIsOk) {
    const MessageFactory factory = make_factory();
    EXPECT_EQ(factory.check(make(1, 1)), Validation::kOk);
    EXPECT_EQ(factory.check(make(1, 2, Item::list({Item::ascii("a"), Item::ascii("b")}))),
              Validation::kOk);
}

TEST(MessageFactory, KnownMessageWithWrongBodyIsIllegalData) {
    const MessageFactory factory = make_factory();
    EXPECT_EQ(factory.check(make(1, 1, Item::ascii("unexpected"))), Validation::kIllegalData);
    EXPECT_EQ(factory.check(make(1, 2)), Validation::kIllegalData);
    EXPECT_EQ(factory.check(make(1, 2, Item::list({Item::ascii("only one")}))),
              Validation::kIllegalData);
}

TEST(MessageFactory, UnknownStreamAndUnknownFunctionAreDistinguished) {
    const MessageFactory factory = make_factory();
    EXPECT_EQ(factory.check(make(7, 1)), Validation::kUnknownStream);
    EXPECT_EQ(factory.check(make(1, 99)), Validation::kUnknownFunction);
    EXPECT_EQ(factory.check(make(0, 0)), Validation::kUnknownStream);
    EXPECT_EQ(factory.check(make(127, 255)), Validation::kUnknownStream);
}

TEST(MessageFactory, NamesAreLookedUpAndUnknownPairsHaveNone) {
    const MessageFactory factory = make_factory();
    EXPECT_EQ(factory.name(1, 2), "S1F2");
    EXPECT_EQ(factory.name(9, 9), "");
}

TEST(MessageFactory, RegisteringAgainReplacesTheEntry) {
    MessageFactory factory = make_factory();
    factory.register_message(1, 1, "S1F1-new", nullptr);  // no check: any body is fine
    EXPECT_EQ(factory.name(1, 1), "S1F1-new");
    EXPECT_EQ(factory.check(make(1, 1, Item::ascii("now allowed"))), Validation::kOk);
}

TEST(MessageFactory, EmptyFactoryKnowsNoStream) {
    const MessageFactory factory;
    EXPECT_EQ(factory.check(make(1, 1)), Validation::kUnknownStream);
}

}  // namespace
}  // namespace ssim::secsgem::secs2
