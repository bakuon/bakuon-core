#include <gtest/gtest.h>

#include <bakuon/bakuon.h>

TEST(BakuonTest, bakuon)
{
    EXPECT_EQ(bakuon::versionString(), "0.1.0");
}
