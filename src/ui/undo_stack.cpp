#include "luil/ui/undo_stack.h"

#include <algorithm>
#include <utility>

namespace luil {
    namespace {
        // 재생 중이라는 표시다. `apply`가 예외를 던져도 풀려 stack이 영영 잠기지 않는다.
        class replay_guard
        {
        public:
            explicit replay_guard(bool& flag) noexcept
                : flag_ { flag }
            {
                flag_ = true;
            }

            ~replay_guard()
            {
                flag_ = false;
            }

            replay_guard(const replay_guard&) = delete;
            replay_guard& operator=(const replay_guard&) = delete;

        private:
            bool& flag_;
        };
    }

    undo_stack::undo_stack(const undo_stack_limits limits)
        : limits_ { limits }
    {
    }

    bool undo_stack::push(undo_step step)
    {
        if (replaying_ || step.undo.empty() || step.redo.empty())
            return false;
        if (macro_depth_ > 0)
        {
            // 묶음 안의 걸음은 합치지 않는다. 키도 무시한다.
            macro_.undo.push_back(std::move(step.undo));
            macro_.redo.push_back(std::move(step.redo));
            macro_.weight += step.weight;
            return true;
        }

        // 저장 지점에 서 있을 때 합치면 저장한 상태로 돌아갈 수 없다.
        if (merge_armed_
            && step.merge_key.empty() == false
            && undo_.empty() == false
            && undo_.back().merge_key == step.merge_key
            && clean() == false)
        {
            // 앞 걸음의 되돌림과 뒤 걸음의 다시 실행이 남는다. 합친 걸음은 메시지가 하나씩이다.
            record& last { undo_.back() };
            const std::size_t weight { std::max(last.weight, step.weight) };
            bytes_ = bytes_ - last.weight + weight;
            last.weight = weight;
            last.label = std::move(step.label);
            last.redo.clear();
            last.redo.push_back(std::move(step.redo));
            return true;
        }

        const bool armed { step.merge_key.empty() == false };
        record entry { std::move(step.label), {}, {}, std::move(step.merge_key), step.weight };
        entry.undo.push_back(std::move(step.undo));
        entry.redo.push_back(std::move(step.redo));
        append(std::move(entry));
        merge_armed_ = armed;
        return true;
    }

    void undo_stack::begin(std::u8string label)
    {
        if (replaying_)
            return;
        merge_armed_ = false;
        if (macro_depth_++ == 0)
        {
            macro_ = {};
            macro_.label = std::move(label);
        }
    }

    bool undo_stack::end()
    {
        if (replaying_ || macro_depth_ == 0)
            return false;
        if (--macro_depth_ > 0)
            return true;
        record entry { std::exchange(macro_, record {}) };
        if (entry.undo.empty())
            return true;
        append(std::move(entry));
        return true;
    }

    bool undo_stack::undo(const undo_apply& apply)
    {
        if (replaying_ || macro_depth_ > 0 || apply == nullptr || undo_.empty())
            return false;
        // 위치를 먼저 옮긴다. 재생 중에 stack을 보는 질의가 이미 옮긴 자리를 본다.
        bytes_ -= undo_.back().weight;
        redo_.push_back(std::move(undo_.back()));
        undo_.pop_back();
        merge_armed_ = false;

        const record& step { redo_.back() };
        const replay_guard guard { replaying_ };
        for (std::size_t index { step.undo.size() }; index > 0; --index)
            apply(step.undo[index - 1]);
        return true;
    }

    bool undo_stack::redo(const undo_apply& apply)
    {
        if (replaying_ || macro_depth_ > 0 || apply == nullptr || redo_.empty())
            return false;
        bytes_ += redo_.back().weight;
        undo_.push_back(std::move(redo_.back()));
        redo_.pop_back();
        merge_armed_ = false;
        // 상한을 줄인 뒤에 돌아온 걸음도 되돌릴 쪽의 상한을 지킨다.
        trim();

        const record& step { undo_.back() };
        const replay_guard guard { replaying_ };
        for (const app_message& message : step.redo)
            apply(message);
        return true;
    }

    bool undo_stack::can_undo() const noexcept
    {
        return macro_depth_ == 0 && undo_.empty() == false;
    }

    bool undo_stack::can_redo() const noexcept
    {
        return macro_depth_ == 0 && redo_.empty() == false;
    }

    std::u8string_view undo_stack::undo_label() const noexcept
    {
        return can_undo() ? std::u8string_view { undo_.back().label } : std::u8string_view {};
    }

    std::u8string_view undo_stack::redo_label() const noexcept
    {
        return can_redo() ? std::u8string_view { redo_.back().label } : std::u8string_view {};
    }

    std::size_t undo_stack::undo_count() const noexcept
    {
        return undo_.size();
    }

    std::size_t undo_stack::redo_count() const noexcept
    {
        return redo_.size();
    }

    std::size_t undo_stack::bytes() const noexcept
    {
        return bytes_;
    }

    void undo_stack::break_merge() noexcept
    {
        if (replaying_)
            return;
        merge_armed_ = false;
    }

    void undo_stack::set_clean() noexcept
    {
        if (replaying_ || macro_depth_ > 0)
            return;
        clean_ = undo_.size();
        merge_armed_ = false;
    }

    bool undo_stack::clean() const noexcept
    {
        // 열린 묶음에 담긴 메시지는 앱이 이미 적용했으므로 상태가 저장한 자리를 떠났다.
        return macro_.undo.empty() && clean_.has_value() && *clean_ == undo_.size();
    }

    void undo_stack::clear() noexcept
    {
        if (replaying_)
            return;
        undo_.clear();
        redo_.clear();
        bytes_ = 0;
        clean_ = 0;
        merge_armed_ = false;
        macro_depth_ = 0;
        macro_ = {};
    }

    void undo_stack::set_limits(const undo_stack_limits limits)
    {
        if (replaying_)
            return;
        limits_ = limits;
        trim();
    }

    void undo_stack::append(record entry)
    {
        discard_redo();
        bytes_ += entry.weight;
        undo_.push_back(std::move(entry));
        merge_armed_ = false;
        trim();
    }

    void undo_stack::discard_redo() noexcept
    {
        // 저장 지점이 버려질 redo 쪽에 있었으면 그 상태로는 돌아갈 길이 없다.
        if (clean_.has_value() && *clean_ > undo_.size())
            clean_.reset();
        redo_.clear();
    }

    void undo_stack::trim()
    {
        while (undo_.size() > 1
            && ((limits_.steps != 0 && undo_.size() > limits_.steps)
                || (limits_.bytes != 0 && bytes_ > limits_.bytes)))
        {
            bytes_ -= undo_.front().weight;
            undo_.pop_front();
            if (clean_.has_value())
            {
                // 맨 아래 걸음이 버려지면 걸음 없는 자리가 사라진다.
                if (*clean_ == 0)
                    clean_.reset();
                else
                    --*clean_;
            }
        }
    }

    undo_macro::undo_macro(undo_stack& stack, std::u8string label)
        : stack_ { stack }
    {
        stack_.begin(std::move(label));
    }

    undo_macro::~undo_macro()
    {
        static_cast<void>(stack_.end());
    }
}
