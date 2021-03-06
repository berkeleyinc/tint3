#include "catch.hpp"

#include <cstdlib>
#include <cstring>
#include "util/environment.hh"

TEST_CASE("Get", "Reading from the environment is sane") {
  setenv("__BOGUS_NAME__", "__BOGUS_VALUE__", 1);
  REQUIRE(environment::Get("__BOGUS_NAME__") == "__BOGUS_VALUE__");

  unsetenv("__BOGUS_NAME__");
  REQUIRE(environment::Get("__BOGUS_NAME__").empty());
}

TEST_CASE("Set", "Writing to the environment works") {
  // Fails on invalid key.
  REQUIRE_FALSE(environment::Set("", "__BOGUS_VALUE__"));

  REQUIRE(getenv("__BOGUS_NAME__") == nullptr);
  REQUIRE(environment::Set("__BOGUS_NAME__", "__BOGUS_VALUE__"));
  REQUIRE(std::strcmp(getenv("__BOGUS_NAME__"), "__BOGUS_VALUE__") == 0);
}

TEST_CASE("Unset", "Deleting from the environment works") {
  // Fails on invalid key.
  REQUIRE_FALSE(environment::Unset(""));

  setenv("__BOGUS_NAME__", "__BOGUS_VALUE__", 1);
  REQUIRE(std::strcmp(getenv("__BOGUS_NAME__"), "__BOGUS_VALUE__") == 0);
  REQUIRE(environment::Unset("__BOGUS_NAME__"));
  REQUIRE(getenv("__BOGUS_NAME__") == nullptr);

  // No-op when removing an already deleted key name.
  REQUIRE(environment::Unset("__BOGUS_NAME__"));
}

TEST_CASE("ScopedEnvironmentOverride",
          "Temporary environment overrides work as expected") {
  SECTION("An pre-existing variable gets overwritten, then restored") {
    setenv("__BOGUS_NAME__", "__BOGUS_VALUE__", 1);
    REQUIRE(std::strcmp(getenv("__BOGUS_NAME__"), "__BOGUS_VALUE__") == 0);

    {
      auto new_value =
          environment::MakeScopedOverride("__BOGUS_NAME__", "__NEW_VALUE__");
      REQUIRE(std::strcmp(getenv("__BOGUS_NAME__"), "__NEW_VALUE__") == 0);
    }

    REQUIRE(std::strcmp(getenv("__BOGUS_NAME__"), "__BOGUS_VALUE__") == 0);
  }

  SECTION("A non-existing variable gets set, then unset") {
    unsetenv("__BOGUS_NAME__");
    REQUIRE(getenv("__BOGUS_NAME__") == nullptr);

    {
      auto new_value =
          environment::MakeScopedOverride("__BOGUS_NAME__", "__NEW_VALUE__");
      REQUIRE(std::strcmp(getenv("__BOGUS_NAME__"), "__NEW_VALUE__") == 0);
    }

    REQUIRE(getenv("__BOGUS_NAME__") == nullptr);
  }
}
