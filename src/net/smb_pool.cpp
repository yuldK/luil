#include "net/smb_pool.h"

#include <algorithm>
#include <utility>

namespace luil::net {
    namespace {
        // 지운 자리가 최적화로 빠지지 않게 volatile로 쓴다.
        void wipe(std::u8string& text) noexcept
        {
            volatile char8_t* const data { text.data() };
            for (std::size_t index { 0 }; index < text.size(); ++index)
                data[index] = u8'\0';
            text.clear();
        }

        [[nodiscard]] std::u8string lowered(const std::u8string_view text)
        {
            std::u8string result { text };
            for (char8_t& character : result)
                if (character >= u8'A' && character <= u8'Z')
                    character = static_cast<char8_t>(character - u8'A' + u8'a');
            return result;
        }
    } // namespace

    std::u8string smb_server_key(const std::u8string_view server)
    {
        constexpr std::u8string_view default_port { u8":445" };
        std::u8string key { lowered(server) };
        // 괄호 친 IPv6 주소이거나 콜론이 하나뿐일 때만 끝이 포트다. 괄호 없는 IPv6 주소의 끝 조각은 포트가 아니다.
        const bool bracketed { key.empty() == false && key.front() == u8'[' };
        const bool one_colon { key.find(u8':') != std::u8string::npos && key.find(u8':') == key.rfind(u8':') };
        if ((bracketed || one_colon) && key.ends_with(default_port))
        {
            const std::size_t host_end { key.size() - default_port.size() };
            if (bracketed == false || (host_end > 0 && key[host_end - 1] == u8']'))
                key.resize(host_end);
        }
        return key;
    }

    smb_connection_key::~smb_connection_key()
    {
        wipe(password);
    }

    smb_connection_key make_smb_connection_key(const smb_location& location, const std::u8string_view share)
    {
        smb_connection_key key {};
        key.server = smb_server_key(location.server);
        key.share = share;
        key.domain = location.domain;
        key.user = location.user.empty() ? std::u8string { u8"guest" } : location.user;
        key.password = location.password;
        key.encrypt = location.encrypt;
        return key;
    }

    smb_connection_pool::smb_connection_pool(const smb_pool_config config) noexcept
        : config_ { config }
    {}

    std::optional<smb_lease> smb_connection_pool::acquire(const smb_connection_key& key, const time_point now, const bool fresh, std::vector<void*>& evicted)
    {
        if (closed_)
            return std::nullopt;
        // 쉬는 시간이 지난 연결은 다시 쓰지 않는다. 장부에서 빼는 것은 `expire`의 일이지만, 그 사이에 빌려 가지 않게
        // 여기서도 거른다.
        const auto fresh_enough = [this, now](const entry& item) { return now - item.idle_since < config_.idle_timeout; };
        if (fresh == false)
        {
            for (entry& item : entries_)
                if (item.busy == false && item.key == key && fresh_enough(item))
                {
                    item.busy = true;
                    item.generation = generation_;
                    return smb_lease { item.id, item.handle, true };
                }
        }
        const auto same_server = [&key](const entry& item) { return item.key.server == key.server; };
        if (static_cast<std::size_t>(std::ranges::count_if(entries_, same_server)) >= config_.max_per_server)
        {
            // 상한이 찼다. 그 서버의 쉬는 연결 가운데 가장 오래 쉰 것을 비운다.
            auto oldest { entries_.end() };
            for (auto item { entries_.begin() }; item != entries_.end(); ++item)
                if (item->busy == false && same_server(*item) && (oldest == entries_.end() || item->idle_since < oldest->idle_since))
                    oldest = item;
            if (oldest == entries_.end())
                return std::nullopt;
            evicted.push_back(oldest->handle);
            entries_.erase(oldest);
        }
        entry item {};
        item.id = ++next_id_;
        item.key = key;
        item.generation = generation_;
        entries_.push_back(std::move(item));
        return smb_lease { next_id_, nullptr, false };
    }

    void smb_connection_pool::attach(const std::uint64_t id, void* const handle) noexcept
    {
        for (entry& item : entries_)
            if (item.id == id)
                item.handle = handle;
    }

    void smb_connection_pool::release(const std::uint64_t id, const bool keep, const time_point now, std::vector<void*>& closed)
    {
        const auto found { std::ranges::find(entries_, id, &entry::id) };
        if (found == entries_.end())
            return;
        const bool rest { keep && found->handle != nullptr && closed_ == false && config_.idle_timeout.count() > 0 && found->generation == generation_ };
        if (rest)
        {
            found->busy = false;
            found->idle_since = now;
            return;
        }
        if (found->handle != nullptr)
            closed.push_back(found->handle);
        entries_.erase(found);
    }

    std::optional<smb_connection_pool::time_point> smb_connection_pool::expire(const time_point now, std::vector<void*>& closed)
    {
        std::optional<time_point> next {};
        for (auto item { entries_.begin() }; item != entries_.end();)
        {
            if (item->busy)
            {
                ++item;
                continue;
            }
            const time_point due { item->idle_since + config_.idle_timeout };
            if (now >= due)
            {
                closed.push_back(item->handle);
                item = entries_.erase(item);
                continue;
            }
            if (next.has_value() == false || due < *next)
                next = due;
            ++item;
        }
        return next;
    }

    void smb_connection_pool::close_idle(std::vector<void*>& closed)
    {
        ++generation_;
        for (auto item { entries_.begin() }; item != entries_.end();)
        {
            if (item->busy)
            {
                ++item;
                continue;
            }
            closed.push_back(item->handle);
            item = entries_.erase(item);
        }
    }

    void smb_connection_pool::close_all(std::vector<void*>& closed)
    {
        closed_ = true;
        close_idle(closed);
    }

    std::size_t smb_connection_pool::idle_count() const noexcept
    {
        return static_cast<std::size_t>(std::ranges::count_if(entries_, [](const entry& item) { return item.busy == false; }));
    }
} // namespace luil::net
