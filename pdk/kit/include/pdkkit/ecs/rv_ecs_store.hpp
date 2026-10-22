#pragma once

// Component storage of the scene ECS: one sparse set per component type, held by the world.

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "pdk/rv_err.h"
#include "pdkkit/ecs/rv_ecs.hpp"

namespace rv_pdklib
{

// Sparse entry of an entity without the component. No slot index reaches this value.
inline constexpr uint32_t ecs_absent = std::numeric_limits<uint32_t>::max();
// Pool arrays start at this capacity and grow by the factor, so inserts stay amortized constant time.
inline constexpr std::size_t ecs_pool_first_capacity = 1;
inline constexpr std::size_t ecs_pool_growth_factor = 2;

// Numbers each component type on its first use; one counter serves all types.
// The world runs on one thread (the disk frame loop), so the counter is not atomic.
inline std::size_t ecs_issue_type_number()
{
    static std::size_t last = 0;
    return last++;
}

template <typename T>
std::size_t ecs_type_number()
{
    static const std::size_t number = ecs_issue_type_number();
    return number;
}

template <typename T>
struct ecs_pool final : ecs_pool_base {
    // drop() assigns values mid-update; a throwing move would leave the pool half-changed.
    static_assert(std::is_nothrow_move_assignable_v<T>, "component type needs a nothrow move assignment");

    std::vector<ecs_id> ids;      // dense: the holder of each value
    std::vector<T> values;        // dense: component values, parallel to ids
    std::vector<uint32_t> sparse; // slot index -> position in ids, ecs_absent if none

    // Position of id's component, or ecs_absent. A stale generation does not match.
    uint32_t find(ecs_id id) const
    {
        if (id.index >= sparse.size()) {
            return ecs_absent;
        }
        const uint32_t pos = sparse[id.index];
        if (pos == ecs_absent) {
            return ecs_absent;
        }
        return ids[pos] == id ? pos : ecs_absent;
    }

    T *value(ecs_id id)
    {
        const uint32_t pos = find(id);
        return pos == ecs_absent ? nullptr : &values[pos];
    }

    // Preconditions: id holds no T yet and is alive.
    template <typename... Args>
    void insert(ecs_id id, Args &&...args)
    {
        if (id.index >= sparse.size()) {
            sparse.resize(id.index + 1, ecs_absent);
        }
        // Reserve both arrays before any push, so a failed allocation leaves the pool unchanged.
        if (ids.size() == ids.capacity()) {
            const std::size_t grown = ids.capacity() == 0 ? ecs_pool_first_capacity : ecs_pool_growth_factor * ids.capacity();
            ids.reserve(grown);
            values.reserve(grown);
        }
        const uint32_t pos = static_cast<uint32_t>(ids.size());
        values.emplace_back(std::forward<Args>(args)...);
        ids.push_back(id);
        sparse[id.index] = pos;
    }

    // Swap-pop keeps the arrays packed; the move is skipped for the last entry (self-move-assign is unsafe).
    void drop(ecs_id id) override
    {
        const uint32_t pos = find(id);
        if (pos == ecs_absent) {
            return;
        }
        const uint32_t last = static_cast<uint32_t>(ids.size() - 1);
        if (pos != last) {
            ids[pos] = ids[last];
            values[pos] = std::move(values[last]);
            sparse[ids[pos].index] = pos;
        }
        ids.pop_back();
        values.pop_back();
        sparse[id.index] = ecs_absent;
    }

    const std::vector<ecs_id> &holders() const override
    {
        return ids;
    }
};

// Pool of T in the world, or nullptr when no T was ever added.
template <typename T>
ecs_pool<T> *ecs_pool_find(ecs_world &world)
{
    const std::size_t number = ecs_type_number<T>();
    const bool present = number < world.pools.size() && world.pools[number];
    return present ? static_cast<ecs_pool<T> *>(world.pools[number].get()) : nullptr;
}

// Pool of T in the world, created on first use.
template <typename T>
ecs_pool<T> &ecs_pool_ensure(ecs_world &world)
{
    const std::size_t number = ecs_type_number<T>();
    if (number >= world.pools.size()) {
        world.pools.resize(number + 1);
    }
    std::unique_ptr<ecs_pool_base> &pool = world.pools[number];
    if (!pool) {
        pool = std::make_unique<ecs_pool<T>>();
    }
    return static_cast<ecs_pool<T> &>(*pool);
}

// RV_ERR_NOENT for a stale id, RV_ERR_ALREADY when the entity already has a T.
template <typename T, typename... Args>
int ecs_component_add(ecs_world &world, ecs_id id, Args &&...args)
{
    if (!ecs_entity_alive(world, id)) {
        return RV_ERR_NOENT;
    }
    ecs_pool<T> &pool = ecs_pool_ensure<T>(world);
    if (pool.find(id) != ecs_absent) {
        return RV_ERR_ALREADY;
    }
    pool.insert(id, std::forward<Args>(args)...);
    return RV_OK;
}

// RV_ERR_NOENT for a stale id or an id without a T.
template <typename T>
int ecs_component_remove(ecs_world &world, ecs_id id)
{
    if (!ecs_entity_alive(world, id)) {
        return RV_ERR_NOENT;
    }
    ecs_pool<T> *pool = ecs_pool_find<T>(world);
    const bool held = pool && pool->find(id) != ecs_absent;
    if (!held) {
        return RV_ERR_NOENT;
    }
    pool->drop(id);
    return RV_OK;
}

// Value of T held by id, or nullptr for a stale id or a missing T.
template <typename T>
T *ecs_component_get(ecs_world &world, ecs_id id)
{
    ecs_pool<T> *pool = ecs_pool_find<T>(world);
    return pool ? pool->value(id) : nullptr;
}

// Pool with the fewest holders; nullptr when a type has no pool, as nothing can match then.
template <std::size_t N>
const ecs_pool_base *ecs_smallest_pool(const std::array<const ecs_pool_base *, N> &pools)
{
    const ecs_pool_base *smallest = nullptr;
    for (const ecs_pool_base *pool : pools) {
        if (!pool) {
            return nullptr;
        }
        if (!smallest || pool->holders().size() < smallest->holders().size()) {
            smallest = pool;
        }
    }
    return smallest;
}

// Calls fn(id, Ts &...) for each live entity that holds all listed types, walking the smallest pool.
// A type with no pool visits nothing. fn must not add/remove these types or destroy entities.
template <typename... Ts, typename Fn>
void ecs_component_each(ecs_world &world, Fn &&fn)
{
    const std::tuple<ecs_pool<Ts> *...> pools{ ecs_pool_find<Ts>(world)... };
    const std::array<const ecs_pool_base *, sizeof...(Ts)> bases = std::apply(
        [](auto... pool) {
            return std::array<const ecs_pool_base *, sizeof...(Ts)>{ pool... };
        },
        pools);
    const ecs_pool_base *driver = ecs_smallest_pool(bases);
    if (!driver) {
        return;
    }
    for (std::size_t i = 0; i < driver->holders().size(); ++i) {
        const ecs_id id = driver->holders()[i];
        const std::tuple<Ts *...> values = std::apply(
            [id](auto... pool) {
                return std::tuple<Ts *...>{ pool->value(id)... };
            },
            pools);
        const bool complete = std::apply(
            [](auto... value) {
                return (value && ...);
            },
            values);
        if (!complete) {
            continue;
        }
        std::apply(
            [&fn, id](auto... value) {
                fn(id, *value...);
            },
            values);
    }
}

} // namespace rv_pdklib
