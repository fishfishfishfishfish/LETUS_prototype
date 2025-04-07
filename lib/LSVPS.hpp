#ifndef _LSVPS_H_
#define _LSVPS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "DMMTrie.hpp"
#include "common.hpp"

// 索引块结构体
struct IndexBlock {
  static constexpr size_t INDEXBLOCK_SIZE = 12288;  // 12KB

  struct Mapping {
    PageKey pagekey;
    uint64_t location;
  };

  static constexpr size_t MAPPINGS_PER_BLOCK =
      (INDEXBLOCK_SIZE - sizeof(size_t)) / sizeof(Mapping);

  IndexBlock();
  bool AddMapping(const PageKey &pagekey, uint64_t location);
  bool IsFull() const;
  const std::vector<Mapping> &GetMappings() const;
  bool SerializeTo(std::ofstream &out) const;
  bool Deserialize(std::ifstream &in);

 private:
  std::vector<Mapping> mappings_;
};

// 索引文件结构体
struct IndexFile {
  PageKey min_pagekey;
  PageKey max_pagekey;
  std::string filepath;
};

// 查找块结构体
struct LookupBlock {
  static const size_t BLOCK_SIZE = 12288;  // 12KB

  std::vector<std::pair<PageKey, size_t>>
      entries;  // mapping indexblock to its location
  bool SerializeTo(std::ostream &out) const;
  bool Deserialize(std::istream &in);
};

// LSVPS类定义
class LSVPS {
 public:
  LSVPS(std::string index_file_path = ".",
        std::string delta_cache_dir = "./delta_cache")
      : cache_(),
        table_(*this),
        index_file_path_(index_file_path),
        active_delta_page_cache_(300000, index_file_path) {}
  Page *PageQuery(uint64_t version);
  BasePage *LoadPage(const PageKey &pagekey);
  void StorePage(Page *page);
  void AddIndexFile(const IndexFile &index_file);
  int GetNumOfIndexFile();
  void RegisterTrie(DMMTrie *DMM_trie);
  const std::vector<Page *> &GetTable() const;
  void Flush();
  void StoreActiveDeltaPage(DeltaPage *page);
  DeltaPage *GetActiveDeltaPage(const string &pid);

 private:
  // 块缓存类（占位）
  class blockCache {};

  // 内存索引表类声明
  class MemIndexTable {
   public:
    explicit MemIndexTable(LSVPS &parent);
    const std::vector<Page *> &GetBuffer() const;
    void Store(Page *page);
    bool IsFull() const;
    void Flush();

   private:
    void writeToStorage(const std::vector<IndexBlock> &index_blocks,
                        const LookupBlock &lookup_blocks,
                        const std::filesystem::path &filepath);
    std::vector<Page *> buffer_;
    // gurantee that max_size >= one version pages
    const size_t max_size_ = 20000;
    LSVPS &parent_LSVPS_;
  };

  class ActiveDeltaPageCache {
   public:
    ActiveDeltaPageCache(size_t max_size, std::string cache_dir);
    ~ActiveDeltaPageCache();
    void Store(DeltaPage *page);
    DeltaPage *Get(const string &pid);
    void FlushToDisk();

   private:
    void evictIfNeeded();
    void writeToDisk(const string &pid, DeltaPage *page);
    DeltaPage *readFromDisk(const string &pid);
    void writeIndexBlock();
    void readIndexBlock();
    
    unordered_map<string, DeltaPage *> cache_;
    unordered_map<string, size_t> pid_to_offset_;  // Maps pid to file offset
    const size_t max_size_;        // 缓存最大容量
    std::string cache_dir_;        // 磁盘缓存目录
    std::string cache_file_;       // 统一存储文件路径
    std::list<string> lru_queue_;  // 用于LRU淘汰策略
  };

  Page *pageLookup(const PageKey &pagekey);
  Page *readPageFromIndexFile(std::vector<IndexFile>::const_iterator file_it,
                              const PageKey &pagekey);
  void applyDelta(BasePage *basepage, const DeltaPage *deltapage,
                  PageKey pagekey);

  blockCache cache_;
  MemIndexTable table_;
  std::string index_file_path_;
  ActiveDeltaPageCache active_delta_page_cache_;
  DMMTrie *trie_;
  std::vector<IndexFile> index_files_;
};

#endif