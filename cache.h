#pragma once
#ifndef __evx_cache_h
#define __evx_cache_h

#include <list>
#include <map>
#include <netinet/in.h>
#include "pool.h"
#include "buffer_string.h"

struct DomainName
{
    static const unsigned max_name = 253;
    unsigned char length = 0;
    union name_union
    {
        name_union() {}
        char data[max_name];
        /* To avoid string copying when finding,
           we may use external buffer 'str' (used when length == 0).
         */
        buffer::istring str;
    } name;

    buffer::istring str() const
    {
        return length ? buffer::istring(name.data, length) : name.str;
    }

    bool operator< (const DomainName &op) const
    {
        // std::map can do 1 extra comparison:
        // a < b, if false, then b < a (second one could be spared
        // if map used 'cmp' semantics).
        return str() < op.str();
    }

    DomainName(buffer::istring _name, bool copy = true)
    {
        if (_name.size() > max_name)
            throw std::invalid_argument("Domain name is too long!");

        if (copy) {
            length = (unsigned char) _name.size();
            _name.copy(name.data, max_name);
        } else {
            length = 0;
            name.str = _name;
        }
    }

    DomainName(const DomainName &src)
    {
        // copy-constructor always copies data!
        buffer::istring _name = src.str();
        length = _name.size();
        _name.copy(name.data, max_name);
    }
};

struct DomainValue
{
    typedef std::list<void *>::iterator proxy_iterator;
    proxy_iterator list_it;
    time_t ctime;
    in_addr host_ip;

    DomainValue(in_addr &_host_ip) :
        host_ip {_host_ip}
    {
        ctime = time(nullptr);
    }
};

template <class Alloc>
class NameCache : public std::map<DomainName, DomainValue, std::less<DomainName>, Alloc>
{
    typedef std::map<DomainName, DomainValue, std::less<DomainName>, Alloc> map;
    static size_t max_capacity;
    static time_t item_lifetime;

    typedef std::list<typename map::iterator, Alloc> list;
    typedef typename list::iterator list_iterator;
    list mru; // most-recently-used list

    static const bool NO_COPY = false;

public:
    static
    void init(size_t _max_capacity, time_t _lifetime)
    {
        max_capacity = _max_capacity;
        item_lifetime = _lifetime;
    }

    NameCache()
    {
        assert(max_capacity);
    }

    bool get(in_addr &host_ip, DomainName &name)
    {
        auto it = map::find(name);
        if (it == map::end())
            return false;

        list_iterator *l_it = (list_iterator *) (void *) &it->second.list_it;
        assert(**l_it == it);

        if (it->second.ctime + item_lifetime < time(nullptr)) {
            map::erase(it);
            mru.erase(*l_it);
            return false;
        }

        host_ip = it->second.host_ip;

        // most recently used goes to top:
        mru.splice(mru.begin(), mru, *l_it);
        return true;
    }

    bool get(in_addr &host_ip, buffer::istring &name)
    {
        DomainName d(name, NO_COPY);
        return get(host_ip, d);
    }

    bool get(in_addr &host_ip, const char *name)
    {
        buffer::istring s(name);
        return get(host_ip, s);
    }

    void insert(in_addr &host_ip, DomainName &name)
    {
        list_iterator l_it;
        if (map::size() == max_capacity) {
            // erase least recently used
            l_it = --mru.end();
            map::erase(*l_it);
            mru.erase(l_it);
        }
        std::pair<typename map::iterator, bool> res =
            map::insert(typename map::value_type(name, DomainValue(host_ip)));
        if (!res.second)
            return;
        mru.push_front(res.first);
        l_it = mru.begin();
        res.first->second.list_it = *(DomainValue::proxy_iterator *)(void *)&l_it;
    }

    void insert(in_addr &host_ip, buffer::istring &name)
    {
        DomainName d(name, NO_COPY);
        return insert(host_ip, d);
    }

    void insert(in_addr &host_ip, const char *name)
    {
        buffer::istring s(name);
        return insert(host_ip, s);
    }
};

typedef std::_List_node<std::_Rb_tree_iterator<std::pair<DomainName const, DomainValue> > > ListNode;
typedef std::_Rb_tree_node<std::pair<DomainName const, DomainValue> > MapNode;

struct NameCacheInit
{
    Pool<MapNode> map_pool;
    Pool<ListNode> list_pool;

    NameCacheInit(size_t pool_size, time_t lifetime) :
        map_pool(pool_size),
        list_pool(pool_size)
    {
        PoolAllocator<MapNode>::init(map_pool);
        PoolAllocator<ListNode>::init(list_pool);
        NameCache<PoolAllocator<> >::init(pool_size, lifetime);
    }
};

class NameCacheOnPool : NameCacheInit, public NameCache<PoolAllocator<> >
{
public:
    NameCacheOnPool(size_t pool_size, time_t lifetime) :
        NameCacheInit(pool_size, lifetime)
    {
    }
};

#endif // __evx_cache_h