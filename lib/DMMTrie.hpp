#ifndef _DMMTRIE_HPP_
#define _DMMTRIE_HPP_

#include <array>
#include <cstring>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <mutex>
#include "VDLS.hpp"
#include "common.hpp"
#include "ddpg_binding.hpp"
static constexpr size_t HASH_SIZE = 32;
static constexpr size_t DMM_NODE_FANOUT = 16;
static constexpr uint16_t Td_ = 128;  // update threshold of DeltaPage
static constexpr uint16_t Tb_ = 256;  // update threshold of BasePage

using namespace std;

class LSVPS;
class DMMTrie;
class DeltaPage;

string HashFunction(const string &input);

struct NodeProof {
  int level;
  int index;
  uint16_t bitmap;
  vector<string> sibling_hash;
};

struct DMMTrieProof {
  string value;
  vector<NodeProof> proofs;
};

class Node {
 public:
  virtual ~Node() = default;

  virtual void CalculateHash();
  virtual void SerializeTo(char *buffer, size_t &current_size,
                           bool is_root) const = 0;
  virtual void DeserializeFrom(char *buffer, size_t &current_size,
                               bool is_root) = 0;
  virtual void AddChild(int index, Node *child, uint64_t version,
                        const string &hash);
  virtual Node *GetChild(int index) const;
  virtual bool HasChild(int index) const;
  virtual void SetChild(int index, uint64_t version, string hash);
  virtual string GetChildHash(int index);
  virtual uint64_t GetChildVersion(int index);
  virtual void UpdateNode();
  virtual void SetLocation(tuple<uint64_t, uint64_t, uint64_t> location);

  virtual string GetHash() = 0;
  virtual uint64_t GetVersion() = 0;
  virtual void SetVersion(uint64_t version) = 0;
  virtual void SetHash(string hash) = 0;

  virtual bool IsLeaf() const = 0;

  virtual NodeProof GetNodeProof(int level, int index);
};

class LeafNode : public Node {
 public:
  LeafNode(uint64_t V = 0, const string &k = "",
           const tuple<uint64_t, uint64_t, uint64_t> &l = {},
           const string &h = "");
  void CalculateHash(const string &value);
  void SerializeTo(char *buffer, size_t &current_size,
                   bool is_root) const override;
  void DeserializeFrom(char *buffer, size_t &current_size,
                       bool is_root) override;
  void UpdateNode(uint64_t version,
                  const tuple<uint64_t, uint64_t, uint64_t> &location,
                  const string &value, uint8_t location_in_page,
                  DeltaPage *deltapage);
  tuple<uint64_t, uint64_t, uint64_t> GetLocation() const;
  void SetLocation(tuple<uint64_t, uint64_t, uint64_t> location) override;
  string GetHash();
  uint64_t GetVersion();
  void SetVersion(uint64_t version);
  void SetHash(string hash);
  bool IsLeaf() const override;

 private:
  uint64_t version_;
  string key_;
  tuple<uint64_t, uint64_t, uint64_t>
      location_;  // location tuple (fileID, offset, size)
  string hash_;
  const bool is_leaf_;
};

class IndexNode : public Node {
 public:
  IndexNode(uint64_t V = 0, const string &h = "", uint16_t b = 0);
  IndexNode(
      uint64_t version, const string &hash, uint16_t bitmap,
      const array<tuple<uint64_t, string, Node *>, DMM_NODE_FANOUT> &children);
  IndexNode(const IndexNode &other);
  void CalculateHash() override;
  void SerializeTo(char *buffer, size_t &current_size,
                   bool is_root) const override;
  void DeserializeFrom(char *buffer, size_t &current_size,
                       bool is_root) override;
  void UpdateNode(uint64_t version, int index, const string &child_hash,
                  uint8_t location_in_page, DeltaPage *deltapage);
  void AddChild(int index, Node *child, uint64_t version = 0,
                const string &hash = "") override;
  Node *GetChild(int index) const override;
  bool HasChild(int index) const override;
  void SetChild(int index, uint64_t version, string hash) override;
  string GetChildHash(int index);
  uint64_t GetChildVersion(int index);
  string GetHash();
  uint64_t GetVersion();
  void SetVersion(uint64_t version);
  void SetHash(string hash);
  bool IsLeaf() const override;
  NodeProof GetNodeProof(int level, int index);

 private:
  uint64_t version_;
  string hash_;
  uint16_t bitmap_;  // bitmap for children
  array<tuple<uint64_t, string, Node *>, DMM_NODE_FANOUT> children_;  // trie
  const bool is_leaf_;
};

// IndexNode的对象池实现
class IndexNodePool {
private:
    // 内存块结构
    struct MemoryBlock {
        char* memory;
        size_t capacity;
        size_t used;
        MemoryBlock* next;
        
        MemoryBlock(size_t blockSize) : capacity(blockSize), used(0), next(nullptr) {
            memory = static_cast<char*>(::operator new(blockSize * sizeof(IndexNode)));
        }
        
        ~MemoryBlock() {
            ::operator delete(memory);
        }
    };
    
    MemoryBlock* head;
    std::vector<IndexNode*> freeList;
    // std::mutex mutex;
    size_t blockSize;
    size_t totalAllocated;
    
public:
    // 单例模式
    static IndexNodePool& getInstance() {
        static IndexNodePool instance(5000000);  // 每块10000个对象
        return instance;
    }
    
    // 基本构造函数版本
    IndexNode* allocate(uint64_t V, const std::string &h, uint16_t b) {
        // std::lock_guard<std::mutex> lock(mutex);
        
        IndexNode* result = allocateRaw();
        // 使用placement new构造对象
        new (result) IndexNode(V, h, b);
        return result;
    }
    
    // 带children的构造函数版本
    IndexNode* allocate(
        uint64_t version, const std::string &hash, uint16_t bitmap,
        const std::array<std::tuple<uint64_t, std::string, Node *>, 16> &children) {
            
        // std::lock_guard<std::mutex> lock(mutex);
        
        IndexNode* result = allocateRaw();
        // 使用placement new构造对象
        new (result) IndexNode(version, hash, bitmap, children);
        return result;
    }
    
    // 拷贝构造函数版本
    IndexNode* allocate(const IndexNode &other) {
        // std::lock_guard<std::mutex> lock(mutex);
        
        IndexNode* result = allocateRaw();
        // 使用placement new构造对象
        new (result) IndexNode(other);
        return result;
    }
    
    // 回收一个IndexNode对象
    void deallocate(IndexNode* node) {
        if (!node) return;
        
        // std::lock_guard<std::mutex> lock(mutex);
        
        // 调用析构函数但不释放内存
        node->~IndexNode();
        
        // 将对象添加到回收列表
        freeList.push_back(node);
    }
    
    // 获取统计信息
    size_t getAllocatedCount() const { return totalAllocated; }
    size_t getFreeListSize() const { return freeList.size(); }
    
private:
    // 分配一个原始IndexNode空间（不调用构造函数）
    IndexNode* allocateRaw() {
        // 优先从回收列表获取
        if (!freeList.empty()) {
            IndexNode* result = freeList.back();
            freeList.pop_back();
            return result;
        }
        
        // 检查当前块是否已满
        if (!head || head->used >= head->capacity) {
            // 创建新块，大小为原来的1.5倍
            size_t newBlockSize = head ? head->capacity * 1.5 : blockSize;
            MemoryBlock* newBlock = new MemoryBlock(newBlockSize);
            newBlock->next = head;
            head = newBlock;
        }
        
        // 从当前块分配
        IndexNode* result = reinterpret_cast<IndexNode*>(head->memory + (head->used * sizeof(IndexNode)));
        head->used++;
        totalAllocated++;
        
        return result;
    }
    
    // 私有构造函数（单例模式）
    explicit IndexNodePool(size_t initialBlockSize) 
        : head(nullptr), blockSize(initialBlockSize), totalAllocated(0) {
        // 分配第一个内存块
        head = new MemoryBlock(blockSize);
    }
    
    // 禁止拷贝和赋值
    IndexNodePool(const IndexNodePool&) = delete;
    IndexNodePool& operator=(const IndexNodePool&) = delete;
    
    // 析构函数
    ~IndexNodePool() {
        // 释放所有内存块
        while (head) {
            MemoryBlock* next = head->next;
            delete head;
            head = next;
        }
    }
};

// LeafNode的对象池实现
class LeafNodePool {
private:
    // 内存块结构
    struct MemoryBlock {
        char* memory;
        size_t capacity;
        size_t used;
        MemoryBlock* next;
        
        MemoryBlock(size_t blockSize) : capacity(blockSize), used(0), next(nullptr) {
            memory = static_cast<char*>(::operator new(blockSize * sizeof(LeafNode)));
        }
        
        ~MemoryBlock() {
            ::operator delete(memory);
        }
    };
    
    MemoryBlock* head;
    std::vector<LeafNode*> freeList;
    // std::mutex mutex;
    size_t blockSize;
    size_t totalAllocated;
    
public:
    // 单例模式
    static LeafNodePool& getInstance() {
        static LeafNodePool instance(10000);  // 每块10000个对象
        return instance;
    }

    // 基本构造函数版本
    LeafNode* allocate(uint64_t V, const string &k,
                   const tuple<uint64_t, uint64_t, uint64_t> &l,
                   const string &h) {
        // std::lock_guard<std::mutex> lock(mutex);
        
        LeafNode* result = allocateRaw();
        // 使用placement new构造对象
        new (result) LeafNode(V, k, l, h);
        return result;
    }
    LeafNode* allocate(const LeafNode &other) {
        // std::lock_guard<std::mutex> lock(mutex);
        
        LeafNode* result = allocateRaw();
        // 使用placement new构造对象
        new (result) LeafNode(other);
        return result;
    }
    
    // 回收一个IndexNode对象
    void deallocate(LeafNode* node) {
        if (!node) return;
        
        // std::lock_guard<std::mutex> lock(mutex);
        
        // 调用析构函数但不释放内存
        node->~LeafNode();
        
        // 将对象添加到回收列表
        freeList.push_back(node);
    }
    
    // 获取统计信息
    size_t getAllocatedCount() const { return totalAllocated; }
    size_t getFreeListSize() const { return freeList.size(); }
    
private:
    // 分配一个原始LeafNode空间（不调用构造函数）
    LeafNode* allocateRaw() {
        // 优先从回收列表获取
        if (!freeList.empty()) {
            LeafNode* result = freeList.back();
            freeList.pop_back();
            return result;
        }
        
        // 检查当前块是否已满
        if (!head || head->used >= head->capacity) {
            // 创建新块，大小为原来的1.5倍
            size_t newBlockSize = head ? head->capacity * 1.5 : blockSize;
            MemoryBlock* newBlock = new MemoryBlock(newBlockSize);
            newBlock->next = head;
            head = newBlock;
        }
        
        // 从当前块分配
        LeafNode* result = reinterpret_cast<LeafNode*>(head->memory + (head->used * sizeof(LeafNode)));
        head->used++;
        totalAllocated++;
        
        return result;
    }
    
    // 私有构造函数（单例模式）
    explicit LeafNodePool(size_t initialBlockSize) 
        : head(nullptr), blockSize(initialBlockSize), totalAllocated(0) {
        // 分配第一个内存块
        head = new MemoryBlock(blockSize);
    }
    
    // 禁止拷贝和赋值
    LeafNodePool(const LeafNodePool&) = delete;
    LeafNodePool& operator=(const LeafNodePool&) = delete;
    
    // 析构函数
    ~LeafNodePool() {
        // 释放所有内存块
        while (head) {
            MemoryBlock* next = head->next;
            delete head;
            head = next;
        }
    }
};

// 修改IndexNode的拷贝构造函数 - 对象池版本
// 注意：这不是修改原有的拷贝构造函数，而是添加一个静态工厂方法
class IndexNodeFactory {
public:
    // 创建IndexNode基本版本
    static IndexNode* create(uint64_t V = 0, const std::string &h = "", uint16_t b = 0) {
        return IndexNodePool::getInstance().allocate(V, h, b);
    }
    
    // 创建带children的IndexNode
    static IndexNode* create(
        uint64_t version, const std::string &hash, uint16_t bitmap,
        const std::array<std::tuple<uint64_t, std::string, Node *>, 16> &children) {
        return IndexNodePool::getInstance().allocate(version, hash, bitmap, children);
    }
    
    // 创建IndexNode副本
    static IndexNode* createCopy(const IndexNode &other) {
        return IndexNodePool::getInstance().allocate(other);
    }
    
    // 回收IndexNode
    static void recycle(IndexNode* node) {
        IndexNodePool::getInstance().deallocate(node);
    }
};

class LeafNodeFactory {
public:
    // 创建IndexNode基本版本
    static LeafNode* create(uint64_t V = 0, const string &k = "",
                   const tuple<uint64_t, uint64_t, uint64_t> &l = {},
                   const string &h = "") {
        return LeafNodePool::getInstance().allocate(V, k, l, h);
    } 
    static LeafNode* createCopy(const LeafNode &other) {
        return LeafNodePool::getInstance().allocate(other);
    }
    // 回收IndexNode
    static void recycle(LeafNode* node) {
        LeafNodePool::getInstance().deallocate(node);
    }
};

class DeltaPage : public Page {
 public:
  struct DeltaItem {
    uint8_t location_in_page;
    bool is_leaf_node;
    uint64_t version;
    string hash;

    // unique items for leafnode
    uint64_t fileID;
    uint64_t offset;
    uint64_t size;

    // unique items for indexnode
    uint8_t index;
    string child_hash;

    DeltaItem() {}
    DeltaItem(uint8_t loc, bool leaf, uint64_t ver, const string &h,
              uint64_t fID = 0, uint64_t off = 0, uint64_t sz = 0,
              uint8_t idx = 0, const string &ch_hash = "");
    DeltaItem(char *buffer, size_t &current_size);
    void SerializeTo(std::ofstream &out) const;
    void SerializeTo(char *buffer, size_t &current_size) const;
    bool Deserialize(std::ifstream &in);
  };

  DeltaPage(PageKey last_pagekey = {0, 0, true, ""}, uint16_t update_count = 0,
            uint16_t b_update_count = 0);
  DeltaPage(char *buffer);
  DeltaPage(const DeltaPage &other);
  ~DeltaPage();
  void AddIndexNodeUpdate(uint8_t location, uint64_t version,
                          const string &hash, uint8_t index,
                          const string &child_hash);
  void AddLeafNodeUpdate(uint8_t location, uint64_t version, const string &hash,
                         uint64_t fileID, uint64_t offset, uint64_t size);
  void SerializeTo();
  void ClearDeltaPage();
  const vector<DeltaItem> &GetDeltaItems() const;
  PageKey GetLastPageKey() const;
  void SetLastPageKey(PageKey pagekey);
  uint16_t GetDeltaPageUpdateCount();
  uint16_t GetBasePageUpdateCount();
  void ClearBasePageUpdateCount();
  void SerializeTo(std::ofstream &out) const;
  bool Deserialize(std::ifstream &in);
  bool Deserialize(char *buffer);

 private:
  vector<DeltaItem> deltaitems_;
  PageKey last_pagekey_;
  uint16_t update_count_;
  uint16_t b_update_count_;
};

class BasePage : public Page {
 public:
  BasePage(DMMTrie *trie = nullptr, Node *root = nullptr,
           const string &pid = "");
  BasePage(DMMTrie *trie, char *buffer);
  BasePage(DMMTrie *trie, string key, string pid, string nibbles);
  BasePage(const BasePage &other);  // deep copy
  ~BasePage();
  void SerializeTo();
  void UpdatePage(uint64_t version,
                  tuple<uint64_t, uint64_t, uint64_t> location,
                  const string &value, const string &nibbles,
                  const string &child_hash, DeltaPage *deltapage,
                  PageKey pagekey);
  void UpdateDeltaItem(const DeltaPage::DeltaItem &deltaitem);
  Node *GetRoot() const;

 private:
  DMMTrie *trie_;
  Node *root_;  // the root of the page
};

class DMMTrie {
 public:
  DMMTrie(uint64_t tid, LSVPS *page_store, VDLS *value_store,
          uint64_t current_version = 0);
  ~DMMTrie();
  bool Put(uint64_t tid, uint64_t version, const string &key,
           const string &value);
  string Get(uint64_t tid, uint64_t version, const string &key);
  void Delete(uint64_t tid, uint64_t version, const string &key);
  void Commit(uint64_t version);
  void CalcRootHash(uint64_t tid, uint64_t version);
  string GetRootHash(uint64_t tid, uint64_t version);
  DMMTrieProof GetProof(uint64_t tid, uint64_t version, const string &key);
  bool Verify(uint64_t tid, const string &key, const string &value,
              string root_hash, DMMTrieProof proof);
  bool Verify(uint64_t tid, uint64_t version, string root_hash);
  void Flush(uint64_t tid, uint64_t version);
  void Revert(uint64_t tid, uint64_t version);
  DeltaPage *GetDeltaPage(const string &pid);
  pair<uint64_t, uint64_t> GetPageVersion(PageKey pagekey);
  PageKey GetLatestBasePageKey(PageKey pagekey) const;
  void UpdatePageVersion(PageKey pagekey, uint64_t current_version,
                         uint64_t latest_basepage_version);
  void WritePageCache(PageKey pagekey, Page *page);
  void AddDeltaPageVersion(const string &pid, uint64_t version);
  uint64_t GetVersionUpperbound(const string &pid, uint64_t version);

 private:
  DDPGBinding* ddpg_binding_instance;
  LSVPS *page_store_;
  VDLS *value_store_;
  uint64_t tid;
  BasePage *root_page_;
  uint64_t current_version_;
  unordered_map<PageKey, list<pair<PageKey, BasePage *>>::iterator,
                PageKey::Hash>
      lru_cache_;                             //  use a hash map as lru cache
  list<pair<PageKey, BasePage *>> pagekeys_;  // list to maintain cache order
  const size_t max_cache_size_ = 3000000;      // maximum pages in cache
  unordered_map<string, DeltaPage>
      active_deltapages_;  // deltapage of all pages, delta pages are indexed by
                           // pid
  unordered_map<string, pair<uint64_t, uint64_t>>
      page_versions_;  // current version, latest basepage version
  map<PageKey, Page *> page_cache_;
  map<string, string> put_cache_;  // temporarily store the key of value of Put
  unordered_map<string, vector<uint64_t>>
      deltapage_versions_;  // the versions of deltapages for every pid

  BasePage *GetPage(const PageKey &pagekey);
  void PutPage(const PageKey &pagekey, BasePage *page);
  void UpdatePageKey(const PageKey &old_pagekey, const PageKey &new_pagekey);
  string RecursiveVerify(PageKey pagekey);
};

#endif