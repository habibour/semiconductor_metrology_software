#include "ssim/secsgem/hsms/connection_state.hpp"

#include <gtest/gtest.h>

namespace ssim::secsgem::hsms {
namespace {

ConnectionStateMachine machine_in(ConnectionState target) {
    ConnectionStateMachine m;
    if (target != ConnectionState::kNotConnected) {
        EXPECT_TRUE(m.apply(ConnectionTrigger::kConnect));
    }
    if (target == ConnectionState::kSelected) {
        EXPECT_TRUE(m.apply(ConnectionTrigger::kSelect));
    }
    EXPECT_EQ(m.state(), target);
    return m;
}

TEST(ConnectionStateMachine, StartsNotConnected) {
    EXPECT_EQ(ConnectionStateMachine().state(), ConnectionState::kNotConnected);
}

TEST(ConnectionStateMachine, LegalTransitionsFollowTheTable) {
    struct Case {
        ConnectionState from;
        ConnectionTrigger trigger;
        ConnectionState to;
    };
    const Case cases[] = {
        {ConnectionState::kNotConnected, ConnectionTrigger::kConnect,
         ConnectionState::kNotSelected},
        {ConnectionState::kNotSelected, ConnectionTrigger::kSelect, ConnectionState::kSelected},
        {ConnectionState::kSelected, ConnectionTrigger::kDeselect, ConnectionState::kNotSelected},
        {ConnectionState::kNotSelected, ConnectionTrigger::kDisconnect,
         ConnectionState::kNotConnected},
        {ConnectionState::kSelected, ConnectionTrigger::kDisconnect,
         ConnectionState::kNotConnected},
        {ConnectionState::kNotSelected, ConnectionTrigger::kT7Timeout,
         ConnectionState::kNotConnected},
    };
    for (const Case& c : cases) {
        ConnectionStateMachine m = machine_in(c.from);
        auto r = m.apply(c.trigger);
        ASSERT_TRUE(r) << to_string(c.from) << " + " << to_string(c.trigger);
        EXPECT_EQ(r.value(), c.to);
        EXPECT_EQ(m.state(), c.to);
    }
}

// Every pair not in the table is rejected with the same stable code and
// leaves the state alone.
TEST(ConnectionStateMachine, EveryOtherPairIsRejectedAndLeavesTheStateUnchanged) {
    struct Legal {
        ConnectionState from;
        ConnectionTrigger trigger;
    };
    const Legal legal[] = {
        {ConnectionState::kNotConnected, ConnectionTrigger::kConnect},
        {ConnectionState::kNotSelected, ConnectionTrigger::kSelect},
        {ConnectionState::kSelected, ConnectionTrigger::kDeselect},
        {ConnectionState::kNotSelected, ConnectionTrigger::kDisconnect},
        {ConnectionState::kSelected, ConnectionTrigger::kDisconnect},
        {ConnectionState::kNotSelected, ConnectionTrigger::kT7Timeout},
    };
    const ConnectionState states[] = {ConnectionState::kNotConnected, ConnectionState::kNotSelected,
                                      ConnectionState::kSelected};
    const ConnectionTrigger triggers[] = {
        ConnectionTrigger::kConnect, ConnectionTrigger::kSelect, ConnectionTrigger::kDeselect,
        ConnectionTrigger::kDisconnect, ConnectionTrigger::kT7Timeout};
    int rejected = 0;
    for (ConnectionState s : states) {
        for (ConnectionTrigger t : triggers) {
            bool is_legal = false;
            for (const Legal& l : legal) {
                if (l.from == s && l.trigger == t) is_legal = true;
            }
            if (is_legal) continue;
            ConnectionStateMachine m = machine_in(s);
            auto r = m.apply(t);
            ASSERT_FALSE(r) << to_string(s) << " + " << to_string(t);
            EXPECT_EQ(r.error().code, kReasonIllegalConnectionTransition);
            EXPECT_EQ(m.state(), s);
            ++rejected;
        }
    }
    EXPECT_EQ(rejected, 15 - 6);
}

}  // namespace
}  // namespace ssim::secsgem::hsms
