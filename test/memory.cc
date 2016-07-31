#include <pool.h>
#include <cache.h>

#include <iostream>
#include <buffer_string.h>
#include <sys/unistd.h>

using namespace std;

class Test : public OnPool<Test>
{
    char data[12];
};
INIT_POOL(Test);

int check_invocation = 0;

template <class Obj>
void check(int pool_size)
{
    ++check_invocation;
    int check = 0;
    Pool<Obj> *pool = new Pool<Obj>(pool_size);
    if (++check, pool_size != pool->free_chunks()) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": free_chunks() " << pool->free_chunks()
            << "; expected: " << pool_size << "\n";
        exit(check);
    }
    void *got = pool->get();
    if (++check, pool_size - 1 != pool->free_chunks()) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": free_chunks() " << pool->free_chunks()
            << "; expected: " << pool_size - 1 << "\n";
        exit(check);
    }
    pool->release(got);
    if (++check, pool_size != pool->free_chunks()) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": free_chunks() " << pool->free_chunks()
            << "; expected: " << pool_size << "\n";
        exit(check);
    }
    Test *t = new(*pool) Obj;
    if (++check, pool_size - 1 != pool->free_chunks()) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": free_chunks() " << pool->free_chunks()
            << "; expected: " << pool_size - 1 << "\n";
        exit(check);
    }
    if (++check, t != got) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": t " << hex << t << dec <<
            "; expected: " << got << "\n";
        exit(5);
    }
    delete t;
    if (++check, pool_size != pool->free_chunks()) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": free_chunks() " << pool->free_chunks()
            << "; expected: " << pool_size << "\n";
        exit(check);
    }
    size_t i = 0;
    bool catched = false;
    try {
        for (i = 0; i < pool_size + 1; ++i)
            new (*pool) Obj;
    } catch (std::bad_alloc) {
        catched = true;
        if (++check, i != pool_size) {
            std::cerr << "Failed check " << check_invocation <<
                "." << check << ": i = " << i
                << "; expected: " << pool_size << "\n";
            exit(check);
        }
    }
    if (++check, !catched) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": no std::bad_alloc thrown\n";
        exit(check);
    }
    if (++check, i != pool_size) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": i " << i
            << "; expected: " << pool_size << "\n";
        exit(check);        
    }
}

struct Test2
{
    const char *data;
    Test2(const char *d)
    {
        data = d;
        cout << "Constructed: " << d << endl;
    }
    Test2(const Test2& d)
    {
        data = d.data;
        cout << "Copied: " << data << endl;
    }
    ~Test2()
    {
        cout << "Destructed: " << data << endl;
    }
    operator const char*() const
    {
        return data;
    }
};

void check2(int pool_size, int timeout)
{
    ++check_invocation;
    int check = 0;
    Pool<MapNode> map_pool(pool_size);
    Pool<ListNode> list_pool(pool_size);
    PoolAllocator<MapNode>::init_thread(map_pool);
    PoolAllocator<ListNode>::init_thread(list_pool);
    NameCache<PoolAllocator<> >::init_static(pool_size, timeout);
    NameCache<PoolAllocator<> > cache;

    if (++check, pool_size != map_pool.free_chunks()) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": free_chunks() " << map_pool.free_chunks()
            << "; expected: " << pool_size << "\n";
        exit(check + 10);
    }
    if (++check, pool_size != list_pool.free_chunks()) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": free_chunks() " << list_pool.free_chunks()
            << "; expected: " << pool_size << "\n";
        exit(check + 10);
    }

    in_addr host_ip;

    int i = -2;
    char name[33];
    try {
        cache.insert(host_ip, "ya.ru");
        cache.insert(host_ip, "mail.ru");
        for (i = 0; i <= pool_size; ++i) {
            snprintf(name, 33, "traktor%d.es", i);
            cache.insert(host_ip, name);
        }
    } catch (std::bad_alloc) {
        if (++check) {
            std::cerr << "Failed check " << check_invocation <<
                "." << check << ": catched std::bad_alloc\n";
            exit(check);
        }
    }
    if (++check, pool_size != cache.size()) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": cache.size() " << cache.size()
            << "; expected: " << pool_size << "\n";
        exit(check + 10);
    }
    if (++check, 0 != map_pool.free_chunks()) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": free_chunks() " << map_pool.free_chunks()
            << "; expected: " << 0 << "\n";
        exit(check + 10);
    }
    if (++check, 0 != list_pool.free_chunks()) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": free_chunks() " << list_pool.free_chunks()
            << "; expected: " << 0 << "\n";
        exit(check + 10);
    }

    bool res = cache.get(host_ip, "ya.ru");
    if (++check, res != false) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": res = " << res
            << "; expected: " << false << "\n";
        exit(check);
    }

    res = cache.get(host_ip, "traktor4.es");
    if (++check, res != true) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": res = " << res
            << "; expected: " << true << "\n";
        exit(check);
    }

    sleep(timeout + 1);
    res = cache.get(host_ip, "traktor4.es");
    if (++check, res != false) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": res = " << res
            << "; expected: " << false << "\n";
        exit(check);
    }

    if (++check, 1 != map_pool.free_chunks()) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": free_chunks() " << map_pool.free_chunks()
            << "; expected: " << 1 << "\n";
        exit(check + 10);
    }
    if (++check, 1 != list_pool.free_chunks()) {
        std::cerr << "Failed check " << check_invocation <<
            "." << check << ": free_chunks() " << list_pool.free_chunks()
            << "; expected: " << 1 << "\n";
        exit(check + 10);
    }
}

int main()
{
    check<Test>(10);
    check2(10, 3);
}

