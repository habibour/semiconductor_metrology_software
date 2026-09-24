// IT-GEM-1 and IT-CTRL-2 at the GEM layer (PRD 10.2): communication, status
// variables, control state, remote commands, events, alarms and the S9 errors.
// The controller, event bus and alarm manager are real; only the sockets are
// faked, so every message goes through the real encoder and every command
// through the real state machine.

#include "ssim/secsgem/gem/gem_service.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>

#include "ssim/core/alarms.hpp"
#include "ssim/core/clock.hpp"
#include "ssim/core/controller.hpp"
#include "ssim/core/events.hpp"
#include "ssim/core/machine_api.hpp"
#include "ssim/secsgem/gem/identifiers.hpp"
#include "ssim/secsgem/hsms/data_message.hpp"
#include "ssim/secsgem/secs2/codec.hpp"
#include "ssim/secsgem/secs2/text_dump.hpp"

namespace ssim::secsgem::gem {
namespace {

using secs2::Item;
using secs2::Message;

constexpr std::uint16_t kDevice = 7;

class FakeScanDriver : public ssim::core::IScanDriver {
public:
    void start(std::string wafer_id) override {
        started_wafer_id = std::move(wafer_id);
        ++start_count;
    }
    void request_abort() override { abort_requested = true; }
    void join() override {}

    std::string started_wafer_id;
    std::atomic<int> start_count{0};
    std::atomic<bool> abort_requested{false};
};

// Records what the service sends. post_task only queues: the bus subscribers
// call it on the controller thread, and the service's state belongs to the
// (test) I/O thread, so tasks run when the test calls run_tasks().
class FakeSender final : public hsms::IMessageSender {
public:
    void post_request(Message m, std::function<void(std::uint32_t)> on_sent) override {
        std::lock_guard lock(mutex_);
        requests.push_back(m);
        const std::uint32_t sys = next_system_++;
        system_bytes.push_back(sys);
        if (on_sent) on_sent(sys);
    }
    void post_reply(std::uint32_t sys, Message m) override {
        std::lock_guard lock(mutex_);
        replies.emplace_back(sys, std::move(m));
    }
    void post_task(std::function<void()> task) override {
        std::lock_guard lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    void run_tasks() {
        for (;;) {
            std::function<void()> t;
            {
                std::lock_guard lock(mutex_);
                if (tasks_.empty()) return;
                t = std::move(tasks_.front());
                tasks_.pop_front();
            }
            t();
        }
    }

    std::vector<Message> requests;
    std::vector<std::uint32_t> system_bytes;
    std::vector<std::pair<std::uint32_t, Message>> replies;

private:
    std::mutex mutex_;
    std::deque<std::function<void()>> tasks_;
    std::uint32_t next_system_ = 1000;
};

struct Fixture {
    ssim::core::EventBus bus;
    FakeScanDriver driver;
    ssim::core::AlarmManager alarms{bus};
    ssim::core::Controller controller{bus, driver, alarms};
    ssim::core::MachineApi api{controller, bus};
    ssim::core::FakeClock clock;
    FakeSender sender;
    std::unique_ptr<GemService> gem;

    explicit Fixture(GemConfig config = default_config()) {
        controller.start();
        gem = std::make_unique<GemService>(config, api, bus, alarms, clock, sender);
    }
    ~Fixture() {
        controller.stop();
        gem.reset();
    }

    static GemConfig default_config() {
        GemConfig c;
        c.identity = {"SSIM-128", "0.1.0"};
        c.device_id = kDevice;
        return c;
    }

    // Delivers a host message to the service, as the HSMS layer would.
    void host_sends(const Message& m, std::uint32_t system = 500, std::uint16_t device = kDevice) {
        std::vector<std::uint8_t> body;
        if (m.body) body = secs2::encode(*m.body).value();
        hsms::Delivery d;
        d.frame = hsms::make_data_frame(device, m.stream, m.function, m.w_bit, system, body);
        gem->on_data(d);
    }
    void host_sends_raw_body(std::uint8_t s, std::uint8_t f, std::vector<std::uint8_t> body) {
        hsms::Delivery d;
        d.frame = hsms::make_data_frame(kDevice, s, f, true, 501, std::move(body));
        gem->on_data(d);
    }

    void establish_communication() {
        host_sends(make_s1f13());
        sender.replies.clear();
    }
    void operator_goes_remote() {
        ASSERT_TRUE(api.set_control_mode(ssim::core::ControlMode::kOnlineRemote,
                                         ssim::core::CommandSource::kUi));
        sender.run_tasks();
    }

    const Message& last_reply() const { return sender.replies.back().second; }
    std::string reply_text() const { return secs2::to_text(*last_reply().body); }
};

Message w(Message m) {
    m.w_bit = true;
    return m;
}

Message command(const std::string& rcmd, std::vector<CommandParam> params = {}) {
    RemoteCommand c;
    c.rcmd = rcmd;
    c.params = std::move(params);
    return make_s2f41(c);
}

std::uint8_t hcack_of(const Fixture& f) { return parse_s2f42(f.last_reply()).value().hcack; }

// ---- communication ------------------------------------------------------------

TEST(GemCommunication, S1F1IsAnsweredEvenBeforeCommunicationIsEstablished) {
    Fixture f;
    EXPECT_EQ(f.gem->comm_state(), CommState::kNotCommunicating);
    f.host_sends(make_s1f1());
    ASSERT_EQ(f.sender.replies.size(), 1u);
    EXPECT_EQ(f.sender.replies[0].first, 500u);  // echoes the request's system bytes
    EXPECT_EQ(f.reply_text(), "<L [2] <A \"SSIM-128\"> <A \"0.1.0\">>");
    EXPECT_EQ(f.gem->comm_state(), CommState::kNotCommunicating);
}

TEST(GemCommunication, S1F13AcceptsAndEntersCommunicating) {
    Fixture f;
    f.host_sends(make_s1f13());
    ASSERT_EQ(f.sender.replies.size(), 1u);
    EXPECT_EQ(f.last_reply().function, 14);
    EXPECT_EQ(f.reply_text(), "<L [2] <B 0x00> <L [2] <A \"SSIM-128\"> <A \"0.1.0\">>>");
    EXPECT_EQ(f.gem->comm_state(), CommState::kCommunicating);
    f.host_sends(make_s1f13(), 501);  // and again: still answered
    EXPECT_EQ(f.sender.replies.size(), 2u);
}

TEST(GemCommunication, AMessageWithoutTheWBitGetsNoReply) {
    Fixture f;
    Message m = make_s1f1();
    m.w_bit = false;
    f.host_sends(m);
    EXPECT_TRUE(f.sender.replies.empty());
}

TEST(GemCommunication, EventsAreDroppedUntilCommunicatingAndSentAfterwards) {
    Fixture f;
    f.bus.publish(ssim::core::ScanStarted{"W001", 1});
    f.sender.run_tasks();
    EXPECT_TRUE(f.sender.requests.empty());

    f.establish_communication();
    f.bus.publish(ssim::core::ScanStarted{"W002", 1});
    f.sender.run_tasks();
    ASSERT_EQ(f.sender.requests.size(), 1u);
    auto event = parse_s6f11(f.sender.requests[0]);
    ASSERT_TRUE(event);
    EXPECT_EQ(event.value().ceid, kCeidWaferScanStarted);
    ASSERT_EQ(event.value().reports.size(), 1u);
    EXPECT_EQ(event.value().reports[0].rptid, kRptidScanStarted);
    EXPECT_EQ(secs2::to_text(Item::list(event.value().reports[0].values)),
              "<L [3] <A \"W002\"> <U1 1> <U1 6>>");
}

TEST(GemCommunication, LosingTheLinkEndsCommunication) {
    Fixture f;
    f.establish_communication();
    f.gem->on_session_state(hsms::ConnectionState::kNotConnected);
    EXPECT_EQ(f.gem->comm_state(), CommState::kNotCommunicating);
    f.bus.publish(ssim::core::ScanStarted{"W001", 1});
    f.sender.run_tasks();
    EXPECT_TRUE(f.sender.requests.empty());  // events raised while not communicating are dropped
}

// ---- status variables ---------------------------------------------------------

TEST(GemStatusVariables, SpecificSvidsAnUnknownOneIsAnEmptyItem) {
    Fixture f;
    f.host_sends(make_s1f3({4001, 4002, 4010, 9999}));
    EXPECT_EQ(f.last_reply().function, 4);
    EXPECT_EQ(f.reply_text(), "<L [4] <U1 1> <U1 0> <A \"0.1.0\"> <L [0]>>");
}

TEST(GemStatusVariables, AnEmptyRequestReturnsAllTwelve) {
    Fixture f;
    f.host_sends(make_s1f3({}));
    ASSERT_TRUE(f.last_reply().body->is_list());
    EXPECT_EQ(f.last_reply().body->count(), 12u);
    const auto& v = f.last_reply().body->as_list();
    EXPECT_EQ(v[2].format(), secs2::Format::kAscii);  // 4003 wafer id
    EXPECT_EQ(v[4].format(), secs2::Format::kF4);     // 4005 progress
    EXPECT_EQ(v[8].format(), secs2::Format::kU2);     // 4009 alarm count
    EXPECT_EQ(v[10].format(), secs2::Format::kU4);    // 4011 uptime
}

TEST(GemStatusVariables, ReflectTheLiveMachine) {
    Fixture f;
    ssim::core::WaferResultReady r;
    r.wafer_id = "W001";
    r.stress_mpa = -179.5;
    r.curvature_per_m = -0.008;
    r.fit_rms_um = 0.5;
    f.bus.publish(r);
    f.sender.run_tasks();
    f.alarms.set(ssim::core::AlarmId::kScanStall, "x");
    f.clock.advance(std::chrono::seconds(90));

    f.host_sends(make_s1f3({4006, 4007, 4008, 4009, 4011, 4012}));
    const auto& v = f.last_reply().body->as_list();
    EXPECT_FLOAT_EQ(v[0].as_array<float>()[0], -179.5f);
    EXPECT_FLOAT_EQ(v[2].as_array<float>()[0], 0.5f);
    EXPECT_EQ(v[3].as_array<std::uint16_t>()[0], 1);
    EXPECT_EQ(v[4].as_array<std::uint32_t>()[0], 90u);
    EXPECT_EQ(v[5].as_array<std::uint32_t>()[0], 1u);
}

// ---- control state --------------------------------------------------------------

TEST(GemControl, HostGoesOfflineAndBackOnlineToTheOperatorsLastMode) {
    Fixture f;  // starts Online-Local
    f.host_sends(make_s1f15());
    EXPECT_EQ(parse_ack(f.last_reply()).value(), kOflackOk);
    EXPECT_EQ(f.api.snapshot().control_mode, ssim::core::ControlMode::kOffline);

    f.host_sends(make_s1f17());
    EXPECT_EQ(parse_ack(f.last_reply()).value(), kOnlackAccepted);
    EXPECT_EQ(f.api.snapshot().control_mode, ssim::core::ControlMode::kOnlineLocal);

    f.host_sends(make_s1f17());  // already online
    EXPECT_EQ(parse_ack(f.last_reply()).value(), kOnlackAlreadyOnline);
}

TEST(GemControl, OnlineRestoresRemoteIfThatWasTheOperatorsChoice) {
    Fixture f;
    f.operator_goes_remote();
    f.host_sends(make_s1f15());
    f.sender.run_tasks();
    f.host_sends(make_s1f17());
    EXPECT_EQ(f.api.snapshot().control_mode, ssim::core::ControlMode::kOnlineRemote);
}

TEST(GemControl, OnlineIsRefusedWhenTheConfigurationForbidsIt) {
    GemConfig c = Fixture::default_config();
    c.allow_host_online = false;
    Fixture f(c);
    f.host_sends(make_s1f15());
    f.host_sends(make_s1f17());
    EXPECT_EQ(parse_ack(f.last_reply()).value(), kOnlackNotAllowed);
    EXPECT_EQ(f.api.snapshot().control_mode, ssim::core::ControlMode::kOffline);
}

TEST(GemControl, ControlStateChangesAreReportedAsEvent2001) {
    Fixture f;
    f.establish_communication();
    ASSERT_TRUE(f.api.set_control_mode(ssim::core::ControlMode::kOnlineRemote,
                                       ssim::core::CommandSource::kUi));
    f.sender.run_tasks();
    ASSERT_EQ(f.sender.requests.size(), 1u);
    auto e = parse_s6f11(f.sender.requests[0]);
    ASSERT_TRUE(e);
    EXPECT_EQ(e.value().ceid, kCeidControlStateChanged);
    EXPECT_EQ(secs2::to_text(Item::list(e.value().reports[0].values)), "<L [2] <U1 2> <U1 0>>");
}

// ---- remote commands (IT-CTRL-2) ------------------------------------------------

TEST(GemRemoteCommands, RefusedWithHcack2OutsideOnlineRemote) {
    Fixture f;  // Online-Local
    f.host_sends(command("START", {{"WAFER_ID", Item::ascii("W042")}}));
    EXPECT_EQ(hcack_of(f), kHcackCannotPerformNow);
    EXPECT_EQ(f.driver.start_count.load(), 0);
    f.host_sends(command("STOP"));
    EXPECT_EQ(hcack_of(f), kHcackCannotPerformNow);
    f.host_sends(command("ABORT"));
    EXPECT_EQ(hcack_of(f), kHcackCannotPerformNow);
}

TEST(GemRemoteCommands, StartIsAcceptedInRemoteWithHcack4AndReachesTheScanDriver) {
    Fixture f;
    f.operator_goes_remote();
    f.host_sends(command("START", {{"WAFER_ID", Item::ascii("W042")}}));
    EXPECT_EQ(hcack_of(f), kHcackAcceptedLater);
    EXPECT_EQ(f.driver.start_count.load(), 1);
    EXPECT_EQ(f.driver.started_wafer_id, "W042");
    EXPECT_EQ(f.api.snapshot().process_state, ssim::core::ProcessState::kScanning);
    ASSERT_EQ(parse_s2f42(f.last_reply()).value().params.size(), 1u);
    EXPECT_EQ(parse_s2f42(f.last_reply()).value().params[0].cpack, kCpackOk);
}

TEST(GemRemoteCommands, StopAndAbortAreDone) {
    Fixture f;
    f.operator_goes_remote();
    f.host_sends(command("START", {{"WAFER_ID", Item::ascii("W1")}}));
    f.host_sends(command("STOP"));
    EXPECT_EQ(hcack_of(f), kHcackDone);
    f.host_sends(command("ABORT"));
    EXPECT_EQ(hcack_of(f), kHcackDone);
    EXPECT_TRUE(f.driver.abort_requested.load());
}

TEST(GemRemoteCommands, UnknownCommandIsHcack1) {
    Fixture f;
    f.operator_goes_remote();
    f.host_sends(command("REBOOT"));
    EXPECT_EQ(hcack_of(f), kHcackUnknownCommand);
}

TEST(GemRemoteCommands, BadStartParametersAreHcack3WithPerParameterCpack) {
    Fixture f;
    f.operator_goes_remote();

    f.host_sends(command("START"));  // WAFER_ID missing
    EXPECT_EQ(hcack_of(f), kHcackInvalidParameter);

    f.host_sends(command("START", {{"WAFER_ID", Item::ascii("../etc/passwd")}}));
    EXPECT_EQ(hcack_of(f), kHcackInvalidParameter);
    EXPECT_EQ(parse_s2f42(f.last_reply()).value().params[0].cpack, kCpackIllegalValue);

    f.host_sends(command("START", {{"WAFER_ID", Item::u4(std::uint32_t{5})}}));
    EXPECT_EQ(parse_s2f42(f.last_reply()).value().params[0].cpack, kCpackIllegalFormat);

    f.host_sends(command("START", {{"CASSETTE_ID", Item::ascii("C001")}}));
    EXPECT_EQ(hcack_of(f), kHcackInvalidParameter);
    EXPECT_EQ(parse_s2f42(f.last_reply()).value().params[0].cpack, kCpackIllegalValue);

    f.host_sends(
        command("START", {{"WAFER_ID", Item::ascii("W1")}, {"COLOUR", Item::ascii("red")}}));
    EXPECT_EQ(hcack_of(f), kHcackInvalidParameter);
    EXPECT_EQ(parse_s2f42(f.last_reply()).value().params[1].cpack, kCpackUnknownName);

    f.host_sends(command("START", {{"WAFER_ID", Item::ascii("")}}));
    EXPECT_EQ(hcack_of(f), kHcackInvalidParameter);
    f.host_sends(command("START", {{"WAFER_ID", Item::ascii(std::string(33, 'a'))}}));
    EXPECT_EQ(hcack_of(f), kHcackInvalidParameter);

    EXPECT_EQ(f.driver.start_count.load(), 0);  // nothing above ever reached the machine
}

TEST(GemRemoteCommands, StopWithAParameterIsInvalid) {
    Fixture f;
    f.operator_goes_remote();
    f.host_sends(command("STOP", {{"NOW", Item::ascii("yes")}}));
    EXPECT_EQ(hcack_of(f), kHcackInvalidParameter);
}

// IT-CTRL-2: Start during an alarm is refused; ClearAlarm allows the next Start.
TEST(GemRemoteCommands, StartDuringAnAlarmIsRefusedAndClearAlarmUnblocksIt) {
    Fixture f;
    f.operator_goes_remote();
    f.host_sends(command("START", {{"WAFER_ID", Item::ascii("W1")}}));
    f.controller.notify_fault(ssim::core::AlarmId::kScanStall, "test");
    ASSERT_EQ(f.api.snapshot().process_state, ssim::core::ProcessState::kAlarm);

    f.host_sends(command("START", {{"WAFER_ID", Item::ascii("W2")}}));
    EXPECT_EQ(hcack_of(f), kHcackCannotPerformNow);
    EXPECT_EQ(f.driver.start_count.load(), 1);

    f.host_sends(command("CLEAR_ALARM"));
    EXPECT_EQ(hcack_of(f), kHcackDone);
    f.host_sends(command("START", {{"WAFER_ID", Item::ascii("W2")}}));
    EXPECT_EQ(hcack_of(f), kHcackAcceptedLater);
    EXPECT_EQ(f.driver.start_count.load(), 2);
}

TEST(GemRemoteCommands, ClearAlarmValidatesTheAlid) {
    Fixture f;
    f.operator_goes_remote();
    f.host_sends(command("START", {{"WAFER_ID", Item::ascii("W1")}}));
    f.controller.notify_fault(ssim::core::AlarmId::kScanStall, "test");

    f.host_sends(command("CLEAR_ALARM", {{"ALID", Item::u4(std::uint32_t{9999})}}));
    EXPECT_EQ(hcack_of(f), kHcackInvalidParameter);
    f.host_sends(command("CLEAR_ALARM", {{"ALID", Item::ascii("1004")}}));
    EXPECT_EQ(parse_s2f42(f.last_reply()).value().params[0].cpack, kCpackIllegalFormat);
    f.host_sends(command("CLEAR_ALARM", {{"ALID", Item::u4(std::uint32_t{1004})}}));
    EXPECT_EQ(hcack_of(f), kHcackDone);
}

TEST(GemRemoteCommands, ClearAlarmWithNoAlarmIsCannotPerformNow) {
    Fixture f;
    f.operator_goes_remote();
    f.host_sends(command("CLEAR_ALARM"));
    EXPECT_EQ(hcack_of(f), kHcackCannotPerformNow);
}

// ---- events and alarms (FR-GEM-4, FR-GEM-5) --------------------------------------

TEST(GemEvents, ResultInSpecIsOneEventOutOfSpecIsTwo) {
    Fixture f;
    f.establish_communication();

    ssim::core::WaferResultReady ok;
    ok.wafer_id = "W001";
    ok.slot = 1;
    ok.stress_mpa = -180.0;
    ok.stress_unc_mpa = 0.5;
    ok.curvature_per_m = -0.008;
    ok.fit_rms_um = 0.5;
    f.bus.publish(ok);
    f.sender.run_tasks();
    ASSERT_EQ(f.sender.requests.size(), 1u);
    auto e = parse_s6f11(f.sender.requests[0]);
    ASSERT_TRUE(e);
    EXPECT_EQ(e.value().ceid, kCeidWaferScanComplete);
    EXPECT_EQ(e.value().reports[0].rptid, kRptidWaferResult);
    const auto& v = e.value().reports[0].values;
    ASSERT_EQ(v.size(), 7u);
    EXPECT_EQ(v[0], Item::ascii("W001"));
    EXPECT_FLOAT_EQ(v[2].as_array<float>()[0], -180.0f);
    EXPECT_EQ(v[6], Item::boolean(false));
    EXPECT_TRUE(f.sender.requests[0].w_bit);

    f.sender.requests.clear();
    ok.wafer_id = "W002";
    ok.out_of_spec = true;
    f.bus.publish(ok);
    f.sender.run_tasks();
    ASSERT_EQ(f.sender.requests.size(), 2u);
    EXPECT_EQ(parse_s6f11(f.sender.requests[0]).value().ceid, kCeidWaferScanComplete);
    EXPECT_EQ(parse_s6f11(f.sender.requests[1]).value().ceid, kCeidWaferOutOfSpec);
    EXPECT_EQ(parse_s6f11(f.sender.requests[1]).value().reports[0].values[6], Item::boolean(true));
}

TEST(GemEvents, DataIdsIncrease) {
    Fixture f;
    f.establish_communication();
    f.bus.publish(ssim::core::ScanStarted{"W1", 1});
    f.bus.publish(ssim::core::ScanStarted{"W2", 1});
    f.sender.run_tasks();
    ASSERT_EQ(f.sender.requests.size(), 2u);
    EXPECT_LT(parse_s6f11(f.sender.requests[0]).value().dataid,
              parse_s6f11(f.sender.requests[1]).value().dataid);
}

TEST(GemEvents, ProcessStateAndRunAbortedAreReported) {
    Fixture f;
    f.establish_communication();
    ASSERT_TRUE(f.api.start("W1", 1, ssim::core::CommandSource::kUi));
    f.sender.run_tasks();
    // Start publishes ScanStarted and the Idle -> Scanning state change.
    std::vector<std::uint32_t> ceids;
    for (const Message& m : f.sender.requests) ceids.push_back(parse_s6f11(m).value().ceid);
    EXPECT_NE(std::find(ceids.begin(), ceids.end(), kCeidProcessStateChanged), ceids.end());
    EXPECT_NE(std::find(ceids.begin(), ceids.end(), kCeidWaferScanStarted), ceids.end());

    f.sender.requests.clear();
    ASSERT_TRUE(f.api.abort(ssim::core::CommandSource::kUi));
    f.controller.notify_stage_stopped();
    f.sender.run_tasks();
    ceids.clear();
    for (const Message& m : f.sender.requests) ceids.push_back(parse_s6f11(m).value().ceid);
    EXPECT_NE(std::find(ceids.begin(), ceids.end(), kCeidRunAborted), ceids.end());
}

TEST(GemAlarms, SetAndClearAreReportedAsS5F1) {
    Fixture f;
    f.establish_communication();
    f.alarms.set(ssim::core::AlarmId::kSensorSpikeRateHigh, "too many spikes");
    f.sender.run_tasks();
    ASSERT_EQ(f.sender.requests.size(), 1u);
    EXPECT_EQ(f.sender.requests[0].stream, 5);
    auto set = parse_s5f1(f.sender.requests[0]);
    ASSERT_TRUE(set);
    EXPECT_EQ(set.value().alid, 1001u);
    EXPECT_EQ(set.value().altx, "too many spikes");
    EXPECT_NE(set.value().alcd & kAlcdSetBit, 0);

    f.alarms.clear(ssim::core::AlarmId::kSensorSpikeRateHigh);
    f.sender.run_tasks();
    ASSERT_EQ(f.sender.requests.size(), 2u);
    auto cleared = parse_s5f1(f.sender.requests[1]);
    ASSERT_TRUE(cleared);
    EXPECT_EQ(cleared.value().alid, 1001u);
    EXPECT_EQ(cleared.value().alcd & kAlcdSetBit, 0);
}

TEST(GemAlarms, AlarmsRaisedWhileNotCommunicatingAreNotSent) {
    Fixture f;
    f.alarms.set(ssim::core::AlarmId::kScanStall, "x");
    f.sender.run_tasks();
    EXPECT_TRUE(f.sender.requests.empty());
}

// ---- transactions and T3 ----------------------------------------------------------

TEST(GemTransactions, AHostAcknowledgeClosesTheRequest) {
    Fixture f;
    f.establish_communication();
    f.bus.publish(ssim::core::ScanStarted{"W1", 1});
    f.sender.run_tasks();
    const std::uint32_t sys = f.sender.system_bytes.at(0);

    hsms::Delivery ack;
    ack.frame =
        hsms::make_data_frame(kDevice, 6, 12, false, sys, secs2::encode(Item::binary({0})).value());
    ack.reply_to = sys;
    f.gem->on_data(ack);
    f.gem->on_transaction_timeout(sys);       // a late timeout for a closed request does nothing
    EXPECT_EQ(f.sender.requests.size(), 1u);  // no S9F9
}

TEST(GemTransactions, AnUnansweredEventProducesS9F9CarryingTheHeaderWeSent) {
    Fixture f;
    f.establish_communication();
    f.bus.publish(ssim::core::ScanStarted{"W1", 1});
    f.sender.run_tasks();
    const std::uint32_t sys = f.sender.system_bytes.at(0);

    f.gem->on_transaction_timeout(sys);
    ASSERT_EQ(f.sender.requests.size(), 2u);
    const Message& s9 = f.sender.requests[1];
    EXPECT_EQ(s9.stream, 9);
    EXPECT_EQ(s9.function, 9);
    EXPECT_FALSE(s9.w_bit);
    const auto& shead = s9.body->as_array<std::uint8_t>();
    ASSERT_EQ(shead.size(), 10u);
    EXPECT_EQ((shead[0] << 8) | shead[1], kDevice);  // session id
    EXPECT_EQ(shead[2], 0x86);                       // W-bit | stream 6
    EXPECT_EQ(shead[3], 11);                         // function
    EXPECT_EQ(shead[9], sys & 0xFF);                 // system bytes
}

// ---- S9 errors (FR-GEM-9) ----------------------------------------------------------

std::vector<std::uint8_t> mhead_of(const Message& s9) { return s9.body->as_array<std::uint8_t>(); }

TEST(GemErrors, WrongDeviceIdIsS9F1) {
    Fixture f;
    f.host_sends(make_s1f1(), 77, /*device=*/99);
    ASSERT_EQ(f.sender.requests.size(), 1u);
    EXPECT_EQ(f.sender.requests[0].function, 1);
    const auto head = mhead_of(f.sender.requests[0]);
    EXPECT_EQ((head[0] << 8) | head[1], 99);
    EXPECT_EQ(head[9], 77);
    EXPECT_TRUE(f.sender.replies.empty());
}

TEST(GemErrors, UnknownStreamIsS9F3UnknownFunctionIsS9F5) {
    Fixture f;
    Message s3;
    s3.stream = 3;
    s3.function = 1;
    s3.w_bit = true;
    f.host_sends(s3);
    ASSERT_EQ(f.sender.requests.size(), 1u);
    EXPECT_EQ(f.sender.requests[0].function, 3);

    Message s1f99;
    s1f99.stream = 1;
    s1f99.function = 99;
    s1f99.w_bit = true;
    f.host_sends(s1f99);
    EXPECT_EQ(f.sender.requests[1].function, 5);

    Message s2f13;  // equipment constants: known stream, not built (FR-GEM-7)
    s2f13.stream = 2;
    s2f13.function = 13;
    s2f13.w_bit = true;
    f.host_sends(s2f13);
    EXPECT_EQ(f.sender.requests[2].function, 5);
}

TEST(GemErrors, IllegalDataIsS9F7) {
    Fixture f;
    Message bad = w(make_s1f1());
    bad.body = Item::ascii("S1F1 takes no body");
    f.host_sends(bad);
    ASSERT_EQ(f.sender.requests.size(), 1u);
    EXPECT_EQ(f.sender.requests[0].function, 7);

    f.host_sends_raw_body(1, 3, {0x41, 0x09, 0x61});  // truncated item: does not even decode
    EXPECT_EQ(f.sender.requests[1].function, 7);
}

TEST(GemErrors, ABodyOverTheLimitIsS9F11) {
    GemConfig c = Fixture::default_config();
    c.max_body_bytes = 16;
    Fixture f(c);
    f.host_sends(command("START", {{"WAFER_ID", Item::ascii("W042")}}));
    ASSERT_EQ(f.sender.requests.size(), 1u);
    EXPECT_EQ(f.sender.requests[0].function, 11);
    EXPECT_TRUE(f.sender.replies.empty());
}

TEST(GemErrors, TheErrorMessageCarriesTheOffendingHeader) {
    Fixture f;
    Message s7;
    s7.stream = 7;
    s7.function = 3;
    s7.w_bit = true;
    f.host_sends(s7, 0x01020304);
    const auto head = mhead_of(f.sender.requests.at(0));
    ASSERT_EQ(head.size(), 10u);
    EXPECT_EQ(head[2], 0x87);
    EXPECT_EQ(head[3], 3);
    EXPECT_EQ(head[6], 0x01);
    EXPECT_EQ(head[9], 0x04);
}

}  // namespace
}  // namespace ssim::secsgem::gem
