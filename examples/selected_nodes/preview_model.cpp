// Linux용 모델 미리보기 자료다. UI/플랫폼 통합 test가 아니며 Skia 없이 순수 값만 출력한다.
#include "selected_nodes/scene.h"
#include "ui/ligature_carets.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
    std::string bytes(const std::u8string_view value)
    {
        return { reinterpret_cast<const char*>(value.data()), value.size() };
    }

    nlohmann::json node_json(const luil::dock_node& node)
    {
        nlohmann::json result { { "key", bytes(node.key) }, { "selected", bytes(node.selected) }, { "axis", node.axis == luil::split_axis::horizontal ? "horizontal" : "vertical" }, { "ratio", node.ratio } };
        result["panels"] = nlohmann::json::array();
        result["children"] = nlohmann::json::array();
        for (const auto& panel : node.panels)
            result["panels"].push_back(bytes(panel));
        for (const auto& child : node.children)
            result["children"].push_back(node_json(child));
        return result;
    }
}

int main(int argc, char** argv)
{
    const selected_nodes::scene_state state {};
    nlohmann::json result {};
    result["base"] = "0e3927ecb2c48e145c5b553ec797103be3bcc646";
    result["root"] = node_json(state.dock.root);
    result["floating"] = nlohmann::json::array();
    for (const auto& floating : state.dock.floating)
        result["floating"].push_back({ { "node", node_json(floating.group) }, { "bounds", { floating.bounds.x, floating.bounds.y, floating.bounds.width, floating.bounds.height } } });
    const auto time { luil::read_time_picker(state.hour.text, state.minute.text) };
    if (time.has_value())
        result["time"] = { { "hour", time->hour }, { "minute", time->minute } };
    result["page"] = luil::apply_pager_command(state.page, 3, { .operation = luil::pager_operation::step, .delta = 1.0f });
    if (argc == 3)
    {
        std::ifstream input { argv[1], std::ios::binary };
        const std::vector<std::uint8_t> table { std::istreambuf_iterator<char> { input }, std::istreambuf_iterator<char> {} };
        result["carets"] = luil::ligature_caret_coordinates(table, static_cast<std::uint16_t>(std::stoul(argv[2])));
    }
    std::cout << result.dump(2) << '\n';
}
