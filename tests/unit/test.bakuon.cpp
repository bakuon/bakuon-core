#include <gtest/gtest.h>

#include <bakuon/core/bakuon.h>

TEST(BakuonTest, bakuon)
{
    EXPECT_EQ(bakuon::versionString(), "0.1.0");
}
