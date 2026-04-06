#include "storage/storage.h"
#include <cstring>

namespace flexql {
namespace storage {

std::unique_ptr<Page> page_alloc(uint32_t page_id) {
    auto p = std::make_unique<Page>();
    std::memset(p.get(), 0, PAGE_SIZE);
    p->page_id = page_id;
    p->row_count = 0;
    p->free_offset = PAGE_HEADER_SIZE;
    p->flags = 0;
    return p;
}

bool page_has_space(const Page* page, size_t row_size) {
    size_t required = row_size + 2; // 2 bytes for the length prefix
    return (page->free_offset + required) <= PAGE_SIZE;
}

int page_write_row(Page* page, const std::vector<uint8_t>& row_data) {
    if (!page_has_space(page, row_data.size())) return -1;
    
    uint16_t len = row_data.size();
    
    // We treat the 'data' array as starting at offset 9 from the struct beginning.
    // So writing to offset X in the file means data[X - PAGE_HEADER_SIZE].
    uint8_t* ptr = page->data + (page->free_offset - PAGE_HEADER_SIZE);
    
    // Write 2-byte length
    std::memcpy(ptr, &len, 2);
    // Write data
    std::memcpy(ptr + 2, row_data.data(), len);
    
    int slot_index = page->row_count;
    page->free_offset += (len + 2);
    page->row_count++;
    
    return slot_index;
}

bool page_read_row(const Page* page, int slot_index, std::vector<uint8_t>& row_out) {
    if (slot_index >= page->row_count || slot_index < 0) return false;
    
    uint16_t current_offset = PAGE_HEADER_SIZE;
    
    // Scan sequentially to find the slot
    for (int i = 0; i <= slot_index; ++i) {
        const uint8_t* ptr = page->data + (current_offset - PAGE_HEADER_SIZE);
        uint16_t len;
        std::memcpy(&len, ptr, 2);
        
        if (i == slot_index) {
            row_out.resize(len);
            std::memcpy(row_out.data(), ptr + 2, len);
            return true;
        }
        current_offset += (len + 2);
    }
    return false;
}

} // namespace storage
} // namespace flexql
