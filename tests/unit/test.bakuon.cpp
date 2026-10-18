#include "b_bakuon.h"

#include <gtest/gtest.h>

TEST(BakuonTest, bakuon)
{
    EXPECT_EQ(bakuon::core::versionString(), "0.1.0");
}
