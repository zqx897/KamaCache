#include <iostream>
#include <string>

#include "KLfuCache.h"
#include "KLruCache.h"
#include "KArcCache.h"

int main()
{
    KamaCaches::KHashLfuCache<std::string, std::string> cache(4, 2);
    cache.put("hello", "world");
    cache.put("hello1", "world7");
    cache.put("hello2", "world");
    cache.put("hello4", "world5");
    cache.put("hello5", "world2");
    std::string val;
    val = cache.get("hello");
    val = cache.get("hello");
    cache.put("hello3", "world3");
    if(cache.get("hello3", val))
        std::cout << "找到了，打印一下获取的值：" << val << std::endl;
    if(cache.get("hello2", val))
        std::cout << "找到了，打印一下获取的值：" << val << std::endl;
    cache.purge();

    std::cout << "----------------------------------------" << std::endl;

    KamaCaches::KHashLruCaches<std::string, std::string> cache2(4, 2);
    cache2.put("hello", "你好");
    cache2.put("hello", "你好");
    cache2.put("hello4", "你好1");
    std::string rt;
    if(cache2.get("hello", rt))
        std::cout << "找到了，打印一下获取的值：" << rt << std::endl;
    if(cache2.get("hello4", rt))
        std::cout << "找到了，打印一下获取的值：" << rt << std::endl;

    std::cout << "----------------------------------------" << std::endl;

    // 访问超过两次才将其放回
    KamaCaches::KLruKCache<std::string, std::string> cache3(4, 4, 2);
    cache3.put("hello", "你好");
    cache3.put("hello", "你好");
    if (cache3.get("hello") == "")
    {
        std::cout << "没找到了" << std::endl;
    }
    else
    {
        std::cout << "找到了，打印一下获取的值：" << cache3.get("hello") << std::endl;
    }

    std::cout << "----------------------------------------" << std::endl;

    KamaCaches::KArcCache<std::string, std::string> cache6(4, 2);
    std::string rt5;
    cache6.put("hello", "你好afsfdsf");
    cache6.put("hello", "你好afsfdsf");
    cache6.put("hello2", "你好a");
    cache6.put("hello4", "你好f");
    cache6.put("hello5", "你好f");
    cache6.put("hello6", "你好f");
    cache6.get("hello", rt5);
    if (cache6.get("hello", rt5))
    {
        std::cout << "找到了，打印一下获取的值：" << rt5 << std::endl;
    }

    return 0;
}