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
}

TEST_CASE("File dialog requests without an ID fail instead of opening once", "[file_dialog]")
{
    luil::file_dialog_gate gate {};
    const luil::file_dialog_request unnamed {};
    // 기본값 0은 열지 않고 매번 같은 frame에 실패를 거듭 보내지도 않는다.
    REQUIRE_FALSE(gate.try_begin(0));
    const auto rejected { gate.rejection(unnamed) };
    REQUIRE(rejected.has_value());
    REQUIRE(rejected->id == 0);
    REQUIRE(rejected->status == luil::file_dialog_status::failed);
    REQUIRE(rejected->error.empty() == false);
    REQUIRE_FALSE(gate.rejection(unnamed).has_value());
    // 요청이 빠졌다가 다시 오면 다시 알린다.
    gate.idle();
    REQUIRE(gate.rejection(unnamed).has_value());
    // 0은 gate를 잠그지 않는다.
    REQUIRE(gate.try_begin(9));
    REQUIRE_FALSE(gate.rejection(unnamed).has_value());
    gate.finish();
    REQUIRE(gate.rejection(unnamed).has_value());
    REQUIRE_FALSE(gate.try_begin(0));
    const luil::file_dialog_request named { .id = 10 };
    REQUIRE_FALSE(gate.rejection(named).has_value());
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
