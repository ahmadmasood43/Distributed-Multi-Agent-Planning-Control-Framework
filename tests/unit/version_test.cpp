#include "mapc/version.hpp"

#include <gtest/gtest.h>

#include <string>

namespace mapc {
namespace {

TEST(VersionTest, StringMatchesComponents) {
    const std::string expected = std::to_string(kVersionMajor) + "." +
                                 std::to_string(kVersionMinor) + "." +
                                 std::to_string(kVersionPatch);
    EXPECT_EQ(kVersionString, expected);
}

TEST(VersionTest, MacrosMatchConstants) {
    EXPECT_EQ(MAPC_VERSION_MAJOR, kVersionMajor);
    EXPECT_EQ(MAPC_VERSION_MINOR, kVersionMinor);
    EXPECT_EQ(MAPC_VERSION_PATCH, kVersionPatch);
}

}  // namespace
}  // namespace mapc
