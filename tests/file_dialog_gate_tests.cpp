#include "host/file_dialog_gate.h"
#include "luil/app/file_dialog.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("File dialog requests are deduplicated across intervening IDs", "[file_dialog]")
{
    luil::file_dialog_gate gate {};
    REQUIRE(gate.try_begin(7));
    REQUIRE_FALSE(gate.try_begin(7));
    REQUIRE_FALSE(gate.try_begin(8));
    gate.finish();
    REQUIRE(gate.try_begin(8));
    gate.finish();
    REQUIRE_FALSE(gate.try_begin(7));
    REQUIRE_FALSE(gate.try_begin(8));
    REQUIRE(gate.try_begin(0));
}

TEST_CASE("File dialog results preserve UTF-8 paths and distinct outcomes", "[file_dialog]")
{
    const luil::file_dialog_result chosen { 9, luil::file_dialog_status::chosen, { u8"C:/자료/그림.png", u8"D:/📷/b.png" } };
    REQUIRE(chosen.paths.size() == 2);
    REQUIRE(chosen.paths[1] == u8"D:/📷/b.png");
    REQUIRE(luil::file_dialog_result {}.status == luil::file_dialog_status::cancelled);
    REQUIRE(luil::file_dialog_status::cancelled != luil::file_dialog_status::failed);
    REQUIRE(luil::file_dialog_status::unsupported != luil::file_dialog_status::failed);
}
