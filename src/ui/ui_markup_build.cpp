#include "ui/ui_markup_document.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

// 화면 마크업의 펼치기다 (docs/markup-binding-design.md).
//  - 속성은 요소의 기본값 위에 표의 행 순서대로 다시 건다. 적은 값은 그대로, 바인딩은 찾은 값으로 건다. 적은 값만 건 값
//    (`ui_markup_literal_values`)을 바탕으로 쓰면 바인딩한 `padding`이 적은 `paddingLeft`를 덮는다.
//  - 재귀하지 않고 쌓은 frame으로 돈다. 읽기와 같은 까닭이다 (앱이 `max_depth`를 크게 올린 틀).
//  - 펼칠 때 오류는 요소를 세운 채로 모은다 (결정 7). 찾지 못한 값은 빈 값으로 선다.
namespace luil {
    ui_markup_scope& ui_markup_scope::set(std::u8string name, ui_markup_value value)
    {
        for (auto& [existing, held] : values_)
        {
            if (existing == name)
            {
                held = std::move(value);
                return *this;
            }
        }
        values_.emplace_back(std::move(name), std::move(value));
        return *this;
    }

    const ui_markup_value* ui_markup_scope::find(const std::u8string_view name) const noexcept
    {
        for (const auto& [existing, held] : values_)
        {
            if (existing == name)
                return &held;
        }
        return nullptr;
    }

    namespace {
        // 확인 대화 상자의 단추와 별점 칸이 쓰는 단위 구분자다 (ui_element_id.h). `id`에 쓰지 못해 겹치지 않는다.
        constexpr char8_t unit_separator { u8'\x1F' };

        [[nodiscard]] std::u8string widened(const std::string_view text)
        {
            return { reinterpret_cast<const char8_t*>(text.data()), text.size() };
        }

        // 둘러싼 반복 하나의 지금 항목이다.
        struct loop_binding
        {
            std::u8string_view variable {};
            const ui_markup_scope* item { nullptr };
        };

        class markup_builder
        {
        public:
            markup_builder(const ui_markup_document& document, const ui_markup_context& context)
                : document_ { document }
                , context_ { context }
            {
            }

            [[nodiscard]] ui_markup_build_result run()
            {
                ui_markup_build_result result {};
                if (document_.nodes.empty())
                    return result;
                std::unique_ptr<ui_element> root { make_element(0) };
                ++built_;
                // 깊은 틀은 펼치지 않는다. 지은 tree를 버리는 해제가 자식을 따라 재귀해 호출 스택을 넘칠 수 있다.
                if (const std::optional<std::size_t> deep { first_beyond_depth() }; deep.has_value())
                {
                    result.element = std::move(root);
                    result.errors.push_back(ui_markup_error_at(ui_markup_error_kind::too_large, document_.nodes[*deep].at, {}, u8"max_depth", ui_markup_decimal(ui_markup_build_max_depth)));
                    result.built = built_;
                    return result;
                }
                if (is_container(document_.nodes.front().tag))
                {
                    frames_.push_back({ .node = 0, .element = std::move(root) });
                    root = walk();
                }
                std::stable_sort(errors_.begin(), errors_.end(), [](const ui_markup_error& left, const ui_markup_error& right) {
                    return left.offset < right.offset;
                });
                result.element = std::move(root);
                result.errors = std::move(errors_);
                result.built = built_;
                return result;
            }

        private:
            // 들어간 노드 하나다. 요소(stack·wrap·panel)는 지은 element를 쥐고, 구조 요소는 자식을 둘러싼 요소에 넘긴다.
            struct frame
            {
                std::size_t node { 0 };
                // 다음에 볼 자식의 차례다 (`ui_markup_node::children`).
                std::size_t next { 0 };
                // 요소면 지은 element다. 자식을 다 담은 뒤 둘러싼 요소에 담는다. 구조 요소면 비어 있다.
                std::unique_ptr<ui_element> element {};
                // 이 element를 둘러싼 요소에 담을 자리다.
                stack_item item {};
                // 자식을 담을 요소의 frame이다. 요소면 자기, 구조 요소면 둘러싼 요소의 것이다.
                std::size_t owner_frame { 0 };
                // 요소를 다 지은 뒤 담을 frame이다. 뿌리와 구조 요소에는 뜻이 없다.
                std::size_t parent_frame { 0 };
                // 들어가기 전 자리 글의 길이다. 나올 때 되돌린다.
                std::size_t position_length { 0 };
                // 반복이다. 목록과 지금 항목, 이 펼침에서 본 key다.
                ui_markup_list list {};
                std::size_t item_index { 0 };
                bool in_item { false };
                std::set<std::u8string> keys {};
            };

            [[nodiscard]] static bool is_container(const ui_markup_tag tag) noexcept
            {
                return tag == ui_markup_tag::stack || tag == ui_markup_tag::wrap || tag == ui_markup_tag::panel;
            }

            [[nodiscard]] static bool is_structural(const ui_markup_tag tag) noexcept
            {
                return tag == ui_markup_tag::for_each || tag == ui_markup_tag::if_true || tag == ui_markup_tag::unless_true;
            }

            // 깊이가 `ui_markup_build_max_depth`를 넘는 첫 노드다 (글 차례). 셈은 읽기와 같다.
            [[nodiscard]] std::optional<std::size_t> first_beyond_depth() const
            {
                // 노드와 그 깊이다. 자식을 거꾸로 쌓아 글 차례로 꺼낸다.
                std::vector<std::pair<std::size_t, std::size_t>> pending { { 0, 1 } };
                while (pending.empty() == false)
                {
                    const auto [index, depth] { pending.back() };
                    pending.pop_back();
                    if (depth > ui_markup_build_max_depth)
                        return index;
                    const std::vector<std::size_t>& children { document_.nodes[index].children };
                    for (auto child { children.rbegin() }; child != children.rend(); ++child)
                        pending.emplace_back(*child, depth + 1);
                }
                return std::nullopt;
            }

            [[nodiscard]] std::unique_ptr<ui_element> walk()
            {
                std::unique_ptr<ui_element> root {};
                while (frames_.empty() == false)
                {
                    const std::size_t top { frames_.size() - 1 };
                    const ui_markup_node& node { document_.nodes[frames_[top].node] };
                    if (node.tag == ui_markup_tag::for_each && advance_loop(top) == false)
                    {
                        static_cast<void>(leave(top));
                        continue;
                    }
                    if (stopped_ || frames_[top].next == node.children.size())
                    {
                        if (node.tag == ui_markup_tag::for_each && frames_[top].in_item && stopped_ == false)
                        {
                            // 항목 하나를 다 돌았다. 다음 항목은 다음 차례의 `advance_loop`가 연다.
                            end_item(top);
                            continue;
                        }
                        if (std::unique_ptr<ui_element> finished { leave(top) }; finished != nullptr)
                            root = std::move(finished);
                        continue;
                    }
                    const std::size_t child_index { node.children[frames_[top].next] };
                    const std::size_t position { frames_[top].next };
                    ++frames_[top].next;
                    visit(child_index, position, frames_[top].owner_frame);
                }
                return root;
            }

            // 반복의 항목을 연다. 더 돌 항목이 없으면 거짓이다.
            [[nodiscard]] bool advance_loop(const std::size_t index)
            {
                frame& loop { frames_[index] };
                if (loop.in_item || stopped_)
                    return loop.in_item || stopped_ == false;
                if (loop.list == nullptr || loop.item_index >= loop.list->size())
                    return false;
                const ui_markup_node& node { document_.nodes[loop.node] };
                bindings_.push_back({ .variable = node.as, .item = &(*loop.list)[loop.item_index] });
                std::u8string key { node.key.has_value() ? text_of(*node.key, loop.node, u8"key") : std::u8string {} };
                if (loop.keys.insert(key).second == false)
                {
                    const ui_markup_position at { node.key.has_value() ? node.key->at : node.at };
                    error(ui_markup_error_kind::duplicate_key, at, loop.node, u8"key", key);
                }
                keys_.push_back(std::move(key));
                loop.in_item = true;
                loop.next = 0;
                return true;
            }

            void end_item(const std::size_t index)
            {
                frame& loop { frames_[index] };
                bindings_.pop_back();
                keys_.pop_back();
                loop.in_item = false;
                ++loop.item_index;
            }

            // frame을 닫는다. 요소면 둘러싼 요소에 담고, 뿌리면 돌려준다.
            [[nodiscard]] std::unique_ptr<ui_element> leave(const std::size_t index)
            {
                if (frames_[index].in_item)
                    end_item(index);
                position_.resize(frames_[index].position_length);
                frame closing { std::move(frames_[index]) };
                frames_.pop_back();
                if (closing.element == nullptr)
                    return nullptr;
                if (frames_.empty())
                    return std::move(closing.element);
                attach(closing.parent_frame, std::move(closing.element), closing.node, closing.item);
                return nullptr;
            }

            // 자식 하나를 본다. `position`은 부모 안의 차례이고 `owner_frame`은 자식을 담을 요소의 frame이다.
            void visit(const std::size_t index, const std::size_t position, const std::size_t owner_frame)
            {
                const ui_markup_node& node { document_.nodes[index] };
                const std::size_t position_length { position_.size() };
                position_ += u8"/" + ui_markup_decimal(position);
                if (is_structural(node.tag))
                {
                    enter_structure(index, owner_frame, position_length);
                    return;
                }
                if (node.tag == ui_markup_tag::gap)
                {
                    add_gap(owner_frame, index);
                    position_.resize(position_length);
                    return;
                }
                if (count() == false)
                {
                    error(ui_markup_error_kind::too_many_built, node.at, index, u8"max_built", ui_markup_decimal(document_.limits.max_built));
                    stopped_ = true;
                    position_.resize(position_length);
                    return;
                }
                if (node.tag == ui_markup_tag::slot)
                {
                    add_slot(owner_frame, index);
                    position_.resize(position_length);
                    return;
                }
                std::unique_ptr<ui_element> element { make_element(index) };
                if (is_container(node.tag))
                {
                    frames_.push_back({ .node = index, .element = std::move(element), .item = item_, .owner_frame = frames_.size(), .parent_frame = owner_frame, .position_length = position_length });
                    return;
                }
                attach(owner_frame, std::move(element), index, item_);
                position_.resize(position_length);
            }

            void enter_structure(const std::size_t index, const std::size_t owner_frame, const std::size_t position_length)
            {
                const ui_markup_node& node { document_.nodes[index] };
                if (node.tag == ui_markup_tag::for_each)
                {
                    ui_markup_list list { node.each.has_value() ? list_of(*node.each, index) : ui_markup_list {} };
                    frames_.push_back({ .node = index, .owner_frame = owner_frame, .position_length = position_length, .list = std::move(list) });
                    return;
                }
                const bool test { node.test.has_value() && boolean_of(*node.test, index, u8"test") };
                if (test == (node.tag == ui_markup_tag::if_true))
                {
                    frames_.push_back({ .node = index, .owner_frame = owner_frame, .position_length = position_length });
                    return;
                }
                position_.resize(position_length);
            }

            // 한 번 펼치기의 element 수를 하나 늘린다. 한도에 닿았으면 거짓이다.
            [[nodiscard]] bool count() noexcept
            {
                if (built_ >= document_.limits.max_built)
                    return false;
                ++built_;
                return true;
            }

            // 펼친 자식을 담는다. 담는 쪽의 꼴은 읽을 때 정했다 (stack 안의 자리, panel의 내용 하나, wrap의 항목).
            void attach(const std::size_t owner_frame, std::unique_ptr<ui_element> child, const std::size_t index, stack_item item)
            {
                frame& owner { frames_[owner_frame] };
                switch (document_.nodes[owner.node].tag)
                {
                case ui_markup_tag::stack:
                    if (document_.nodes[index].placement == ui_markup_placement::fit)
                        item.fit = true;
                    static_cast<stack_element*>(owner.element.get())->add(std::move(child), item);
                    return;
                case ui_markup_tag::wrap:
                    static_cast<wrap_element*>(owner.element.get())->add(std::move(child));
                    return;
                case ui_markup_tag::panel:
                    static_cast<panel_element*>(owner.element.get())->set_content(std::move(child));
                    return;
                default:
                    return;
                }
            }

            void add_gap(const std::size_t owner_frame, const std::size_t index)
            {
                const ui_markup_node& node { document_.nodes[index] };
                ui_markup_values values {};
                apply_attributes(index, values);
                auto* const stack { static_cast<stack_element*>(frames_[owner_frame].element.get()) };
                if (node.placement == ui_markup_placement::flexible)
                    stack->add_flexible_gap(values.item.weight);
                else
                    stack->add_gap(values.item.length);
            }

            void add_slot(const std::size_t owner_frame, const std::size_t index)
            {
                const ui_markup_node& node { document_.nodes[index] };
                ui_markup_values values {};
                apply_attributes(index, values);
                std::unique_ptr<ui_element> element {};
                if (context_.make_slot)
                    element = context_.make_slot(node.slot_name, slot_scope());
                if (element != nullptr)
                {
                    attach(owner_frame, std::move(element), index, values.item);
                    return;
                }
                --built_;
                error(ui_markup_error_kind::unknown_slot, node.slot_name_at, index, node.slot_name, {});
                // 자리는 빈 채로 남긴다. 앱이 slot을 늦게 채워도 둘레의 배치가 들썩이지 않는다.
                if (document_.nodes[frames_[owner_frame].node].tag != ui_markup_tag::stack)
                    return;
                auto* const stack { static_cast<stack_element*>(frames_[owner_frame].element.get()) };
                if (node.placement == ui_markup_placement::flexible)
                    stack->add_flexible_gap(values.item.weight);
                else if (node.placement == ui_markup_placement::fixed)
                    stack->add_gap(values.item.length);
            }

            // slot이 받는 표다. 바깥 표 위에 둘러싼 반복의 항목을 바깥부터 덮는다 (안쪽이 이긴다).
            [[nodiscard]] const ui_markup_scope& slot_scope()
            {
                if (bindings_.empty())
                    return context_.values != nullptr ? *context_.values : empty_scope_;
                merged_scope_ = context_.values != nullptr ? *context_.values : ui_markup_scope {};
                for (const loop_binding& binding : bindings_)
                {
                    for (const auto& [name, value] : binding.item->entries())
                        merged_scope_.set(name, value);
                }
                return merged_scope_;
            }

            // 요소 하나를 짓는다. 자식은 담지 않는다.
            [[nodiscard]] std::unique_ptr<ui_element> make_element(const std::size_t index)
            {
                const ui_markup_node& node { document_.nodes[index] };
                ui_markup_values values { ui_markup_default_values(node.tag) };
                apply_attributes(index, values);
                item_ = values.item;
                const std::u8string owner { owner_of(node) };
                const ui_element_id id { node.tag == ui_markup_tag::check ? ui_element_kind::check : kind_of(node, index), owner };
                std::optional<input_action> action {};
                if (node.action.empty() == false)
                    action = make_action(node, index, id);

                std::unique_ptr<ui_element> element {};
                switch (node.tag)
                {
                case ui_markup_tag::stack:
                    element = std::make_unique<stack_element>(id, std::get<stack_config>(std::move(values.config)));
                    break;
                case ui_markup_tag::wrap:
                    element = std::make_unique<wrap_element>(id, std::get<wrap_config>(std::move(values.config)));
                    break;
                case ui_markup_tag::panel:
                    element = std::make_unique<panel_element>(id, std::get<panel_config>(std::move(values.config)));
                    break;
                case ui_markup_tag::label:
                    element = std::make_unique<label_element>(id, std::get<label_config>(std::move(values.config)));
                    break;
                case ui_markup_tag::progress:
                    element = std::make_unique<progress_element>(id, std::get<progress_config>(std::move(values.config)));
                    break;
                case ui_markup_tag::text_button:
                {
                    auto button { std::make_unique<text_button_element>(id, std::get<text_button_config>(std::move(values.config))) };
                    button->set_cursor(ui_cursor::hand);
                    if (action.has_value())
                        button->set_action(ui_trigger::left_click, action_of(std::move(*action)));
                    element = std::move(button);
                    break;
                }
                case ui_markup_tag::check:
                {
                    check_config config { std::get<check_config>(std::move(values.config)) };
                    config.owner = owner;
                    // 액션이 없으면 체크는 눌리지 않는다 (check_config::toggle).
                    if (action.has_value())
                        config.toggle = action_of(std::move(*action));
                    element = std::make_unique<check_element>(std::move(config));
                    break;
                }
                default:
                    return nullptr;
                }
                element->set_enabled(values.common.enabled);
                element->set_visible(values.common.visible);
                if (values.common.tooltip.empty() == false)
                    element->set_tooltip(std::move(values.common.tooltip));
                if (values.common.access_name.empty() == false)
                    element->set_access_name(std::move(values.common.access_name));
                // 앱이 모르는 사건이면 단추는 비활성이다. 누르는 시늉만 하는 단추를 두지 않는다.
                if (node.tag == ui_markup_tag::text_button && node.action.empty() == false && action.has_value() == false)
                    element->set_enabled(false);
                return element;
            }

            // 사건 이름을 앱 메시지로 바꾼다. 앱 함수는 여기서 한 번만 불린다. 모르는 이름이면 비어 있다.
            [[nodiscard]] std::optional<input_action> make_action(const ui_markup_node& node, const std::size_t index, const ui_element_id& id)
            {
                const std::u8string argument { node.argument.has_value() ? text_of(*node.argument, index, u8"argument") : std::u8string {} };
                input_action made {};
                if (context_.make_action)
                    made = context_.make_action({ .name = node.action, .argument = argument, .id = id });
                if (std::holds_alternative<std::monostate>(made))
                {
                    error(ui_markup_error_kind::unknown_action, node.action_at, index, node.action, {});
                    return std::nullopt;
                }
                return made;
            }

            [[nodiscard]] static ui_action action_of(input_action action)
            {
                return [action = std::move(action)](const ui_action_context&) -> std::vector<input_action> {
                    return { action };
                };
            }

            [[nodiscard]] ui_element_kind kind_of(const ui_markup_node& node, const std::size_t index)
            {
                if (node.kind.empty())
                    return ui_element_kind::markup_node;
                for (const inspector_kind_name& entry : context_.kinds)
                {
                    if (entry.name == node.kind)
                        return entry.kind;
                }
                error(ui_markup_error_kind::unknown_kind, node.kind_at, index, node.kind, {});
                return ui_element_kind::markup_node;
            }

            // owner는 `id`이고, 없으면 뿌리부터 센 자식 차례의 자리(`/0/2`)다. 반복 안이면 바깥 key부터 단위 구분자로 잇는다.
            [[nodiscard]] std::u8string owner_of(const ui_markup_node& node) const
            {
                std::u8string owner {};
                if (context_.owner_prefix.empty() == false)
                {
                    owner = context_.owner_prefix;
                    owner.push_back(unit_separator);
                }
                // 뿌리는 `/`다. 자식부터 `/0`처럼 차례를 잇는다.
                if (node.id.empty() == false)
                    owner += node.id;
                else if (position_.empty())
                    owner.push_back(u8'/');
                else
                    owner += position_;
                for (const std::u8string& key : keys_)
                {
                    owner.push_back(unit_separator);
                    owner += key;
                }
                return owner;
            }

            // 요소의 기본값 위에 속성을 표의 행 순서대로 건다. 바인딩은 찾은 값으로 건다.
            void apply_attributes(const std::size_t index, ui_markup_values& values)
            {
                for (const ui_markup_attribute& attribute : document_.nodes[index].attributes)
                {
                    if (attribute.row == nullptr)
                        continue;
                    if (attribute.bound() == false)
                    {
                        attribute.row->apply(values, attribute.literal);
                        continue;
                    }
                    const std::u8string name { widened(attribute.row->name) };
                    switch (attribute.row->type)
                    {
                    case ui_markup_value_type::text:
                        attribute.row->apply(values, text_of(attribute, index, name));
                        break;
                    case ui_markup_value_type::boolean:
                        attribute.row->apply(values, boolean_of(attribute, index, name));
                        break;
                    case ui_markup_value_type::number:
                        if (const std::optional<double> number { number_of(attribute, *attribute.row, index, name) }; number.has_value())
                            attribute.row->apply(values, *number);
                        break;
                    case ui_markup_value_type::choice:
                        break;
                    }
                }
            }

            // 이름 하나를 찾는다. 반복 변수(`{item.title}`)는 그 반복의 항목만, 이름(`{title}`)은 안쪽 항목부터 바깥 표까지 찾는다.
            [[nodiscard]] const ui_markup_value* lookup(const ui_markup_reference& reference) const noexcept
            {
                for (auto binding { bindings_.rbegin() }; binding != bindings_.rend(); ++binding)
                {
                    if (reference.variable.empty() == false && binding->variable != reference.variable)
                        continue;
                    if (const ui_markup_value* const value { binding->item->find(reference.name) }; value != nullptr || reference.variable.empty() == false)
                        return value;
                }
                if (reference.variable.empty() && context_.values != nullptr)
                    return context_.values->find(reference.name);
                return nullptr;
            }

            [[nodiscard]] static std::u8string written(const ui_markup_reference& reference)
            {
                if (reference.variable.empty())
                    return reference.name;
                return reference.variable + u8"." + reference.name;
            }

            // 값 하나를 찾는다. 없으면 `unknown_value`, 형이 틀리면 `wrong_value_type`을 남기고 비어 있다.
            template <class value_type>
            [[nodiscard]] const value_type* typed(const ui_markup_attribute& attribute, const ui_markup_reference& reference, const std::size_t index, const std::u8string_view name)
            {
                const ui_markup_value* const value { lookup(reference) };
                if (value == nullptr)
                {
                    error(ui_markup_error_kind::unknown_value, attribute.at, index, written(reference), {});
                    return nullptr;
                }
                const value_type* const held { std::get_if<value_type>(value) };
                if (held == nullptr)
                    error(ui_markup_error_kind::wrong_value_type, attribute.at, index, name, written(reference));
                return held;
            }

            // 글 속성이다. 적은 글과 끼운 글을 잇는다. 하나라도 찾지 못하면 속성 전체가 빈 글이다.
            [[nodiscard]] std::u8string text_of(const ui_markup_attribute& attribute, const std::size_t index, const std::u8string_view name)
            {
                if (attribute.bound() == false)
                {
                    const std::u8string* const literal { std::get_if<std::u8string>(&attribute.literal) };
                    return literal != nullptr ? *literal : std::u8string {};
                }
                std::u8string text {};
                bool failed { false };
                for (const ui_markup_piece& piece : attribute.pieces)
                {
                    if (piece.reference.has_value() == false)
                    {
                        text += piece.text;
                        continue;
                    }
                    if (const std::u8string* const value { typed<std::u8string>(attribute, *piece.reference, index, name) }; value != nullptr)
                        text += *value;
                    else
                        failed = true;
                }
                return failed ? std::u8string {} : text;
            }

            [[nodiscard]] bool boolean_of(const ui_markup_attribute& attribute, const std::size_t index, const std::u8string_view name)
            {
                if (attribute.bound() == false)
                {
                    const bool* const literal { std::get_if<bool>(&attribute.literal) };
                    return literal != nullptr && *literal;
                }
                const bool* const value { typed<bool>(attribute, *attribute.pieces.front().reference, index, name) };
                return value != nullptr && *value;
            }

            // 수 속성이다. 그 행의 범위 밖이면 `wrong_value_type`이고 비어 있어 요소의 기본값이 남는다.
            //  - 0~1(`value`·`shadow`)은 요소처럼 자른다. 그 밖은 자르지 않는다. 0 이하의 `fontSize`나 음수 `maxLines`를 짓지 않는다.
            [[nodiscard]] std::optional<double> number_of(const ui_markup_attribute& attribute, const ui_markup_row& row, const std::size_t index, const std::u8string_view name)
            {
                const ui_markup_reference& reference { *attribute.pieces.front().reference };
                const double* const value { typed<double>(attribute, reference, index, name) };
                if (value == nullptr)
                    return std::nullopt;
                const double number { *value };
                const bool finite { std::isfinite(number) && std::fabs(number) <= static_cast<double>(std::numeric_limits<float>::max()) };
                if (finite && row.rule == ui_markup_number_rule::unit)
                    return std::clamp(number, 0.0, 1.0);
                bool fits { finite };
                switch (row.rule)
                {
                case ui_markup_number_rule::non_negative:
                    fits = fits && std::signbit(number) == false;
                    break;
                case ui_markup_number_rule::positive:
                    fits = fits && number > 0.0;
                    break;
                case ui_markup_number_rule::unit:
                    break;
                case ui_markup_number_rule::count:
                    fits = fits && std::signbit(number) == false && number == std::floor(number) && number <= 4294967295.0;
                    break;
                }
                if (fits == false)
                {
                    error(ui_markup_error_kind::wrong_value_type, attribute.at, index, name, written(reference));
                    return std::nullopt;
                }
                return number;
            }

            [[nodiscard]] ui_markup_list list_of(const ui_markup_attribute& attribute, const std::size_t index)
            {
                const ui_markup_list* const value { typed<ui_markup_list>(attribute, *attribute.pieces.front().reference, index, u8"each") };
                return value != nullptr ? *value : ui_markup_list {};
            }

            // 오류 하나를 남긴다. 반복이 같은 자리에서 같은 실수를 되풀이하면 하나만 둔다.
            void error(const ui_markup_error_kind kind, const ui_markup_position& at, const std::size_t index, const std::u8string_view name, const std::u8string_view value)
            {
                ui_markup_error made { ui_markup_error_at(kind, at, {}, name, value) };
                if (seen_.insert({ made.offset, made.kind, made.name, made.value }).second == false)
                    return;
                made.path = ui_markup_path(document_.nodes, index);
                errors_.push_back(std::move(made));
            }

            const ui_markup_document& document_;
            const ui_markup_context& context_;
            std::vector<frame> frames_ {};
            std::vector<loop_binding> bindings_ {};
            // 둘러싼 반복의 지금 key다. 바깥부터다.
            std::vector<std::u8string> keys_ {};
            // 뿌리부터 센 자식 차례의 자리다 (`/0/2`). 뿌리에서는 비어 있다.
            std::u8string position_ {};
            // 마지막으로 지은 요소의 stack 안 자리다. 지은 직후 담거나 frame에 옮긴다.
            stack_item item_ {};
            std::vector<ui_markup_error> errors_ {};
            std::set<std::tuple<std::size_t, ui_markup_error_kind, std::u8string, std::u8string>> seen_ {};
            std::size_t built_ { 0 };
            bool stopped_ { false };
            ui_markup_scope merged_scope_ {};
            const ui_markup_scope empty_scope_ {};
        };
    }

    ui_markup_build_result build_ui_markup(const ui_markup_document& document, const ui_markup_context& context)
    {
        markup_builder builder { document, context };
        return builder.run();
    }
}
