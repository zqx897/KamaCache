#pragma once

#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "KICachePolicy.h"

namespace KamaCaches
{

template <typename Key, typename Value>
struct FNode
{
    int freq; // 访问频次
    Key key;
    Value value;
    std::shared_ptr<FNode<Key, Value>> pre; // 上一结点
    std::shared_ptr<FNode<Key, Value>> next;

    FNode() 
    : freq(1), pre(nullptr), next(nullptr) {}
    FNode(Key key, Value value) 
    : freq(1), key(key), value(value), pre(nullptr), next(nullptr) {}
};

template<typename Key, typename Value>
struct FreqList
{
    using Node = FNode<Key, Value>;
    using spNode = std::shared_ptr<Node>;

    int freq; // 访问频率
    spNode head;
    spNode tail;

    FreqList(int n) 
     : freq(n) 
    {
      head = std::make_shared<Node>();
      tail = std::make_shared<Node>();
      head->next = tail;
      tail->pre = head;
    }

    bool isNull()
    {
      return head->next == tail;
    }
};

template <typename Key, typename Value>
class KLfuCache : public KICachePolicy<Key, Value>
{
public:
  using Node = FNode<Key, Value>;
  using spNode = std::shared_ptr<Node>;
  using NodeMap = std::unordered_map<Key, spNode>;

  KLfuCache(int capacity, int maxAverageNum = 10)
  : capacity_(capacity), minFreq_(INT8_MAX), maxAverageNum_(maxAverageNum),
    curAverageNum_(0), curTotalNum_(0) 
  {}

  ~KLfuCache() override = default;

  void put(Key key, Value value) override
  {
    if (capacity_ == 0)
        return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = keyToNode_.find(key);
    if (it != keyToNode_.end())
    {
        // 重置其value值
        it->second->value = value;
        // 找到了直接调整就好了，不用再去get中再找一遍，但其实影响不大
        getInternal(it->second, value);
        return;
    }

    putInternal(key, value);
  }

  // value值为传出参数
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
    get(key, value);
    return value;
  }

    // 清空缓存,回收资源
  void purge()
  {
    keyToNode_.clear();
    freqToFreqList_.clear();
  }

private:
  void putInternal(Key key, Value value); // 添加缓存
  void getInternal(spNode node, Value& value); // 获取缓存

  void kickOut(); // 移除缓存中的过期数据

  void removeFromFreqList(spNode node); // 从频率列表中移除节点
  void addToFreqList(spNode node); // 添加到频率列表

  void addFreqNum(); // 增加平均访问等频率
  void decreaseFreqNum(int num); // 减少平均访问等频率
  void handleOverMaxAverageNum(); // 处理当前平均访问频率超过上限的情况

private:
  int                                            capacity_; // 缓存容量
  int                                            minFreq_; // 最小访问频次(用于找到最小访问频次结点)
  int                                            maxAverageNum_; // 最大平均访问频次
  int                                            curAverageNum_; // 当前平均访问频次
  int                                            curTotalNum_; // 当前访问所有缓存次数总数 
  std::mutex                                     mutex_; // 互斥锁
  NodeMap                                        keyToNode_; // key 到 缓存节点的映射
  std::unordered_map<int, FreqList<Key, Value>*> freqToFreqList_;// 访问频次到该频次链表的映射
};

template<typename Key, typename Value>
void KLfuCache<Key, Value>::getInternal(spNode node, Value& value)
{
    // 找到之后需要将其从低访问频次的链表中删除，并且添加到+1的访问频次链表中，
    // 访问频次+1, 然后把value值返回
    value = node->value;
    // 从原有访问频次的链表中删除节点
    removeFromFreqList(node); 
    node->freq++;
    addToFreqList(node);
    // 如果当前node的访问频次如果等于minFreq+1，并且其前驱链表为空，则说明
    // freqToFreqList_[node->freq - 1]链表因node的迁移已经空了，需要更新最小访问频次
    if (node->freq - 1 == minFreq_ && freqToFreqList_[node->freq - 1]->isNull())
        minFreq_++;

    // 总访问频次和当前平均访问频次都随之增加
    addFreqNum();
}

template<typename Key, typename Value>
void KLfuCache<Key, Value>::putInternal(Key key, Value value)
{   
    // 如果不在缓存中，则需要判断缓存是否已满
    if (keyToNode_.size() == capacity_)
    {
        // 缓存已满，删除最不常访问的结点，更新当前平均访问频次和总访问频次
        kickOut();
    }
    
    // 创建新结点，将新结点添加进入，更新最小访问频次
    spNode node = std::make_shared<Node>(key, value);
    keyToNode_[key] = node;
    addToFreqList(node);
    addFreqNum();
    minFreq_ = std::min(minFreq_, 1);
}

template<typename Key, typename Value>
void KLfuCache<Key, Value>::kickOut()
{
    spNode node = freqToFreqList_[minFreq_]->head->next;
    removeFromFreqList(node);
    keyToNode_.erase(node->key);
    decreaseFreqNum(node->freq);
}

template<typename Key, typename Value>
void KLfuCache<Key, Value>::removeFromFreqList(spNode node)
{
    node->pre->next = node->next;
    node->next->pre = node->pre;
    node->pre = nullptr;
    node->next = nullptr;
}

template<typename Key, typename Value>
void KLfuCache<Key, Value>::addToFreqList(spNode node)
{
    // 添加进入相应的频次链表前需要判断该频次链表是否存在
    if (freqToFreqList_.find(node->freq) == freqToFreqList_.end())
    {
        // 不存在则创建
        freqToFreqList_[node->freq] = new FreqList<Key, Value>(node->freq);
    }

    // 从后边插入到该访问频次链表中
    spNode tail = freqToFreqList_[node->freq]->tail;
    node->pre = tail->pre;
    node->next = tail;
    tail->pre->next = node;
    tail->pre = node;
}

template<typename Key, typename Value>
void KLfuCache<Key, Value>::addFreqNum()
{
    curTotalNum_++;
    if (keyToNode_.empty())
        curAverageNum_ = 0;
    else
        curAverageNum_ = curTotalNum_ / keyToNode_.size();

    if (curAverageNum_ > maxAverageNum_)
    {
       handleOverMaxAverageNum();
    }
}

template<typename Key, typename Value>
void KLfuCache<Key, Value>::decreaseFreqNum(int num)
{
    // 减少平均访问频次和总访问频次
    curTotalNum_ -= num;
    if (keyToNode_.empty())
        curAverageNum_ = 0;
    else
        curAverageNum_ = curTotalNum_ / keyToNode_.size();
}

template<typename Key, typename Value>
void KLfuCache<Key, Value>::handleOverMaxAverageNum()
{
    // 当前平均访问频次已经超过了最大平均访问频次，所有结点的访问频次- (maxAverageNum_ / 2)
    for (auto it = keyToNode_.begin(); it != keyToNode_.end(); ++it)
    {
        spNode node = it->second;
        node->freq -= maxAverageNum_ / 2;
        // 所有节点的位置都要重新调整,虽然操作所有结点的时间复杂度是o(n)，但是考虑到很久才触发一次，可以忽略不计
        removeFromFreqList(node);
        addToFreqList(node);
        // 这里需要更换minFreq_
        minFreq_ = std::min(minFreq_, node->freq);
    }
}

// 并没有牺牲空间换时间，他是把原有缓存大小进行了分片。
template<typename Key, typename Value>
class KHashLfuCache
{
public:
    KHashLfuCache(size_t capacity, int sliceNum, int maxAverageNum = 10)
        : sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())
        , capacity_(capacity)
    {
        size_t sliceSize = std::ceil(capacity_ / static_cast<double>(sliceNum_)); // 每个lfu分片的容量
        for (int i = 0; i < sliceNum_; ++i)
        {
            lfuSliceCaches_.emplace_back(new KLfuCache<Key, Value>(sliceSize, maxAverageNum));
        }
    }

    void put(Key key, Value value)
    {
        // 根据key找出对应的lfu分片
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lfuSliceCaches_[sliceIndex]->put(key, value);
    }

    bool get(Key key, Value& value)
    {
        // 根据key找出对应的lfu分片
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lfuSliceCaches_[sliceIndex]->get(key, value);
    }

    Value get(Key key)
    {
        Value value;
        get(key, value);
        return value;
    }

    // 清除缓存
    void purge()
    {
        for (auto& lfuSliceCache : lfuSliceCaches_)
        {
            lfuSliceCache->purge();
        }
    }

private:
    // 将key计算成对应哈希值
    size_t Hash(Key key)
    {
        std::hash<Key> hashFunc;
        return hashFunc(key);
    }

private:
    size_t capacity_; // 缓存总容量
    int sliceNum_; // 缓存分片数量
    std::vector<std::unique_ptr<KLfuCache<Key, Value>>> lfuSliceCaches_; // 缓存lfu分片容器
};

} // namespace KamaCaches

