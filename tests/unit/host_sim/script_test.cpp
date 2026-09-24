#include "ssim/host_sim/script.hpp"

#include <gtest/gtest.h>

#include "ssim/secsgem/secs2/text_dump.hpp"

namespace ssim::host_sim {
namespace {

using std::chrono::milliseconds;

std::vector<Command> parse_ok(const char* text, std::map<std::string, std::string> vars = {}) {
    auto r = parse_script(text, vars);
    EXPECT_TRUE(r) << (r ? "" : r.error().message);
    return r ? r.value() : std::vector<Command>{};
}

std::string error_of(const char* text) {
    auto r = parse_script(text);
    EXPECT_FALSE(r) << "accepted: " << text;
    return r ? std::string() : r.error().message;
}

TEST(ScriptParse, ThePrdExampleScript) {
    const auto cmds = parse_ok(R"(
# scenarios/normal_run.scn
connect 127.0.0.1:5000
select
send   S1F13  L[]
expect S1F14  L[ B[0]  L[ A[*]  A[*] ] ]
send   S2F41  L[ A"START"  L[ L[ A"WAFER_ID"  A"W042" ] ] ]
expect S2F42  L[ B[4]  L[] ]
wait-event 2004  timeout=5s
wait-event 2005  timeout=30s
assert event.STRESS_MPA  within  -180  +-4
disconnect
)");
    ASSERT_EQ(cmds.size(), 10u);
    EXPECT_EQ(cmds[0].kind, CommandKind::kConnect);
    EXPECT_EQ(cmds[0].host, "127.0.0.1");
    EXPECT_EQ(cmds[0].port, 5000);
    EXPECT_EQ(cmds[2].kind, CommandKind::kSend);
    EXPECT_EQ(cmds[2].stream, 1);
    EXPECT_EQ(cmds[2].function, 13);
    EXPECT_FALSE(cmds[2].w_bit.has_value());  // the runner defaults it from the function number
    EXPECT_EQ(ssim::secsgem::secs2::to_text(*cmds[2].body), "<L [0]>");
    EXPECT_EQ(cmds[3].kind, CommandKind::kExpect);
    ASSERT_TRUE(cmds[3].pattern.has_value());
    EXPECT_EQ(cmds[6].kind, CommandKind::kWaitEvent);
    EXPECT_EQ(cmds[6].id, 2004u);
    EXPECT_EQ(cmds[6].timeout, milliseconds(5000));
    EXPECT_EQ(cmds[7].timeout, milliseconds(30000));
    EXPECT_EQ(cmds[8].kind, CommandKind::kAssert);
    EXPECT_EQ(cmds[8].field, "event.STRESS_MPA");
    EXPECT_EQ(cmds[8].op, AssertOp::kWithin);
    EXPECT_DOUBLE_EQ(cmds[8].number, -180.0);
    EXPECT_DOUBLE_EQ(cmds[8].tolerance, 4.0);
    EXPECT_EQ(cmds[9].kind, CommandKind::kDisconnect);
}

TEST(ScriptParse, CommentsBlankLinesAndAHashInsideAStringAreHandled) {
    const auto cmds = parse_ok(
        "\n   # only a comment\nsend S1F1   # trailing comment\nsend S2F41 L[ A\"a#b\" L[] ]\n");
    ASSERT_EQ(cmds.size(), 2u);
    EXPECT_EQ(ssim::secsgem::secs2::to_text(*cmds[1].body), "<L [2] <A \"a#b\"> <L [0]>>");
    EXPECT_EQ(cmds[0].line, 3);
    EXPECT_EQ(cmds[1].line, 4);
}

TEST(ScriptParse, VariablesAreSubstituted) {
    const auto cmds = parse_ok(
        "connect 127.0.0.1:$PORT\nsend S2F41 L[ A\"START\" L[ L[ A\"WAFER_ID\" A\"$WAFER\" ] ] ]\n",
        {{"PORT", "6001"}, {"WAFER", "W7"}});
    EXPECT_EQ(cmds[0].port, 6001);
    EXPECT_NE(ssim::secsgem::secs2::to_text(*cmds[1].body).find("\"W7\""), std::string::npos);
}

TEST(ScriptParse, SendOptions) {
    const auto cmds =
        parse_ok("send S1F1 nowait\nsend S1F1 W device=99\nsend S1F1 device=0x10 L[]\n");
    EXPECT_EQ(cmds[0].w_bit, std::optional<bool>(false));
    EXPECT_EQ(cmds[1].w_bit, std::optional<bool>(true));
    EXPECT_EQ(cmds[1].device, std::optional<int>(99));
    EXPECT_EQ(cmds[2].device, std::optional<int>(16));
    EXPECT_TRUE(cmds[2].body.has_value());
}

TEST(ScriptParse, ExpectWithAndWithoutPatternAndTimeout) {
    const auto cmds = parse_ok(
        "expect S9F1\nexpect S1F2 timeout=250ms L[*]\nexpect-no S6F11\nexpect-no S6F11 "
        "timeout=2s\n");
    EXPECT_FALSE(cmds[0].pattern.has_value());
    EXPECT_EQ(cmds[1].timeout, milliseconds(250));
    EXPECT_TRUE(cmds[1].pattern.has_value());
    EXPECT_EQ(cmds[2].kind, CommandKind::kExpectNo);
    EXPECT_EQ(cmds[2].timeout, milliseconds(1000));  // default for expect-no
    EXPECT_EQ(cmds[3].timeout, milliseconds(2000));
}

TEST(ScriptParse, ControlCommands) {
    const auto cmds = parse_ok(
        "select\nselect status=1\ndeselect\nlinktest\nseparate\nauto-ack off\nauto-ack on\nsleep "
        "1500ms\nsleep 2\nexpect-closed timeout=3s\n");
    EXPECT_EQ(cmds[1].status, std::optional<int>(1));
    EXPECT_FALSE(cmds[0].status.has_value());
    EXPECT_FALSE(cmds[5].flag);
    EXPECT_TRUE(cmds[6].flag);
    EXPECT_EQ(cmds[7].timeout, milliseconds(1500));
    EXPECT_EQ(cmds[8].timeout, milliseconds(2000));
    EXPECT_EQ(cmds[9].timeout, milliseconds(3000));
}

TEST(ScriptParse, WaitAlarmExpectStatusAndRaw) {
    const auto cmds = parse_ok(
        "wait-alarm 1001 set timeout=10s\nwait-alarm 1001 clear\nexpect-status 4012 timeout=20s "
        "U4[1]\nraw 00 00 00 05 67 0x61 7F\n");
    EXPECT_EQ(cmds[0].id, 1001u);
    EXPECT_TRUE(cmds[0].alarm_set);
    EXPECT_FALSE(cmds[1].alarm_set);
    EXPECT_EQ(cmds[2].id, 4012u);
    EXPECT_EQ(cmds[2].timeout, milliseconds(20000));
    EXPECT_EQ(cmds[3].raw, (std::vector<std::uint8_t>{0, 0, 0, 5, 0x67, 0x61, 0x7F}));
}

TEST(ScriptParse, AssertForms) {
    const auto cmds = parse_ok(
        "assert event.WAFER_ID == \"W042\"\nassert event.SLOT == 1\nassert alarm.ALID != "
        "1002\nassert event.STRESS_MPA within -180 +- 4\n");
    EXPECT_EQ(cmds[0].value, "W042");
    EXPECT_FALSE(cmds[0].value_is_number);
    EXPECT_TRUE(cmds[1].value_is_number);
    EXPECT_DOUBLE_EQ(cmds[1].number, 1.0);
    EXPECT_EQ(cmds[2].op, AssertOp::kNotEqual);
    EXPECT_DOUBLE_EQ(cmds[3].tolerance, 4.0);
}

TEST(ScriptParse, ErrorsNameTheLine) {
    EXPECT_NE(error_of("select\nfrobnicate\n").find("line 2"), std::string::npos);
    EXPECT_NE(error_of("connect nowhere\n").find("HOST:PORT"), std::string::npos);
    EXPECT_NE(error_of("connect h:99999\n").find("line 1"), std::string::npos);
    EXPECT_NE(error_of("send S1F1 U4[abc]\n").find("line 1"), std::string::npos);
    EXPECT_NE(error_of("send X1F1\n").find("S1F13"), std::string::npos);
    EXPECT_NE(error_of("send S200F1\n").find("line 1"), std::string::npos);
    EXPECT_NE(error_of("send S1F1 *\n").find("wildcards"), std::string::npos);
    EXPECT_NE(error_of("expect S1F1 U4[\n").find("line 1"), std::string::npos);
    EXPECT_NE(error_of("wait-event abc\n").find("CEID"), std::string::npos);
    EXPECT_NE(error_of("wait-alarm 1001 maybe\n").find("set|clear"), std::string::npos);
    EXPECT_NE(error_of("assert stress within 1 +-1\n").find("event.FIELD"), std::string::npos);
    EXPECT_NE(error_of("assert event.X within 1\n").find("+-"), std::string::npos);
    EXPECT_NE(error_of("assert event.X <= 3\n").find("within"), std::string::npos);
    EXPECT_NE(error_of("auto-ack maybe\n").find("on or off"), std::string::npos);
    EXPECT_NE(error_of("sleep soon\n").find("duration"), std::string::npos);
    EXPECT_NE(error_of("raw zz\n").find("hex"), std::string::npos);
    EXPECT_NE(error_of("raw\n").find("at least one"), std::string::npos);
    EXPECT_NE(error_of("select now\n").find("unexpected"), std::string::npos);
    EXPECT_NE(error_of("send S1F1 timeout=fast\n").find("duration"), std::string::npos);
}

TEST(ScriptParse, AnUnknownVariableIsAnError) {
    auto r = parse_script("connect 127.0.0.1:$PORT\n");
    ASSERT_FALSE(r);
    EXPECT_NE(r.error().message.find("$PORT"), std::string::npos);
}

TEST(ParseDuration, UnitsAndLimits) {
    EXPECT_EQ(parse_duration("500ms").value(), milliseconds(500));
    EXPECT_EQ(parse_duration("2s").value(), milliseconds(2000));
    EXPECT_EQ(parse_duration("1.5s").value(), milliseconds(1500));
    EXPECT_EQ(parse_duration("3").value(), milliseconds(3000));
    EXPECT_FALSE(parse_duration(""));
    EXPECT_FALSE(parse_duration("-1s"));
    EXPECT_FALSE(parse_duration("abc"));
    EXPECT_FALSE(parse_duration("99999999s"));
}

}  // namespace
}  // namespace ssim::host_sim
