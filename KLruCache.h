#pragma once 

#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "KICachePolicy.h"

namespace KamaCaches 
{

template<typename Key, typename Value>
struct RNode
{
    Key                                key;
    Value                              value;
    size_t                             time; // 访问次数(arc中才用到,lru算法中可忽略他)
    std::shared_ptr<RNode<Key, Value>> pre;
    std::shared_ptr<RNode<Key, Value>> next;
    RNode(Key key, Value value)
        : key(key), value(value), time(1), pre(nullptr), next(nullptr)
    {}
};


template<typename Key, typename Value>
class KLruCache : public KICachePolicy<Key, Value>
{
public:
    using Node = RNode<Key, Value>;
    using spNode = std::shared_ptr<Node>;
    using NodeMap = std::unordered_map<Key, spNode>;

    KLruCache(int capacity)
        : capacity_(capacity)
    {
        // 创建首尾虚拟节点
        DummyHead_ = std::make_shared<Node>(Key(), Value());
        DummyTail_ = std::make_shared<Node>(Key(), Value());
        DummyHead_->next = DummyTail_;
        DummyTail_->pre = DummyHead_;
    }

    ~KLruCache() override = default;

    // 添加缓存
    void put(Key key, Value value) override
    {
        if (capacity_ <= 0)
            return;
    
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = keyToNode_.find(key);
        if (it != keyToNode_.end())
        {
            // 如果在当前容器中,则更新value,并调用get方法，代表该数据刚被访问
            it->second->value = value;
            getInternal(it->second, value);
            return ;
        }

        putInternal(key, value);
    }

    bool get(Key key, Value& value) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = keyToNode_.find(key);
        if (it != keyToNode_.end())
        {
            getInternal(it->second, value);
            return true;
        }
        return false;
    }

    Value get(Key key) override
    {
        Value value;
        memset(&value, 0, sizeof(value));
        get(key, value);
        return value;
    }

    // 删除指定元素
    void remove(Key key) 
    { 
        auto it = keyToNode_.find(key);
        if (it != keyToNode_.end())
        {
            removeFromList(it->second);
            keyToNode_.erase(it);
        }
    }

private:
    void putInternal(Key key, Value value);
    void getInternal(spNode& node, Value& value);

    // 移除最久未被访问的数据
    void kickOut()
    {
        spNode data = DummyHead_->next;
        removeFromList(data);
        keyToNode_.erase(data->key);
    }

    // 将数据结点从链表中移除
    void removeFromList(spNode& node)
    {
        node->pre->next = node->next;
        node->next->pre = node->pre;
        node->pre = nullptr;
        node->next = nullptr;
    }

    // 将数据结点插到链表尾部
    void insertToList(spNode& node)
    {
        node->next = DummyTail_;
        node->pre = DummyTail_->pre;
        DummyTail_->pre->next = node;
        DummyTail_->pre = node;
    }

private:
    int          capacity_; // 缓存容量
    NodeMap      keyToNode_; // key到结点的映射
    std::mutex   mutex_;
    spNode       DummyHead_; // 虚拟头结点
    spNode       DummyTail_;
};

template<typename Key, typename Value>
void KLruCache<Key,Value>::getInternal(spNode& node, Value& value)
{
    // 将其从链表中原位置移除，接着从链表尾部插入(因为链表从头到尾的顺序是访问顺序，最久未被访问的放在头部)
    removeFromList(node);
    insertToList(node);
    value = node->value;
}

template<typename Key, typename Value>
void KLruCache<Key,Value>::putInternal(Key key, Value value)
{
    // 判断容器是否满
    if (keyToNode_.size() >= capacity_)
    {
        // 移除最久未被访问的数据
        kickOut();
    }

    // 将数据结点插入容器
    spNode data = std::make_shared<Node>(key, value);
    insertToList(data);
    keyToNode_[key] = data;
}

// LRU优化：Lru-k版本。 通过继承的方式进行再优化
template<typename Key, typename Value>
class KLruKCache : public KLruCache<Key, Value>
{
public:
    KLruKCache(int capacity, int historyCapacity, int k)
        : KLruCache<Key, Value>(capacity) // 调用基类构造
        , historyList_(std::make_unique<KLruCache<Key, size_t>>(historyCapacity))
        , k_(k)
    {}

    Value get(Key key)
    {
        // 获取该数据访问次数
        int historyCount = historyList_->get(key);
        // 如果访问到数据，则更新历史访问记录节点值count++
        historyList_->put(key, ++historyCount); 
        
        // 从缓存中获取数据，不一定能获取到，因为可能不在缓存中
        return KLruCache<Key, Value>::get(key);
    }

    void put(Key key, Value value)
    {
        // 先判断是否存在于缓存中，如果存在于则直接覆盖，如果不存在则不直接添加到缓存
        if (KLruCache<Key, Value>::get(key) != "")
            KLruCache<Key, Value>::put(key, value);
        
        // 如果数据历史访问次数达到上限，则添加入缓存
        int historyCount = historyList_->get(key);
        historyList_->put(key, ++historyCount); 

        if (historyCount >= k_)
        {
            // 移除历史访问记录
            historyList_->remove(key);
            // 添加入缓存中
            KLruCache<Key, Value>::put(key, value);
        }
    }

private:
    int                                     k_; // 进入缓存队列的评判标准
    std::unique_ptr<KLruCache<Key, size_t>> historyList_; // 访问数据历史记录(value为访问次数)
}; 

// lru优化：对lru进行分片，提高高并发使用的性能
template<typename Key, typename Value>
class KHashLruCaches
{
public:
    KHashLruCaches(size_t capacity, int sliceNum)
        : capacity_(capacity)
        , sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())
    {
        size_t sliceSize = std::ceil(capacity / static_cast<double>(sliceNum_)); // 获取每个分片的大小
        for (int i = 0; i < sliceNum_; ++i)
        {
            lruSliceCaches_.emplace_back(new KLruCache<Key, Value>(sliceSize)); 
        }
    }

    void put(Key key, Value value)
    {
        // 获取key的hash值，并计算出对应的分片索引
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lruSliceCaches_[sliceIndex]->put(key, value);
    }

    bool get(Key key, Value& value)
    {
        // 获取key的hash值，并计算出对应的分片索引
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lruSliceCaches_[sliceIndex]->get(key, value);
    }

    Value get(Key key)
    {
        Value value;
        memset(&value, 0, sizeof(value));
        get(key, value);
        return value;
    }

private:
    // 将key转换为对应hash值
    size_t Hash(Key key)
    {
        std::hash<Key> hashFunc;
        return hashFunc(key);
    }

private:
    size_t                                              capacity_;  // 总容量
    int                                                 sliceNum_;  // 切片数量
    std::vector<std::unique_ptr<KLruCache<Key, Value>>> lruSliceCaches_; // 切片LRU缓存
};

} // namespace KamaCaches