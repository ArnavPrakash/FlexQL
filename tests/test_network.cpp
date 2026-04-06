#include <gtest/gtest.h>
#include "network/protocol.h"
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include <vector>

using namespace flexql::network;

class NetworkTest : public ::testing::Test {
protected:
    int sv[2];

    void SetUp() override {
        // Create a local socket pair for testing sending and receiving directly
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    }

    void TearDown() override {
        close(sv[0]);
        close(sv[1]);
    }
};

TEST_F(NetworkTest, SendAndReceiveStringFrame) {
    std::string test_msg = "Hello, FlexQL!";
    
    // Send on sv[0]
    EXPECT_EQ(net_send_string_frame(sv[0], test_msg), 0);

    // Receive on sv[1]
    std::string received;
    EXPECT_EQ(net_recv_string_frame(sv[1], received), 0);
    EXPECT_EQ(received, test_msg);
}

TEST_F(NetworkTest, SendAndReceiveZeroLengthFrame) {
    std::string test_msg = "";
    
    EXPECT_EQ(net_send_string_frame(sv[0], test_msg), 0);

    std::string received;
    EXPECT_EQ(net_recv_string_frame(sv[1], received), 0);
    EXPECT_EQ(received, test_msg);
}

TEST_F(NetworkTest, SendAndReceiveMultipleFrames) {
    std::vector<std::string> messages = {"Frame 1", "Second frame here", "A much longer third frame for testing."};

    for (const auto& msg : messages) {
        EXPECT_EQ(net_send_string_frame(sv[0], msg), 0);
    }

    for (const auto& msg : messages) {
        std::string received;
        EXPECT_EQ(net_recv_string_frame(sv[1], received), 0);
        EXPECT_EQ(received, msg);
    }
}
