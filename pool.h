#ifndef __cd_pool_h
#define __cd_pool_h

#include <vector>
#include <cstddef>
#include <cassert>

template <class Object>
union PoolNode
{
    char data[sizeof(Object)];
    PoolNode *next;
};

struct PoolIface
{
    virtual void add_pool(size_t size) = 0;
    virtual ~PoolIface() {}
};

template <class Object>
class NoGrow : public PoolIface
{
public:
    void init(size_t capacity)
    {
        add_pool(capacity);
    }

    static
    void on_get(PoolNode<Object> *free) throw (std::bad_alloc)
    {
        if (!free)
            throw std::bad_alloc();
    }
};

template <class Object, class GrowPolicy = NoGrow<Object> >
class Pool : public GrowPolicy
{
    typedef PoolNode<Object> Node;
    typedef std::pair<Node *, size_t> PoolItem;
    std::vector<PoolItem> pools;
    Node *free = nullptr;
    Node *last_ = nullptr;

    void add_pool(size_t size) override
    {
        free = new Node[size];

        // form a linked list of blocks of this pool
        pools.push_back(PoolItem(free, size));
        for (int i = 0; i < size; ++i) {
            free[i].next = &free[i + 1];
        }
        free[size - 1].next = nullptr;
    }

public:
    Pool(const Pool&) = delete;
    Pool(Pool&&) = delete;

    Pool(size_t capacity)
    {
        GrowPolicy::init(capacity);
    }

    static
    size_t
    memsize(size_t capacity, bool gross = false)
    {
        return (gross ? sizeof(Pool) : 0) + sizeof(Node) * capacity;
    }

    size_t
    memusage(bool gross = false)
    {
        size_t result = gross ? sizeof(Pool) + pools.capacity() : 0;
        for (auto item: pools) {
            result += sizeof(Node) * item.second;
        }
        return result;
    }

    size_t
    free_chunks() const
    {
        size_t chunks = 0;
        for (Node *n = free; n; n = n->next)
            chunks++;
        return chunks;
    }

    void*
    get() throw (std::bad_alloc)
    {
        GrowPolicy::on_get(free);
        last_ = free;
        free = free->next;
        return static_cast<void*>(last_);
    }

    void*
    last() const
    {
        return static_cast<void*>(last_);
    }

    void
    release(void * block)
    {
        Node* node = static_cast<Node*>(block);
        node->next = free;
        free = node;
    }

    ~Pool()
    {
        for (auto item : pools) {
            delete item.first;
        }
    }
};

#define INIT_POOL(Object) \
template<> \
thread_local Pool<Object>* OnPool<Object>::pool = nullptr;

template <class Object>
class OnPool
{
    static thread_local Pool<Object> *pool;

public:
    void * operator new(size_t bytes, Pool<Object> &_pool) throw(std::bad_alloc)
    {
        assert(bytes == sizeof(Object));
        if (!pool)
            pool = &_pool;
        else
            assert(pool == &_pool);
        return _pool.get();
    }

    void operator delete (void * addr)
    {
        pool->release(addr);
    }

    void release()
    {
        delete static_cast<Object *>(this);
    }
};

template <class Object = int>
class PoolAllocator
{
    static thread_local Pool<Object> *pool;

public:
    using value_type = Object;
    using pointer = Object*;
    using const_pointer = const Object*;
    using reference = Object&;
    using const_reference = const Object&;
    using size_type = std::size_t;

    static
    void init_thread(Pool<Object> &_pool)
    {
        pool = &_pool;
    }

    PoolAllocator()
    {
        assert(pool);
    }

    template <class U>
    class rebind
    {
    public:
        using other = PoolAllocator<U>;
    };

    static
    pointer allocate(size_t n)
    {
        assert(n == 1);
        assert(pool);
        return static_cast<Object*>(pool->get());
    }

    static
    void deallocate(pointer p, size_t n)
    {
        assert(n == 1);
        assert(pool);
        pool->release(static_cast<void*>(p));
    }

    static
    void construct(pointer p, const_reference t)
    {
        new (p) Object(t);
    }

    static
    void destroy(pointer p)
    {
        p->~Object();
    }

    static
    size_type max_size()
    {
        return 1;
    }
};

#endif // __cd_pool_h
