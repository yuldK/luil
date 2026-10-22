#include "luil/app/menu_popup.h"

#include "luil/ui/ui_tree.h"

#include <cmath>
#include <memory>
#include <utility>

namespace luil {
    ui_popup make_menu_popup(menu_config menu, menu_popup_config config)
    {
        const float scale { std::isfinite(config.scale) && config.scale > 0.0f ? config.scale : 1.0f };
        ui_popup popup {};
        popup.id = std::move(config.id);
        popup.anchor = std::move(config.anchor);
        popup.x = config.x;
        popup.y = config.y;
        popup.width = std::isfinite(config.width) && config.width > 0.0f ? config.width : menu_element::width_for(menu);
        popup.height = menu_element::height_for(menu);
        popup.dismiss = std::move(config.dismiss);
        auto root { std::make_unique<menu_element>(std::move(menu)) };
        popup.tree = std::make_shared<const ui_tree>(make_arranged_tree(std::move(root), { 0.0f, 0.0f, popup.width * scale, popup.height * scale }, scale));
        return popup;
    }
} // namespace luil
