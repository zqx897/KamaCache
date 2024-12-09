#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "KICachePolicy.h"
#include "KLfuCache.h"
#include "KLruCache.h"

namespace KamaCaches
{
#define InitCapacity 10
#define InitTransformTime 2

template<typename Key, typename Value>
class KLruPart 
{
public:
    using Node = RNode<Key, Value>;
    using spNode = std::shared_ptr<Node>;

    KLruPart(int capacity = InitCapacity, int transformTime = InitTransformTime)
        : capacity_(capacity)
        , ghostCapacity_(capacity) // lruGhost的存储空间跟lru相同
        , transformTime_(transformTime)
        , lruDummyHead_(std::make_shared<Node>(Key(), Value()))
        , lruDummyTail_(std::make_shared<Node>(Key(), Value()))
        , lruGhostDummyHead_(std::make_shared<Node>(Key(), Value()))
        , lruGhostDummyTail_(std::make_shared<Node>(Key(), Value()))
    {
        lruDummyHead_->next = lruDummyTail_;
        lruDummyTail_->pre = lruDummyHead_;
        lruGhostDummyHead_->next = lruGhostDummyTail_;
        lruGhostDummyTail_->pre = lruGhostDummyHead_;
    }

    // 插入元素，当访问次数到达transformTime时，返回true，反之返回false
    bool put(Key key, Value value)
    {
        if (capacity_ == 0)
            return false;
        std::lock_guard<std::mutex> lock(mutex_);
        // 判断key是否存在于lru中，如果存在调用get方法增加一次访问次数
        // 并且将结点重新插入链表尾部，并且修改value值
        auto it = lruMap_.find(key);
        if (it != lruMap_.end())
        {
            auto node = it->second;
            adjustList(node);
            if (node->time == transformTime_) // 往LFU中添加
                return true;
        }
        // 如果key不存在于lru中，判断lru是否已满，如果已满，则将lru中元素淘汰一个，加入到lruGhost中
        // 如果lru未满，则直接将key插入到lruMap中，并将value插入到lru尾部  
        if (lruMap_.size() >= capacity_)
        {
            // 将结点从lru中删除，然后往lruGhost中添加
            kickOut(false);
        }
        // 插入新结点
        spNode newNode = std::make_shared<Node>(key, value);
        insert(newNode, false);
        return false;
    }

    bool get(Key key, Value& value, bool& isTransform)
    {
        // 判断是否有元素
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = lruMap_.find(key);
        if (it != lruMap_.end())
        {
            auto node = it->second;
            adjustList(node);
            value = node->value;
            if (node->time == transformTime_)
                isTransform = true;

            return true;
        }
        return false;
    }

    bool checkGhost(Key key)
    {
        auto it = lruGhostMap_.find(key);
        if (it != lruGhostMap_.end())
        {
            // 如果存在则移除ghost中的该节点 并返回true
            removeNode(it->second, true);
            lruGhostMap_.erase(it);
            return true;
        }
        return false;
    }

    // 容量+1
    void increase()
    {
        capacity_++;
    }

    // 容量-1
    bool decrease()
    {
        if (capacity_ <= 0)
            return false;
        
        if (lruMap_.size() == capacity_)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // 移除最近最久不使用元素
            kickOut(false);
        }
        capacity_--;
        return true;
    }

private:
    void adjustList(spNode node)
    {
        node->time++; // 该数据访问次数++
        // 将原结点移除，并重新插入链表尾部
        removeNode(node, false);
        insertToList(node, false);
    }

    void removeNode(spNode node, bool isGhost)
    {   
        if (!isGhost)
        {
            if (lruMap_.empty())
            {
                std::cout << "lru nothing to remove" << std::endl;
                return ;
            }
        }
        else // 移除淘汰链表中的元素
        {
            if (lruGhostMap_.empty())
            {
                std::cout << "lru ghost nothing to remove" << std::endl;
                return ;
            }
        }
        removeFromList(node);
    } 

    void removeFromList(spNode node)
    {
        node->pre->next = node->next;
        node->next->pre = node->pre;
        node->pre = nullptr;
        node->next = nullptr;
    }  

    // 删除最久未访问元素，如果是主缓存，则加入ghost中，如果是ghost缓存，则直接删除 
    void kickOut(bool isGhost)
    {
        if (!isGhost)
        {
            if (lruMap_.empty())
                return;

            // lru非空，移除链表头部结点，从map中删除，插入ghost表中
            auto rmNode = lruDummyHead_->next;
            removeFromList(rmNode);
            lruMap_.erase(rmNode->key);
            // 如果ghost缓存已满，则将ghost链表头结点移除，并且从ghost表中删除
            // 然后将元素加入ghost中
            if (lruGhostMap_.size() >= ghostCapacity_)
                kickOut(true);
            insert(rmNode, true);
        }
        else // 删除ghost表中的最久未访问结点
        {
            if (lruGhostMap_.empty())
                throw("no element to delete");
            
            auto rmNode = lruGhostDummyHead_->next;
            removeFromList(rmNode);
            lruGhostMap_.erase(rmNode->key);
        }
    }

    void insert(spNode node, bool isGhost)
    {
        if (!node)
            return;
        
        if (!isGhost)
            lruMap_[node->key] = node;
        else
            lruGhostMap_[node->key] = node;
        
        insertToList(node, isGhost);
    }

    void insertToList(spNode node, bool isGhost)
    {
        if (!node) 
            return;

        if (!isGhost)
        {
            // 向lru主表尾部插入（尾结点是最近访问的元素）
            node->next = lruDummyTail_;
            node->pre = lruDummyTail_->pre;
            lruDummyTail_->pre->next = node;
            lruDummyTail_->pre = node;
        }
        else // 插入ghost链表
        {
            node->next = lruGhostDummyTail_;
            node->pre = lruGhostDummyTail_->pre;
            lruGhostDummyTail_->pre->next = node;
            lruGhostDummyTail_->pre = node;
        }
    }

private:
    std::atomic<size_t>             capacity_; 
    std::unordered_map<Key, spNode> lruMap_;
    spNode                          lruDummyHead_; // 虚拟头结点
    spNode                          lruDummyTail_;

    int                             ghostCapacity_; // ghost链表的存储空间（固定不变）
    std::unordered_map<Key, spNode> lruGhostMap_;
    spNode                          lruGhostDummyHead_;
    spNode                          lruGhostDummyTail_;

    int                             transformTime_; // 默认从LRU到LFU转换阈值为2
    std::mutex                      mutex_;
};

template<typename Key, typename Value>
class KLfuPart
{
public:
    using spNode = std::shared_ptr<FNode<Key, Value>>;
    using upFreqList = std::unique_ptr<FreqList<Key, Value>>;

    KLfuPart(size_t capacity = InitCapacity, size_t transformTime = InitTransformTime)
        : capacity_(capacity)
        , freq(transformTime)
        , ghostList_(std::make_unique<FreqList<Key, Value>>(-1))
    {
    }

    void put(Key key, Value value)
    {
        if (capacity_ == 0)
            return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        // 判断是否在lfu中，如果在则从原有链表中拿出来，插入新频次的链表中去。
        auto it = keyToNode_.find(key);
        if (it != keyToNode_.end())
        {
            // 重置其value值
            it->second->value = value;
            adjustList(it->second);
            return;
        }

        // 判断lfu是否已满，如果满了，则先移除频次最少的元素（并且最久未访问），再插入新元素
        putInternal(key, value);
    }

    bool get(Key key, Value& value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = keyToNode_.find(key);
        if (it != keyToNode_.end())
        {
            value = it->second->value;
            adjustList(it->second);
            return true;
        }
        return false;
    }

    bool checkGhost(Key key)
    {
        auto it = ghostKeyToNode_.find(key);
        if (it != ghostKeyToNode_.end())
        {
            // 如果存在则移除ghost链表中的结点
            remove(it->second, true);
            return true;
        }
        return false;
    }

    void increase()
    {
        capacity_++;
    }

    // 删除低频元素并减少容量
    bool decrease()
    {
        if (capacity_ <= 0)
            return false;
        // 如果表满，则从lfu中淘汰最不经常访问结点（加入ghost表）
        if (keyToNode_.size() == capacity_)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            kickOut(false);
        }
            
        capacity_--;
        return true;
    }

private:
    void adjustList(spNode& node)
    {
        // 找到后从低访问频次的链表中删除，并且添加到+1访问频次的链表中
        // 访问频次+1
        removeFromList(node);
        node->freq++;
        insertToList(node, false);

        // 如果当前node的访问频次如果等于minFreq+1，并且其前驱链表已为空,则更新minFreq
        if (node->freq == minFreq_ + 1 && freqToFreqList_[minFreq_]->isNull())
            minFreq_++;
    }

    void remove(spNode& rmNode, bool isGhost)
    {
        if (!isGhost)
            keyToNode_.erase(rmNode->key);
        else
            ghostKeyToNode_.erase(rmNode->key);

        removeFromList(rmNode);
    }

    void removeFromList(spNode& node)
    {
        node->pre->next = node->next;
        node->next->pre = node->pre;
        node->pre = nullptr;
        node->next = nullptr;
    }

    void putInternal(Key key, Value value)
    {
        if (keyToNode_.size() >= capacity_)
        {
            // 缓存已满，把最不常访问的结点转移到ghost链表中
            kickOut(false);
        }

        // 创建新结点，将新结点添加进入，更新最小访问频次
        spNode node = std::make_shared<FNode<Key, Value>>(key, value);
        insert(node, false);
        minFreq_ = 1;
    }

    void insert(spNode& node, bool isGhost)
    {
        if (!node)
            return;

        if (!isGhost)
            keyToNode_[node->key] = node;
        else
            ghostKeyToNode_[node->key] = node;

        insertToList(node, isGhost);
    }

    void insertToList(spNode& node, bool isGhost)
    {
        if (!isGhost)
        {
            // 添加进入相应的频次链表前需要判断该频次链表是否存在
            if (freqToFreqList_.find(node->freq) == freqToFreqList_.end())
            {
                // 不存在则创建
                freqToFreqList_[node->freq] = std::make_unique<FreqList<Key, Value>>(node->freq);
            }

            // 从后边插入到该访问频次链表中
            spNode tail = freqToFreqList_[node->freq]->tail;
            node->pre = tail->pre;
            node->next = tail;
            tail->pre->next = node;
            tail->pre = node;
        }
        else // 如果是ghost，则直接往ghost链表后边插入
        {
            spNode tail = ghostList_->tail;
            node->pre = tail->pre;
            node->next = tail;
            tail->pre->next = node;
            tail->pre = node;
        }
    }

    void kickOut(bool isGhost)
    {
        if (!isGhost)
        {
            // 从lfu缓存中移到ghost缓存中
            spNode rmNode = freqToFreqList_[minFreq_]->head->next;
            remove(rmNode, false);

            // 如果ghost缓存已满，则将ghost链表头结点移除，并且从ghost表中删除
            // 然后将元素加入ghost中
            if (ghostKeyToNode_.size() >= ghostCapacity_)
                kickOut(true);
            insert(rmNode, true);
        }
        else // 删除ghost表中最久未访问结点
        {
            spNode rmNode = ghostList_->head->next;
            remove(rmNode, true);
        }
    }
    
private:
    std::atomic<size_t>                 capacity_; // 容量
    size_t                              freq; // 访问频次
    std::mutex                          mutex_;
    size_t                              minFreq_; // 当前最小访问频次
    
    std::unordered_map<Key, spNode>     keyToNode_; 
    // 访问频次->对应频次链表
    std::unordered_map<int, upFreqList> freqToFreqList_;

    // ghost链表是不考虑访问频次的(lruGhost和lfuGhost都一样)
    size_t                              ghostCapacity_;
    std::unordered_map<Key, spNode>     ghostKeyToNode_; 
    upFreqList                          ghostList_;
};


template<typename Key, typename Value>
class KArcCache : public KICachePolicy<Key, Value> 
{
public:
    KArcCache(size_t capacity = InitCapacity, size_t transformTime = InitTransformTime)
        : capacity_(capacity)
        , transformTime_(transformTime)
        , lruCache_(std::make_unique<KLruPart<Key, Value>>(capacity, transformTime))
        , lfuCache_(std::make_unique<KLfuPart<Key, Value>>(capacity, transformTime))
    {}

    ~KArcCache() override = default;

    void put(Key key, Value value) override
    {
        // 先在各个表的ghost中搜索
        bool find = checkGhost(key);

        // 如果两个ghost都没有对应的元素，则不改变partition（两者容量不变）
        if (!find)
        {
            // 优先加入LruCache中，如果添加元素的频次到达转换频次，则加入LfuCache中
            if (lruCache_->put(key, value))
                lfuCache_->put(key, value);
                
            return;
        }

        lruCache_->put(key, value);
    }

    bool get(Key key, Value& value) override
    {
        // 访问元素，分别在lru和lfu的ghost链表中搜索并进行相应收缩和扩张
        checkGhost(key);

        // 判断资源是否在lru或者在lfu中
        // 如果存在则执行相应逻辑，不在则直接返回false
        bool isTransform = false;
        if (lruCache_->get(key, value, isTransform))
        {
            if (isTransform)
                lfuCache_->put(key, value);
            
            return true;
        }
        else if (lfuCache_->get(key, value))
        {
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

private:
    bool checkGhost(Key key)
    {
        // 先在各个表的ghost中搜索
        bool find = false;
        if (lruCache_->checkGhost(key)) 
        {
            // 缩减lfuCache的容量
            if (lfuCache_->decrease())
                lruCache_->increase();
            
            find = true;
        }
        else if (lfuCache_->checkGhost(key))
        {   // lru缩小容量，lfu扩容
            if (lruCache_->decrease())
                lfuCache_->increase();
            
            find = true;
        }
        return find;
    }

private:
    size_t                                capacity_;
    size_t                                transformTime_; // 某数据访问频次达到此值，则添加进入LFU
    std::unique_ptr<KLruPart<Key, Value>> lruCache_;
    std::unique_ptr<KLfuPart<Key, Value>> lfuCache_;
};

} // namespace KamaCaches