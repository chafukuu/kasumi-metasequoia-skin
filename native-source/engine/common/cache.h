#pragma once

#include <list>
#include <optional>
#include <unordered_map>

template <typename Key, typename Value> class CircularBuffer
{
  public:
    explicit CircularBuffer(size_t capacity) : _capacity(capacity)
    {
    }

    void insert(const Key &key, const Value &value)
    {
        auto it = _map.find(key);

        if (it != _map.end())
        {
            it->second.first = value;
            return;
        }

        if (_list.size() >= _capacity)
        {
            const Key &oldest_key = _list.front();
            _map.erase(oldest_key);
            _list.pop_front();
        }

        _list.push_back(key);
        _map[key] = {value, std::prev(_list.end())};
    }

    // 命中时把条目挪到最近使用端（真正的 LRU 淘汰），返回指向缓存值的指针而不复制。
    // 候选列表一份就是几百个 WordItem，每键的热路径上应当用它而不是 get()。
    // 指针在下一次 insert / remove / clear 之前有效。
    const Value *find(const Key &key)
    {
        auto it = _map.find(key);
        if (it == _map.end())
        {
            return nullptr;
        }
        _list.splice(_list.end(), _list, it->second.second);
        return &it->second.first;
    }

    std::optional<Value> get(const Key &key) const
    {
        auto it = _map.find(key);
        if (it != _map.end())
        {
            return it->second.first;
        }
        return std::nullopt;
    }

    bool remove(const Key &key)
    {
        auto it = _map.find(key);
        if (it == _map.end())
            return false;

        _list.erase(it->second.second);
        _map.erase(it);
        return true;
    }

    void clear()
    {
        _map.clear();
        _list.clear();
    }

    size_t size() const
    {
        return _map.size();
    }

    bool empty() const
    {
        return _map.empty();
    }

  private:
    size_t _capacity;
    std::list<Key> _list;
    std::unordered_map<Key, std::pair<Value, typename std::list<Key>::iterator>> _map;
};
