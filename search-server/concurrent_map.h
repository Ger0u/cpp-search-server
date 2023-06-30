#include <vector>
#include <map>
#include <mutex>

using namespace std::string_literals;

template <typename Key, typename Value>
class ConcurrentMap {
private:
    struct MapWithLock {
        std::map<uint64_t, Value> map;
        std::mutex mutex;
    };
    
public:
    static_assert(std::is_integral_v<Key>, "ConcurrentMap supports only integer keys"s);

    struct Access {
        std::lock_guard<std::mutex> guard;
        Value& ref_to_value;
    
        Access(MapWithLock& map_with_lock, uint64_t ukey)
        : guard(map_with_lock.mutex)
        , ref_to_value(map_with_lock.map[ukey]) {
        }
    };

    explicit ConcurrentMap(size_t bucket_count)
    : maps_(bucket_count) {
    }

    Access operator[](const Key& key) {
        uint64_t ukey = key;
        return {maps_[ukey % maps_.size()], ukey};
    }
    
    void erase(const Key& key) {
        uint64_t ukey = key;
        auto& map_with_lock = maps_[ukey % maps_.size()];
        std::lock_guard guard(map_with_lock.mutex);
        map_with_lock.map.erase(ukey);
    }

    std::map<Key, Value> BuildOrdinaryMap() {
        std::map<Key, Value> result;
        for (auto& [map, mutex] : maps_) {
            std::lock_guard guard(mutex);
            result.insert(map.begin(), map.end());
        }
        return result;
    }

private:
    std::vector<MapWithLock> maps_;
};