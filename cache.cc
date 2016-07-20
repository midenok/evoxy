#include "cache.h"

template<>
thread_local Pool<ListNode>*
PoolAllocator<ListNode>::pool = nullptr;

template<>
thread_local Pool<MapNode>*
PoolAllocator<MapNode>::pool = nullptr;

template<>
size_t
NameCache<PoolAllocator<> >::max_capacity = 0;

template<>
time_t
NameCache<PoolAllocator<> >::item_lifetime = 0;
