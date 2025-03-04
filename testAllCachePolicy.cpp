#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <iomanip>
#include <random>
#include <algorithm>
#include <array>  // 添加这行

#include "KICachePolicy.h"
#include "KLfuCache.h"
#include "KLruCache.h"
#include "KArcCache/KArcCache.h"

class Timer {
public:
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}
    
    /**
     * @brief 计算从计时器启动到当前时刻所经过的时间（以毫秒为单位）
     * 
     * 该函数通过获取当前时间点，并与计时器启动时记录的时间点相减，
     * 然后将时间差转换为毫秒并返回。
     * 
     * @return double 经过的毫秒数
     */
    double elapsed() {
        // 获取当前的高精度时间点
        auto now = std::chrono::high_resolution_clock::now();
        // 计算从计时器启动到当前时刻的时间差，并使用模板函数将其转换为毫秒，count()函数返回时间差的计数值
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count();
    }

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

// 辅助函数：打印结果
void printResults(const std::string& testName, int capacity, 
                 const std::vector<int>& get_operations, 
                 const std::vector<int>& hits) {
    std::cout << "缓存大小: " << capacity << std::endl;
    std::cout << "LRU - 命中率: " << std::fixed << std::setprecision(2) 
              << (100.0 * hits[0] / get_operations[0]) << "%" << std::endl;
    std::cout << "LFU - 命中率: " << std::fixed << std::setprecision(2) 
              << (100.0 * hits[1] / get_operations[1]) << "%" << std::endl;
    std::cout << "ARC - 命中率: " << std::fixed << std::setprecision(2) 
              << (100.0 * hits[2] / get_operations[2]) << "%" << std::endl;
}

/**
 * @brief 测试热点数据访问场景下不同缓存策略的性能
 * 
 * 该函数模拟了一个热点数据访问的场景，其中 70% 的操作是访问热点数据，30% 的操作是访问冷数据。
 * 分别对 LRU、LFU 和 ARC 三种缓存策略进行测试，并打印出它们的命中率。
 */
void testHotDataAccess() {
    std::cout << "\n=== 测试场景1：热点数据访问测试 ===" << std::endl;
    
    const int CAPACITY = 50;  // 增加缓存容量
    const int OPERATIONS = 500000;  // 增加操作次数
    const int HOT_KEYS = 20;   // 增加热点数据的数量
    const int COLD_KEYS = 5000;        
    
    // 创建 LRU、lfu、arc缓存对象，键的类型为 int，值的类型为 std::string
    KamaCache::KLruCache<int, std::string> lru(CAPACITY);
    KamaCache::KLfuCache<int, std::string> lfu(CAPACITY);
    KamaCache::KArcCache<int, std::string> arc(CAPACITY);

    // 用于生成随机数的设备
    std::random_device rd;
    // 随机数生成器，类型为 std::mt19937
    std::mt19937 gen(rd());
    
    // 存储三种缓存策略的指针数组，类型为 std::array<KamaCache::KICachePolicy<int, std::string>*, 3>
    std::array<KamaCache::KICachePolicy<int, std::string>*, 3> caches = {&lru, &lfu, &arc};
    // 存储每种缓存策略的命中次数
    std::vector<int> hits(3, 0);
    // 存储每种缓存策略的 get 操作次数
    std::vector<int> get_operations(3, 0);

    // 先进行一系列 put 操作
    for (int i = 0; i < caches.size(); ++i) {
        for (int op = 0; op < OPERATIONS; ++op) {
            int key;
            if (op % 100 < 70) {  // 70% 热点数据
                key = gen() % HOT_KEYS;
            } else {  // 30% 冷数据
                key = HOT_KEYS + (gen() % COLD_KEYS);
            }
            // 生成键对应的值，类型为 std::string
            std::string value = "value" + std::to_string(key);
            // 向缓存中插入键值对
            caches[i]->put(key, value);
        }
        
        // 然后进行随机 get 操作
        for (int get_op = 0; get_op < OPERATIONS; ++get_op) {
            int key;
            if (get_op % 100 < 70) {  // 70% 概率访问热点
                key = gen() % HOT_KEYS;
            } else {  // 30% 概率访问冷数据
                key = HOT_KEYS + (gen() % COLD_KEYS);
            }
            
            // 用于存储 get 操作的结果，类型为 std::string
            std::string result;
            // get 操作次数加 1
            get_operations[i]++;
            // 如果 get 操作成功命中缓存
            if (caches[i]->get(key, result)) {
                // 命中次数加 1
                hits[i]++;
            }
        }
    }

    // 打印测试结果
    printResults("热点数据访问测试", CAPACITY, get_operations, hits);
}

/**
 * @brief 测试循环扫描场景下不同缓存策略的性能
 * 
 * 该函数模拟了一个循环扫描的场景，其中 60% 的操作是顺序扫描，30% 的操作是随机跳跃，10% 的操作是访问范围外数据。
 * 分别对 LRU、LFU 和 ARC 三种缓存策略进行测试，并打印出它们的命中率。
 */
void testLoopPattern() {
    std::cout << "\n=== 测试场景2：循环扫描测试 ===" << std::endl;
    
    const int CAPACITY = 50;  // 增加缓存容量
    const int LOOP_SIZE = 500;         
    const int OPERATIONS = 200000;  // 增加操作次数
    
    // 创建 LRU 缓存对象，键的类型为 int，值的类型为 std::string
    KamaCache::KLruCache<int, std::string> lru(CAPACITY);
    KamaCache::KLfuCache<int, std::string> lfu(CAPACITY);
    KamaCache::KArcCache<int, std::string> arc(CAPACITY);

    std::array<KamaCache::KICachePolicy<int, std::string>*, 3> caches = {&lru, &lfu, &arc};
    std::vector<int> hits(3, 0);
    std::vector<int> get_operations(3, 0);

    std::random_device rd;
    std::mt19937 gen(rd());

    // 先填充数据
    for (int i = 0; i < caches.size(); ++i) {
        for (int key = 0; key < LOOP_SIZE; ++key) {  // 只填充 LOOP_SIZE 的数据
            // 生成键对应的值，类型为 std::string
            std::string value = "loop" + std::to_string(key);
            // 向缓存中插入键值对
            caches[i]->put(key, value);
        }
        
        // 然后进行访问测试
        int current_pos = 0;
        for (int op = 0; op < OPERATIONS; ++op) {
            int key;
            if (op % 100 < 60) {  // 60% 顺序扫描
                key = current_pos;
                current_pos = (current_pos + 1) % LOOP_SIZE;
            } else if (op % 100 < 90) {  // 30% 随机跳跃
                key = gen() % LOOP_SIZE;
            } else {  // 10% 访问范围外数据
                key = LOOP_SIZE + (gen() % LOOP_SIZE);
            }
            
            // 用于存储 get 操作的结果，类型为 std::string
            std::string result;
            // get 操作次数加 1
            get_operations[i]++;
            // 如果 get 操作成功命中缓存
            if (caches[i]->get(key, result)) {
                // 命中次数加 1
                hits[i]++;
            }
        }
    }

    // 打印测试结果
    printResults("循环扫描测试", CAPACITY, get_operations, hits);
}

/**
 * @brief 测试工作负载剧烈变化场景下不同缓存策略的性能
 * 
 * 该函数模拟了一个工作负载剧烈变化的场景，分为多个阶段，每个阶段有不同的访问模式。
 * 分别对 LRU、LFU 和 ARC 三种缓存策略进行测试，并打印出它们的命中率。
 */
void testWorkloadShift() {
    std::cout << "\n=== 测试场景3：工作负载剧烈变化测试 ===" << std::endl;
    
    const int CAPACITY = 4;            
    const int OPERATIONS = 80000;      
    const int PHASE_LENGTH = OPERATIONS / 5;
    
    // 创建 LRU 缓存对象，键的类型为 int，值的类型为 std::string
    KamaCache::KLruCache<int, std::string> lru(CAPACITY);
    KamaCache::KLfuCache<int, std::string> lfu(CAPACITY);
    KamaCache::KArcCache<int, std::string> arc(CAPACITY);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::array<KamaCache::KICachePolicy<int, std::string>*, 3> caches = {&lru, &lfu, &arc};
    std::vector<int> hits(3, 0);
    std::vector<int> get_operations(3, 0);

    // 先填充一些初始数据
    for (int i = 0; i < caches.size(); ++i) {
        for (int key = 0; key < 1000; ++key) {
            std::string value = "init" + std::to_string(key);
            // 向缓存中插入键值对
            caches[i]->put(key, value);
        }
        
        // 然后进行多阶段测试
        for (int op = 0; op < OPERATIONS; ++op) {
            int key;
            // 根据不同阶段选择不同的访问模式
            if (op < PHASE_LENGTH) {  // 热点访问
                key = gen() % 5;
            } else if (op < PHASE_LENGTH * 2) {  // 大范围随机
                key = gen() % 1000;
            } else if (op < PHASE_LENGTH * 3) {  // 顺序扫描
                key = (op - PHASE_LENGTH * 2) % 100;
            } else if (op < PHASE_LENGTH * 4) {  // 局部性随机
                int locality = (op / 1000) % 10;
                key = locality * 20 + (gen() % 20);
            } else {  // 混合访问
                int r = gen() % 100;
                if (r < 30) {
                    key = gen() % 5;
                } else if (r < 60) {
                    key = 5 + (gen() % 95);
                } else {
                    key = 100 + (gen() % 900);
                }
            }
            
            // 用于存储 get 操作的结果，类型为 std::string
            std::string result;
            // get 操作次数加 1
            get_operations[i]++;
            // 如果 get 操作成功命中缓存
            if (caches[i]->get(key, result)) {
                // 命中次数加 1
                hits[i]++;
            }
            
            // 随机进行 put 操作，更新缓存内容
            if (gen() % 100 < 30) {  // 30% 概率进行 put
                // 生成新的值，类型为 std::string
                std::string value = "new" + std::to_string(key);
                // 向缓存中插入键值对
                caches[i]->put(key, value);
            }
        }
    }

    // 打印测试结果
    printResults("工作负载剧烈变化测试", CAPACITY, get_operations, hits);
}

int main() {
    testHotDataAccess();
    testLoopPattern();
    testWorkloadShift();
    return 0;
}