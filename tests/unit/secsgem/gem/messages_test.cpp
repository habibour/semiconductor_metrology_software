// FR-S2-3 / PRD 8.6.3: the message catalogue. Every message is built, sent
// through the real encoder and decoder, and parsed back; the two catalogues
// classify messages into ok / unknown stream / unknown function / illegal data.

#include "ssim/secsgem/gem/messages.hpp"

#include <gtest/gtest.h>

#include "ssim/secsgem/gem/identifiers.hpp"
#include "ssim/secsgem/secs2/codec.hpp"
#include "ssim/secsgem/secs2/text_dump.hpp"

namespace ssim::secsgem::gem {
namespace {

using secs2::Item;
using secs2::Message;
using secs2::Validation;

// Sends a message through the wire format and back.
Message over_the_wire(const Message& m) {
    Message out = m;
    out.body.reset();
    if (m.body) {
        auto bytes = secs2::encode(*m.body);
        EXPECT_TRUE(bytes);
        auto back = secs2::decode_body(ByteSpan(bytes.value()));
        EXPECT_TRUE(back);
        out.body = std::move(back).value();
    }
    return out;
}

Message with_body(std::uint8_t s, std::uint8_t f, Item body) {
    Message m;
    m.stream = s;
    m.function = f;
    m.body = std::move(body);
    return m;
}

const Identity kId{"SSIM-128", "0.1.0"};

TEST(GemMessages, S1F1HasNoBodyAndS1F2CarriesModelAndRevision) {
    EXPECT_FALSE(make_s1f1().body.has_value());
    EXPECT_TRUE(make_s1f1().w_bit);
    const Message m = over_the_wire(make_s1f2(kId));
    EXPECT_EQ(secs2::to_text(*m.body), "<L [2] <A \"SSIM-128\"> <A \"0.1.0\">>");
    EXPECT_FALSE(m.w_bit);
}

TEST(GemMessages, S1F13AndS1F14) {
    EXPECT_EQ(secs2::to_text(*make_s1f13().body), "<L [0]>");
    const Message m = over_the_wire(make_s1f14(kCommackAccepted, kId));
    EXPECT_EQ(secs2::to_text(*m.body), "<L [2] <B 0x00> <L [2] <A \"SSIM-128\"> <A \"0.1.0\">>>");
}

TEST(GemMessages, S1F3RoundTripsAndAnEmptyListMeansAll) {
    auto svids = parse_s1f3(over_the_wire(make_s1f3({4001, 4003, 4012})));
    ASSERT_TRUE(svids);
    EXPECT_EQ(svids.value(), (std::vector<std::uint32_t>{4001, 4003, 4012}));

    auto all = parse_s1f3(over_the_wire(make_s1f3({})));
    ASSERT_TRUE(all);
    EXPECT_TRUE(all.value().empty());
}

TEST(GemMessages, S1F3AcceptsAnyNonNegativeIntegerWidthButRejectsTheRest) {
    Message m = with_body(
        1, 3,
        Item::list({Item::u1(std::uint8_t{7}), Item::u2(std::uint16_t{4001}), Item::i4({4002})}));
    auto ok = parse_s1f3(m);
    ASSERT_TRUE(ok);
    EXPECT_EQ(ok.value(), (std::vector<std::uint32_t>{7, 4001, 4002}));

    EXPECT_FALSE(parse_s1f3(with_body(1, 3, Item::list({Item::i4({-1})}))));
    EXPECT_FALSE(parse_s1f3(with_body(1, 3, Item::list({Item::ascii("4001")}))));
    EXPECT_FALSE(parse_s1f3(with_body(1, 3, Item::ascii("x"))));
    EXPECT_FALSE(parse_s1f3(with_body(1, 3, Item::list({Item::u8({0x1FFFFFFFFull})}))));
}

TEST(GemMessages, S2F41RoundTrips) {
    RemoteCommand c;
    c.rcmd = "START";
    c.params.push_back({"WAFER_ID", Item::ascii("W042")});
    auto parsed = parse_s2f41(over_the_wire(make_s2f41(c)));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value().rcmd, "START");
    ASSERT_EQ(parsed.value().params.size(), 1u);
    EXPECT_EQ(parsed.value().params[0].name, "WAFER_ID");
    EXPECT_EQ(parsed.value().params[0].value, Item::ascii("W042"));

    RemoteCommand none;
    none.rcmd = "STOP";
    EXPECT_TRUE(parse_s2f41(over_the_wire(make_s2f41(none))));
}

TEST(GemMessages, S2F41WithABadShapeIsRejected) {
    EXPECT_FALSE(parse_s2f41(with_body(2, 41, Item::ascii("START"))));
    EXPECT_FALSE(parse_s2f41(with_body(2, 41, Item::list({Item::ascii("START")}))));
    EXPECT_FALSE(
        parse_s2f41(with_body(2, 41, Item::list({Item::u1(std::uint8_t{1}), Item::list({})}))));
    EXPECT_FALSE(parse_s2f41(with_body(
        2, 41, Item::list({Item::ascii("START"), Item::list({Item::ascii("not a pair")})}))));
    EXPECT_FALSE(parse_s2f41(with_body(
        2, 41,
        Item::list({Item::ascii("START"),
                    Item::list({Item::list({Item::u1(std::uint8_t{1}), Item::ascii("v")})})}))));
    Message no_body;
    EXPECT_FALSE(parse_s2f41(no_body));
}

TEST(GemMessages, S2F42RoundTrips) {
    auto ack = parse_s2f42(over_the_wire(make_s2f42(
        kHcackInvalidParameter, {{"WAFER_ID", kCpackIllegalValue}, {"X", kCpackUnknownName}})));
    ASSERT_TRUE(ack);
    EXPECT_EQ(ack.value().hcack, kHcackInvalidParameter);
    ASSERT_EQ(ack.value().params.size(), 2u);
    EXPECT_EQ(ack.value().params[1].name, "X");
    EXPECT_EQ(ack.value().params[1].cpack, kCpackUnknownName);
}

TEST(GemMessages, S6F11RoundTripsWithReportValues) {
    EventReport e;
    e.dataid = 9;
    e.ceid = kCeidWaferScanComplete;
    e.reports.push_back(
        {kRptidWaferResult,
         {Item::ascii("W001"), Item::u1(std::uint8_t{1}), Item::f4(-179.4f), Item::f4(0.6f),
          Item::f4(-0.008f), Item::f4(0.5f), Item::boolean(false)}});
    auto parsed = parse_s6f11(over_the_wire(make_s6f11(e)));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed.value().dataid, 9u);
    EXPECT_EQ(parsed.value().ceid, kCeidWaferScanComplete);
    ASSERT_EQ(parsed.value().reports.size(), 1u);
    EXPECT_EQ(parsed.value().reports[0].rptid, kRptidWaferResult);
    ASSERT_EQ(parsed.value().reports[0].values.size(), 7u);
    EXPECT_EQ(parsed.value().reports[0].values[0], Item::ascii("W001"));
    EXPECT_FLOAT_EQ(parsed.value().reports[0].values[2].as_array<float>()[0], -179.4f);
}

TEST(GemMessages, S6F11WithNoReportsIsLegal) {
    EventReport e;
    e.dataid = 1;
    e.ceid = 2001;
    EXPECT_TRUE(parse_s6f11(over_the_wire(make_s6f11(e))));
}

TEST(GemMessages, S5F1RoundTrips) {
    auto a = parse_s5f1(over_the_wire(make_s5f1(
        {static_cast<std::uint8_t>(kAlcdSetBit | kAlarmCategory), 1001, "spike rate high"})));
    ASSERT_TRUE(a);
    EXPECT_EQ(a.value().alid, 1001u);
    EXPECT_EQ(a.value().altx, "spike rate high");
    EXPECT_NE(a.value().alcd & kAlcdSetBit, 0);
}

TEST(GemMessages, AcknowledgesAreOneBinaryByte) {
    EXPECT_EQ(parse_ack(over_the_wire(make_s6f12(0))).value(), 0);
    EXPECT_EQ(parse_ack(over_the_wire(make_s1f18(kOnlackAlreadyOnline))).value(), 2);
    EXPECT_FALSE(parse_ack(with_body(6, 12, Item::binary({0, 1}))));
    EXPECT_FALSE(parse_ack(with_body(6, 12, Item::u1(std::uint8_t{0}))));
}

TEST(GemMessages, S9CarriesTheTenHeaderBytesAndNeverAsksForAReply) {
    const std::vector<std::uint8_t> header = {0, 7, 0x81, 13, 0, 0, 0, 0, 0, 42};
    const Message m = make_s9(5, header);
    EXPECT_EQ(m.stream, 9);
    EXPECT_EQ(m.function, 5);
    EXPECT_FALSE(m.w_bit);
    EXPECT_EQ(over_the_wire(m).body->as_array<std::uint8_t>(), header);
}

// ---- catalogues -------------------------------------------------------------

TEST(EquipmentCatalogue, AcceptsWhatAHostMayLegallySend) {
    secs2::MessageFactory f;
    register_equipment_catalogue(f);
    EXPECT_EQ(f.check(make_s1f1()), Validation::kOk);
    EXPECT_EQ(f.check(make_s1f13()), Validation::kOk);
    Message no_body_s1f13;
    no_body_s1f13.stream = 1;
    no_body_s1f13.function = 13;
    EXPECT_EQ(f.check(no_body_s1f13), Validation::kOk);  // some hosts send no body
    EXPECT_EQ(f.check(with_body(1, 13, Item::list({Item::ascii("M"), Item::ascii("R")}))),
              Validation::kOk);
    EXPECT_EQ(f.check(make_s1f3({})), Validation::kOk);
    EXPECT_EQ(f.check(make_s1f15()), Validation::kOk);
    EXPECT_EQ(f.check(make_s1f17()), Validation::kOk);
    RemoteCommand c;
    c.rcmd = "STOP";
    EXPECT_EQ(f.check(make_s2f41(c)), Validation::kOk);
    EXPECT_EQ(f.check(make_s5f2(0)), Validation::kOk);
    EXPECT_EQ(f.check(make_s6f12(0)), Validation::kOk);
}

TEST(EquipmentCatalogue, ClassifiesTheThreeGemErrorSituations) {
    secs2::MessageFactory f;
    register_equipment_catalogue(f);
    Message unknown_stream;
    unknown_stream.stream = 3;
    unknown_stream.function = 1;
    EXPECT_EQ(f.check(unknown_stream), Validation::kUnknownStream);  // S9F3

    Message unknown_function;
    unknown_function.stream = 1;
    unknown_function.function = 99;
    EXPECT_EQ(f.check(unknown_function), Validation::kUnknownFunction);  // S9F5

    // Equipment constants (S2F13) are FR-GEM-7: known stream, not implemented.
    Message not_implemented;
    not_implemented.stream = 2;
    not_implemented.function = 13;
    EXPECT_EQ(f.check(not_implemented), Validation::kUnknownFunction);

    EXPECT_EQ(f.check(with_body(1, 1, Item::ascii("S1F1 takes no body"))),
              Validation::kIllegalData);  // S9F7
    EXPECT_EQ(f.check(with_body(2, 41, Item::list({}))), Validation::kIllegalData);
    EXPECT_EQ(f.check(with_body(1, 3, Item::ascii("x"))), Validation::kIllegalData);
}

TEST(HostCatalogue, AcceptsWhatTheMachineSends) {
    secs2::MessageFactory f;
    register_host_catalogue(f);
    EXPECT_EQ(f.check(make_s1f2(kId)), Validation::kOk);
    EXPECT_EQ(f.check(make_s1f4({Item::u1(std::uint8_t{1})})), Validation::kOk);
    EXPECT_EQ(f.check(make_s1f14(0, kId)), Validation::kOk);
    EXPECT_EQ(f.check(make_s1f16(0)), Validation::kOk);
    EXPECT_EQ(f.check(make_s1f18(0)), Validation::kOk);
    EXPECT_EQ(f.check(make_s2f42(4, {})), Validation::kOk);
    EXPECT_EQ(f.check(make_s5f1({0x85, 1001, "x"})), Validation::kOk);
    EventReport e;
    e.ceid = 2001;
    EXPECT_EQ(f.check(make_s6f11(e)), Validation::kOk);
    for (std::uint8_t fn : {1, 3, 5, 7, 9, 11}) {
        EXPECT_EQ(f.check(make_s9(fn, std::vector<std::uint8_t>(10, 0))), Validation::kOk)
            << int(fn);
    }
    EXPECT_EQ(f.check(make_s9(5, std::vector<std::uint8_t>(9, 0))), Validation::kIllegalData);
}

TEST(Identifiers, TablesAreConsistent) {
    EXPECT_EQ(status_variables().size(), 12u);
    EXPECT_EQ(status_variables().front().svid, 4001u);
    EXPECT_EQ(status_variables().back().svid, 4012u);
    EXPECT_EQ(report_fields(kRptidWaferResult).size(), 7u);
    EXPECT_EQ(report_fields(kRptidStates).size(), 2u);
    EXPECT_EQ(report_fields(kRptidScanStarted).size(), 3u);
    EXPECT_TRUE(report_fields(1).empty());
    EXPECT_EQ(ceid_name(kCeidRunAborted), "RunAborted");
    EXPECT_EQ(ceid_name(1), "");
    EXPECT_EQ(report_for_event(kCeidWaferOutOfSpec), kRptidWaferResult);
    EXPECT_EQ(report_for_event(kCeidWaferScanStarted), kRptidScanStarted);
    EXPECT_EQ(report_for_event(9999), 0u);
}

}  // namespace
}  // namespace ssim::secsgem::gem
